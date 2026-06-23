#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <string>
#include <windows.h>
#include "imgui.h"
#include "HardwareUtils.h" // For COM port enumeration

enum class LogDirection {
    TX,     // Transmit (PC to Scope)
    RX,     // Receive (Scope to PC)
    INFO,   // System messages (Connected, Overrun Error, etc.)
    ERR     // Hardware Errors
};

struct SerialLog {
    LogDirection direction;
    std::string message;
};

class OscilloscopeManager {
private:
    // Threading controls
    std::thread m_workerThread;
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_isConnected;
    
    // Shared memory and gatekeeper
    std::mutex m_dataMutex;
    std::vector<uint8_t> m_latestData;

    // shared comand queue for UI to send instructions to the worker thread (e.g., change timebase, trigger settings, etc.)
    std::mutex m_commandMutex;
    std::vector<std::string> m_commandQueue;

    // shared results queue for worker thread to send back status updates or responses to the UI
    std::mutex m_responseMutex;
    std::unordered_map<std::string, std::string> m_responseMap; // e.x., {"TIMEBASE": "1ms/div", "TRIGGER": "ON"}

    // Windows Serial State
    HANDLE m_hSerial;
    std::string m_comPort;

    int m_timebaseIndex = 3;
    float verticalScale[4] = { 0.5f, 0.5f, 0.5f, 0.5f };

    std::vector<SerialLog> m_terminalLog;
    std::mutex m_logMutex;

    void AddLog(LogDirection dir, const std::string& msg);

    // The background loop function
    void WorkerLoop();
    std::string ReadTextResponse();

public:
    OscilloscopeManager();
    ~OscilloscopeManager();

    // oscilloscope connection management
    bool Connect(const std::string& comPort);
    void Disconnect();
    void GetLatestData(std::vector<uint8_t>& outBuffer);
    void SendCommand(const std::string& cmd);

    void EnqueueCommand(const std::string& command);
    bool GetQueryResult(const std::string& command, std::string& outResult);

    //oscilloscope control functions
    void SetTimebase(const float& timebase);
    void SetTrigger(const int& channel, const float& level, const bool& risingEdge = true, const bool& __auto = true, const bool& noiseRejection = false, const bool& holdoff = false, const float& holdoffTime = 0.0f);

    void SetVerticalScale(const int& channel, const float& scale);
    void SetAcquisitionMode(const std::string& mode);
    void SetAverageCount(const int& count);
    void SetCoupling(const int& channel, const std::string& couplingType);
    void SetHorizontalPosition(const float& position);
    void SetVerticalPosition(const int& channel, const float& position);
    void SetAcquisitionState(bool run);
    void SetMeasurement(const std::string& measurementType, const int& channel);
    void SetDisplayMode(const std::string& mode);
    void SetDataFormat(const std::string& format);
    void SetProbeAttenuation(const int& channel, const std::string& attenuation);
    void SetBandwidthLimit(const int& channel, bool enabled);
    void SetAutoscale();
    void SetChannelOnOff(const int& channel, bool on);
    void GetMeasurementResult(const std::string& measurementType, const int& channel, std::string& outResult);
    void GetCurrentSettings(std::unordered_map<std::string, std::string>& outSettings);
    void GetWaveformData(std::vector<uint8_t>& outBuffer, const int& channel, const std::string& format);
    
    // UI drawing function 
    //----------------------------------
    void drawAllControls();
    void drawConnectionControls();
    void drawBasicPloter(const std::vector<uint8_t>& data);
    void DrawSerialTerminal();
    // individual control drawing functions for each setting category (called by drawControlUI)
    void drawAutoScaleButton();
    void drawTriggerControl();
    void drawTimebaseControl();
    void drawAcquisitionControl();
    void drawMeasurementControlls();
    void drawDisplayModeControl();
    void drawDataFormatControl();
    void drawChannelToggle(const int& channel);
    void drawVerticalScaleControl(const int& channel);
    void drawCouplingControl(const int& channel);
    void drawVerticalPositionControl(const int& channel);
    void drawProbeAttenuationControl(const int& channel);
    void drawBandwidthLimitControl(const int& channel);
    void drawMultiChannelControls(const std::vector<int>& channels); // this will call the individual channel control functions below for each channel in the list
    void drawChannelControls(const int& channel);
    

};