// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_SHELLSUP_DIAG_STANDALONE
#include "precomp.h"
#endif

#include "shellsup_diag.h"

#include <stdio.h>
#include <string.h>

CShellMenuDiagLog ShellMenuDiag;

ShellMenuDiagRecord* CShellMenuDiagLog::Begin(const wchar_t* dirPathW, int selCount, bool background)
{
    Head = (Head + 1) % SHELLMENU_DIAG_CAPACITY;
    if (Filled < SHELLMENU_DIAG_CAPACITY)
        Filled++;

    ShellMenuDiagRecord* rec = &Records[Head];
    *rec = ShellMenuDiagRecord();

    rec->Tick = GetTickCount();
    rec->SelCount = selCount;
    rec->Background = background;
    if (dirPathW != NULL)
    {
        // Truncation is fine here; the record is diagnostic, never a path we act on.
        wcsncpy_s(rec->DirPathW, dirPathW, _TRUNCATE);
    }

    Open = true;
    return rec;
}

const ShellMenuDiagRecord* CShellMenuDiagLog::At(int index) const
{
    if (index < 0 || index >= Filled)
        return NULL;

    // Oldest first: the slot after Head is the oldest once the ring has wrapped.
    int oldest = (Filled == SHELLMENU_DIAG_CAPACITY) ? (Head + 1) % SHELLMENU_DIAG_CAPACITY
                                                     : 0;
    return &Records[(oldest + index) % SHELLMENU_DIAG_CAPACITY];
}

void CShellMenuDiagLog::Reset()
{
    Head = -1;
    Filled = 0;
    Open = false;
}

int AppendAsciiEscapedW(const wchar_t* text, char* buf, int bufSize)
{
    if (buf == NULL || bufSize <= 0)
        return 0;

    int written = 0;
    buf[0] = 0;
    if (text == NULL)
        return 0;

    for (const wchar_t* s = text; *s != 0; s++)
    {
        // Six characters for the widest escape plus the terminator.
        if (written + 7 > bufSize)
            break;

        if (*s >= 0x20 && *s < 0x7f)
        {
            buf[written++] = (char)*s;
        }
        else
        {
            written += sprintf_s(buf + written, bufSize - written, "\\u%04X", (unsigned)*s);
        }
        buf[written] = 0;
    }
    return written;
}

namespace
{

const char* OwnerText(ShellMenuOwner owner)
{
    switch (owner)
    {
    case ShellMenuOwner::ItemMenu:
        return "ItemMenu";
    case ShellMenuOwner::NewMenu:
        return "NewMenu";
    case ShellMenuOwner::SallyOwned:
        return "SallyOwned";
    case ShellMenuOwner::Cancelled:
        return "Cancelled";
    default:
        return "Unknown";
    }
}

} // namespace

int FormatShellMenuDiagRecord(const ShellMenuDiagRecord& record, char* buf, int bufSize)
{
    if (buf == NULL || bufSize <= 0)
        return 0;

    buf[0] = 0;
    int written = 0;

    // The ACP rendering is what the user recognizes; the escaped form is what we can
    // actually reason about when the ACP mangled it.
    char pathAcp[MAX_PATH * 2];
    pathAcp[0] = 0;
    WideCharToMultiByte(CP_ACP, 0, record.DirPathW, -1, pathAcp, (int)sizeof(pathAcp), NULL, NULL);
    pathAcp[sizeof(pathAcp) - 1] = 0;

    char pathEsc[MAX_PATH * 6];
    AppendAsciiEscapedW(record.DirPathW, pathEsc, (int)sizeof(pathEsc));

    written += sprintf_s(buf + written, bufSize - written,
                         "tick=%u dir=\"%s\" sel=%d bg=%s wideNames=%s\n",
                         record.Tick, pathAcp, record.SelCount,
                         record.Background ? "yes" : "no",
                         record.AnyNameNeedsWide ? "yes" : "no");

    if (strcmp(pathAcp, pathEsc) != 0)
        written += sprintf_s(buf + written, bufSize - written, "    dir(escaped)=\"%s\"\n", pathEsc);

    written += sprintf_s(buf + written, bufSize - written,
                         "    QueryContextMenu=0x%08X items=%d\n",
                         (unsigned)record.QueryContextMenuHr, record.MenuItemCount);

    written += sprintf_s(buf + written, bufSize - written,
                         "    cmd=%u topLevel=%s verb=\"%s\" (GetCommandString=0x%08X)\n",
                         record.TrackedCmd, record.TopLevel ? "yes" : "no",
                         record.Verb, (unsigned)record.VerbHr);

    written += sprintf_s(buf + written, bufSize - written,
                         "    owner=%s InvokeCommand=0x%08X parent=%s aliveAfter=%s\n",
                         OwnerText(record.Owner), (unsigned)record.InvokeHr,
                         record.ParentWasTransient ? "transient" : "durable",
                         record.ParentAliveAfter ? "yes" : "no");

    return written;
}
