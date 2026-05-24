#pragma once

// Dark context menu for the tray and the preferences-window dropdowns.
//
// Built on the OS's TrackPopupMenuEx + an owner-drawn HMENU: the modal loop,
// hit-testing and click-away dismissal are all handled by Windows (battle-
// tested), while owner-draw + a dark MIM_BACKGROUND brush give it the dark
// Fluent look. Win11 rounds the popup and adds the shadow automatically.

#include "Common.h"
#include <string>
#include <vector>
#include <functional>

struct MenuItem
{
    int iconId = -1;        // optional leading vector icon (DrawIconGlyph)
    std::wstring text;      // label, always drawn at the same x
    std::wstring value;     // optional: drawn in an aligned right column
    bool separator = false;
    bool enabled = true;
    bool checkable = false;
    bool checked = false;
    std::function<void()> onClick;
};

class TrayMenu
{
public:
    TrayMenu() = default;
    ~TrayMenu();

    // x,y = screen anchor. Tray style (below=false) opens up-and-left of the
    // anchor; dropdown style (below=true) opens down-and-right, at least
    // minWidth wide. Blocks until an item is chosen or the menu is dismissed,
    // then invokes the chosen item's callback.
    void Track(std::vector<MenuItem> items, int x, int y,
               bool below = false, int minWidth = 0);

private:
    static LRESULT CALLBACK OwnerProc(HWND, UINT, WPARAM, LPARAM);
    void EnsureOwner();
    void MeasureItem(MEASUREITEMSTRUCT* m);
    void DrawItem(const DRAWITEMSTRUCT* d);

    HWND owner_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH bg_ = nullptr;
    std::vector<MenuItem> items_;
    int dpi_ = 96;
    int minWidth_ = 0;
    int valueColX_ = 0;     // x (from item left) where value text aligns
};
