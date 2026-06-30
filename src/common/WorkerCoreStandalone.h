// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Minimal include set for private/headless targets that link the extracted
// worker script core without the full Sally UI/plugin precompiled header.

#pragma once

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <ostream>
#include <string>
#include <utility>
#include <vector>
#include <stdlib.h>
#include <string.h>

#include "common/handles.h"
#include "common/array.h"
#include "common/widepath.h"
#include "plugins/shared/spl_com.h"

#ifndef CALL_STACK_MESSAGE_NONE
#define CALL_STACK_MESSAGE_NONE
#endif

#ifndef CALL_STACK_MESSAGE1
#define CALL_STACK_MESSAGE1(message)
#endif

#ifndef DEBUG_SLOW_CALL_STACK_MESSAGE1
#define DEBUG_SLOW_CALL_STACK_MESSAGE1(message)
#endif
