#include "Timer.h"
#include <map>

// Maps the OS-assigned timer id back to the owning Timer so the static
// TimerProc can dispatch. Single-threaded (app thread) — no lock needed.
static std::map<UINT_PTR, Timer*>& Registry()
{
    static std::map<UINT_PTR, Timer*> r;
    return r;
}

void CALLBACK Timer::Proc(HWND, UINT, UINT_PTR id, DWORD)
{
    auto& reg = Registry();
    auto it = reg.find(id);
    if (it != reg.end() && it->second->tick_)
        it->second->tick_();
}

void Timer::Start()
{
    Stop();
    // SetTimer with a NULL window returns a fresh system-wide id; WM_TIMER
    // for it carries the TimerProc in lParam, so DispatchMessage calls Proc.
    id_ = SetTimer(nullptr, 0, intervalMs_, &Timer::Proc);
    if (id_) Registry()[id_] = this;
}

void Timer::Stop()
{
    if (id_)
    {
        KillTimer(nullptr, id_);
        Registry().erase(id_);
        id_ = 0;
    }
}
