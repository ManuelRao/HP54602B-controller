#include "headers/HP54602B_manager.h"
#include <iostream>

OscilloscopeManager::OscilloscopeManager(const std::string& comPort) 
    : m_comPort(comPort), m_isRunning(false), m_hSerial(INVALID_HANDLE_VALUE) {}

OscilloscopeManager::~OscilloscopeManager() {
    Disconnect();
}

bool OscilloscopeManager::Connect() {
    // 1. Open the COM Port
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

void OscilloscopeManager::WorkerLoop() {
    while (m_isRunning) {
        // 

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

void OscilloscopeManager::GetLatestData(std::vector<uint8_t>& outBuffer) {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    outBuffer = m_latestData;
}

void OscilloscopeManager::SendCommand(const std::string& cmd) {
    if (m_hSerial != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten;
        WriteFile(m_hSerial, cmd.c_str(), cmd.length(), &bytesWritten, NULL);
        std::cout << "Sent command: " << cmd << std::endl;
    }
}

void OscilloscopeManager::drawControlUI() {
    ImGui::Begin("Oscilloscope Control Panel");
    

    ImGui::End();
}

/*
---------------------------------------------------
ALL OSCILLOSCOPE 
---------------------------------------------------
*/