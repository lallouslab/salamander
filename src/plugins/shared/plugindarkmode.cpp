// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "plugindarkmode.h"

#include <commctrl.h>
#include <tchar.h>

#include "registry_names.h"

namespace
{
typedef HRESULT(WINAPI* PFNDWMSETWINDOWATTRIBUTE)(HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD cbAttribute);
typedef HRESULT(WINAPI* PFNSETWINDOWTHEME)(HWND hwnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList);

const int PLUGIN_THEME_MODE_LIGHT = 0;
const int PLUGIN_THEME_MODE_DARK = 1;
const int PLUGIN_THEME_MODE_SYSTEM = 2;

const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_NEW = 20;
const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19;
const DWORD DWMWA_CAPTION_COLOR = 35;
const DWORD DWMWA_TEXT_COLOR = 36;
const COLORREF DWMWA_COLOR_DEFAULT = 0xFFFFFFFF;

const COLORREF DARK_DIALOG_BG = RGB(45, 45, 48);
const COLORREF DARK_DIALOG_TEXT = RGB(232, 232, 232);
const COLORREF DARK_INPUT_BG = RGB(30, 30, 30);
const COLORREF DARK_INPUT_TEXT = RGB(245, 245, 245);
const COLORREF DARK_DISABLED_TEXT = RGB(160, 160, 160);
const COLORREF DARK_BORDER = RGB(70, 70, 70);
const COLORREF DARK_HIGHLIGHT = RGB(0, 120, 215);
const COLORREF DARK_INACTIVE_SELECTION = RGB(75, 75, 78);
const COLORREF DARK_TOOLTIP_BG = RGB(43, 43, 43);
const COLORREF DARK_CAPTION_BG = RGB(32, 32, 32);
const COLORREF DARK_INACTIVE_CAPTION_BG = RGB(48, 48, 48);

const TCHAR* IMMERSIVE_COLOR_SET_PARAM = TEXT("ImmersiveColorSet");
const TCHAR* WINDOWS_THEME_ELEMENT_PARAM = TEXT("WindowsThemeElement");
const TCHAR* SCROLLBAR_CLASS_NAME = TEXT("ScrollBar");
const WCHAR* UXTHEME_DARKMODE_EXPLORER = L"DarkMode_Explorer";

BOOL Initialized = FALSE;
PFNDWMSETWINDOWATTRIBUTE DwmSetWindowAttributePtr = NULL;
PFNSETWINDOWTHEME SetWindowThemePtr = NULL;
BOOL CaptionColorAttrSupported = TRUE;
BOOL TextColorAttrSupported = TRUE;
HBRUSH DialogDarkBrush = NULL;
HBRUSH InputDarkBrush = NULL;
thread_local int ListTreeThemeApplyDepth = 0;

int NormalizeThemeMode(int mode)
{
    if (mode == PLUGIN_THEME_MODE_DARK || mode == PLUGIN_THEME_MODE_SYSTEM)
        return mode;
    return PLUGIN_THEME_MODE_LIGHT;
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

BOOL ReadRegistryDword(HKEY rootKey, const char* subKey, const char* valueName, DWORD* value)
{
    HKEY hKey = NULL;
    if (RegOpenKeyExA(rootKey, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;

    DWORD type = 0;
    DWORD size = sizeof(*value);
    LONG result = RegQueryValueExA(hKey, valueName, NULL, &type, (LPBYTE)value, &size);
    RegCloseKey(hKey);

    return result == ERROR_SUCCESS && type == REG_DWORD;
}

BOOL ReadSystemPrefersDarkApps()
{
    DWORD value = 1;
    if (!ReadRegistryDword(HKEY_CURRENT_USER,
                           SAL_REG_KEY_WINDOWS_THEME_PERSONALIZE_A,
                           SAL_REG_VALUE_APPS_USE_LIGHT_THEME_A,
                           &value))
    {
        return FALSE;
    }

    return value == 0;
}

BOOL ReadConfiguredThemeMode(int* themeMode)
{
    static const char* configRoots[] = {
        SAL_REG_ROOT_SALLY_1_0_A "\\" SAL_REG_SUBKEY_CONFIGURATION_A,
        SAL_REG_ROOT_OPENSAL_5_0_A "\\" SAL_REG_SUBKEY_CONFIGURATION_A,
    };

    for (int i = 0; i < _countof(configRoots); i++)
    {
        DWORD value = PLUGIN_THEME_MODE_LIGHT;
        if (ReadRegistryDword(HKEY_CURRENT_USER, configRoots[i], "Theme mode", &value))
        {
            *themeMode = NormalizeThemeMode((int)value);
            return TRUE;
        }
    }

    return FALSE;
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

    HMODULE hUxTheme = GetModuleHandle(TEXT("uxtheme.dll"));
    if (hUxTheme == NULL)
        hUxTheme = LoadLibrary(TEXT("uxtheme.dll"));
    if (hUxTheme != NULL)
        SetWindowThemePtr = (PFNSETWINDOWTHEME)GetProcAddress(hUxTheme, "SetWindowTheme");
}

void EnsureDialogBrushes()
{
    if (DialogDarkBrush == NULL)
        DialogDarkBrush = CreateSolidBrush(DARK_DIALOG_BG);
    if (InputDarkBrush == NULL)
        InputDarkBrush = CreateSolidBrush(DARK_INPUT_BG);
}

void MaybeSetWindowTheme(HWND hwnd, BOOL useDark)
{
    EnsureInitialized();
    if (SetWindowThemePtr != NULL)
        SetWindowThemePtr(hwnd, useDark ? UXTHEME_DARKMODE_EXPLORER : NULL, NULL);
}

BOOL IsThemeSettingHint(LPARAM lParam)
{
    if (lParam == 0)
        return FALSE;

    LPCTSTR valueName = (LPCTSTR)lParam;
    if (valueName == NULL || *valueName == 0)
        return FALSE;

    return _tcsicmp(valueName, IMMERSIVE_COLOR_SET_PARAM) == 0 ||
           _tcsicmp(valueName, WINDOWS_THEME_ELEMENT_PARAM) == 0;
}

void ApplyListTreeThemeToControl(HWND hwnd, BOOL useDark)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    TCHAR className[64] = {0};
    if (GetClassName(hwnd, className, _countof(className)) == 0)
        return;

    PluginDarkModeColors colors;
    PluginDarkMode_GetColors(&colors);

    if (_tcsicmp(className, SCROLLBAR_CLASS_NAME) == 0)
    {
        MaybeSetWindowTheme(hwnd, useDark);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_tcsicmp(className, WC_LISTVIEW) == 0)
    {
        MaybeSetWindowTheme(hwnd, useDark);
        ListView_SetBkColor(hwnd, colors.InputBackground);
        ListView_SetTextBkColor(hwnd, colors.InputBackground);
        ListView_SetTextColor(hwnd, colors.InputText);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    if (_tcsicmp(className, WC_TREEVIEW) == 0)
    {
        MaybeSetWindowTheme(hwnd, useDark);
        TreeView_SetBkColor(hwnd, colors.InputBackground);
        TreeView_SetTextColor(hwnd, colors.InputText);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    if (_tcsicmp(className, TOOLTIPS_CLASS) == 0)
    {
        PluginDarkMode_ApplyTooltipTheme(hwnd);
        return;
    }
}

BOOL CALLBACK ApplyListTreeThemeEnumProc(HWND hwnd, LPARAM lParam)
{
    ApplyListTreeThemeToControl(hwnd, (BOOL)lParam);
    return TRUE;
}

} // namespace

void PluginDarkMode_Initialize()
{
    EnsureInitialized();
}

BOOL PluginDarkMode_ShouldUseDark()
{
    EnsureInitialized();

    if (IsHighContrastEnabled())
        return FALSE;

    int themeMode = PLUGIN_THEME_MODE_LIGHT;
    if (!ReadConfiguredThemeMode(&themeMode))
        return FALSE;

    switch (themeMode)
    {
    case PLUGIN_THEME_MODE_DARK:
        return TRUE;

    case PLUGIN_THEME_MODE_SYSTEM:
        return ReadSystemPrefersDarkApps();

    default:
        return FALSE;
    }
}

BOOL PluginDarkMode_GetColors(PluginDarkModeColors* colors)
{
    if (colors == NULL)
        return FALSE;

    BOOL useDark = PluginDarkMode_ShouldUseDark();
    if (useDark)
    {
        colors->DialogBackground = DARK_DIALOG_BG;
        colors->DialogText = DARK_DIALOG_TEXT;
        colors->InputBackground = DARK_INPUT_BG;
        colors->InputText = DARK_INPUT_TEXT;
        colors->DisabledText = DARK_DISABLED_TEXT;
        colors->Border = DARK_BORDER;
        colors->Highlight = DARK_HIGHLIGHT;
        colors->HighlightText = RGB(255, 255, 255);
        colors->InactiveSelection = DARK_INACTIVE_SELECTION;
        colors->ToolTipBackground = DARK_TOOLTIP_BG;
        colors->ToolTipText = DARK_INPUT_TEXT;
        colors->CaptionBackground = DARK_CAPTION_BG;
        colors->CaptionText = RGB(255, 255, 255);
        colors->InactiveCaptionBackground = DARK_INACTIVE_CAPTION_BG;
        colors->InactiveCaptionText = DARK_DIALOG_TEXT;
    }
    else
    {
        colors->DialogBackground = GetSysColor(COLOR_BTNFACE);
        colors->DialogText = GetSysColor(COLOR_BTNTEXT);
        colors->InputBackground = GetSysColor(COLOR_WINDOW);
        colors->InputText = GetSysColor(COLOR_WINDOWTEXT);
        colors->DisabledText = GetSysColor(COLOR_GRAYTEXT);
        colors->Border = GetSysColor(COLOR_BTNSHADOW);
        colors->Highlight = GetSysColor(COLOR_HIGHLIGHT);
        colors->HighlightText = GetSysColor(COLOR_HIGHLIGHTTEXT);
        colors->InactiveSelection = GetSysColor(COLOR_3DFACE);
        colors->ToolTipBackground = GetSysColor(COLOR_INFOBK);
        colors->ToolTipText = GetSysColor(COLOR_INFOTEXT);
        colors->CaptionBackground = GetSysColor(COLOR_ACTIVECAPTION);
        colors->CaptionText = GetSysColor(COLOR_CAPTIONTEXT);
        colors->InactiveCaptionBackground = GetSysColor(COLOR_INACTIVECAPTION);
        colors->InactiveCaptionText = GetSysColor(COLOR_INACTIVECAPTIONTEXT);
    }

    return useDark;
}

BOOL PluginDarkMode_OnSettingChange(LPARAM lParam)
{
    return IsThemeSettingHint(lParam);
}

void PluginDarkMode_ApplyTitleBar(HWND hwnd)
{
    EnsureInitialized();

    if (DwmSetWindowAttributePtr == NULL || !IsTopLevelWindow(hwnd))
        return;

    BOOL useDark = PluginDarkMode_ShouldUseDark();
    HRESULT hrNew = DwmSetWindowAttributePtr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_NEW, &useDark, sizeof(useDark));
    if (FAILED(hrNew))
        DwmSetWindowAttributePtr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &useDark, sizeof(useDark));

    COLORREF captionColor = DWMWA_COLOR_DEFAULT;
    COLORREF textColor = DWMWA_COLOR_DEFAULT;
    if (useDark)
    {
        captionColor = DARK_CAPTION_BG;
        textColor = RGB(255, 255, 255);
    }

    if (CaptionColorAttrSupported)
    {
        HRESULT hrCaption = DwmSetWindowAttributePtr(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
        if (FAILED(hrCaption))
            CaptionColorAttrSupported = FALSE;
    }
    if (TextColorAttrSupported)
    {
        HRESULT hrText = DwmSetWindowAttributePtr(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
        if (FAILED(hrText))
            TextColorAttrSupported = FALSE;
    }
}

void PluginDarkMode_ApplyListTreeThemeRecursive(HWND root)
{
    if (root == NULL || !IsWindow(root))
        return;

    if (ListTreeThemeApplyDepth > 0)
        return;

    ListTreeThemeApplyDepth++;
    BOOL useDark = PluginDarkMode_ShouldUseDark();
    ApplyListTreeThemeToControl(root, useDark);
    EnumChildWindows(root, ApplyListTreeThemeEnumProc, (LPARAM)useDark);
    ListTreeThemeApplyDepth--;
}

void PluginDarkMode_ApplyTooltipTheme(HWND hwndTooltip)
{
    if (hwndTooltip == NULL || !IsWindow(hwndTooltip))
        return;

    PluginDarkModeColors colors;
    PluginDarkMode_GetColors(&colors);
    SendMessage(hwndTooltip, TTM_SETTIPBKCOLOR, colors.ToolTipBackground, 0);
    SendMessage(hwndTooltip, TTM_SETTIPTEXTCOLOR, colors.ToolTipText, 0);
    InvalidateRect(hwndTooltip, NULL, TRUE);
}

HBRUSH PluginDarkMode_GetDialogCtlColorBrush(UINT msg, HDC hdc, HWND hCtrl)
{
    if (hdc == NULL || !PluginDarkMode_ShouldUseDark())
        return NULL;

    EnsureDialogBrushes();
    if (DialogDarkBrush == NULL || InputDarkBrush == NULL)
        return NULL;

    switch (msg)
    {
    case WM_CTLCOLORDLG:
        SetBkColor(hdc, DARK_DIALOG_BG);
        return DialogDarkBrush;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor(hdc, hCtrl != NULL && !IsWindowEnabled(hCtrl) ? DARK_DISABLED_TEXT : DARK_DIALOG_TEXT);
        SetBkColor(hdc, DARK_DIALOG_BG);
        SetBkMode(hdc, TRANSPARENT);
        return DialogDarkBrush;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(hdc, DARK_INPUT_TEXT);
        SetBkColor(hdc, DARK_INPUT_BG);
        SetBkMode(hdc, OPAQUE);
        return InputDarkBrush;
    }

    return NULL;
}

BOOL PluginDarkMode_FillRect(HDC hdc, const RECT* rect, COLORREF color)
{
    if (hdc == NULL || rect == NULL)
        return FALSE;

    HBRUSH brush = CreateSolidBrush(color);
    if (brush == NULL)
        return FALSE;

    FillRect(hdc, rect, brush);
    DeleteObject(brush);
    return TRUE;
}

BOOL PluginDarkMode_FillDialogBackground(HDC hdc, const RECT* rect)
{
    PluginDarkModeColors colors;
    PluginDarkMode_GetColors(&colors);
    return PluginDarkMode_FillRect(hdc, rect, colors.DialogBackground);
}

void PluginDarkMode_FillOwnerDrawBackground(HDC hdc, const RECT* rect, BOOL selected, BOOL focused)
{
    PluginDarkModeColors colors;
    BOOL useDark = PluginDarkMode_GetColors(&colors);
    COLORREF color = selected ? (focused || !useDark ? colors.Highlight : colors.InactiveSelection) : colors.InputBackground;
    PluginDarkMode_FillRect(hdc, rect, color);
}

COLORREF PluginDarkMode_GetOwnerDrawTextColor(BOOL selected, BOOL enabled)
{
    PluginDarkModeColors colors;
    PluginDarkMode_GetColors(&colors);
    if (!enabled)
        return colors.DisabledText;
    return selected ? colors.HighlightText : colors.InputText;
}
