#pragma once

// Re-applies gamma when external events nudge the GPU LUT. Port of
// GammaWatchdog.cs.
//
// The shell-UI gamma flash happens because opening Action Center / Settings
// briefly resets per-output gamma. Defense: subscribe to every event that
// signals "something just touched gamma" and reapply within a frame.
//
// The WinEvent hooks are installed from a dedicated high-priority thread
// running its own native GetMessage pump, so callbacks deliver with
// sub-frame latency. A message-only window additionally listens for display,
// session, device and power changes. Plus a 1s defensive heartbeat.

#include <windows.h>
#include "Timer.h"

class GammaService;

class GammaWatchdog
{
public:
    explicit GammaWatchdog(GammaService& gamma);
    ~GammaWatchdog();

    GammaWatchdog(const GammaWatchdog&) = delete;
    GammaWatchdog& operator=(const GammaWatchdog&) = delete;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    static DWORD WINAPI HookThreadMain(LPVOID);

    GammaService& gamma_;
    HWND msgWindow_ = nullptr;
    Timer heartbeat_;

    HANDLE hookThread_ = nullptr;
    DWORD hookThreadId_ = 0;
    HWINEVENTHOOK systemHook_ = nullptr;
    HWINEVENTHOOK objectHook_ = nullptr;

    HPOWERNOTIFY hConsoleDisplay_ = nullptr;
    HPOWERNOTIFY hMonitorPower_ = nullptr;
    HPOWERNOTIFY hSessionDisplay_ = nullptr;
};
