#pragma once

#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <vector>
#include <string>

// A simple struct to hold what we find
struct SerialPortInfo {
    std::string portName;    // "COM3"
    std::string identifier;  // "USB Serial Device (COM3)"
};

inline std::vector<SerialPortInfo> GetAvailableComPorts() {
    std::vector<SerialPortInfo> portList;

    // Ask Windows for a list of ALL hardware devices in the "Ports" category (COM & LPT)
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return portList;

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    // Loop through every device Windows found
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); ++i) {
        char friendlyName[256] = {0};
        DWORD buffersize = sizeof(friendlyName);
        
        // Ask Windows for the "Friendly Name" of the device
        if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &deviceInfoData, SPDRP_FRIENDLYNAME, NULL, 
                                              (PBYTE)friendlyName, buffersize, NULL)) {
            
            std::string nameStr(friendlyName);
            
            // We only care about COM ports, ignore parallel LPT ports
            if (nameStr.find("(COM") != std::string::npos) {
                
                // Extract just the "COMX" part from inside the parentheses
                size_t start = nameStr.find("(COM") + 1;
                size_t end = nameStr.find(")", start);
                std::string purePortName = nameStr.substr(start, end - start);

                portList.push_back({purePortName, nameStr});
            }
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo); // Clean up Windows memory
    return portList;
}

