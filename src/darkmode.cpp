// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "darkmode.h"

#include <commctrl.h>
#include <tchar.h>
#include <uxtheme.h>

namespace
{
typedef HRESULT(WINAPI * PFNDWMSETWINDOWATTRIBUTE)(HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD cbAttribute);

PFNDWMSETWINDOWATTRIBUTE DwmSetWindowAttributePtr = NULL;
BOOL Initialized = FALSE;
int ThemeMode = THEME_MODE_LIGHT;
BOOL InitSupportLogged = FALSE;
BOOL SupportWarningLogged = FALSE;
BOOL CaptionColorAttrSupported = TRUE;
BOOL TextColorAttrSupported = TRUE;
BOOL BorderColorAttrSupported = TRUE;
thread_local int ListTreeThemeApplyDepth = 0;
thread_local int GroupBoxThemeApplyDepth = 0;

const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_NEW = 20; // Win10 1903+
const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19; // older Win10 builds
const DWORD DWMWA_BORDER_COLOR = 34;
const DWORD DWMWA_CAPTION_COLOR = 35;
const DWORD DWMWA_TEXT_COLOR = 36;
const COLORREF DWMWA_COLOR_DEFAULT = 0xFFFFFFFF;
const COLORREF MAINFRAME_DARK_FILL = RGB(45, 45, 48);
const COLORREF MAINFRAME_DARK_LINE_DARK = RGB(28, 28, 28);
const COLORREF MAINFRAME_DARK_LINE_LIGHT = RGB(62, 62, 66);
const COLORREF MAINFRAME_DARK_BORDER = RGB(70, 70, 70);
const COLORREF DIALOG_DARK_BG = RGB(45, 45, 48);
const COLORREF DIALOG_DARK_TEXT = RGB(232, 232, 232);
const COLORREF DIALOG_DARK_INPUT_BG = RGB(30, 30, 30);
const COLORREF DIALOG_DARK_INPUT_TEXT = RGB(245, 245, 245);
const COLORREF DIALOG_DARK_DISABLED_TEXT = RGB(160, 160, 160);
const COLORREF DIALOG_DARK_HIGHLIGHT = RGB(0, 120, 215);
const COLORREF DIALOG_DARK_INACTIVE_SELECTION = RGB(75, 75, 78);
const COLORREF DIALOG_DARK_TOOLTIP_BG = RGB(43, 43, 43);
const COLORREF DIALOG_DARK_FRAME = RGB(62, 62, 66);
const COLORREF DIALOG_DARK_SUBTLE_LINE = RGB(55, 55, 58);
const TCHAR* IMMERSIVE_COLOR_SET_PARAM = TEXT("ImmersiveColorSet");
const TCHAR* WINDOWS_THEME_ELEMENT_PARAM = TEXT("WindowsThemeElement");
const TCHAR* BUTTON_CLASS_NAME = TEXT("Button");
const TCHAR* COMBOBOX_CLASS_NAME = TEXT("ComboBox");
const TCHAR* EDIT_CLASS_NAME = TEXT("Edit");
const TCHAR* HEADER_CLASS_NAME = TEXT("SysHeader32");
const TCHAR* LISTBOX_CLASS_NAME = TEXT("ListBox");
const TCHAR* SCROLLBAR_CLASS_NAME = TEXT("ScrollBar");
const TCHAR* STATIC_CLASS_NAME = TEXT("Static");
const WCHAR* UXTHEME_DARKMODE_EXPLORER = L"DarkMode_Explorer";
const WCHAR* UXTHEME_EXPLORER = L"Explorer";
const UINT_PTR GROUPBOX_SUBCLASS_ID = 1;
const UINT_PTR STATIC_EDGE_SUBCLASS_ID = 2;
const UINT_PTR HEADER_SUBCLASS_ID = 4;

HBRUSH DialogDarkBrush = NULL;
HBRUSH DialogDarkInputBrush = NULL;

void DebugOutA(const char* text)
{
    OutputDebugStringA(text);
}

int NormalizeThemeMode(int mode)
{
    if (mode == THEME_MODE_DARK || mode == THEME_MODE_SYSTEM)
        return mode;
    return THEME_MODE_LIGHT;
}

BOOL IsTopLevelWindow(HWND hwnd)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return FALSE;

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    return (style & WS_CHILD) == 0;
}

BOOL IsHighContrastEnabled()
{
    HIGHCONTRAST highContrast = {0};
    highContrast.cbSize = sizeof(highContrast);
    if (!SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0))
        return FALSE;
    return (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

BOOL ReadSystemPrefersDarkApps()
{
    HKEY hKey = NULL;
    if (RegOpenKeyEx(HKEY_CURRENT_USER,
                     SAL_REG_KEY_WINDOWS_THEME_PERSONALIZE_T,
                     0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;

    DWORD value = 1;
    DWORD valueSize = sizeof(value);
    DWORD type = 0;
    LONG regRet = RegQueryValueEx(hKey, SAL_REG_VALUE_APPS_USE_LIGHT_THEME_T, NULL, &type, (LPBYTE)&value, &valueSize);
    RegCloseKey(hKey);

    if (regRet != ERROR_SUCCESS || type != REG_DWORD)
        return FALSE;

    return value == 0;
}

void EnsureInitialized()
{
    if (Initialized)
        return;

    Initialized = TRUE;

    HMODULE hDwm = GetModuleHandle(TEXT("dwmapi.dll"));
    if (hDwm == NULL)
        hDwm = LoadLibrary(TEXT("dwmapi.dll"));

    if (hDwm != NULL)
        DwmSetWindowAttributePtr = (PFNDWMSETWINDOWATTRIBUTE)GetProcAddress(hDwm, "DwmSetWindowAttribute");

    if (!InitSupportLogged)
    {
        InitSupportLogged = TRUE;
        TRACE_I("DarkMode init: Windows10AndLater=" << Windows10AndLater
                                                    << ", DwmSetWindowAttribute=" << (void*)DwmSetWindowAttributePtr);
        char msg[200];
        sprintf_s(msg, "DarkMode init: Windows10AndLater=%d DwmSetWindowAttribute=%p\n",
                  (int)Windows10AndLater, (void*)DwmSetWindowAttributePtr);
        DebugOutA(msg);
    }
}

BOOL ShouldUseDarkColorsInternal()
{
    if (IsHighContrastEnabled())
        return FALSE;

    switch (NormalizeThemeMode(ThemeMode))
    {
    case THEME_MODE_DARK:
        return TRUE;

    case THEME_MODE_SYSTEM:
        return ReadSystemPrefersDarkApps();

    default:
        return FALSE;
    }
}

BOOL IsThemeSettingHint(LPARAM lParam)
{
    // WM_SETTINGCHANGE with NULL lParam is used for many unrelated updates.
    if (lParam == 0)
        return FALSE;

    LPCTSTR valueName = (LPCTSTR)lParam;
    if (valueName == NULL || *valueName == 0)
        return FALSE;

    return _tcsicmp(valueName, IMMERSIVE_COLOR_SET_PARAM) == 0 ||
           _tcsicmp(valueName, WINDOWS_THEME_ELEMENT_PARAM) == 0;
}

BOOL CALLBACK ApplyThreadWindowProc(HWND hwnd, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    DarkMode_ApplyTitleBar(hwnd);
    DarkMode_ApplyListTreeThemeRecursive(hwnd);
    return TRUE;
}

void EnsureDialogBrushes()
{
    if (DialogDarkBrush == NULL)
        DialogDarkBrush = CreateSolidBrush(DIALOG_DARK_BG);
    if (DialogDarkInputBrush == NULL)
        DialogDarkInputBrush = CreateSolidBrush(DIALOG_DARK_INPUT_BG);
}

void FillRectSolid(HDC hdc, const RECT* rect, COLORREF color)
{
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    COLORREF oldColor = SetDCBrushColor(hdc, color);
    FillRect(hdc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    SetDCBrushColor(hdc, oldColor);
    SelectObject(hdc, oldBrush);
}

void DrawRectOutline(HDC hdc, const RECT* rect, COLORREF color)
{
    if (rect == NULL || rect->right <= rect->left || rect->bottom <= rect->top)
        return;

    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    COLORREF oldColor = SetDCPenColor(hdc, color);
    Rectangle(hdc, rect->left, rect->top, rect->right, rect->bottom);
    SetDCPenColor(hdc, oldColor);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
}

BOOL HasClassName(HWND hwnd, LPCTSTR expectedClassName)
{
    if (hwnd == NULL || expectedClassName == NULL || !IsWindow(hwnd))
        return FALSE;

    TCHAR className[64] = {0};
    if (GetClassName(hwnd, className, _countof(className)) == 0)
        return FALSE;

    return _tcsicmp(className, expectedClassName) == 0;
}

BOOL HasAnyClassName(HWND hwnd, LPCTSTR firstClassName, LPCTSTR secondClassName = NULL, LPCTSTR thirdClassName = NULL)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return FALSE;

    TCHAR className[64] = {0};
    if (GetClassName(hwnd, className, _countof(className)) == 0)
        return FALSE;

    return (firstClassName != NULL && _tcsicmp(className, firstClassName) == 0) ||
           (secondClassName != NULL && _tcsicmp(className, secondClassName) == 0) ||
           (thirdClassName != NULL && _tcsicmp(className, thirdClassName) == 0);
}

BOOL IsGroupBox(HWND hwnd)
{
    if (!HasClassName(hwnd, BUTTON_CLASS_NAME))
        return FALSE;

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    return (style & BS_TYPEMASK) == BS_GROUPBOX;
}

BOOL IsStaticEdge(HWND hwnd)
{
    if (!HasClassName(hwnd, STATIC_CLASS_NAME))
        return FALSE;

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    switch (style & SS_TYPEMASK)
    {
    case SS_ETCHEDHORZ:
    case SS_ETCHEDVERT:
    case SS_GRAYFRAME:
    case SS_BLACKFRAME:
    case SS_WHITEFRAME:
        return TRUE;
    }
    return FALSE;
}

void InvalidateGroupBox(HWND hwnd)
{
    if (hwnd != NULL && IsWindow(hwnd))
        InvalidateRect(hwnd, NULL, TRUE);
}

BOOL PaintDarkStaticEdge(HWND hwnd, HDC paintDC)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return FALSE;

    PAINTSTRUCT ps;
    HDC hdc = paintDC;
    if (hdc == NULL)
        hdc = BeginPaint(hwnd, &ps);
    if (hdc == NULL)
        return FALSE;

    RECT client;
    GetClientRect(hwnd, &client);
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    switch (style & SS_TYPEMASK)
    {
    case SS_ETCHEDHORZ:
    {
        FillRectSolid(hdc, &client, colors.DialogBackground);
        int y = max(client.top, min(client.bottom - 1, (client.top + client.bottom) / 2));
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
        COLORREF oldColor = SetDCPenColor(hdc, DIALOG_DARK_SUBTLE_LINE);
        MoveToEx(hdc, client.left, y, NULL);
        LineTo(hdc, client.right, y);
        SetDCPenColor(hdc, oldColor);
        SelectObject(hdc, oldPen);
        break;
    }

    case SS_ETCHEDVERT:
    {
        FillRectSolid(hdc, &client, colors.DialogBackground);
        int x = max(client.left, min(client.right - 1, (client.left + client.right) / 2));
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
        COLORREF oldColor = SetDCPenColor(hdc, DIALOG_DARK_SUBTLE_LINE);
        MoveToEx(hdc, x, client.top, NULL);
        LineTo(hdc, x, client.bottom);
        SetDCPenColor(hdc, oldColor);
        SelectObject(hdc, oldPen);
        break;
    }

    default:
        FillRectSolid(hdc, &client, colors.DialogBackground);
        InflateRect(&client, -1, -1);
        if (client.right > client.left && client.bottom > client.top)
            DrawRectOutline(hdc, &client, DIALOG_DARK_SUBTLE_LINE);
        break;
    }

    if (paintDC == NULL)
        EndPaint(hwnd, &ps);
    return TRUE;
}

BOOL PaintDarkHeader(HWND hwnd, HDC paintDC)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return FALSE;

    PAINTSTRUCT ps;
    HDC hdc = paintDC;
    if (hdc == NULL)
        hdc = BeginPaint(hwnd, &ps);
    if (hdc == NULL)
        return FALSE;

    RECT client;
    GetClientRect(hwnd, &client);
    FillRectSolid(hdc, &client, colors.InputBackground);

    HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
    HFONT hOldFont = NULL;
    if (hFont != NULL)
        hOldFont = (HFONT)SelectObject(hdc, hFont);

    int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    COLORREF oldTextColor = SetTextColor(hdc, colors.InputText);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    COLORREF oldPenColor = SetDCPenColor(hdc, DIALOG_DARK_FRAME);

    int count = Header_GetItemCount(hwnd);
    for (int i = 0; i < count; i++)
    {
        RECT itemRect;
        if (!Header_GetItemRect(hwnd, i, &itemRect))
            continue;

        TCHAR text[256] = {0};
        HDITEM item = {0};
        item.mask = HDI_TEXT | HDI_FORMAT;
        item.pszText = text;
        item.cchTextMax = _countof(text);
        Header_GetItem(hwnd, i, &item);

        RECT textRect = itemRect;
        textRect.left += 6;
        textRect.right -= 6;
        DWORD flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
        if ((item.fmt & HDF_CENTER) != 0)
            flags |= DT_CENTER;
        else if ((item.fmt & HDF_RIGHT) != 0)
            flags |= DT_RIGHT;
        else
            flags |= DT_LEFT;
        DrawText(hdc, text, -1, &textRect, flags);

        MoveToEx(hdc, itemRect.right - 1, itemRect.top, NULL);
        LineTo(hdc, itemRect.right - 1, itemRect.bottom);
    }

    MoveToEx(hdc, client.left, client.bottom - 1, NULL);
    LineTo(hdc, client.right, client.bottom - 1);

    SetDCPenColor(hdc, oldPenColor);
    SelectObject(hdc, oldPen);
    SetTextColor(hdc, oldTextColor);
    SetBkMode(hdc, oldBkMode);
    if (hOldFont != NULL)
        SelectObject(hdc, hOldFont);

    if (paintDC == NULL)
        EndPaint(hwnd, &ps);
    return TRUE;
}

DWORD GetGroupBoxTextFlags(HWND hwnd)
{
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    DWORD flags = DT_SINGLELINE | DT_VCENTER;

    if ((style & BS_CENTER) == BS_CENTER)
        flags |= DT_CENTER;
    else if ((style & BS_RIGHT) == BS_RIGHT)
        flags |= DT_RIGHT;
    else
        flags |= DT_LEFT;

    LRESULT uiState = SendMessage(hwnd, WM_QUERYUISTATE, 0, 0);
    if ((uiState & UISF_HIDEACCEL) != 0)
        flags |= DT_HIDEPREFIX;

    return flags;
}

BOOL PaintDarkGroupBox(HWND hwnd, HDC paintDC)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return FALSE;

    PAINTSTRUCT ps;
    HDC hdc = paintDC;
    if (hdc == NULL)
        hdc = BeginPaint(hwnd, &ps);
    if (hdc == NULL)
        return FALSE;

    RECT client;
    GetClientRect(hwnd, &client);
    if (client.right > client.left && client.bottom > client.top)
    {
        HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        if (hFont == NULL)
            hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        int oldBkMode = SetBkMode(hdc, TRANSPARENT);
        COLORREF oldTextColor = SetTextColor(hdc, IsWindowEnabled(hwnd) ? colors.DialogText : colors.DisabledText);
        COLORREF oldBkColor = SetBkColor(hdc, colors.DialogBackground);

        TEXTMETRIC tm;
        memset(&tm, 0, sizeof(tm));
        GetTextMetrics(hdc, &tm);
        int frameTop = max(1, (tm.tmHeight + 1) / 2);

        HPEN hPen = CreatePen(PS_SOLID, 1, colors.Border);
        HPEN hOldPen = NULL;
        if (hPen != NULL)
            hOldPen = (HPEN)SelectObject(hdc, hPen);

        RECT frame = client;
        frame.top = min(frame.bottom - 1, frameTop);
        frame.right--;
        frame.bottom--;
        if (frame.right > frame.left && frame.bottom > frame.top)
        {
            MoveToEx(hdc, frame.left, frame.top, NULL);
            LineTo(hdc, frame.right, frame.top);
            LineTo(hdc, frame.right, frame.bottom);
            LineTo(hdc, frame.left, frame.bottom);
            LineTo(hdc, frame.left, frame.top);
        }

        if (hOldPen != NULL)
            SelectObject(hdc, hOldPen);
        if (hPen != NULL)
            DeleteObject(hPen);

        int textLen = GetWindowTextLength(hwnd);
        if (textLen > 0)
        {
            TCHAR* text = new TCHAR[textLen + 1];
            if (text != NULL)
            {
                int copied = GetWindowText(hwnd, text, textLen + 1);
                if (copied > 0)
                {
                    DWORD textFlags = GetGroupBoxTextFlags(hwnd);
                    DWORD calcFlags = (textFlags & ~(DWORD)(DT_CENTER | DT_RIGHT)) | DT_LEFT | DT_CALCRECT;
                    RECT textCalc = {0, 0, max(0, client.right - client.left), tm.tmHeight + tm.tmExternalLeading + 4};
                    DrawText(hdc, text, copied, &textCalc, calcFlags);

                    int textWidth = textCalc.right - textCalc.left;
                    int textHeight = max(tm.tmHeight, textCalc.bottom - textCalc.top);
                    int margin = max(7, tm.tmAveCharWidth);
                    int pad = max(2, tm.tmAveCharWidth / 2);
                    int maxTextWidth = max(0, client.right - client.left - 2 * margin);
                    if (textWidth > maxTextWidth)
                        textWidth = maxTextWidth;

                    int textLeft = margin;
                    if ((textFlags & DT_CENTER) != 0)
                        textLeft = (client.right - client.left - textWidth) / 2;
                    else if ((textFlags & DT_RIGHT) != 0)
                        textLeft = client.right - margin - textWidth;
                    textLeft = max(margin, textLeft);

                    RECT gap = {
                        max(client.left, textLeft - pad),
                        client.top,
                        min(client.right, textLeft + textWidth + pad),
                        min(client.bottom, max(textHeight, frameTop + 2))};
                    HBRUSH hBrush = CreateSolidBrush(colors.DialogBackground);
                    if (hBrush != NULL)
                    {
                        FillRect(hdc, &gap, hBrush);
                        DeleteObject(hBrush);
                    }

                    RECT textRect = {
                        textLeft,
                        client.top,
                        min(client.right - margin, textLeft + textWidth),
                        gap.bottom};
                    DrawText(hdc, text, copied, &textRect, textFlags);
                }
                delete[] text;
            }
        }

        SetBkColor(hdc, oldBkColor);
        SetTextColor(hdc, oldTextColor);
        SetBkMode(hdc, oldBkMode);
        if (hOldFont != NULL)
            SelectObject(hdc, hOldFont);
    }

    if (paintDC == NULL)
        EndPaint(hwnd, &ps);
    return TRUE;
}

LRESULT CALLBACK StaticEdgeSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_PAINT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkStaticEdge(hwnd, NULL))
            return 0;
        break;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkStaticEdge(hwnd, (HDC)wParam))
            return 0;
        break;
    }

    case WM_ERASEBKGND:
    {
        if (DarkMode_ShouldUseDark())
            return TRUE;
        break;
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        InvalidateRect(hwnd, NULL, TRUE);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, StaticEdgeSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK HeaderSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_PAINT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkHeader(hwnd, NULL))
            return 0;
        break;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkHeader(hwnd, (HDC)wParam))
            return 0;
        break;
    }

    case WM_ERASEBKGND:
    {
        if (DarkMode_ShouldUseDark())
            return TRUE;
        break;
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_SIZE:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        InvalidateRect(hwnd, NULL, TRUE);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, HeaderSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK GroupBoxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_PAINT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkGroupBox(hwnd, NULL))
            return 0;
        break;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkGroupBox(hwnd, (HDC)wParam))
            return 0;
        break;
    }

    case WM_ERASEBKGND:
    {
        if (DarkMode_ShouldUseDark())
            return TRUE;
        break;
    }

    case WM_SETTEXT:
    case WM_SETFONT:
    case WM_ENABLE:
    case WM_UPDATEUISTATE:
    case WM_THEMECHANGED:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        InvalidateGroupBox(hwnd);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, GroupBoxSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void ApplyWindowTheme(HWND hwnd, BOOL useDark, LPCWSTR lightTheme = NULL)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    SetWindowTheme(hwnd, useDark ? UXTHEME_DARKMODE_EXPLORER : lightTheme, NULL);
}

void ApplyComboBoxChildThemes(HWND hwnd, BOOL useDark)
{
    COMBOBOXINFO cbi = {0};
    cbi.cbSize = sizeof(cbi);
    if (GetComboBoxInfo(hwnd, &cbi))
    {
        ApplyWindowTheme(cbi.hwndItem, useDark);
        ApplyWindowTheme(cbi.hwndList, useDark);
        if (cbi.hwndItem != NULL)
            InvalidateRect(cbi.hwndItem, NULL, TRUE);
        if (cbi.hwndList != NULL)
            InvalidateRect(cbi.hwndList, NULL, TRUE);
    }
}

void ApplyTooltipTheme(HWND hwndTooltip)
{
    if (hwndTooltip == NULL || !IsWindow(hwndTooltip))
        return;

    DarkModeColors colors;
    DarkMode_GetColors(&colors);
    SendMessage(hwndTooltip, TTM_SETTIPBKCOLOR, colors.ToolTipBackground, 0);
    SendMessage(hwndTooltip, TTM_SETTIPTEXTCOLOR, colors.ToolTipText, 0);
    InvalidateRect(hwndTooltip, NULL, TRUE);
}

void ApplyListTreeThemeToControl(HWND hwnd, BOOL useDark)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    TCHAR className[64] = {0};
    if (GetClassName(hwnd, className, _countof(className)) == 0)
        return;

    if (_tcsicmp(className, SCROLLBAR_CLASS_NAME) == 0)
    {
        // Custom panel scrollbars are separate controls and need explicit theming.
        ApplyWindowTheme(hwnd, useDark);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_tcsicmp(className, WC_LISTVIEW) == 0)
    {
        ApplyWindowTheme(hwnd, useDark, UXTHEME_EXPLORER);
        HWND hHeader = ListView_GetHeader(hwnd);
        if (hHeader != NULL)
        {
            ApplyWindowTheme(hHeader, useDark, UXTHEME_EXPLORER);
            SetWindowSubclass(hHeader, HeaderSubclassProc, HEADER_SUBCLASS_ID, 0);
            InvalidateRect(hHeader, NULL, TRUE);
        }
        DarkModeColors colors;
        DarkMode_GetColors(&colors);
        COLORREF bgColor = colors.InputBackground;
        COLORREF textColor = colors.InputText;
        ListView_SetBkColor(hwnd, bgColor);
        ListView_SetTextBkColor(hwnd, bgColor);
        ListView_SetTextColor(hwnd, textColor);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    if (_tcsicmp(className, WC_TREEVIEW) == 0)
    {
        ApplyWindowTheme(hwnd, useDark, UXTHEME_EXPLORER);
        DarkModeColors colors;
        DarkMode_GetColors(&colors);
        COLORREF bgColor = colors.InputBackground;
        COLORREF textColor = colors.InputText;
        TreeView_SetBkColor(hwnd, bgColor);
        TreeView_SetTextColor(hwnd, textColor);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    if (_tcsicmp(className, LISTBOX_CLASS_NAME) == 0)
    {
        ApplyWindowTheme(hwnd, useDark, UXTHEME_EXPLORER);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_tcsicmp(className, WC_HEADER) == 0 ||
        _tcsicmp(className, HEADER_CLASS_NAME) == 0)
    {
        ApplyWindowTheme(hwnd, useDark, UXTHEME_EXPLORER);
        SetWindowSubclass(hwnd, HeaderSubclassProc, HEADER_SUBCLASS_ID, 0);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_tcsicmp(className, TOOLTIPS_CLASS) == 0)
    {
        ApplyWindowTheme(hwnd, useDark);
        ApplyTooltipTheme(hwnd);
        return;
    }

    if (_tcsicmp(className, STATIC_CLASS_NAME) == 0)
    {
        ApplyWindowTheme(hwnd, useDark);
        if (IsStaticEdge(hwnd))
            SetWindowSubclass(hwnd, StaticEdgeSubclassProc, STATIC_EDGE_SUBCLASS_ID, 0);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_tcsicmp(className, BUTTON_CLASS_NAME) == 0 ||
        _tcsicmp(className, EDIT_CLASS_NAME) == 0 ||
        _tcsicmp(className, UPDOWN_CLASS) == 0)
    {
        ApplyWindowTheme(hwnd, useDark);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_tcsicmp(className, COMBOBOX_CLASS_NAME) == 0)
    {
        ApplyWindowTheme(hwnd, useDark);
        ApplyComboBoxChildThemes(hwnd, useDark);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_tcsicmp(className, WC_COMBOBOXEX) == 0)
    {
        ApplyWindowTheme(hwnd, useDark);
        HWND hCombo = (HWND)SendMessage(hwnd, CBEM_GETCOMBOCONTROL, 0, 0);
        ApplyWindowTheme(hCombo, useDark);
        if (hCombo != NULL)
            ApplyComboBoxChildThemes(hCombo, useDark);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }
}

void ApplyGroupBoxThemeToControl(HWND hwnd)
{
    if (!IsGroupBox(hwnd))
        return;

    SetWindowSubclass(hwnd, GroupBoxSubclassProc, GROUPBOX_SUBCLASS_ID, 0);
    InvalidateGroupBox(hwnd);
}

BOOL CALLBACK ApplyListTreeThemeEnumProc(HWND hwnd, LPARAM lParam)
{
    ApplyListTreeThemeToControl(hwnd, (BOOL)lParam);
    return TRUE;
}

BOOL CALLBACK ApplyGroupBoxThemeEnumProc(HWND hwnd, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    ApplyGroupBoxThemeToControl(hwnd);
    return TRUE;
}

} // namespace

void DarkMode_Initialize()
{
    EnsureInitialized();
}

BOOL DarkMode_IsSupported()
{
    EnsureInitialized();
    return DwmSetWindowAttributePtr != NULL;
}

void DarkMode_SetThemeMode(int themeMode)
{
    int normalized = NormalizeThemeMode(themeMode);
    if (ThemeMode != normalized)
    {
        ThemeMode = normalized;
        TRACE_I("DarkMode theme mode changed to " << ThemeMode);
        char msg[80];
        sprintf_s(msg, "DarkMode theme mode changed to %d\n", ThemeMode);
        DebugOutA(msg);
    }
    else
        ThemeMode = normalized;
}

BOOL DarkMode_ShouldUseDark()
{
    EnsureInitialized();
    return ShouldUseDarkColorsInternal();
}

BOOL DarkMode_GetColors(DarkModeColors* colors)
{
    if (colors == NULL)
        return FALSE;

    BOOL useDark = DarkMode_ShouldUseDark();
    if (useDark)
    {
        colors->DialogBackground = DIALOG_DARK_BG;
        colors->DialogText = DIALOG_DARK_TEXT;
        colors->InputBackground = DIALOG_DARK_INPUT_BG;
        colors->InputText = DIALOG_DARK_INPUT_TEXT;
        colors->DisabledText = DIALOG_DARK_DISABLED_TEXT;
        colors->Border = MAINFRAME_DARK_BORDER;
        colors->Highlight = DIALOG_DARK_HIGHLIGHT;
        colors->HighlightText = RGB(255, 255, 255);
        colors->InactiveSelection = DIALOG_DARK_INACTIVE_SELECTION;
        colors->ToolTipBackground = DIALOG_DARK_TOOLTIP_BG;
        colors->ToolTipText = DIALOG_DARK_INPUT_TEXT;
        colors->ViewerBackground = DIALOG_DARK_INPUT_BG;
        colors->ViewerText = DIALOG_DARK_INPUT_TEXT;
        colors->ViewerSelectionBackground = DIALOG_DARK_HIGHLIGHT;
        colors->ViewerSelectionText = RGB(255, 255, 255);
    }
    else
    {
        colors->DialogBackground = GetSysColor(COLOR_BTNFACE);
        colors->DialogText = GetSysColor(COLOR_WINDOWTEXT);
        colors->InputBackground = GetSysColor(COLOR_WINDOW);
        colors->InputText = GetSysColor(COLOR_WINDOWTEXT);
        colors->DisabledText = GetSysColor(COLOR_GRAYTEXT);
        colors->Border = GetSysColor(COLOR_3DLIGHT);
        colors->Highlight = GetSysColor(COLOR_HIGHLIGHT);
        colors->HighlightText = GetSysColor(COLOR_HIGHLIGHTTEXT);
        colors->InactiveSelection = GetSysColor(COLOR_3DFACE);
        colors->ToolTipBackground = GetSysColor(COLOR_INFOBK);
        colors->ToolTipText = GetSysColor(COLOR_INFOTEXT);
        colors->ViewerBackground = GetSysColor(COLOR_WINDOW);
        colors->ViewerText = GetSysColor(COLOR_WINDOWTEXT);
        colors->ViewerSelectionBackground = GetSysColor(COLOR_WINDOWTEXT);
        colors->ViewerSelectionText = GetSysColor(COLOR_WINDOW);
    }
    return useDark;
}

BOOL DarkMode_GetMainFramePalette(DarkModeMainFramePalette* palette)
{
    if (palette == NULL)
        return FALSE;

    BOOL useDark = DarkMode_ShouldUseDark();
    if (useDark)
    {
        palette->Fill = MAINFRAME_DARK_FILL;
        palette->LineDark = MAINFRAME_DARK_LINE_DARK;
        palette->LineLight = MAINFRAME_DARK_LINE_LIGHT;
        palette->Border = MAINFRAME_DARK_BORDER;
    }
    else
    {
        palette->Fill = GetSysColor(COLOR_BTNFACE);
        palette->LineDark = GetSysColor(COLOR_BTNSHADOW);
        palette->LineLight = GetSysColor(COLOR_BTNHIGHLIGHT);
        palette->Border = GetSysColor(COLOR_BTNFACE);
    }
    return useDark;
}

HBRUSH DarkMode_GetDialogCtlColorBrush(UINT msg, HDC hdc, HWND hCtrl)
{
    if (hdc == NULL || !DarkMode_ShouldUseDark())
        return NULL;

    EnsureDialogBrushes();
    if (DialogDarkBrush == NULL || DialogDarkInputBrush == NULL)
        return NULL;

    switch (msg)
    {
    case WM_CTLCOLORDLG:
        SetBkColor(hdc, DIALOG_DARK_BG);
        return DialogDarkBrush;

    case WM_CTLCOLORSTATIC:
        if (HasClassName(hCtrl, EDIT_CLASS_NAME))
        {
            SetTextColor(hdc, hCtrl != NULL && !IsWindowEnabled(hCtrl) ? DIALOG_DARK_DISABLED_TEXT : DIALOG_DARK_INPUT_TEXT);
            SetBkColor(hdc, DIALOG_DARK_INPUT_BG);
            SetBkMode(hdc, OPAQUE);
            return DialogDarkInputBrush;
        }
        SetTextColor(hdc, DIALOG_DARK_TEXT);
        if (hCtrl != NULL && !IsWindowEnabled(hCtrl))
            SetTextColor(hdc, DIALOG_DARK_DISABLED_TEXT);
        SetBkColor(hdc, DIALOG_DARK_BG);
        SetBkMode(hdc, TRANSPARENT);
        return DialogDarkBrush;

    case WM_CTLCOLORBTN:
        SetTextColor(hdc, DIALOG_DARK_TEXT);
        if (hCtrl != NULL && !IsWindowEnabled(hCtrl))
            SetTextColor(hdc, DIALOG_DARK_DISABLED_TEXT);
        SetBkColor(hdc, DIALOG_DARK_BG);
        SetBkMode(hdc, TRANSPARENT);
        return DialogDarkBrush;

    case WM_CTLCOLOREDIT:
        SetTextColor(hdc, DIALOG_DARK_INPUT_TEXT);
        SetBkColor(hdc, DIALOG_DARK_INPUT_BG);
        SetBkMode(hdc, OPAQUE);
        return DialogDarkInputBrush;

    case WM_CTLCOLORLISTBOX:
        SetTextColor(hdc, DIALOG_DARK_INPUT_TEXT);
        SetBkColor(hdc, DIALOG_DARK_INPUT_BG);
        SetBkMode(hdc, OPAQUE);
        return DialogDarkInputBrush;
    }

    return NULL;
}

BOOL DarkMode_OnSettingChange(LPARAM lParam)
{
    EnsureInitialized();

    if (!DarkMode_IsSupported())
        return FALSE;

    BOOL changed = IsThemeSettingHint(lParam);
    if (changed)
        TRACE_I("DarkMode: relevant WM_SETTINGCHANGE received");
    return changed;
}

void DarkMode_DrawSunkenFrame(HDC hDC, const RECT* r, const DarkModeMainFramePalette& palette)
{
    HGDIOBJ oldPen = SelectObject(hDC, GetStockObject(DC_PEN));

    SetDCPenColor(hDC, palette.LineDark);
    MoveToEx(hDC, r->left, r->bottom - 1, NULL);
    LineTo(hDC, r->left, r->top);
    LineTo(hDC, r->right - 1, r->top);

    SetDCPenColor(hDC, palette.Border);
    MoveToEx(hDC, r->right - 1, r->top, NULL);
    LineTo(hDC, r->right - 1, r->bottom - 1);
    LineTo(hDC, r->left - 1, r->bottom - 1);

    SelectObject(hDC, oldPen);
}

void DarkMode_ApplyTitleBar(HWND hwnd)
{
    EnsureInitialized();

    if (!DarkMode_IsSupported())
    {
        if (!SupportWarningLogged)
        {
            SupportWarningLogged = TRUE;
            TRACE_I("DarkMode unsupported: DwmSetWindowAttribute is unavailable");
            DebugOutA("DarkMode unsupported: DwmSetWindowAttribute is unavailable\n");
        }
        return;
    }

    if (!IsTopLevelWindow(hwnd))
        return;

    BOOL useDark = DarkMode_ShouldUseDark();
    HRESULT hrNew = DwmSetWindowAttributePtr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_NEW, &useDark, sizeof(useDark));
    HRESULT hrOld = S_OK;
    HRESULT finalHr = hrNew;
    if (FAILED(finalHr))
    {
        hrOld = DwmSetWindowAttributePtr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &useDark, sizeof(useDark));
        finalHr = hrOld;
    }

    char msg[220];
    sprintf_s(msg, "DarkMode apply: hwnd=%p mode=%d useDark=%d hrNew=0x%08lX hrOld=0x%08lX\n",
              hwnd, ThemeMode, (int)useDark, (unsigned long)hrNew, (unsigned long)hrOld);
    DebugOutA(msg);

    if (FAILED(finalHr))
    {
        TRACE_E("DarkMode: failed to apply title bar mode, hwnd=" << hwnd << ", hr=" << std::hex << finalHr);
    }

    // Some systems accept immersive dark mode but keep a light caption.
    // In explicit Dark mode, enforce caption/text colors to make mode visible.
    int normalizedTheme = NormalizeThemeMode(ThemeMode);
    COLORREF captionColor = DWMWA_COLOR_DEFAULT;
    COLORREF textColor = DWMWA_COLOR_DEFAULT;
    COLORREF borderColor = DWMWA_COLOR_DEFAULT;
    if (normalizedTheme == THEME_MODE_DARK)
    {
        captionColor = RGB(32, 32, 32);
        textColor = RGB(255, 255, 255);
    }
    if (useDark)
        borderColor = MAINFRAME_DARK_LINE_DARK;

    HRESULT hrCaption = S_OK;
    HRESULT hrText = S_OK;
    HRESULT hrBorder = S_OK;
    if (BorderColorAttrSupported)
    {
        hrBorder = DwmSetWindowAttributePtr(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
        if (FAILED(hrBorder))
            BorderColorAttrSupported = FALSE;
    }
    if (CaptionColorAttrSupported)
    {
        hrCaption = DwmSetWindowAttributePtr(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
        if (FAILED(hrCaption))
            CaptionColorAttrSupported = FALSE;
    }
    if (TextColorAttrSupported)
    {
        hrText = DwmSetWindowAttributePtr(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
        if (FAILED(hrText))
            TextColorAttrSupported = FALSE;
    }
    if (FAILED(hrBorder) || FAILED(hrCaption) || FAILED(hrText))
    {
        TRACE_I("DarkMode: border/caption/text color attributes not available or failed, hwnd=" << hwnd
                                                                                                 << ", hrBorder=" << std::hex << hrBorder
                                                                                                 << ", hrCaption=" << std::hex << hrCaption
                                                                                                 << ", hrText=" << std::hex << hrText);
    }
}

void DarkMode_ApplyToThreadTopLevelWindows(DWORD threadId)
{
    if (threadId == 0)
        threadId = GetCurrentThreadId();

    EnumThreadWindows(threadId, ApplyThreadWindowProc, 0);
}

void DarkMode_ApplyListTreeThemeRecursive(HWND root)
{
    if (root == NULL || !IsWindow(root))
        return;

    // SetWindowTheme() sends WM_THEMECHANGED synchronously. Guard the theme walk so
    // scrollbar theming does not immediately re-enter this routine and overflow.
    if (ListTreeThemeApplyDepth > 0)
        return;

    ListTreeThemeApplyDepth++;
    BOOL useDark = DarkMode_ShouldUseDark();
    ApplyListTreeThemeToControl(root, useDark);
    EnumChildWindows(root, ApplyListTreeThemeEnumProc, (LPARAM)useDark);
    ListTreeThemeApplyDepth--;
}

void DarkMode_ApplyGroupBoxThemeRecursive(HWND root)
{
    if (root == NULL || !IsWindow(root))
        return;

    if (GroupBoxThemeApplyDepth > 0)
        return;

    GroupBoxThemeApplyDepth++;
    ApplyGroupBoxThemeToControl(root);
    EnumChildWindows(root, ApplyGroupBoxThemeEnumProc, 0);
    GroupBoxThemeApplyDepth--;
}
