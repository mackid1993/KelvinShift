#include "LaunchAtLoginService.h"
#include "Common.h"

namespace {
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kValueName = L"KelvinShift";
}

bool LaunchAtLoginService::IsEnabled()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    LONG r = RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, nullptr);
    RegCloseKey(key);
    return r == ERROR_SUCCESS && type == REG_SZ;
}

void LaunchAtLoginService::Apply(bool enabled)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;

    if (enabled)
    {
        // Quoted exe path + the --tray flag (start minimized to tray).
        std::wstring value = L"\"" + ModulePath() + L"\" --tray";
        RegSetValueExW(key, kValueName, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(key, kValueName); // no-op if absent
    }
    RegCloseKey(key);
}
