#pragma once

// Preferences window — the C++ replacement for PreferencesWindow.xaml. A
// custom-chrome, Mica-backed window with the same five cards as the WPF /
// macOS builds: Color Temperature, Brightness, Schedule, Transitions,
// General. Controls are windowless (see Controls.h); only the numeric text
// fields are real EDIT children.

#include "Common.h"
#include "Controls.h"
#include "TrayMenu.h"
#include <vector>
#include <string>
#include <functional>

class SettingsService;
class ScheduleEngine;

class PreferencesWindow
{
public:
    PreferencesWindow(SettingsService& settings, ScheduleEngine& engine);
    ~PreferencesWindow();

    PreferencesWindow(const PreferencesWindow&) = delete;
    PreferencesWindow& operator=(const PreferencesWindow&) = delete;

    void Show();   // create-if-needed, restore, bring to front

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    void BuildUI();
    void Layout();
    void Paint();
    void SyncFromSettings();
    void PositionFields();
    void RecreateEditFont();

    void OnMouseDown(POINT clientPt);
    void OnMouseMove(POINT clientPt);
    void OnMouseUp(POINT clientPt);
    void OnWheel(int delta);
    void StartScrollAnim();
    void StepScroll();
    void OpenDropdown(int widgetId);
    void BeginFieldEdit(int fieldIndex);
    void CommitField(int fieldId);

    int  TitleBarH() const;
    int  ViewportH() const;
    int  MaxScroll() const;
    POINT ToContent(POINT clientPt) const;   // client -> content space
    int  HitWidget(POINT contentPt) const;   // w_ index, or -1
    int  CaptionHitTest(POINT clientPt) const; // 0 = min, 1 = close, -1 = none

    SettingsService& settings_;
    ScheduleEngine& engine_;

    HWND hwnd_ = nullptr;
    int  dpi_ = 96;
    int  scrollY_ = 0;          // rendered (rounded) scroll offset
    int  contentH_ = 0;
    double scrollYf_ = 0.0;     // sub-pixel scroll position
    double scrollVel_ = 0.0;    // scroll velocity (inertia / momentum)
    bool scrollAnim_ = false;   // inertia timer running
    bool placed_ = false;
    bool built_ = false;

    std::vector<Widget> w_;
    struct CardBox { RECT r{}; std::wstring title; int iconId = -1; };
    std::vector<CardBox> cards_;
    std::vector<std::vector<int>> cardIds_;   // widget ids drawn in each card

    int   captured_ = -1;        // w_ index being dragged
    int   hoverWidget_ = -1;     // w_ index under the cursor (repaint guard)
    int   editingField_ = -1;    // fields_ index currently open for editing
    int   captionHot_ = -1;
    int   captionDown_ = -1;
    bool  scrollDrag_ = false;
    int   scrollDragOrigin_ = 0, scrollDragStartY_ = 0;
    POINT mouse_{ -1, -1 };      // last content-space mouse position

    struct NumField
    {
        HWND hwnd = nullptr;
        int  id = 0;
        bool isInt = true;
        RECT rect{};                       // content space
        RECT placed{};                     // last MoveWindow rect (flicker guard)
        bool visible = true;
        std::function<bool()> visibleIf;
        std::function<void(double)> commit;
        std::function<double()> read;
    };
    std::vector<NumField> fields_;
    HFONT  editFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    HICON  appIcon_ = nullptr;
    TrayMenu dropdownPopup_;

    std::wstring locationError_;
    std::wstring extRangeStatus_;
    bool locating_ = false;
};
