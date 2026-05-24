#pragma once

// Tray icon: a state-aware glyph (sun / moon / bed / transition / off),
// regenerated on every state change, tooltip showing current Kelvin +
// brightness, left-click opens Preferences, right-click shows the menu.
// Port of TrayIconService.cs — the glyphs are drawn with the GDI+ C++ API,
// which maps 1:1 onto the C#'s System.Drawing, so the icons are identical.

#include "Common.h"
#include "TrayMenu.h"
#include <functional>
#include <vector>

class ScheduleEngine;
class SettingsService;
struct ScheduleState;

class TrayIconService
{
public:
    TrayIconService(ScheduleEngine& engine, SettingsService& settings,
                    std::function<void()> openPrefs, std::function<void()> quit);
    ~TrayIconService();

    TrayIconService(const TrayIconService&) = delete;
    TrayIconService& operator=(const TrayIconService&) = delete;

    void Show();

    // Debug aid: renders every phase glyph to PNG files in `dir`, with a
    // centre crosshair, for inspection. Invoked via the --dump-icons flag.
    static void DumpIcons(const std::wstring& dir);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void Refresh();
    void OnTrayCallback(WPARAM wParam, LPARAM lParam);
    std::vector<MenuItem> BuildMenuItems();
    HICON RenderStateIcon(const ScheduleState& s);

    ScheduleEngine& engine_;
    SettingsService& settings_;
    std::function<void()> openPrefs_;
    std::function<void()> quit_;

    HWND hwnd_ = nullptr;
    HICON currentIcon_ = nullptr; // owned, dynamically rendered
    HICON fallbackIcon_ = nullptr; // shared PE resource, not destroyed
    TrayMenu menu_;
    bool iconAdded_ = false;
    UINT taskbarCreatedMsg_ = 0;
};
