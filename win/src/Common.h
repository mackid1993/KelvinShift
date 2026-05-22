#pragma once

// Shared primitives for the KelvinShift C++ port: string conversion, known
// folder paths, and a tiny multicast Event<> that replaces C# `event`.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <functional>

// ── UTF-8 <-> UTF-16 ──────────────────────────────────────────────────────
inline std::wstring Widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

inline std::string Narrow(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// ── Known folders ─────────────────────────────────────────────────────────
// %APPDATA%   = FOLDERID_RoamingAppData  -> settings.json
// %LOCALAPPDATA% = FOLDERID_LocalAppData -> debug.log / crash.log
inline std::wstring KnownFolder(REFKNOWNFOLDERID id)
{
    PWSTR p = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p)
        result = p;
    if (p) CoTaskMemFree(p);
    return result;
}

// Full path to this executable.
inline std::wstring ModulePath()
{
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

// ── Multicast event ───────────────────────────────────────────────────────
// Stand-in for C# `event` / delegate. Handlers are kept for the lifetime of
// the owning object; KelvinShift never unsubscribes (everything lives until
// quit), so there is no removal API.
template <class... Args>
class Event
{
public:
    void Add(std::function<void(Args...)> handler)
    {
        handlers_.push_back(std::move(handler));
    }
    void Raise(Args... args) const
    {
        for (const auto& h : handlers_) h(args...);
    }

private:
    std::vector<std::function<void(Args...)>> handlers_;
};
