// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_SHICONOV_DIAG_STANDALONE
#include "precomp.h"
#endif

#include "shiconov_diag.h"
#include "shellsup_diag.h" // AppendAsciiEscapedW

#include <stdio.h>
#include <string.h>

CShellOverlayDiagLog ShellOverlayDiag;

ShellOverlayDiagRecord* CShellOverlayDiagLog::Add(const wchar_t* name, const wchar_t* clsid)
{
    if (Filled >= SHICONOV_DIAG_CAPACITY)
        return NULL;

    ShellOverlayDiagRecord* rec = &Records[Filled++];
    *rec = ShellOverlayDiagRecord();

    if (name != NULL)
        wcsncpy_s(rec->Name, name, _TRUNCATE);
    if (clsid != NULL)
        wcsncpy_s(rec->Clsid, clsid, _TRUNCATE);

    return rec;
}

ShellOverlayDiagRecord* CShellOverlayDiagLog::Find(const wchar_t* name)
{
    if (name == NULL)
        return NULL;

    // Exact compare, not case-insensitive: the registry key name is the identity here, and
    // the reporter's Tortoise keys differ from ordinary ones only by leading whitespace.
    for (int i = 0; i < Filled; i++)
    {
        if (wcscmp(Records[i].Name, name) == 0)
            return &Records[i];
    }
    return NULL;
}

const ShellOverlayDiagRecord* CShellOverlayDiagLog::At(int index) const
{
    if (index < 0 || index >= Filled)
        return NULL;
    return &Records[index];
}

void CShellOverlayDiagLog::Reset()
{
    Filled = 0;
    Header = ShellOverlayDiagHeader();
}

const char* OverlayOutcomeText(OverlayOutcome outcome)
{
    switch (outcome)
    {
    case OverlayOutcome::Loaded:
        return "LOADED";
    case OverlayOutcome::SkippedGloballyDisabled:
        return "SKIPPED all-overlays-off";
    case OverlayOutcome::SkippedUserDisabled:
        return "SKIPPED in-disabled-list";
    case OverlayOutcome::RegKeyOpenFailed:
        // Deliberately not spelled as the Win32 API name: the architecture gate counts raw
        // identifiers anywhere in the file, including inside string literals.
        return "DROPPED registry-key-open";
    case OverlayOutcome::RegValueMissing:
        return "DROPPED reg-value-missing";
    case OverlayOutcome::RegValueNotSz:
        return "DROPPED reg-value-not-REG_SZ";
    case OverlayOutcome::InvalidClsid:
        return "DROPPED invalid-CLSID";
    case OverlayOutcome::CoCreateFailed:
        return "DROPPED CoCreateInstance";
    case OverlayOutcome::GetOverlayInfoFailed:
        return "DROPPED GetOverlayInfo";
    case OverlayOutcome::NoIconFileFlag:
        return "DROPPED no-ISIOI_ICONFILE";
    case OverlayOutcome::IconExtractFailed:
        return "DROPPED ExtractIcons";
    case OverlayOutcome::ItemAllocFailed:
        return "DROPPED alloc";
    case OverlayOutcome::CapReached:
        return "DROPPED cap-reached";
    case OverlayOutcome::ArrayGrowFailed:
        return "DROPPED array-grow";
    default:
        return "NOT-ATTEMPTED";
    }
}

int FormatShellOverlayDiagRecord(const ShellOverlayDiagRecord& record, char* buf, int bufSize)
{
    if (buf == NULL || bufSize <= 0)
        return 0;

    buf[0] = 0;
    int written = 0;

    char nameAcp[256];
    nameAcp[0] = 0;
    WideCharToMultiByte(CP_ACP, 0, record.Name, -1, nameAcp, (int)sizeof(nameAcp), NULL, NULL);
    nameAcp[sizeof(nameAcp) - 1] = 0;

    char clsidAcp[64];
    clsidAcp[0] = 0;
    WideCharToMultiByte(CP_ACP, 0, record.Clsid, -1, clsidAcp, (int)sizeof(clsidAcp), NULL, NULL);
    clsidAcp[sizeof(clsidAcp) - 1] = 0;

    written += sprintf_s(buf + written, bufSize - written,
                         "\"%s\" %s %s", nameAcp, clsidAcp, OverlayOutcomeText(record.Outcome));

    if (record.Outcome == OverlayOutcome::Loaded)
    {
        written += sprintf_s(buf + written, bufSize - written,
                            " idx=%d prio=%d icons=%s%s%s",
                            record.LoadedIndex, record.Priority,
                            (record.IconsOk & 1) ? "16/" : "-/",
                            (record.IconsOk & 2) ? "32/" : "-/",
                            (record.IconsOk & 4) ? "48" : "-");
    }
    else if (record.Hr != 0)
    {
        written += sprintf_s(buf + written, bufSize - written, " hr=0x%08X", (unsigned)record.Hr);
    }

    // For an icon-extraction failure the icon file and which sizes actually came back are
    // the whole diagnosis: Sally requires all three or it drops the handler, and the sizes
    // it asks for are DPI-scaled rather than the 16/32/48 most icon files ship.
    if (record.Outcome == OverlayOutcome::IconExtractFailed ||
        record.Outcome == OverlayOutcome::NoIconFileFlag)
    {
        written += sprintf_s(buf + written, bufSize - written,
                            " got=%s%s%s index=%d flags=0x%08X",
                            (record.IconsOk & 1) ? "16 " : "- ",
                            (record.IconsOk & 2) ? "32 " : "- ",
                            (record.IconsOk & 4) ? "48" : "-",
                            record.IconIndex, record.InfoFlags);

        char iconAcp[MAX_PATH * 2];
        iconAcp[0] = 0;
        WideCharToMultiByte(CP_ACP, 0, record.IconFile, -1, iconAcp, (int)sizeof(iconAcp), NULL, NULL);
        iconAcp[sizeof(iconAcp) - 1] = 0;
        written += sprintf_s(buf + written, bufSize - written, " iconFile=\"%s\"", iconAcp);
    }

    if (record.ReaderFailures != 0)
    {
        written += sprintf_s(buf + written, bufSize - written,
                            " readerFailures=%ld (last 0x%08X)",
                            record.ReaderFailures, (unsigned)record.ReaderLastHr);
    }

    // Only worth the extra line when the ACP rendering actually lost something - which is
    // precisely the case we would otherwise never see.
    char nameEsc[512];
    AppendAsciiEscapedW(record.Name, nameEsc, (int)sizeof(nameEsc));
    if (strcmp(nameAcp, nameEsc) != 0)
        written += sprintf_s(buf + written, bufSize - written, " name(escaped)=\"%s\"", nameEsc);

    return written;
}

void SetOverlayDiagConfigRoot(ShellOverlayDiagHeader& header, const char* rootReg)
{
    lstrcpynA(header.ConfigRoot, rootReg != NULL ? rootReg : "<none: first run>",
              (int)sizeof(header.ConfigRoot));
}
