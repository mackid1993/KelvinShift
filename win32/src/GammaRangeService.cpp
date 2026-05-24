#include "GammaRangeService.h"
#include "Common.h"
#include <shellapi.h>
#include <string>

namespace {
const wchar_t* kKeyPath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ICM";
const wchar_t* kValueName = L"GdiIcmGammaRange";
}

bool GammaRangeService::IsEnabled()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kKeyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, data = 0, size = sizeof(data);
    LONG r = RegQueryValueExW(key, kValueName, nullptr, &type,
                              reinterpret_cast<BYTE*>(&data), &size);
    RegCloseKey(key);
    return r == ERROR_SUCCESS && type == REG_DWORD && (int)data == EnabledValue;
}

bool GammaRangeService::RequestChange(bool enable)
{
    std::wstring exe = ModulePath();
    if (exe.empty()) return false;

    std::wstring args = std::wstring(CliFlag) + L" " +
                        std::to_wstring(enable ? EnabledValue : 0);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    sei.lpVerb = L"runas";          // triggers the UAC elevation prompt
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei) || !sei.hProcess)
        return false; // user cancelled UAC, or launch failed

    WaitForSingleObject(sei.hProcess, 10000);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code == 0;
}

int GammaRangeService::ApplyRegistryValue(int value)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kKeyPath, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return 3;

    LONG r;
    if (value == EnabledValue)
    {
        DWORD dw = EnabledValue;
        r = RegSetValueExW(key, kValueName, 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
    }
    else
    {
        r = RegDeleteValueW(key, kValueName);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS; // absent is fine
    }
    RegCloseKey(key);
    return r == ERROR_SUCCESS ? 0 : 4;
}
