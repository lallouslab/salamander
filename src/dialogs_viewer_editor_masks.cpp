// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/widepath.h"
#include "cfgdlg.h"
#include "darkmode.h"
#include "dialogs.h"
#include "usermenu.h"
#include "execute.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "gui.h"
#include "shellib.h"

#include <uxtheme.h>

static const UINT_PTR SIZE_RESULTS_COMBO_SKIN_SUBCLASS_ID = 1;
static const UINT_PTR SIZE_RESULTS_COMBO_EDIT_SKIN_SUBCLASS_ID = 1;
static const COLORREF SIZE_RESULTS_DARK_LINE = RGB(55, 55, 58);
static const COLORREF SIZE_RESULTS_DARK_FRAME = RGB(62, 62, 66);
static const COLORREF SIZE_RESULTS_DARK_SECTION_LINE = RGB(70, 70, 74);

struct CSizeResultsComboSkinState
{
    LONG_PTR ComboStyle;
    LONG_PTR ComboExStyle;
    LONG_PTR EditStyle;
    LONG_PTR EditExStyle;
    HWND HEdit;
    BOOL EditStyleKnown;
    BOOL EditExStyleKnown;
    BOOL ApplyingTheme;
    BOOL ThemeKnown;
    BOOL LastUseDark;
};

static LRESULT CALLBACK SizeResultsComboSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static LRESULT CALLBACK SizeResultsComboEditSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

static void SizeResultsFillRectSolid(HDC hdc, const RECT* rect, COLORREF color)
{
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    COLORREF oldColor = SetDCBrushColor(hdc, color);
    FillRect(hdc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    SetDCBrushColor(hdc, oldColor);
    SelectObject(hdc, oldBrush);
}

static void SizeResultsDrawRectOutline(HDC hdc, const RECT* rect, COLORREF color)
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

static void SizeResultsDrawDownArrow(HDC hdc, const RECT* rect, COLORREF color)
{
    int centerX = (rect->left + rect->right) / 2;
    int centerY = (rect->top + rect->bottom) / 2;
    POINT arrow[3] = {
        {centerX - 3, centerY - 1},
        {centerX + 4, centerY - 1},
        {centerX, centerY + 3},
    };

    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    COLORREF oldPenColor = SetDCPenColor(hdc, color);
    COLORREF oldBrushColor = SetDCBrushColor(hdc, color);
    Polygon(hdc, arrow, 3);
    SetDCBrushColor(hdc, oldBrushColor);
    SetDCPenColor(hdc, oldPenColor);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
}

static BOOL GetSizeResultsChildRectInParent(HWND hParent, HWND hChild, RECT* rect)
{
    if (hParent == NULL || hChild == NULL || rect == NULL || !IsWindow(hParent) || !IsWindow(hChild))
        return FALSE;

    if (!GetWindowRect(hChild, rect))
        return FALSE;
    MapWindowPoints(NULL, hParent, (POINT*)rect, 2);
    return TRUE;
}

static BOOL GetSizeResultsChildRectInDialog(HWND hDialog, int ctrlID, RECT* rect)
{
    if (hDialog == NULL || rect == NULL || !IsWindow(hDialog))
        return FALSE;

    HWND hChild = GetDlgItem(hDialog, ctrlID);
    if (hChild == NULL || !IsWindow(hChild))
        return FALSE;

    if (!GetWindowRect(hChild, rect))
        return FALSE;
    MapWindowPoints(NULL, hDialog, (POINT*)rect, 2);
    return TRUE;
}

static void SetSizeResultsWindowStyle(HWND hwnd, LONG_PTR style)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    if (GetWindowLongPtr(hwnd, GWL_STYLE) == style)
        return;

    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void SetSizeResultsWindowExStyle(HWND hwnd, LONG_PTR exStyle)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    if (GetWindowLongPtr(hwnd, GWL_EXSTYLE) == exStyle)
        return;

    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static BOOL PaintSizeResultsDarkCombo(HWND hwnd, HDC paintDC)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return FALSE;

    RECT client;
    GetClientRect(hwnd, &client);
    if (client.right <= client.left || client.bottom <= client.top)
        return TRUE;

    COMBOBOXINFO cbi = {0};
    cbi.cbSize = sizeof(cbi);
    GetComboBoxInfo(hwnd, &cbi);

    RECT editRect;
    BOOL haveEditRect = GetSizeResultsChildRectInParent(hwnd, cbi.hwndItem, &editRect);

    int savedDC = SaveDC(paintDC);
    if (haveEditRect)
        ExcludeClipRect(paintDC, editRect.left, editRect.top, editRect.right, editRect.bottom);

    SizeResultsFillRectSolid(paintDC, &client, colors.InputBackground);

    int buttonWidth = max(GetSystemMetrics(SM_CXVSCROLL), client.bottom - client.top);
    RECT button = client;
    button.left = max(client.left + 1, client.right - buttonWidth - 1);
    button.top = client.top + 1;
    button.right = client.right - 1;
    button.bottom = client.bottom - 1;
    if (button.right > button.left && button.bottom > button.top)
    {
        SizeResultsFillRectSolid(paintDC, &button, colors.InputBackground);
        HGDIOBJ oldPen = SelectObject(paintDC, GetStockObject(DC_PEN));
        COLORREF oldPenColor = SetDCPenColor(paintDC, SIZE_RESULTS_DARK_LINE);
        MoveToEx(paintDC, button.left, button.top, NULL);
        LineTo(paintDC, button.left, button.bottom);
        SetDCPenColor(paintDC, oldPenColor);
        SelectObject(paintDC, oldPen);
        SizeResultsDrawDownArrow(paintDC, &button, IsWindowEnabled(hwnd) ? colors.InputText : colors.DisabledText);
    }

    RestoreDC(paintDC, savedDC);
    SizeResultsDrawRectOutline(paintDC, &client, SIZE_RESULTS_DARK_FRAME);
    return TRUE;
}

static void PaintSizeResultsDarkComboFrame(HWND hwnd)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return;

    HDC hdc = GetWindowDC(hwnd);
    if (hdc == NULL)
        return;

    RECT rect;
    GetWindowRect(hwnd, &rect);
    OffsetRect(&rect, -rect.left, -rect.top);
    SizeResultsDrawRectOutline(hdc, &rect, SIZE_RESULTS_DARK_FRAME);
    ReleaseDC(hwnd, hdc);
}

static void PaintSizeResultsDarkComboEditFrame(HWND hwnd)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return;

    HDC hdc = GetWindowDC(hwnd);
    if (hdc == NULL)
        return;

    RECT window;
    GetWindowRect(hwnd, &window);
    OffsetRect(&window, -window.left, -window.top);

    RECT client;
    GetClientRect(hwnd, &client);
    MapWindowPoints(hwnd, NULL, (POINT*)&client, 2);
    RECT screenWindow;
    GetWindowRect(hwnd, &screenWindow);
    OffsetRect(&client, -screenWindow.left, -screenWindow.top);

    int savedDC = SaveDC(hdc);
    ExcludeClipRect(hdc, client.left, client.top, client.right, client.bottom);
    SizeResultsFillRectSolid(hdc, &window, colors.InputBackground);
    RestoreDC(hdc, savedDC);

    ReleaseDC(hwnd, hdc);
}

static void ApplySizeResultsComboEditSkin(HWND hEdit, BOOL useDark)
{
    if (hEdit == NULL || !IsWindow(hEdit))
        return;

    SetWindowSubclass(hEdit, SizeResultsComboEditSkinSubclassProc, SIZE_RESULTS_COMBO_EDIT_SKIN_SUBCLASS_ID, 0);
    SetWindowTheme(hEdit, useDark ? L"" : NULL, NULL);
    RedrawWindow(hEdit, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
}

static void UpdateSizeResultsComboSkin(HWND hCombo, CSizeResultsComboSkinState* state)
{
    if (hCombo == NULL || state == NULL || !IsWindow(hCombo))
        return;

    COMBOBOXINFO cbi = {0};
    cbi.cbSize = sizeof(cbi);
    GetComboBoxInfo(hCombo, &cbi);

    BOOL editChanged = cbi.hwndItem != NULL && cbi.hwndItem != state->HEdit;
    if (editChanged)
    {
        state->HEdit = cbi.hwndItem;
        state->EditStyle = GetWindowLongPtr(cbi.hwndItem, GWL_STYLE);
        state->EditExStyle = GetWindowLongPtr(cbi.hwndItem, GWL_EXSTYLE);
        state->EditStyleKnown = TRUE;
        state->EditExStyleKnown = TRUE;
    }

    BOOL useDark = DarkMode_ShouldUseDark();
    LONG_PTR darkStyleMask = WS_BORDER;
    LONG_PTR darkEdgeMask = WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;
    SetSizeResultsWindowStyle(hCombo, useDark ? (state->ComboStyle & ~darkStyleMask) : state->ComboStyle);
    SetSizeResultsWindowExStyle(hCombo, useDark ? (state->ComboExStyle & ~darkEdgeMask) : state->ComboExStyle);

    if (state->HEdit != NULL && state->EditStyleKnown && IsWindow(state->HEdit))
        SetSizeResultsWindowStyle(state->HEdit, useDark ? (state->EditStyle & ~darkStyleMask) : state->EditStyle);
    if (state->HEdit != NULL && state->EditExStyleKnown && IsWindow(state->HEdit))
        SetSizeResultsWindowExStyle(state->HEdit, useDark ? (state->EditExStyle & ~darkEdgeMask) : state->EditExStyle);

    if (!state->ApplyingTheme &&
        (!state->ThemeKnown || state->LastUseDark != useDark || editChanged))
    {
        state->ApplyingTheme = TRUE;
        SetWindowTheme(hCombo, useDark ? L"" : NULL, NULL);
        ApplySizeResultsComboEditSkin(state->HEdit, useDark);
        if (cbi.hwndList != NULL && IsWindow(cbi.hwndList))
            SetWindowTheme(cbi.hwndList, useDark ? L"DarkMode_Explorer" : NULL, NULL);
        state->ThemeKnown = TRUE;
        state->LastUseDark = useDark;
        state->ApplyingTheme = FALSE;
    }

    RedrawWindow(hCombo, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

static void ApplySizeResultsComboSkin(HWND hCombo)
{
    if (hCombo == NULL || !IsWindow(hCombo))
        return;

    DWORD_PTR data = 0;
    CSizeResultsComboSkinState* state = NULL;
    if (GetWindowSubclass(hCombo, SizeResultsComboSkinSubclassProc, SIZE_RESULTS_COMBO_SKIN_SUBCLASS_ID, &data))
        state = (CSizeResultsComboSkinState*)data;
    else
    {
        state = new CSizeResultsComboSkinState;
        if (state == NULL)
            return;
        state->ComboStyle = GetWindowLongPtr(hCombo, GWL_STYLE);
        state->ComboExStyle = GetWindowLongPtr(hCombo, GWL_EXSTYLE);
        state->EditStyle = 0;
        state->EditExStyle = 0;
        state->HEdit = NULL;
        state->EditStyleKnown = FALSE;
        state->EditExStyleKnown = FALSE;
        state->ApplyingTheme = FALSE;
        state->ThemeKnown = FALSE;
        state->LastUseDark = FALSE;
        if (!SetWindowSubclass(hCombo, SizeResultsComboSkinSubclassProc, SIZE_RESULTS_COMBO_SKIN_SUBCLASS_ID, (DWORD_PTR)state))
        {
            delete state;
            return;
        }
    }

    UpdateSizeResultsComboSkin(hCombo, state);
}

static void ApplySizeResultsDialogTheme(HWND hDialog)
{
    if (hDialog == NULL || !IsWindow(hDialog))
        return;

    ApplySizeResultsComboSkin(GetDlgItem(hDialog, IDC_EST_CLUSTER));

    int lineIDs[] = {IDC_STATIC_16, IDC_STATIC_10, IDC_STATIC_15};
    BOOL useDark = DarkMode_ShouldUseDark();
    for (int i = 0; i < _countof(lineIDs); i++)
    {
        HWND hLine = GetDlgItem(hDialog, lineIDs[i]);
        if (hLine != NULL && IsWindow(hLine))
            ShowWindow(hLine, useDark ? SW_HIDE : SW_SHOWNA);
    }

    InvalidateRect(hDialog, NULL, TRUE);
}

static BOOL PaintSizeResultsDialogSectionLines(HWND hDialog, HDC paintDC)
{
    if (!DarkMode_ShouldUseDark())
        return FALSE;

    HDC hdc = paintDC;
    if (hdc == NULL)
        hdc = GetDC(hDialog);
    if (hdc == NULL)
        return FALSE;

    int lineIDs[] = {IDC_STATIC_16, IDC_STATIC_10, IDC_STATIC_15};
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    COLORREF oldColor = SetDCPenColor(hdc, SIZE_RESULTS_DARK_SECTION_LINE);
    for (int i = 0; i < _countof(lineIDs); i++)
    {
        RECT rect;
        if (!GetSizeResultsChildRectInDialog(hDialog, lineIDs[i], &rect))
            continue;

        int y = max(rect.top, min(rect.bottom - 1, (rect.top + rect.bottom) / 2));
        MoveToEx(hdc, rect.left, y, NULL);
        LineTo(hdc, rect.right, y);
    }
    SetDCPenColor(hdc, oldColor);
    SelectObject(hdc, oldPen);

    if (paintDC == NULL)
        ReleaseDC(hDialog, hdc);
    return TRUE;
}

static LRESULT CALLBACK SizeResultsComboEditSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_NCPAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            PaintSizeResultsDarkComboEditFrame(hwnd);
            return 0;
        }
        break;
    }

    case WM_PAINT:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        if (DarkMode_ShouldUseDark())
            PaintSizeResultsDarkComboEditFrame(hwnd);
        return ret;
    }

    case WM_ERASEBKGND:
    {
        DarkModeColors colors;
        if (DarkMode_GetColors(&colors))
        {
            RECT client;
            GetClientRect(hwnd, &client);
            SizeResultsFillRectSolid((HDC)wParam, &client, colors.InputBackground);
            return TRUE;
        }
        break;
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_ENABLE:
    case WM_SIZE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, SizeResultsComboEditSkinSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK SizeResultsComboSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    CSizeResultsComboSkinState* state = (CSizeResultsComboSkinState*)dwRefData;

    switch (uMsg)
    {
    case WM_NCPAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            PaintSizeResultsDarkComboFrame(hwnd);
            return 0;
        }
        break;
    }

    case WM_PAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            PAINTSTRUCT ps;
            HDC hdc = HANDLES(BeginPaint(hwnd, &ps));
            if (hdc != NULL)
                PaintSizeResultsDarkCombo(hwnd, hdc);
            HANDLES(EndPaint(hwnd, &ps));
            return 0;
        }
        break;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark() && PaintSizeResultsDarkCombo(hwnd, (HDC)wParam))
            return 0;
        break;
    }

    case WM_ERASEBKGND:
    {
        if (DarkMode_ShouldUseDark())
            return TRUE;
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        HBRUSH hBrush = DarkMode_GetDialogCtlColorBrush(uMsg, (HDC)wParam, (HWND)lParam);
        if (hBrush != NULL)
            return (LRESULT)hBrush;
        break;
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_ENABLE:
    case WM_SIZE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case CB_SETCURSEL:
    case CB_ADDSTRING:
    case CB_DELETESTRING:
    case CB_RESETCONTENT:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        if (state != NULL && !state->ApplyingTheme)
            UpdateSizeResultsComboSkin(hwnd, state);
        else
            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, SizeResultsComboSkinSubclassProc, uIdSubclass);
        delete state;
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static BOOL BuildModuleRelativePathW(HINSTANCE module, const wchar_t* relativePath, std::wstring& path)
{
    DWORD capacity = MAX_PATH;
    for (;;)
    {
        std::wstring modulePath;
        modulePath.resize(capacity);

        SetLastError(ERROR_SUCCESS);
        DWORD len = GetModuleFileNameW(module, &modulePath[0], capacity);
        if (len == 0)
            return FALSE;

        DWORD err = GetLastError();
        // Some Windows versions report truncation as capacity - 1 plus ERROR_INSUFFICIENT_BUFFER.
        BOOL truncated = len >= capacity || (len == capacity - 1 && err == ERROR_INSUFFICIENT_BUFFER);
        if (!truncated)
        {
            modulePath.resize(len);
            size_t slash = modulePath.find_last_of(L"\\/");
            if (slash == std::wstring::npos)
                return FALSE;
            path.assign(modulePath, 0, slash + 1);
            path.append(relativePath);
            return TRUE;
        }

        if (capacity >= SAL_MAX_LONG_PATH)
            return FALSE;
        capacity = capacity > SAL_MAX_LONG_PATH / 2 ? SAL_MAX_LONG_PATH : capacity * 2;
    }
}

//****************************************************************************
//
// CViewerMasksItem
//

// this number keeps growing - is used as a source for unique IDs
DWORD ViewerHandlerID = 0;

CViewerMasksItem::CViewerMasksItem(const char* masks, const char* command, const char* arguments, const char* initDir,
                                   int viewerType, BOOL oldType)
{
    CALL_STACK_MESSAGE7("CViewerMasksItem(%s, %s, %s, %s, %d, %d)",
                        masks, command, arguments, initDir, viewerType, oldType);
    OldType = oldType;
    Masks = NULL;
    ViewerType = viewerType;
    HandlerID = ViewerHandlerID++;
    Set(masks, command, arguments, initDir);
}

CViewerMasksItem::CViewerMasksItem()
{
    CALL_STACK_MESSAGE1("CViewerMasksItem()");
    Masks = NULL;
    ViewerType = VIEWER_EXTERNAL;
    HandlerID = ViewerHandlerID++;
    OldType = FALSE;
    Set("", "", "\"$(Name)\"", "$(FullPath)");
}

CViewerMasksItem::CViewerMasksItem(CViewerMasksItem& item)
{
    CALL_STACK_MESSAGE1("CViewerMasksItem(&)");
    Masks = NULL;
    ViewerType = item.ViewerType;
    OldType = item.OldType;
    HandlerID = item.HandlerID;
    Set(item.Masks->GetMasksString(), item.Command.c_str(), item.Arguments.c_str(), item.InitDir.c_str());
}

CViewerMasksItem::~CViewerMasksItem()
{
    if (Masks != NULL)
        delete Masks;
}

BOOL CViewerMasksItem::IsGood()
{
    return Masks != NULL;
}

BOOL CViewerMasksItem::Set(const char* masks, const char* command, const char* arguments, const char* initDir)
{
    CALL_STACK_MESSAGE5("CViewerMasksItem::Set(%s, %s, %s, %s)", masks, command, arguments, initDir);

    if (Masks == NULL)
        Masks = new CMaskGroup;
    if (Masks == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    Masks->SetMasksString(masks);
    Command = command;
    Arguments = arguments;
    InitDir = initDir;

    return TRUE;
}

BOOL CViewerMasks::Load(CViewerMasks& source)
{
    CALL_STACK_MESSAGE1("CViewerMasks::Load()");
    CViewerMasksItem* item;
    DestroyMembers();
    int i;
    for (i = 0; i < source.Count; i++)
    {
        item = new CViewerMasksItem(*source[i]);
        if (!item->IsGood())
        {
            delete item;
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        Add(item);
        if (!IsGood())
        {
            delete item;
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
    }
    return TRUE;
}

//****************************************************************************
//
// CEditorMasksItem
//

// this number keeps growing - is used as a source for unique IDs
DWORD EditorHandlerID = 0;

CEditorMasksItem::CEditorMasksItem(char* masks, char* command, char* arguments, char* initDir)
{
    CALL_STACK_MESSAGE5("CEditorMasksItem(%s, %s, %s, %s)", masks, command, arguments, initDir);
    Masks = new CMaskGroup;
    HandlerID = EditorHandlerID++;
    Set(masks, command, arguments, initDir);
}

CEditorMasksItem::CEditorMasksItem()
{
    CALL_STACK_MESSAGE1("CEditorMasksItem()");
    Masks = new CMaskGroup;
    HandlerID = EditorHandlerID++;
    Set("", "", "\"$(Name)\"", "$(FullPath)");
}

CEditorMasksItem::CEditorMasksItem(CEditorMasksItem& item)
{
    CALL_STACK_MESSAGE1("CEditorMasksItem(&)");
    Masks = new CMaskGroup;
    HandlerID = item.HandlerID;
    Set(item.Masks->GetMasksString(), item.Command.c_str(), item.Arguments.c_str(), item.InitDir.c_str());
}

CEditorMasksItem::~CEditorMasksItem()
{
    if (Masks != NULL)
        delete Masks;
}

BOOL CEditorMasksItem::Set(const char* masks, const char* command, const char* arguments, const char* initDir)
{
    CALL_STACK_MESSAGE5("CEditorMasksItem::Set(%s, %s, %s, %s)", masks, command, arguments, initDir);
    if (Masks != NULL)
        Masks->SetMasksString(masks);
    Command = command;
    Arguments = arguments;
    InitDir = initDir;
    return TRUE;
}

BOOL CEditorMasksItem::IsGood()
{
    return Masks != NULL;
}

BOOL CEditorMasks::Load(CEditorMasks& source)
{
    CALL_STACK_MESSAGE1("CEditorMasks::Load()");
    CEditorMasksItem* item;
    DestroyMembers();
    int i;
    for (i = 0; i < source.Count; i++)
    {
        item = new CEditorMasksItem(*source[i]);
        if (!item->IsGood())
        {
            delete item;
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        Add(item);
        if (!IsGood())
        {
            delete item;
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
    }
    return TRUE;
}

//
// ****************************************************************************
// CCommonDialog
//

void CCommonDialog::NotifDlgJustCreated()
{
    ArrangeHorizontalLines(HWindow);
}

INT_PTR
CCommonDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // prevent panels from refreshing on the background of modal dialogs or messageboxes
        if (Modal && MainWindow != NULL && Parent != NULL && Parent == MainWindow->HWindow)
        {
            BeginStopRefresh(FALSE, TRUE); // the sniffer takes a break
            CallEndStopRefresh = TRUE;
        }
        else
            CallEndStopRefresh = FALSE;

        // when opening the dialog set the plug-ins' msgbox parent to this dialog (main thread only)
        if (Modal && MainThreadID == GetCurrentThreadId())
        {
            HOldPluginMsgBoxParent = PluginMsgBoxParent;
            PluginMsgBoxParent = HWindow;
        }

        HWND hCenterBy;
        if (HCenterAgains != NULL)
            hCenterBy = HCenterAgains;
        else
            hCenterBy = Parent;

        if (hCenterBy != NULL)
            MultiMonCenterWindow(HWindow, hCenterBy, TRUE);
        else
            MultiMonCenterWindow(HWindow, NULL, FALSE);

        break;
    }

        /* j.r.: the VK_ESCAPE variant seems better because clicking IDCANCEL does not set a variable
    case WM_COMMAND:
    {
      if (LOWORD(wParam) == IDCANCEL) // measure to avoid interrupting panel listing after each ESC
        WaitForESCReleaseBeforeTestingESC = TRUE;
      break;
    }
    */

    case WM_DESTROY:
    {
        if (GetKeyState(VK_ESCAPE) & 0x8000) // measure to avoid interrupting panel listing after each ESC
            WaitForESCReleaseBeforeTestingESC = TRUE;

        // the dialog is closing - the user might have changed the clipboard
        // (for example pasted text from an editline), so we'll verify it
        IdleRefreshStates = TRUE;  // force the state variables check during the next Idle
        IdleCheckClipboard = TRUE; // also let the clipboard be checked

        // when closing the dialog restore the msgbox parent for plug-ins
        if (HOldPluginMsgBoxParent != NULL)
            PluginMsgBoxParent = HOldPluginMsgBoxParent;

        if (CallEndStopRefresh)
        {
            EndStopRefresh(TRUE, FALSE, TRUE); // the sniffer will start again now
            CallEndStopRefresh = FALSE;
        }
        break;
    }
    }

    return CDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCommonPropSheetPage
//

void CCommonPropSheetPage::NotifDlgJustCreated()
{
    ArrangeHorizontalLines(HWindow);
}

//
// ****************************************************************************
// CSizeResultsDlg
//

CSizeResultsDlg::CSizeResultsDlg(HWND parent, const CQuadWord& size, const CQuadWord& compressed,
                                 const CQuadWord& occupied, int files, int dirs, TDirectArray<CQuadWord>* sizes)
    : CCommonDialog(HLanguage, IDD_SIZERESULTS, IDD_SIZERESULTS, parent)
{
    Size = size;
    Compressed = compressed;
    Occupied = occupied;
    Files = files;
    Dirs = dirs;
    Sizes = sizes;
}

void CSizeResultsDlg::UpdateEstimate()
{
    char buf[100];
    SendDlgItemMessage(HWindow, IDC_EST_CLUSTER, WM_GETTEXT, 11, (LPARAM)buf);
    int bytesPerCluster = atoi(buf);

    if (Sizes != NULL && Sizes->IsGood() && bytesPerCluster > 0)
    {
        if (Sizes->Count != Files)
            TRACE_E("Sizes array is not consistent with number of files.");

        CQuadWord estimated(0, 0);
        CQuadWord s;
        int i;
        for (i = 0; i < Sizes->Count; i++)
        {
            s = Sizes->At(i);
            estimated += s - ((s - CQuadWord(1, 0)) % CQuadWord(bytesPerCluster, 0)) +
                         CQuadWord(bytesPerCluster - 1, 0);
        }

        SetWindowText(GetDlgItem(HWindow, IDC_EST_SIZE), PrintDiskSize(buf, estimated, 1));

        if (estimated == CQuadWord(0, 0))
            strcpy(buf, "0 %");
        else
        {
            sprintf(buf, "%-1.4lg %%", 100 * Size.GetDouble() / estimated.GetDouble());
            PointToLocalDecimalSeparator(buf, _countof(buf));
        }
        SetWindowText(GetDlgItem(HWindow, IDC_EST_UTIL), buf);

        EnableWindow(GetDlgItem(HWindow, IDC_EST_SIZE), TRUE);
        EnableWindow(GetDlgItem(HWindow, IDC_EST_UTIL), TRUE);
    }
    else
    {
        EnableWindow(GetDlgItem(HWindow, IDC_EST_SIZE), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_EST_UTIL), FALSE);
        SetWindowText(GetDlgItem(HWindow, IDC_EST_SIZE), UnknownText);
        SetWindowText(GetDlgItem(HWindow, IDC_EST_UTIL), UnknownText);
    }
}

INT_PTR
CSizeResultsDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CSizeResultsDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_PAINT:
    {
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        PaintSizeResultsDialogSectionLines(HWindow, NULL);
        return ret;
    }

    case WM_PRINTCLIENT:
    {
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        PaintSizeResultsDialogSectionLines(HWindow, (HDC)wParam);
        return ret;
    }

    case WM_INITDIALOG:
    {
        GetDlgItemText(HWindow, IDS_OCCUPIED, UnknownText, 100); // obtain the "unknown" string for later use

        char buf[100];

        SetWindowText(GetDlgItem(HWindow, IDS_FILESCOUNT), NumberToStr(buf, CQuadWord(Files, 0)));
        SetWindowText(GetDlgItem(HWindow, IDS_DIRSCOUNT), NumberToStr(buf, CQuadWord(Dirs, 0)));

        if (Occupied != CQuadWord(-1, -1))
        {
            SetWindowText(GetDlgItem(HWindow, IDS_OCCUPIED), PrintDiskSize(buf, Occupied, 1));
            if (Occupied == CQuadWord(0, 0))
                strcpy(buf, "0 %");
            else
            {
                double result = 100 * Size.GetDouble() / Occupied.GetDouble();
                // patch for a 2GB sparse file where 3.052e+006 % was shown instead of 3051757.83 %
                // for values above 1000, lg prints exponential form so we use lf
                // for smaller numbers lg is better because it prints 100 rather than 100.00
                if (result > 1000)
                    sprintf(buf, "%-1.2lf %%", result);
                else
                    sprintf(buf, "%-1.4lg %%", result);
                PointToLocalDecimalSeparator(buf, _countof(buf));
            }
            SetWindowText(GetDlgItem(HWindow, IDS_DISKUTILIZATION), buf);
        }
        else
        {
            EnableWindow(GetDlgItem(HWindow, IDS_OCCUPIED), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDS_DISKUTILIZATION), FALSE);
        }

        SetWindowText(GetDlgItem(HWindow, IDS_SIZE), PrintDiskSize(buf, Size, 1));
        if (Compressed != CQuadWord(-1, -1))
        {
            SetWindowText(GetDlgItem(HWindow, IDS_COMPSIZE), PrintDiskSize(buf, Compressed, 1));
            if (Size == CQuadWord(0, 0))
            {
                strcpy(buf, "100 %");
            }
            else
            {
                sprintf(buf, "%-1.4lg %%", 100 * Compressed.GetDouble() / Size.GetDouble());
                PointToLocalDecimalSeparator(buf, _countof(buf));
            }
            SetWindowText(GetDlgItem(HWindow, IDS_COMPRATIO), buf);
        }
        else
        {
            EnableWindow(GetDlgItem(HWindow, IDS_COMPSIZE), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDS_COMPRATIO), FALSE);
        }

        // fill the combobox

        DWORD clusterSize = 2048; // most likely used for CDs
        CFilesWindow* panel = MainWindow->GetNonActivePanel();
        if (panel->Is(ptDisk))
        {
            DWORD sectorsPerCluster, bytesPerSector, numberOfFreeClusters, totalNumberOfClusters;
            if (MyGetDiskFreeSpace(MainWindow->GetNonActivePanel()->GetPath(),
                                   &sectorsPerCluster, &bytesPerSector,
                                   &numberOfFreeClusters, &totalNumberOfClusters))
            {
                clusterSize = sectorsPerCluster * bytesPerSector;
            }
        }

        HWND hCombo = GetDlgItem(HWindow, IDC_EST_CLUSTER);
        SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
        SendMessage(hCombo, CB_LIMITTEXT, 11, 0);

        int selIndex = -1;
        DWORD arr[] = {512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, (DWORD)-1};
        int i;
        for (i = 0; arr[i] != -1; i++)
        {
            itoa(arr[i], buf, 10);
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)buf);
            if (clusterSize == arr[i])
                selIndex = i;
        }

        if (selIndex != -1)
            SendMessage(hCombo, CB_SETCURSEL, selIndex, 0);
        else
        {
            itoa(clusterSize, buf, 10);
            SendMessage(hCombo, WM_SETTEXT, 0, (LPARAM)buf);
        }

        if (Sizes == NULL || !Sizes->IsGood())
            EnableWindow(hCombo, FALSE);

        UpdateEstimate();
        ApplySizeResultsDialogTheme(HWindow);

        break;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == CBN_SELCHANGE)
        {
            PostMessage(HWindow, WM_COMMAND, MAKELPARAM(0, CBN_EDITCHANGE), 0);
        }
        if (HIWORD(wParam) == CBN_EDITCHANGE)
        {
            UpdateEstimate();
        }
        break;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
    {
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        ApplySizeResultsDialogTheme(HWindow);
        return ret;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CSelectDialog
//

void CSelectDialog::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CSelectDialog::Validate()");
    HWND hWnd;
    if (ti.GetControl(hWnd, IDE_FILEMASK))
    {
        if (ti.Type == ttDataFromWindow)
        {
            CPathBuffer buf; // Heap-allocated for long path support
            strcpy(buf, Mask); // backup
            SendMessage(hWnd, WM_GETTEXT, MAX_PATH, (LPARAM)Mask);
            CMaskGroup mask(Mask);
            int errorPos;
            if (!mask.PrepareMasks(errorPos))
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INCORRECTSYNTAX));
                SetFocus(hWnd);
                SendMessage(hWnd, CB_SETEDITSEL, 0, MAKELPARAM(errorPos, errorPos + 1));
                ti.ErrorOn(IDE_FILEMASK);
            }
            strcpy(Mask, buf); // restoration
        }
    }
}

void CSelectDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CSelectDialog::Transfer()");
    char** history = Configuration.SelectHistory;
    HWND hWnd;
    if (ti.GetControl(hWnd, IDE_FILEMASK))
    {
        if (ti.Type == ttDataToWindow)
        {
            LoadComboFromStdHistoryValues(hWnd, history, SELECT_HISTORY_SIZE);
            SendMessage(hWnd, CB_LIMITTEXT, MAX_PATH - 1, 0);
            SendMessage(hWnd, WM_SETTEXT, 0, (LPARAM)Mask);
        }
        else
        {
            SendMessage(hWnd, WM_GETTEXT, MAX_PATH, (LPARAM)Mask);
            AddValueToStdHistoryValues(history, SELECT_HISTORY_SIZE, Mask, FALSE);
        }
    }
}

INT_PTR
CSelectDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CSelectDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        InstallWordBreakProc(GetDlgItem(HWindow, IDE_FILEMASK)); // install WordBreakProc to the combobox

        CHyperLink* hl = new CHyperLink(HWindow, IDC_FILEMASK_HINT, STF_DOTUNDERLINE);
        if (hl != NULL)
            hl->SetActionShowHint(LoadStr(IDS_MASKS_HINT));

        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CImportConfigDialog
//

CImportConfigDialog::CImportConfigDialog()
    : CCommonDialog(HLanguage, IDD_IMPORTCONFIG, NULL)
{
}

CImportConfigDialog::~CImportConfigDialog()
{
}

extern const char* SalamanderConfigurationVersions[SALCFG_ROOTS_COUNT];

void CImportConfigDialog::Transfer(CTransferInfo& ti)
{
    if (ti.Type == ttDataToWindow)
    {
        char buff[5000];
        char buff2[5000];

        // CAPTION: Welcome to %s
        GetWindowText(HWindow, buff, 5000);
        _snprintf_s(buff2, _TRUNCATE, buff, SALAMANDER_TEXT_VERSION);
        SetWindowText(HWindow, buff2);

        // COMBOBOX Import Configuration
        SendDlgItemMessage(HWindow, IDC_IMPORTCONFIG, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_IMPORTCFG_DEFCFG));
        int selIndex = 0; // use the default item if nothing better is found
        int i;
        for (i = 0; i < SALCFG_ROOTS_COUNT; i++)
        {
            if (ConfigurationExist[i])
            {
                // detect whether this is "Sally", "Open Salamander", "Altap Salamander", or the old "Servant Salamander"
                BOOL sally = StrIStr(SalamanderConfigurationRoots[i], "Sally") != NULL;
                BOOL openSalamander = StrIStr(SalamanderConfigurationRoots[i], "Open Salamander") != NULL;
                BOOL altapSalamander = StrIStr(SalamanderConfigurationRoots[i], "Altap Salamander") != NULL;
                const char* name = sally              ? "Sally %s"
                                   : openSalamander   ? "Open Salamander %s"
                                   : altapSalamander  ? "Altap Salamander %s"
                                                      : "Servant Salamander %s";
                sprintf(buff, name, SalamanderConfigurationVersions[i]);
                SendDlgItemMessage(HWindow, IDC_IMPORTCONFIG, CB_ADDSTRING, 0, (LPARAM)buff);
                if (selIndex == 0)
                    selIndex = 1; // the last configuration becomes default
            }
        }
        if (selIndex == 0) // nothing to choose from, disable the combobox
        {
            EnableWindow(GetDlgItem(HWindow, IDC_IMPORTCONFIG), FALSE);
        }
        SendDlgItemMessage(HWindow, IDC_IMPORTCONFIG, CB_SETCURSEL, selIndex, NULL);

        // LISTVIEW Remove Configuration
        HWND hListView = GetDlgItem(HWindow, IDC_REMOVECONFIG);
        selIndex = -1;
        int index = 0;
        for (i = 0; i < SALCFG_ROOTS_COUNT; i++)
        {
            if (ConfigurationExist[i])
            {
                LVITEM lvi;
                lvi.mask = LVIF_TEXT | LVIF_STATE;
                lvi.iItem = index;
                lvi.iSubItem = 0;
                lvi.state = 0;

                // detect whether this is "Sally", "Open Salamander", "Altap Salamander", or the old "Servant Salamander"
                BOOL sally = StrIStr(SalamanderConfigurationRoots[i], "Sally") != NULL;
                BOOL openSalamander = StrIStr(SalamanderConfigurationRoots[i], "Open Salamander") != NULL;
                BOOL altapSalamander = StrIStr(SalamanderConfigurationRoots[i], "Altap Salamander") != NULL;
                const char* name = sally              ? "Sally %s"
                                   : openSalamander   ? "Open Salamander %s"
                                   : altapSalamander  ? "Altap Salamander %s"
                                                      : "Servant Salamander %s";
                sprintf(buff, name, SalamanderConfigurationVersions[i]);
                lvi.pszText = buff;
                ListView_InsertItem(hListView, &lvi);
                index++;
                if (selIndex == -1)
                {
                    DWORD state = LVIS_SELECTED | LVIS_FOCUSED;
                    ListView_SetItemState(hListView, 0, state, state);
                    selIndex = 0;
                }
            }
        }
    }
    else
    {
        // COMBOBOX Import Configuration
        int sel = (int)SendDlgItemMessage(HWindow, IDC_IMPORTCONFIG, CB_GETCURSEL, 0, NULL);
        if (sel > 0)
        {
            sel--; // the first item is Don't import
            int index = 0;
            int i;
            for (i = 0; i < SALCFG_ROOTS_COUNT; i++)
            {
                if (ConfigurationExist[i])
                {
                    if (sel == index)
                    {
                        IndexOfConfigurationToLoad = i;
                        break;
                    }
                    index++;
                }
            }
        }

        // LISTVIEW Remove Configuration
        HWND hListView = GetDlgItem(HWindow, IDC_REMOVECONFIG);
        int itemsCount = ListView_GetItemCount(hListView);
        int index = 0;
        int i;
        for (i = 0; i < SALCFG_ROOTS_COUNT; i++)
        {
            if (ConfigurationExist[i])
            {
                DWORD state = ListView_GetItemState(hListView, index, LVIS_STATEIMAGEMASK);
                if (state == INDEXTOSTATEIMAGEMASK(2))
                    DeleteConfigurations[i] = TRUE;
                index++;
            }
        }
    }
}

INT_PTR
CImportConfigDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // under W2K when launched via a shortcut set to MAXIMIZED
        // the dialog appeared maximized; SC_RESTORE fixes it
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        SendMessage(HWindow, WM_SYSCOMMAND, SC_RESTORE, 0);

        // checkboxes for the listview
        HWND hListView = GetDlgItem(HWindow, IDC_REMOVECONFIG);
        DWORD exFlags = LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES;
        DWORD origFlags = ListView_GetExtendedListViewStyle(hListView);
        ListView_SetExtendedListViewStyle(hListView, origFlags | exFlags); // 4.71

        // add the Name column to the listview with columns
        LVCOLUMN lvc;
        lvc.mask = LVCF_TEXT | LVCF_FMT;
        char buff[] = "aa";
        lvc.pszText = buff;
        lvc.fmt = LVCFMT_LEFT;
        lvc.iSubItem = 0;
        ListView_InsertColumn(hListView, 0, &lvc);
        ListView_SetColumnWidth(hListView, 0, LVSCW_AUTOSIZE);

        return ret;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CLanguageSelectorDialog
//

CLanguageSelectorDialog::CLanguageSelectorDialog(HWND hParent, char* slgName, const char* pluginName)
    : CCommonDialog(NULL, pluginName == NULL ? IDD_SLGSELECTOR : IDD_SLGSELECTORPLUG, hParent), Items(5, 5)
{
    Web = NULL;
    SLGName = slgName;
    OpenedFromConfiguration = hParent != NULL && pluginName == NULL;
    OpenedForPlugin = pluginName != NULL;
    HListView = NULL;
    PluginName = pluginName;
    ExitButtonLabel[0] = 0;
}

CLanguageSelectorDialog::~CLanguageSelectorDialog()
{
    int i;
    for (i = 0; i < Items.Count; i++)
        Items[i].Free();
}

int CLanguageSelectorDialog::Execute()
{
    HINSTANCE hTmpLanguage = NULL;
    if (OpenedFromConfiguration || OpenedForPlugin)
    {
        // use the template from the currently running language version
        Modul = HLanguage;
    }
    else
    {
        // load the template from the best available SLG
        int index = GetPreferredLanguageIndex(SLGName);
        std::wstring pathW;
        std::wstring slgNameW = AnsiToWide(Items[index].FileName);
        if (BuildModuleRelativePathW(HInstance, (L"lang\\" + slgNameW).c_str(), pathW))
            hTmpLanguage = HANDLES(LoadLibraryW(pathW.c_str()));
        if (hTmpLanguage != NULL)
            Modul = hTmpLanguage;
    }
    if (!LoadString(Modul, IDS_SELLANGEXITBUTTON, ExitButtonLabel, 100))
        strcpy(ExitButtonLabel, "Exit");
    int ret = (int)CCommonDialog::Execute();
    if (hTmpLanguage != NULL)
    {
        Modul = NULL;
        HANDLES(FreeLibrary(hTmpLanguage));
    }

    return ret;
}

BOOL CLanguageSelectorDialog::GetSLGName(char* path, int index)
{
    if (index >= Items.Count)
        return FALSE;
    lstrcpy(path, Items[index].FileName);
    return TRUE;
}

BOOL CLanguageSelectorDialog::SLGNameExists(const char* slgName)
{
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        if (StrICmp(Items[i].FileName, slgName) == 0)
            return TRUE;
    }
    return FALSE;
}

void CLanguageSelectorDialog::FillControls()
{
    int index = ListView_GetNextItem(HListView, -1, LVIS_FOCUSED);
    if (index != -1)
    {
        SetDlgItemTextW(HWindow, IDC_SLG_AUTHOR, Items[index].AuthorW);
        SetDlgItemText(HWindow, IDC_SLG_WEB, Items[index].Web);
        SetDlgItemTextW(HWindow, IDC_SLG_COMMENT, Items[index].CommentW);
        if (PluginName == NULL)
            SetDlgItemText(HWindow, IDC_SLG_HELPDIR, Items[index].HelpDir);
        if (Web != NULL)
        {
            char buff[300];
            sprintf(buff, "http://%s", Items[index].Web);
            Web->SetActionOpen(buff);
        }
    }
}

void CLanguageSelectorDialog::LoadListView()
{
    char buff[500];
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        LVITEM lvi;
        lvi.mask = 0;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        ListView_InsertItem(HListView, &lvi);

        Items[i].GetLanguageName(buff, 200);
        ListView_SetItemText(HListView, i, 0, buff);
        sprintf(buff, "lang\\%s", Items[i].FileName);
        ListView_SetItemText(HListView, i, 1, buff);
    }

    int preferredIndex = GetPreferredLanguageIndex(SLGName);
    DWORD state = LVIS_SELECTED | LVIS_FOCUSED;
    ListView_SetItemState(HListView, preferredIndex, state, state);
    ListView_EnsureVisible(HListView, preferredIndex, FALSE);

    FillControls();
}

void CLanguageSelectorDialog::Transfer(CTransferInfo& ti)
{
    if (PluginName != NULL) // show this checkbox only when selecting an alternative language for a plug-in
        ti.CheckBox(IDC_USESAMESLGINOTHERPLUGINS, Configuration.UseAsAltSLGInOtherPlugins);

    if (ti.Type == ttDataToWindow)
    {
        LoadListView();

        // we do not want a horizontal scrollbar, so first fill items and only then set the column widths
        RECT r;
        GetClientRect(HListView, &r);
        ListView_SetColumnWidth(HListView, 0, r.right / 1.6);
        ListView_SetColumnWidth(HListView, 1, LVSCW_AUTOSIZE_USEHEADER);
    }
    else
    {
        int index = ListView_GetNextItem(HListView, -1, LVIS_FOCUSED);
        if (index != -1)
        {
            lstrcpy(SLGName, Items[index].FileName);
            if (PluginName != NULL) // store the alternative language name only when selecting an alternative language for a plug-in
            {
                if (Configuration.UseAsAltSLGInOtherPlugins)
                    lstrcpy(Configuration.AltPluginSLGName, SLGName);
                else
                    Configuration.AltPluginSLGName[0] = 0;
            }
        }
    }
}

BOOL CLanguageSelectorDialog::Initialize(const char* slgSearchPath, HINSTANCE pluginDLL)
{
    CPathBuffer path; // Heap-allocated for long path support
    std::wstring pathW;
    BOOL useWideSearchPath = FALSE;
    if (slgSearchPath == NULL)
    {
        if (!BuildModuleRelativePathW(NULL, L"lang\\*.slg", pathW))
            return FALSE;
        useWideSearchPath = TRUE;
    }
    else
        lstrcpyn(path, slgSearchPath, path.Size());

    WIN32_FIND_DATAW file;
    HANDLE hFind = useWideSearchPath ? SalFindFirstFileWideH(pathW.c_str(), &file) : SalFindFirstFileHW(path, &file);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            char cFileNameA[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, file.cFileName, -1, cFileNameA, MAX_PATH, NULL, NULL);
            char* point = strrchr(cFileNameA, '.');
            if (point != NULL && stricmp(point + 1, "slg") == 0) // it was returning *.slg*
            {
                CLanguage lang;
                if (lang.Init(cFileNameA, pluginDLL))
                {
                    Items.Add(lang);
                    if (!Items.IsGood())
                    {
                        Items.ResetState();
                        lang.Free();
                        return FALSE;
                    }
                }
            }
        } while (SalLPFindNextFile(hFind, &file));
        HANDLES(FindClose(hFind));
    }
    return TRUE;
}

int CLanguageSelectorDialog::GetPreferredLanguageIndex(const char* selectSLGName, BOOL exactMatch)
{
    WORD langID = GetUserDefaultUILanguage();

    WORD primaryID = PRIMARYLANGID(langID);
    int localeIndex = -1;        // index corresponding to the user's locale
    int primarylocaleIndex = -1; // index corresponding to the user's primary language locale
    int englishIndex = -1;       // index of the file "english.slg"
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        if (selectSLGName != NULL && stricmp(Items[i].FileName, selectSLGName) == 0)
            return i;
        if (localeIndex == -1 && Items[i].LanguageID == langID)
            localeIndex = i;
        if (primarylocaleIndex == -1 && PRIMARYLANGID(Items[i].LanguageID) == primaryID)
            primarylocaleIndex = i;
        if (stricmp(Items[i].FileName, "english.slg") == 0)
            englishIndex = i;
    }
    if (localeIndex == -1)
    {
        // if we didn't find a language exactly matching the user's settings
        if (primarylocaleIndex != -1)
        {
            // try to assign at least the primary language
            localeIndex = primarylocaleIndex;
        }
        else
        {
            if (!exactMatch)
            {
                if (englishIndex != -1)
                {
                    // if even that isn't found, prefer the English version
                    localeIndex = englishIndex;
                }
                else
                {
                    // otherwise take whichever one is available
                    localeIndex = 0;
                }
            }
        }
    }
    return localeIndex;
}

INT_PTR
CLanguageSelectorDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {

        // JRY: For AS 2.53 which ships with Czech, German and English we send other translations to the "Translations" section on the forum
        //     https://forum.altap.cz/viewforum.php?f=23 - in the hope that someone will be motivated to create a translation.

        // There is no download page for languages yet, so this button is disabled
        // EnableWindow(GetDlgItem(HWindow, IDB_GETMORELANGS), FALSE);

        if (!OpenedFromConfiguration && !OpenedForPlugin)
        {
            // put the program name in the title since this is the first window the user sees
            SetWindowText(HWindow, MAINWINDOW_NAME);
        }
        else
        {
            if (PluginName != NULL)
            {
                // put the plug-in name in the title so the user knows which plug-in the language is for
                char buf[200];
                _snprintf_s(buf, _TRUNCATE, "%s: ", PluginName);
                buf[99] = 0; // use only 100 characters for the plug-in name so some space remains for the original title dialog
                int len = (int)strlen(buf);
                if (GetWindowText(HWindow, buf + len, 200 - len))
                    SetWindowText(HWindow, buf);
            }
        }
        if (!OpenedFromConfiguration && PluginName == NULL) // turn the Cancel button into Exit
            SetDlgItemText(HWindow, IDCANCEL, ExitButtonLabel);
        if (PluginName != NULL) // disable closing
            EnableMenuItem(GetSystemMenu(HWindow, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);

        Web = new CHyperLink(HWindow, IDC_SLG_WEB, STF_HYPERLINK_COLOR);

        HListView = GetDlgItem(HWindow, IDC_SLG_LIST);

        DWORD exFlags = LVS_EX_FULLROWSELECT;
        DWORD origFlags = ListView_GetExtendedListViewStyle(HListView);
        ListView_SetExtendedListViewStyle(HListView, origFlags | exFlags); // 4.71

        // add the Language and Path columns to the listview
        char buff[100];
        LVCOLUMN lvc;
        lvc.mask = LVCF_TEXT | LVCF_SUBITEM;
        lvc.pszText = buff;
        lvc.iSubItem = 0;
        GetDlgItemText(HWindow, IDC_SLG_DESCR, buff, 100);
        DestroyWindow(GetDlgItem(HWindow, IDC_SLG_DESCR));
        ListView_InsertColumn(HListView, 0, &lvc);

        lvc.iSubItem = 1;
        GetDlgItemText(HWindow, IDC_SLG_PATH, buff, 100);
        DestroyWindow(GetDlgItem(HWindow, IDC_SLG_PATH));
        ListView_InsertColumn(HListView, 1, &lvc);

        // under W2K when launched via a shortcut set to MAXIMIZED
        // the dialog appeared maximized; SC_RESTORE fixes it
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        SendMessage(HWindow, WM_SYSCOMMAND, SC_RESTORE, 0);
        return ret;
    }

    case WM_COMMAND:
    {
        if (PluginName != NULL && LOWORD(wParam) == IDCANCEL)
            return 0;
        if (LOWORD(wParam) == IDB_GETMORELANGS)
            ShellExecute(HWindow, "open", "https://github.com/0xeb/sally/discussions", NULL, NULL, SW_SHOWNORMAL);
        if (LOWORD(wParam) == IDB_REFRESHLANGS)
        {
            ListView_DeleteAllItems(HListView);
            int i;
            for (i = 0; i < Items.Count; i++)
                Items[i].Free();
            Items.DestroyMembers();
            Initialize();
            if (GetLanguagesCount() == 0) // should not happen because this dialog is loaded from the .slg module (that .slg cannot be deleted)
            {
                MessageBox(HWindow, "Unable to find any language file (.SLG) in subdirectory LANG.\n"
                                    "Please reinstall Open Salamander.",
                           SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONERROR);
                TRACE_E("CLanguageSelectorDialog: unexpected situation (no language file): calling ExitProcess(667).");
                //          ExitProcess(667);
                TerminateProcess(GetCurrentProcess(), 667); // harder exit (this call still performs some operations)
            }
            LoadListView();
        }
        break;
    }

    case WM_NOTIFY:
    {
        if (wParam == IDC_SLG_LIST)
        {
            LPNMHDR nmh = (LPNMHDR)lParam;
            switch (nmh->code)
            {
            case NM_DBLCLK:
            {
                LVHITTESTINFO ht;
                DWORD pos = GetMessagePos();
                ht.pt.x = GET_X_LPARAM(pos);
                ht.pt.y = GET_Y_LPARAM(pos);
                ScreenToClient(HListView, &ht.pt);
                ListView_HitTest(HListView, &ht);
                int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
                if (index != -1 && ht.iItem == index)
                {
                    PostMessage(HWindow, WM_COMMAND, MAKELPARAM(IDOK, BN_CLICKED),
                                (LPARAM)GetDlgItem(HWindow, IDOK));
                    return 0;
                }
                break;
            }

            case LVN_ITEMCHANGED:
            {
                FillControls();
                return 0;
            }
            }
        }
        break;
    }

    case WM_SYSCOLORCHANGE:
    {
        ListView_SetBkColor(HListView, GetSysColor(COLOR_WINDOW));
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CSkillLevelDialog
//

CSkillLevelDialog::CSkillLevelDialog(HWND hParent, int* level)
    : CCommonDialog(HLanguage, IDD_SKILLLEVEL, IDD_SKILLLEVEL, hParent)
{
    Level = level;
}

void CSkillLevelDialog::Transfer(CTransferInfo& ti)
{
    ti.RadioButton(IDC_SL_BEGINNER, SKILL_LEVEL_BEGINNER, *Level);
    ti.RadioButton(IDC_SL_INTERMEDIATE, SKILL_LEVEL_INTERMEDIATE, *Level);
    ti.RadioButton(IDC_SL_ADVANCED, SKILL_LEVEL_ADVANCED, *Level);
}

//****************************************************************************
//
// CCompareArgsDlg
//

CCompareArgsDlg::CCompareArgsDlg(HWND parent, BOOL comparingFiles, char* compareName1,
                                 char* compareName2, int* cnfrmShowNamesToCompare)
    : CCommonDialog(HLanguage, IDD_USERMENUCOMPAREARGS, comparingFiles ? IDH_USERMENUCOMPAREARGS_F : IDH_USERMENUCOMPAREARGS_D, parent)
{
    ComparingFiles = comparingFiles;
    CompareName1 = compareName1;
    CompareName2 = compareName2;
    CnfrmShowNamesToCompare = cnfrmShowNamesToCompare;
}

void CCompareArgsDlg::Validate(CTransferInfo& ti)
{
    CPathBuffer buf; // Heap-allocated for long path support
    ti.EditLine(IDE_UMC_NAME1, buf, buf.Size());
    if (buf[0] == 0)
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_FF_EMPTYSTRING));
        ti.ErrorOn(IDE_UMC_NAME1);
        return;
    }
    ti.EditLine(IDE_UMC_NAME2, buf, buf.Size());
    if (buf[0] == 0)
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_FF_EMPTYSTRING));
        ti.ErrorOn(IDE_UMC_NAME2);
        return;
    }
}

void CCompareArgsDlg::Transfer(CTransferInfo& ti)
{
    ti.EditLine(IDE_UMC_NAME1, CompareName1, SAL_MAX_LONG_PATH);
    ti.EditLine(IDE_UMC_NAME2, CompareName2, SAL_MAX_LONG_PATH);

    int c = !*CnfrmShowNamesToCompare;
    ti.CheckBox(IDC_UMC_SHOWTHISDLG, c);
    *CnfrmShowNamesToCompare = !c;
}

INT_PTR
CCompareArgsDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        if (!ComparingFiles)
        {
            SetWindowText(HWindow, LoadStr(IDS_USERMENUCOMPAREARGSTITLE));
            SetDlgItemText(HWindow, IDT_UMC_NAME1, LoadStr(IDS_USERMENUCOMPAREARG1));
            SetDlgItemText(HWindow, IDT_UMC_NAME2, LoadStr(IDS_USERMENUCOMPAREARG2));
        }
        CHyperLink* hl = new CHyperLink(HWindow, IDT_UMC_HOWTOREVERT, STF_DOTUNDERLINE);
        if (hl != NULL)
            hl->SetActionShowHint(LoadStr(IDS_UMCCONFIRMHOWTOREV));
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDB_UMC_BROWSENAME1:
        case IDB_UMC_BROWSENAME2:
        {
            int editID = LOWORD(wParam) == IDB_UMC_BROWSENAME1 ? IDE_UMC_NAME1 : IDE_UMC_NAME2;
            if (ComparingFiles)
                BrowseCommand(HWindow, editID, IDS_ALLFILTER);
            else
            {
                CPathBuffer path; // Heap-allocated for long path support
                GetDlgItemText(HWindow, editID, path, path.Size());
                if (GetTargetDirectory(HWindow, HWindow, LoadStr(IDS_BROWSEUMCDIRTITLE),
                                       LoadStr(IDS_BROWSEUMCDIRTEXT), path, FALSE, path))
                {
                    SetDlgItemText(HWindow, editID, path);
                }
            }
            return TRUE;
        }
        }
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}
