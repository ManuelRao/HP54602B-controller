#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <string>
#include <windows.h>
#include "imgui.h"

class OscilloscopeManager {
private:
    // Threading controls
    std::thread m_workerThread;
    std::atomic<bool> m_isRunning;
    
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

    

    // The background loop function
    void WorkerLoop();

public:
    OscilloscopeManager(const std::string& comPort);
    ~OscilloscopeManager();

    bool Connect();
    void Disconnect();
    void GetLatestData(std::vector<uint8_t>& outBuffer);
    void SendCommand(const std::string& cmd);

    void drawControlUI();
};