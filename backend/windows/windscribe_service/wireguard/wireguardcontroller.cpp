#include "../all_headers.h"

#include <Cfgmgr32.h>
#include <devguid.h>
#include <ndisguid.h>
#include <winioctl.h>

#include <codecvt>
#include <sstream>

#include <boost/filesystem/path.hpp>

#include "wireguardcontroller.h"
#include "wireguard.h"
#include "../logger.h"
#include "../utils.h"
#include "../ipc/servicecommunication.h"
#include "../../../common/utils/servicecontrolmanager.h"
#include "../../../common/utils/win32handle.h"
#include "../../../common/utils/wsscopeguard.h"

static const DEVPROPKEY WG_DEVP_KEYNAME = DEVPKEY_WG_NAME;


WireGuardController::WireGuardController()
{
}

bool WireGuardController::installService(const std::wstring &exeName, const std::wstring &configFile)
{
    if (!Utils::isFileExists(configFile.c_str()))
    {
        Logger::instance().out(
            L"WireGuardController::installService - the wireguard configuration file does not exist (%s)", configFile.c_str());
        return false;
    }

    is_initialized_ = false;
    try
    {
        {
            boost::filesystem::path path(configFile);
            deviceName_ = path.stem().native();
            std::wostringstream stream;
            stream << L"WireGuardTunnel$" << deviceName_;
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
            serviceName_ = converter.to_bytes(stream.str());
        }

        std::string serviceCmdLine;
        {
            std::wostringstream stream;
            stream << L"\"" << Utils::getExePath() << L"\\" << exeName << ".exe\" \"" << configFile << L"\"";
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
            serviceCmdLine = converter.to_bytes(stream.str());
        }

        wsl::ServiceControlManager svcCtrl;
        svcCtrl.openSCM(SC_MANAGER_ALL_ACCESS);

        if (svcCtrl.isServiceInstalled(serviceName_.c_str()))
        {
            Logger::instance().out("WireGuardController::installService - deleting existing wireguard service instance");
            svcCtrl.deleteService(serviceName_.c_str());
        }

        Logger::instance().out("WireGuardController::installService - command-line: %s", serviceCmdLine.c_str());

        svcCtrl.installService(serviceName_.c_str(), serviceCmdLine.c_str(),
            "Windscribe Wireguard Tunnel", "Manages the Windscribe wireguard tunnel connection",
            SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, "Nsi\0TcpIp\0", true);

        svcCtrl.setServiceSIDType(SERVICE_SID_TYPE_UNRESTRICTED);

        is_initialized_ = true;
    }
    catch (std::system_error& ex)
    {
        Logger::instance().out("WireGuardController::installService - %s", ex.what());
    }

    return is_initialized_;
}

bool WireGuardController::deleteService()
{
    is_initialized_ = false;

    if (serviceName_.empty()) {
        return true;
    }

    bool bServiceDeleted = false;
    try
    {
        wsl::ServiceControlManager svcCtrl;
        svcCtrl.openSCM(SC_MANAGER_ALL_ACCESS);

        if (svcCtrl.isServiceInstalled(serviceName_.c_str()))
        {
            Logger::instance().out("WireGuardController::deleteService - deleting wireguard service instance");
            svcCtrl.deleteService(serviceName_.c_str());
        }

        bServiceDeleted = true;
    }
    catch (std::system_error& ex)
    {
        Logger::instance().out("WireGuardController::deleteService - %s", ex.what());
    }

    return bServiceDeleted;
}

UINT WireGuardController::getStatus(UINT64& lastHandshake, UINT64& txBytes, UINT64& rxBytes) const
{
    UINT result = WIREGUARD_STATE_ACTIVE;

    try
    {
        if (!is_initialized_) {
            throw std::system_error(ERROR_INVALID_STATE, std::generic_category(),
                "WireGuardController::getStatus - the wireguard tunnel is not initialized");
        }

        WinUtils::Win32Handle hDriver(getKernelInterfaceHandle());

        // Look at kernel_get_device() in wireguard-windows-0.5.3\.deps\src\ipc-windows.h for
        // sample code showing how to parse the structures returned from the wireguard-nt
        // kernel driver when we send it the WG_IOCTL_GET io control command.

        DWORD bufferSize = 4096;
        std::unique_ptr< BYTE[] > buffer(new BYTE[bufferSize]);

        // Only perform max 3 attempts, just in case we keep getting ERROR_MORE_DATA for some reason.
        BOOL apiResult = FALSE;
        for (int i = 0; !apiResult && i < 3; ++i)
        {
            apiResult = ::DeviceIoControl(hDriver.getHandle(), WG_IOCTL_GET, NULL, 0, buffer.get(),
                                          bufferSize, &bufferSize, NULL);
            if (!apiResult)
            {
                if (::GetLastError() != ERROR_MORE_DATA) {
                    throw std::system_error(::GetLastError(), std::generic_category(),
                        "WireGuardController::getStatus - DeviceIoControl failed");
                }

                buffer.reset(new BYTE[bufferSize]);
            }
        }

        if (!apiResult)
        {
            throw std::system_error(ERROR_UNIDENTIFIED_ERROR, std::generic_category(),
                "WireGuardController::getStatus - DeviceIoControl failed repeatedly");
        }

        if (bufferSize < sizeof(WG_IOCTL_INTERFACE))
        {
            throw std::system_error(ERROR_INVALID_DATA, std::generic_category(),
                std::string("WireGuardController::getStatus - DeviceIoControl returned ") + std::to_string(bufferSize) +
                std::string(" bytes, expected ") + std::to_string(sizeof(WG_IOCTL_INTERFACE)));
        }

        WG_IOCTL_INTERFACE* wgInterface = (WG_IOCTL_INTERFACE*)buffer.get();
        if (wgInterface->PeersCount > 0)
        {
            if (bufferSize < sizeof(WG_IOCTL_INTERFACE) + sizeof(WG_IOCTL_PEER)) {
                throw std::system_error(ERROR_INVALID_DATA, std::generic_category(),
                    std::string("WireGuardController::getStatus - DeviceIoControl returned ") + std::to_string(bufferSize) +
                    std::string(" bytes, expected ") + std::to_string(sizeof(WG_IOCTL_INTERFACE) + sizeof(WG_IOCTL_PEER)));
            }

            WG_IOCTL_PEER* wgPeerInfo = (WG_IOCTL_PEER*)(buffer.get() + sizeof(WG_IOCTL_INTERFACE));
            lastHandshake = wgPeerInfo->LastHandshake;
            txBytes = wgPeerInfo->TxBytes;
            rxBytes = wgPeerInfo->RxBytes;
        }
        else
        {
            lastHandshake = 0;
            txBytes = 0;
            rxBytes = 0;
        }
    }
    catch (std::system_error& ex)
    {
        result = WIREGUARD_STATE_ERROR;
        Logger::instance().out("WireGuardController::getStatus - %s", ex.what());
    }

    return result;
}

HANDLE WireGuardController::getKernelInterfaceHandle() const
{
    HDEVINFO devInfo = ::SetupDiGetClassDevsExW(&GUID_DEVCLASS_NET,
                                                (Utils::isWindows7() ? L"ROOT\\WIREGUARD" : L"SWD\\WireGuard"),
                                                NULL, DIGCF_PRESENT, NULL, NULL, NULL);
    if (devInfo == INVALID_HANDLE_VALUE) {
        throw std::system_error(::GetLastError(), std::generic_category(),
            "WireGuardController::getKernelInterfaceHandle - SetupDiGetClassDevsExW failed");
    }

    auto exitGuard = wsl::wsScopeGuard([&]
    {
        ::SetupDiDestroyDeviceInfoList(devInfo);
    });

    HANDLE hKernelInterface = INVALID_HANDLE_VALUE;

    const DWORD bufferSize = 4096;
    std::unique_ptr< BYTE[] > buffer(new BYTE[bufferSize]);

    for (DWORD i = 0; true; ++i)
    {
        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        BOOL result = ::SetupDiEnumDeviceInfo(devInfo, i, &devInfoData);
        if (result == FALSE)
        {
            if (::GetLastError() == ERROR_NO_MORE_ITEMS) {
                break;
            }

            continue;
        }

        DEVPROPTYPE propType;
        DWORD requiredSize;
        result = ::SetupDiGetDevicePropertyW(devInfo, &devInfoData, &WG_DEVP_KEYNAME, &propType,
                                             buffer.get(), bufferSize, &requiredSize, 0);

        if (result == FALSE || propType != DEVPROP_TYPE_STRING) {
            continue;
        }

        // requiredSize count includes the null terminator.
        std::wstring adapterName((LPCWSTR)buffer.get(), requiredSize / sizeof(wchar_t) - 1);
        if (!Utils::iequals(adapterName, deviceName_)) {
            continue;
        }

        result = ::SetupDiGetDeviceInstanceIdW(devInfo, &devInfoData, (PWSTR)buffer.get(),
                                               bufferSize / sizeof(wchar_t), &requiredSize);
        if (result == FALSE) {
            throw std::system_error(::GetLastError(), std::generic_category(),
                "WireGuardController::getKernelInterfaceHandle - SetupDiGetDeviceInstanceIdW failed");
        }

        CONFIGRET confRet = ::CM_Get_Device_Interface_List_SizeW(&requiredSize, (GUID*)&GUID_DEVINTERFACE_NET,
                                                                 (DEVINSTID_W)buffer.get(), CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (confRet != CR_SUCCESS) {
            throw std::system_error(0, std::generic_category(),
                std::string("WireGuardController::getKernelInterfaceHandle - CM_Get_Device_Interface_List_SizeW failed: ") + std::to_string(confRet));
        }

        std::unique_ptr< wchar_t > interfaceName(new wchar_t[requiredSize]);

        confRet = ::CM_Get_Device_Interface_ListW((GUID*)&GUID_DEVINTERFACE_NET, (DEVINSTID_W)buffer.get(),
                                                  interfaceName.get(), requiredSize, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (confRet != CR_SUCCESS) {
            throw std::system_error(0, std::generic_category(),
                std::string("WireGuardController::getKernelInterfaceHandle - CM_Get_Device_Interface_ListW failed: ") + std::to_string(confRet));
        }

        size_t nameLen = wcsnlen_s(interfaceName.get(), requiredSize);

        if (nameLen == 0 || nameLen == requiredSize) {
            throw std::system_error(0, std::generic_category(),
                std::string("WireGuardController::getKernelInterfaceHandle - CM_Get_Device_Interface_ListW returned an invalid buffer: ") + std::to_string(nameLen));
        }

        hKernelInterface = ::CreateFileW(interfaceName.get(), GENERIC_READ | GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         NULL, OPEN_EXISTING, 0, NULL);

        if (hKernelInterface == INVALID_HANDLE_VALUE)
        {
            std::wostringstream stream;
            stream << L"WireGuardController::getKernelInterfaceHandle - CreateFileW failed to open " << interfaceName.get();
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
            throw std::system_error(::GetLastError(), std::generic_category(), converter.to_bytes(stream.str()));
        }

        break;
    }
    
    if (hKernelInterface == INVALID_HANDLE_VALUE) {
        throw std::system_error(ERROR_FILE_NOT_FOUND, std::generic_category(),
            "WireGuardController::getKernelInterfaceHandle - could not find the wireguard-nt kernal interface file descriptor");
    }

    return hKernelInterface;
}
