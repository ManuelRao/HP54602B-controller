#include "headers/HP54602B_manager.h"
#include <iostream>

OscilloscopeManager::OscilloscopeManager() 
    : m_isRunning(false), m_hSerial(INVALID_HANDLE_VALUE) {}

OscilloscopeManager::~OscilloscopeManager() {
    Disconnect();
}

bool OscilloscopeManager::Connect(const std::string& comPort) {
    // 1. Open the COM Port
    m_comPort = comPort;
    std::string portPath = "\\\\.\\" + m_comPort;
    m_hSerial = CreateFileA(portPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    if (m_hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open " << m_comPort << std::endl;
        return false;
    }

    // 2. Configure hardware settings
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    GetCommState(m_hSerial, &dcbSerialParams);
    
    dcbSerialParams.BaudRate = CBR_19200; 
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE; // Hardware flow control

    SetCommState(m_hSerial, &dcbSerialParams);

    // 3. Setup timeouts
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 500;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(m_hSerial, &timeouts);

    // 4. Send initial SCPI configuration
    std::string setupCmd = ":WAV:POIN 500; :WAV:FORM BYTE\n";
    DWORD bytesWritten;
    WriteFile(m_hSerial, setupCmd.c_str(), setupCmd.length(), &bytesWritten, NULL);

    // 5. Ignite the background thread
    m_isRunning = true;
    m_workerThread = std::thread(&OscilloscopeManager::WorkerLoop, this);
    
    return true;
}

void OscilloscopeManager::Disconnect() {
    if (m_isRunning) {
        m_isRunning = false;           // Signal thread to stop
        if (m_workerThread.joinable()) {
            m_workerThread.join();     // Wait for it to cleanly exit
        }
        CloseHandle(m_hSerial);        // Release the COM port
        m_hSerial = INVALID_HANDLE_VALUE;
    }
}

std::string OscilloscopeManager::ReadTextResponse() {
    std::string response = "";
    char singleByte = 0;
    DWORD bytesRead = 0;

    // Keep reading until we hit the scope's newline terminator
    while (ReadFile(m_hSerial, &singleByte, 1, &bytesRead, NULL) && bytesRead == 1) {
        if (singleByte == '\n') break;
        response += singleByte;
    }
    return response;
}


/*
--------------------------------
BACKGROUND WORKER THREAD FUNCTION
--------------------------------
*/

void OscilloscopeManager::WorkerLoop() {
    while (m_isRunning) {
        if (m_hSerial == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue; // Skip this iteration if the serial port isn't valid
        }

        // PROCESS COMMANDS
        {
            std::lock_guard<std::mutex> cmdLock(m_commandMutex);
            for (const auto& cmd : m_commandQueue) {
                if (m_hSerial != INVALID_HANDLE_VALUE) {
                    DWORD bytesWritten;
                    WriteFile(m_hSerial, cmd.c_str(), cmd.length(), &bytesWritten, NULL);
                    std::cout << "Worker sent command: " << cmd << std::endl;

                    if (cmd.find("?") != std::string::npos){
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));


                    }
                    

                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            m_commandQueue.clear(); // Clear after processing
        }

        /* Request the waveform data */
        
        std::string request = ":WAV:DATA?\n";
        DWORD bytesWritten;
        WriteFile(m_hSerial, request.c_str(), request.length(), &bytesWritten, NULL);


        // interpret the incoming binary blob according to the HP54602B's SCPI format:
        //  Wait for the '#' sync byte that indicates the start of a new waveform frame
        DWORD bytesRead = 0;
        char syncByte = 0;
        bool synced = false;

        while (ReadFile(m_hSerial, &syncByte, 1, &bytesRead, NULL) && bytesRead == 1) {
            if (syncByte == '#') {
                synced = true;
                break;
            }
        }

        if (!synced) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            continue; 
        }

        // 3. Read the single digit that tells us how long the size string is (usually '8')
        char digitsChar = 0;
        ReadFile(m_hSerial, &digitsChar, 1, &bytesRead, NULL);
        int numDigits = digitsChar - '0'; // Convert ASCII char to integer

        // 4. Read the actual size string (e.g., "00000500")
        std::vector<char> lengthStr(numDigits + 1, '\0'); // +1 for null terminator
        ReadFile(m_hSerial, lengthStr.data(), numDigits, &bytesRead, NULL);
        int expectedDataBytes = std::stoi(lengthStr.data()); // Convert string to integer 500

        // 5. The Accumulator Loop: Now we know EXACTLY how many binary bytes to scoop
        std::vector<uint8_t> incomingBuffer(expectedDataBytes);
        int totalBytesRead = 0;
        
        while (totalBytesRead < expectedDataBytes) {
            DWORD readNow = 0;
            ReadFile(m_hSerial, incomingBuffer.data() + totalBytesRead, expectedDataBytes - totalBytesRead, &readNow, NULL);
            
            if (readNow == 0) break; // Hardware timeout
            totalBytesRead += readNow;
        }

        // 6. Read the final '\n' terminator to completely flush the Windows buffer for the next frame
        char trailingNewline;
        ReadFile(m_hSerial, &trailingNewline, 1, &bytesRead, NULL);

        // 7. If we got a perfect frame, lock the mutex and ship it to the UI!
        if (totalBytesRead == expectedDataBytes) {
            std::lock_guard<std::mutex> lock(m_dataMutex);
            m_latestData = incomingBuffer;
        }

        // Give the oscilloscope processor time to recover
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

void OscilloscopeManager::EnqueueCommand(const std::string& command) {
    std::lock_guard<std::mutex> lock(m_commandMutex);
    m_commandQueue.push_back(command);
}

bool OscilloscopeManager::GetQueryResult(const std::string& command, std::string& outResult) {
    std::lock_guard<std::mutex> lock(m_responseMutex);
    auto it = m_responseMap.find(command);
    if (it != m_responseMap.end()) {
        outResult = it->second;
        m_responseMap.erase(it);
        return true;
    }
    return false;
}

void OscilloscopeManager::GetLatestData(std::vector<uint8_t>& outBuffer) {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    outBuffer = m_latestData;
}

/*
----------------------------------
ALL OSCILLOSCOPE CONTROL FUNCTIONS
----------------------------------
*/

void OscilloscopeManager::SendCommand(const std::string& cmd) {
    EnqueueCommand(cmd);
}

/**
 * Sets the horizontal timebase scale. The HP54602B expects this in seconds/division, so for example:
 * 1ms/div would be sent as 0.001, 500us/div would be sent as 0.0005, etc.
 */
void OscilloscopeManager::SetTimebase(const float& timebase) {
    std::string cmd = ":TIM:SCAL " + std::to_string(timebase) + "\n";
    SendCommand(cmd);
}

/**
 * Configures the trigger settings. The HP54602B uses edge triggering, so you specify the source (e.g., "CHAN1") and the level in volts (e.g., 0.5 for 500mV). The SCPI command format is typically ":TRIG:EDGE:SOUR CHAN1; :TRIG:LEV 0.5\n"
 */
void OscilloscopeManager::SetTrigger(const int& channel, const float& level, const bool& risingEdge, const bool& __auto, const bool& noiseRejection, const bool& holdoff, const float& holdoffTime) {
    std::string cmd = ":TRIG:EDGE:SOUR CHAN" + std::to_string(channel) + "; :TRIG:LEV " + std::to_string(level) + "; :TRIG:EDGE:SLOP " + (risingEdge ? "POS" : "NEG") + "\n";
    SendCommand(cmd);
    std::string autoCmd = ":TRIG:MODE " + std::string(__auto ? "AUTO" : "NORM") + "\n";
    SendCommand(autoCmd);
    std::string noiseCmd = ":TRIG:EDGE:NOIS " + std::string(noiseRejection ? "ON" : "OFF") + "\n";
    SendCommand(noiseCmd);
    std::string holdoffCmd = ":TRIG:EDGE:HOLD " + std::string(holdoff ? "ON" : "OFF") + "\n";
    SendCommand(holdoffCmd);
    std::string holdoffTimeCmd = ":TRIG:EDGE:HTIM " + std::to_string(holdoffTime) + "\n";
    SendCommand(holdoffTimeCmd);
}

/**
 * Sets the vertical scale for a given channel. The HP54602B expects the scale in terms of volts/division 
 * so for example, if you want to set Channel 1 to 500mV/div, you would call SetVerticalScale("1", 0.5f);
 */
void OscilloscopeManager::SetVerticalScale(const int& channel, const float& scale) {
    std::string cmd = ":CHAN" + std::to_string(channel) + ":SCAL " + std::to_string(scale) + "\n";
    SendCommand(cmd);
}

/**
 * Sets the acquisition mode. Includes "NORM" for normal acquisition, "PEAK" for peak detection, and "AVER" for averaging. 
 */
void OscilloscopeManager::SetAcquisitionMode(const std::string& mode) {
    std::string cmd = ":ACQ:TYPE " + mode + "\n";
    SendCommand(cmd);
}

void OscilloscopeManager::SetCoupling(const int& channel, const std::string& couplingType) {
    std::string cmd = ":CHAN" + std::to_string(channel) + ":COUP " + couplingType + "\n";
    SendCommand(cmd);
}

void OscilloscopeManager::SetHorizontalPosition(const float& position) {
    std::string cmd = ":TIM:POS " + std::to_string(position) + "\n";
    SendCommand(cmd);
}

void OscilloscopeManager::SetVerticalPosition(const int& channel, const float& position) {
    std::string cmd = ":CHAN" + std::to_string(channel) + ":POS " + std::to_string(position) + "\n";
    SendCommand(cmd);
}

void OscilloscopeManager::SetAcquisitionState(bool run) {
    std::string cmd = run ? ":RUN\n" : ":STOP\n";
    SendCommand(cmd);
}

void OscilloscopeManager::SetMeasurement(const std::string& measurementType, const int& channel) {
    std::string cmd = ":MEAS:" + measurementType + " CHAN" + std::to_string(channel) + "\n";
    SendCommand(cmd);
}

void OscilloscopeManager::SetDisplayMode(const std::string& mode) {
    std::string cmd = ":DISP:MODE " + mode + "\n";
    SendCommand(cmd);
}

void OscilloscopeManager::SetDataFormat(const std::string& format) {
    std::string cmd = ":WAV:FORM " + format + "\n";
    SendCommand(cmd);
}

void OscilloscopeManager::SetProbeAttenuation(const int& channel, const std::string& attenuation) {
    std::string cmd = ":CHAN" + std::to_string(channel) + ":PROB " + attenuation + "\n";
    SendCommand(cmd);
}

void OscilloscopeManager::SetBandwidthLimit(const int& channel, bool enabled) {
    std::string cmd = ":CHAN" + std::to_string(channel) + ":BWL " + (enabled ? "ON" : "OFF") + "\n";
    SendCommand(cmd);
}

void OscilloscopeManager::SetAutoscale() {
    std::string cmd = ":AUT\n";
    SendCommand(cmd);
}

void OscilloscopeManager::SetChannelOnOff(const int& channel, bool on) {
    std::string cmd = ":CHAN" + std::to_string(channel) + ":DISP " + (on ? "ON" : "OFF") + "\n";
    SendCommand(cmd);
}

void OscilloscopeManager::SetAverageCount(const int& count) {
    std::string cmd = ":ACQ:AVER " + std::to_string(count) + "\n";
    SendCommand(cmd);
}

void OscilloscopeManager::GetMeasurementResult(const std::string& measurementType, const int& channel, std::string& outResult) {
    std::string queryCmd = ":MEAS?" + measurementType + " CHAN" + std::to_string(channel) + "\n";
    SendCommand(queryCmd);
    // The response will be asynchronously read by the worker thread and stored in m_responseMap
}

void OscilloscopeManager::GetCurrentSettings(std::unordered_map<std::string, std::string>& outSettings) {
    // This is a placeholder implementation. You would need to send specific SCPI queries for each setting you're interested in and populate the outSettings map accordingly.
    outSettings["TIMEBASE"] = "1ms/div";
    outSettings["TRIGGER"] = "CHAN1, 500mV";
    outSettings["VERTICAL_SCALE_CHAN1"] = "500mV/div";
    // Add more settings as needed
}


/*
--------------------
UI DRAWING FUNCTIONS 
--------------------
*/

void OscilloscopeManager::drawBasicPloter(const std::vector<uint8_t>& data) {
    ImGui::Begin("Waveform Plot");

    // For demonstration, we'll just plot the raw byte values as a line graph
    if (!data.empty()) {
        std::vector<float> floatData(data.begin(), data.end()); // Convert uint8_t to float for plotting
        ImGui::PlotLines("Channel 1", floatData.data(), static_cast<int>(floatData.size()));
    }

    ImGui::End();
}

void OscilloscopeManager::drawConnectionControls() {

    static std::vector<SerialPortInfo> availablePorts;
    static int selectedPortIndex = -1;
    static bool justScanned = false;

    ImGui::Begin("Connection Controls");

    if (!m_isConnected){ // 1. Scan Button
        if (ImGui::Button("Scan Ports") || !justScanned) {
            availablePorts = GetAvailableComPorts();
            if (!availablePorts.empty()) selectedPortIndex = 0; // Default to the first one found
            justScanned = true;
        }

        ImGui::SameLine();

        // 2. The ImGui Dropdown Menu
        // Set the preview label to the currently selected identifier, or "No ports found"
        std::string previewValue = (selectedPortIndex >= 0 && selectedPortIndex < availablePorts.size()) 
                                   ? availablePorts[selectedPortIndex].identifier 
                                   : "No ports found";

        // ImGui::SetNextItemWidth(300) keeps the dropdown from taking up the entire window width
        ImGui::SetNextItemWidth(300); 
        if (ImGui::BeginCombo("##com_combo", previewValue.c_str())) {
            
            // Loop through our vector and create a selectable item for each
            for (int i = 0; i < availablePorts.size(); ++i) {
                const bool isSelected = (selectedPortIndex == i);
                
                if (ImGui::Selectable(availablePorts[i].identifier.c_str(), isSelected)) {
                    selectedPortIndex = i; // The user clicked this one!
                }

                // Keep the list focused on the selected item if the user re-opens the menu
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // 3. The Connect Button
        ImGui::Spacing();
        if (selectedPortIndex >= 0 && ImGui::Button("Connect to Oscilloscope", ImVec2(200, 30))) {
            try
            {
                Connect(availablePorts[selectedPortIndex].portName);
            }
            catch(const std::exception& e)
            {
                std::cerr << e.what() << '\n';
            }
        }

    } else {
        // 4. What to show when successfully connected
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Connected to %s", m_comPort.c_str());
        if (ImGui::Button("Disconnect", ImVec2(150, 30))) {
            Disconnect();
            justScanned = false; // Force a fresh scan next time they disconnect
        }
    }

    ImGui::End();
}

void OscilloscopeManager::drawAutoScaleButton() {
    if (ImGui::Button("Auto Scale")) {
        SetAutoscale();
    }
}

void OscilloscopeManager::drawTriggerControl() {

    static int selectedChannel = 1;
    static int levelMilliVolts = 500;
    static bool risingEdge = true;
    static bool __auto = true;

    ImGui::Text("Trigger Control");
    if(ImGui::BeginCombo("Channel", ("CHAN" + std::to_string(selectedChannel)).c_str())) {
        for (int i = 1; i <= 4; ++i) {
            bool isSelected = (selectedChannel == i);
            if (ImGui::Selectable(("CHAN" + std::to_string(i)).c_str(), isSelected)) {
                selectedChannel = i;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SliderInt("Level (mV)", &levelMilliVolts, -5000, 5000);
    if(ImGui::RadioButton("Rising Edge", risingEdge == true)) {
        risingEdge = true;
    }
    ImGui::SameLine();
    if(ImGui::RadioButton("Falling Edge", risingEdge == false)) {
        risingEdge = false;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto Trigger", &__auto);
    if (ImGui::Button("Apply Trigger Settings")) {
        SetTrigger(selectedChannel, levelMilliVolts / 1000.0f, risingEdge);
    }
}

void OscilloscopeManager::drawTimebaseControl() {
    const float timebaseValues[] = {
        0.000000002f, 0.000000005f, 0.00000001f, 0.00000002f, 0.00000005f, 0.0000001f, 0.0000002f, 0.0000005f, 0.000001f, 0.000002f, 0.000005f, 0.00001f, 0.00002f, 0.00005f, 0.0001f, 0.0002f, 0.0005f, 0.001f, 0.002f, 0.005f, 0.010f, 0.020f, 0.050f, 0.100f, 0.200f, 0.500f, 1.000f, 2.000f, 5.000f
    };
    
    // 2. Define the exact text you want to appear inside the slider for each step
    const char* timebaseLabels[] = {
        "2 ns/div", "5 ns/div", "10 us/div", "20 ns/div", "50 ns/div", "100 ns/div", "200 ns/div", "500 ns/div", "1 us/div", "2 us/div", "5 us/div", "10 us/div", "20 us/div", "50 us/div", "100 us/div", "200 us/div", "500 us/div", "1 ms/div", "2 ms/div", "5 ms/div", "10 ms/div", "20 ms/div", "50 ms/div", "100 ms/div", "200 ms/div", "500 ms/div", "1 s/div", "2 s/div", "5 s/div"
    };
    
    // Calculate how many steps are in our array
    int maxIndex = (sizeof(timebaseValues) / sizeof(float)) - 1;

    ImGui::Text("Timebase Control");
    if(ImGui::SliderInt("Timebase", &m_timebaseIndex, 0, maxIndex, timebaseLabels[m_timebaseIndex])) {
        SetTimebase(timebaseValues[m_timebaseIndex]);
    }
}

void OscilloscopeManager::drawAcquisitionControl() {
    static int selectedMode = 0; // 0: NORM, 1: PEAK, 2: AVER
    const char* modes[] = { "NORM", "PEAK", "AVER" };

    ImGui::Text("Acquisition Control");
    if (ImGui::Combo("Mode", &selectedMode, modes, IM_ARRAYSIZE(modes))) {
        SetAcquisitionMode(modes[selectedMode]);
    }
    if (selectedMode == 2) { // If AVERaging is selected, show an additional slider for the number of averages
        static int numAverages = 16;
        if(ImGui::SliderInt("Average Count", &numAverages, 2, 256)){
            SetAverageCount(numAverages);
        }
    }
}

void OscilloscopeManager::drawMeasurementControlls() {
    static int selectedChannel = 1;
    static int selectedMeasurement = 0; // 0: FREQ, 1: PER, 2: VPP, etc.
    const char* measurements[] = { "FREQ", "PER", "VPP", "VAVG", "VRMS" };

    ImGui::Text("Measurement Controls");
    if(ImGui::BeginCombo("Measure in channel", ("CHAN" + std::to_string(selectedChannel)).c_str())) {
        for (int i = 1; i <= 4; ++i) {
            bool isSelected = (selectedChannel == i);
            if (ImGui::Selectable(("CHAN" + std::to_string(i)).c_str(), isSelected)) {
                selectedChannel = i;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if(ImGui::Combo("Measurement Type", &selectedMeasurement, measurements, IM_ARRAYSIZE(measurements))) {
        SetMeasurement(measurements[selectedMeasurement], selectedChannel);
    }
}

void OscilloscopeManager::drawDisplayModeControl() {
    static int selectedMode = 0; // 0: YT, 1: XY
    const char* modes[] = { "YT", "XY" };

    ImGui::Text("Display Mode Control");
    if (ImGui::RadioButton("Y-T Mode", selectedMode == 0)) {
        selectedMode = 0;
        SetDisplayMode(modes[selectedMode]);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("X-Y Mode", selectedMode == 1)) {
        selectedMode = 1;
        SetDisplayMode(modes[selectedMode]);
    }
}

void OscilloscopeManager::drawDataFormatControl() {
    static int selectedFormat = 0; // 0: BYTE, 1: WORD, 2: ASCII
    const char* formats[] = { "BYTE", "WORD", "ASCII" };

    ImGui::Text("Data Format Control");
    if (ImGui::Combo("Format", &selectedFormat, formats, IM_ARRAYSIZE(formats))) {
        SetDataFormat(formats[selectedFormat]);
    }
}

void OscilloscopeManager::drawChannelToggle(const int& channel) {
    static bool channelOn[4] = { true, true, false, false }; // Default states for 4 channels

    ImGui::Text("Channel %d Toggle", channel);
    if (ImGui::Checkbox(("##Enable CHAN" + std::to_string(channel)).c_str(), &channelOn[channel - 1])) {
        SetChannelOnOff(channel, channelOn[channel - 1]);
    }
} 

void OscilloscopeManager::drawVerticalScaleControl(const int& channel) { // Default scales for 4 channels
    ImGui::Text("Channel %d Vertical Scale", channel);
    if (ImGui::BeginCombo(("##Scale##CHAN" + std::to_string(channel)).c_str(), (std::to_string(verticalScale[channel - 1]) + " V/div").c_str())) {
        for (float scale : {0.002f, 0.005f, 0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f}) {
            bool isSelected = (verticalScale[channel - 1] == scale);
            if (ImGui::Selectable((std::to_string(scale) + " V/div").c_str(), isSelected)) {
                verticalScale[channel - 1] = scale;
                SetVerticalScale(channel, scale);
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void OscilloscopeManager::drawCouplingControl(const int& channel) {
    static int selectedCoupling[4] = { 0, 0, 0, 0 }; // Default coupling for 4 channels
    const char* couplingTypes[] = { "DC", "AC", "GND" };

    ImGui::Text("Channel %d Coupling", channel);
    if (ImGui::Combo(("##Coupling##CHAN" + std::to_string(channel)).c_str(), &selectedCoupling[channel - 1], couplingTypes, IM_ARRAYSIZE(couplingTypes))) {
        SetCoupling(channel, couplingTypes[selectedCoupling[channel - 1]]);
    }
}

void OscilloscopeManager::drawVerticalPositionControl(const int& channel) {
    static float verticalPosition[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // Default positions for 4 channels

    ImGui::Text("Channel %d Vertical Position", channel);
    if (ImGui::SliderFloat(("##Position##CHAN" + std::to_string(channel)).c_str(), &verticalPosition[channel - 1], verticalScale[channel - 1] * -4.0f, verticalScale[channel - 1] * 4.0f, "%.2f V")) {
        SetVerticalPosition(channel, verticalPosition[channel - 1]);
    }
}

void OscilloscopeManager::drawProbeAttenuationControl(const int& channel) {
    static int selectedAttenuation[4] = { 0, 0, 0, 0 }; // Default attenuation for 4 channels
    const char* attenuationTypes[] = { "1X", "10X", "100X" };

    ImGui::Text("Channel %d Probe Attenuation", channel);
    if (ImGui::Combo(("##Attenuation##CHAN" + std::to_string(channel)).c_str(), &selectedAttenuation[channel - 1], attenuationTypes, IM_ARRAYSIZE(attenuationTypes))) {
        SetProbeAttenuation(channel, attenuationTypes[selectedAttenuation[channel - 1]]);
    }
}

void OscilloscopeManager::drawChannelControls(const int& channel){
    ImGui::Text("Channel %d Controls", channel);
    drawChannelToggle(channel);
    drawVerticalScaleControl(channel);
    drawCouplingControl(channel);
    drawVerticalPositionControl(channel);
    drawProbeAttenuationControl(channel);
}

void OscilloscopeManager::drawMultiChannelControls(const std::vector<int>& channels) {

    ImGui::Text("Channel Controls");
    if (channels.empty()) {
        ImGui::Text("No channels selected");
        return;
    }
    
    if (channels.size() > 4) {
        ImGui::Text("Too many channels selected. Max is 4.");
        return;
    }
    if (channels.size() == 1) {
        drawChannelControls(channels[0]);
        return;
    }

    int tableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    float windowWidth = ImGui::GetWindowSize().x;

    if (windowWidth < 300) {
        // NARROW WINDOW: Stack vertically without a table
        for (int channel : channels) {
            // Draw a pseudo-header for the vertical stack
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "CH %d", channel); 
            ImGui::Separator();
            drawChannelControls(channel);
            ImGui::Spacing();
        }
    } else {
        // MEDIUM OR WIDE WINDOW: Use Tables
        
        // Decide columns: If under 600px, max 2 columns. If over 600px, 1 column per channel.
        int numColumns = (windowWidth < 600) ? 2 : channels.size();

        // Safety check: Don't draw 4 columns if we only have 3 channels
        if (numColumns > channels.size()) {
            numColumns = channels.size(); 
        }

        if (ImGui::BeginTable("##LateralCollection", numColumns, tableFlags)) {
            
            for (int channel : channels) {
                // This magic function automatically jumps to the next column, 
                // and drops to a new row perfectly if it hits the edge!
                ImGui::TableNextColumn();
                
                // Draw our own header INSIDE the cell so it repeats on wrapped rows
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "CH %d", channel);
                ImGui::Separator();
                
                drawChannelControls(channel);
            }
            
            ImGui::EndTable();
        }
    }
}

void OscilloscopeManager::drawAllControls() {
    drawConnectionControls();
    if (!m_isConnected) {
        ImGui::Text("Please connect to an oscilloscope to access controls.");
        return;
    }

    if (m_isConnected) {
        if (ImGui::CollapsingHeader("Basic Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawAutoScaleButton();
            drawTriggerControl();
            drawTimebaseControl();
            drawAcquisitionControl();
            drawMeasurementControlls();
            drawDisplayModeControl();
            drawDataFormatControl();
        }
        if (ImGui::CollapsingHeader("Channel Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawMultiChannelControls({1, 2, 3, 4});
        }
        if (ImGui::CollapsingHeader("Ploter", ImGuiTreeNodeFlags_DefaultOpen)) {
            std::vector<uint8_t> data;
            GetLatestData(data);
            drawBasicPloter(data);
        }
    }
}
