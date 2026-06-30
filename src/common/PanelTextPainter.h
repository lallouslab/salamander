// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

namespace sally::ui
{
UINT GetPanelTextDrawOptions(UINT options);

class IPanelTextPainter
{
public:
    virtual ~IPanelTextPainter() {}

    virtual BOOL DrawAnsi(HDC hdc, int x, int y, UINT options, const RECT* rect,
                          const char* text, UINT count, const INT* dx) = 0;
    virtual BOOL DrawWide(HDC hdc, int x, int y, UINT options, const RECT* rect,
                          const wchar_t* text, UINT count, const INT* dx) = 0;
};

class GdiPanelTextPainter : public IPanelTextPainter
{
public:
    BOOL DrawAnsi(HDC hdc, int x, int y, UINT options, const RECT* rect,
                  const char* text, UINT count, const INT* dx) override;
    BOOL DrawWide(HDC hdc, int x, int y, UINT options, const RECT* rect,
                  const wchar_t* text, UINT count, const INT* dx) override;
};

IPanelTextPainter* GetPanelTextPainter();
void SetPanelTextPainterForTests(IPanelTextPainter* painter);

BOOL DrawPanelTextA(HDC hdc, int x, int y, UINT options, const RECT* rect,
                    const char* text, UINT count, const INT* dx);
BOOL DrawPanelTextW(HDC hdc, int x, int y, UINT options, const RECT* rect,
                    const wchar_t* text, UINT count, const INT* dx);
} // namespace sally::ui
