#pragma once

// Replacement for WPF's DispatcherTimer. Backed by SetTimer(NULL, ...): the
// WM_TIMER lands in the creating thread's message queue and DispatchMessage
// invokes the callback inline — same single-threaded model as the dispatcher.
//
// Every Timer must be created and Start/Stop'd on the app (message-loop)
// thread. The callback runs on that thread.

#include <windows.h>
#include <functional>

class Timer
{
public:
    Timer() = default;
    Timer(unsigned intervalMs, std::function<void()> tick)
        : intervalMs_(intervalMs), tick_(std::move(tick)) {}
    ~Timer() { Stop(); }

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    void SetInterval(unsigned ms) { intervalMs_ = ms; }
    void SetCallback(std::function<void()> tick) { tick_ = std::move(tick); }

    // Restarts the timer from zero (matches DispatcherTimer.Start after Stop).
    void Start();
    void Stop();
    bool IsRunning() const { return id_ != 0; }

private:
    static void CALLBACK Proc(HWND, UINT, UINT_PTR id, DWORD);

    unsigned intervalMs_ = 1000;
    std::function<void()> tick_;
    UINT_PTR id_ = 0;
};
