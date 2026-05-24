// KelvinShift — pure Win32 / C++ port entry point. Replaces App.xaml.cs.
//
// Stage 2: core services + tray icon. The preferences window is layered on
// in stage 3; g_showPreferences stays empty until then.

#include "Common.h"
#include "SettingsService.h"
#include "GammaService.h"
#include "ScheduleEngine.h"
#include "GammaWatchdog.h"
#include "GammaRangeService.h"
#include "TrayIconService.h"
#include "PreferencesWindow.h"
#include "Controls.h"
#include "GdiPlusInc.h"

#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <functional>

namespace {

const wchar_t* kMutexName     = L"KelvinShift-SingleInstance-v1";
const wchar_t* kActivateEvent = L"KelvinShift-Activate-v1";
const wchar_t* kAppClassName  = L"KelvinShiftAppWnd";

// Posted to the app window when a second instance asks us to surface the
// preferences window.
const UINT WM_KS_ACTIVATE = WM_APP + 1;

GammaService* g_gammaForCrash = nullptr;
HWND g_appWindow = nullptr;

// Wired up once the preferences window exists (stage 3); the activate path
// and the tray's left-click both call through it.
std::function<void()> g_showPreferences;

// ── Crash handling ────────────────────────────────────────────────────────
void LogCrash(const wchar_t* source)
{
    std::wstring dir = KnownFolder(FOLDERID_LocalAppData) + L"\\KelvinShift";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\crash.log";
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t line[256];
    int n = wsprintfW(line, L"--- %04d-%02d-%02dT%02d:%02d:%02d [%s] ---\r\n",
                      t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, source);
    std::string utf8 = Narrow(std::wstring(line, n));
    DWORD written;
    WriteFile(f, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(f);
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS*)
{
    // Critical: don't strand the user with a warm-tinted screen and no app.
    if (g_gammaForCrash) g_gammaForCrash->Reset();
    LogCrash(L"UnhandledException");
    return EXCEPTION_CONTINUE_SEARCH; // let the OS still terminate us
}

// ── Single-instance plumbing ──────────────────────────────────────────────
DWORD WINAPI ActivateListener(LPVOID param)
{
    HANDLE ev = static_cast<HANDLE>(param);
    while (WaitForSingleObject(ev, INFINITE) == WAIT_OBJECT_0)
    {
        if (g_appWindow) PostMessageW(g_appWindow, WM_KS_ACTIVATE, 0, 0);
    }
    return 0;
}

LRESULT CALLBACK AppWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_KS_ACTIVATE:
            if (g_showPreferences) g_showPreferences();
            return 0;
        // A reinstall over a running copy, the Restart Manager, and a Windows
        // logoff/shutdown all close us through these messages. Routing them to
        // a window-destroy runs the normal message-loop exit, so the gamma
        // ramp is restored before the process goes away.
        case WM_QUERYENDSESSION:
            return TRUE;                       // yes, we can be closed
        case WM_ENDSESSION:
            if (wParam) DestroyWindow(hwnd);   // wParam == FALSE: session not ending
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int ArgValue(int argc, LPWSTR* argv, const wchar_t* flag, int def)
{
    for (int i = 1; i < argc; ++i)
        if (lstrcmpiW(argv[i], flag) == 0 && i + 1 < argc)
            return _wtoi(argv[i + 1]);
    return def;
}

bool HasArg(int argc, LPWSTR* argv, const wchar_t* flag)
{
    for (int i = 1; i < argc; ++i)
        if (lstrcmpiW(argv[i], flag) == 0) return true;
    return false;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    // ── Elevated registry helper ──────────────────────────────────────────
    if (HasArg(argc, argv, GammaRangeService::CliFlag))
    {
        int value = ArgValue(argc, argv, GammaRangeService::CliFlag, -1);
        return value < 0 ? 2 : GammaRangeService::ApplyRegistryValue(value);
    }

    // ── Uninstall hook ────────────────────────────────────────────────────
    if (HasArg(argc, argv, L"--uninstall-cleanup"))
    {
        GammaService g;
        g.Reset();                                 // restore the display
        GammaRangeService::ApplyRegistryValue(0);  // remove the HKLM gamma-range key
        return 0;
    }

    // Debug: render every tray phase glyph to PNGs and exit.
    if (HasArg(argc, argv, L"--dump-icons"))
    {
        ULONG_PTR tok = 0;
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&tok, &in, nullptr);
        TrayIconService::DumpIcons(KnownFolder(FOLDERID_LocalAppData) + L"\\KelvinShift");
        Gdiplus::GdiplusShutdown(tok);
        return 0;
    }

    // ── Single-instance gate ──────────────────────────────────────────────
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, kActivateEvent);
        if (ev) { SetEvent(ev); CloseHandle(ev); }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    SetUnhandledExceptionFilter(&CrashFilter);

    HANDLE activateEvent = CreateEventW(nullptr, FALSE, FALSE, kActivateEvent);
    if (activateEvent)
    {
        HANDLE t = CreateThread(nullptr, 0, &ActivateListener, activateEvent, 0, nullptr);
        if (t) CloseHandle(t);
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, nullptr);

    // ── App control window ────────────────────────────────────────────────
    // A real (never-shown) top-level window — deliberately NOT message-only,
    // so a logoff/shutdown, the installer's Restart Manager, and `taskkill`
    // can all reach it with WM_CLOSE / WM_QUERYENDSESSION. (A message-only
    // window is invisible to all of those — that left a reinstall unable to
    // close the running copy.) WS_EX_TOOLWINDOW keeps it off the taskbar and
    // Alt-Tab; it is never shown anyway.
    WNDCLASSW wc{};
    wc.lpfnWndProc = &AppWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kAppClassName;
    RegisterClassW(&wc);
    g_appWindow = CreateWindowExW(WS_EX_TOOLWINDOW, kAppClassName, L"KelvinShift",
                                  WS_POPUP, 0, 0, 0, 0,
                                  nullptr, nullptr, hInst, nullptr);

    int exitCode = 0;
    {
        // ── Core services ─────────────────────────────────────────────────
        SettingsService settings;
        settings.Load();

        GammaService gamma;
        g_gammaForCrash = &gamma;

        ScheduleEngine engine(settings, gamma);
        GammaWatchdog watchdog(gamma);
        engine.Start();

        PreferencesWindow prefs(settings, engine);
        g_showPreferences = [&prefs] { prefs.Show(); };

        auto quit = [] { PostQuitMessage(0); };
        auto showPrefs = [] { if (g_showPreferences) g_showPreferences(); };

        TrayIconService tray(engine, settings, showPrefs, quit);
        tray.Show();

        // ── Message loop ──────────────────────────────────────────────────
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        exitCode = (int)msg.wParam;

        engine.Stop();          // resets gamma to identity
        g_showPreferences = nullptr;
        g_gammaForCrash = nullptr;
    } // services destruct here (tray icon removed, watchdog thread stopped)

    ReleaseFontCache();
    Gdiplus::GdiplusShutdown(gdipToken);
    if (activateEvent) CloseHandle(activateEvent);
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return exitCode;
}
