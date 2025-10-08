﻿// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS // some CString constructors will be explicit

#include <atlbase.h>
#include <atlstr.h>
#include <atlcoll.h>
#include <atlpath.h>
#include <atlconv.h>

#include <vector>
#include <list>
#include <algorithm>
#include <functional>

#ifndef __clang__ // we’ll work around clang-cl, which doesn’t support this MS extension; we’re using it for commnet-guard during comments translation
#import <msxml6.dll>
#import <mshtml.tlb>
#endif

// TODO: reference additional headers your program requires here
