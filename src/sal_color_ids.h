// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Panel/viewer color indices into CurrentColors[] (and the per-scheme color arrays).
// Extracted from consts.h so that dependency-light units — notably the render-time
// dark-mode resolver in sal_colors.cpp and its headless test — can reference these
// indices without pulling in the full Sally header set. Pure #defines, no type or
// header dependencies.

// colors in CurrentColors[x]
#define FOCUS_ACTIVE_NORMAL 0 // pen colors for frame around item
#define FOCUS_ACTIVE_SELECTED 1
#define FOCUS_FG_INACTIVE_NORMAL 2
#define FOCUS_FG_INACTIVE_SELECTED 3
#define FOCUS_BK_INACTIVE_NORMAL 4
#define FOCUS_BK_INACTIVE_SELECTED 5

#define ITEM_FG_NORMAL 6 // item text colors in panel
#define ITEM_FG_SELECTED 7
#define ITEM_FG_FOCUSED 8
#define ITEM_FG_FOCSEL 9
#define ITEM_FG_HIGHLIGHT 10

#define ITEM_BK_NORMAL 11 // item background colors in panel
#define ITEM_BK_SELECTED 12
#define ITEM_BK_FOCUSED 13
#define ITEM_BK_FOCSEL 14
#define ITEM_BK_HIGHLIGHT 15

#define ICON_BLEND_SELECTED 16 // colors for icon blend
#define ICON_BLEND_FOCUSED 17
#define ICON_BLEND_FOCSEL 18

#define PROGRESS_FG_NORMAL 19 // progress bar colors
#define PROGRESS_FG_SELECTED 20
#define PROGRESS_BK_NORMAL 21
#define PROGRESS_BK_SELECTED 22

#define HOT_PANEL 23    // hot item color in panel
#define HOT_ACTIVE 24   //                 in active panel caption
#define HOT_INACTIVE 25 //                 in inactive panel caption, statusbar,...

#define ACTIVE_CAPTION_FG 26   // text color in active panel caption
#define ACTIVE_CAPTION_BK 27   // background color in active panel caption
#define INACTIVE_CAPTION_FG 28 // text color in inactive panel caption
#define INACTIVE_CAPTION_BK 29 // background color in inactive panel caption

#define THUMBNAIL_FRAME_NORMAL 30 // pen colors for frame around thumbnails
#define THUMBNAIL_FRAME_FOCUSED 31
#define THUMBNAIL_FRAME_SELECTED 32
#define THUMBNAIL_FRAME_FOCSEL 33

#define VIEWER_FG_NORMAL 0 // normal viewer colors
#define VIEWER_BK_NORMAL 1
#define VIEWER_FG_SELECTED 2 // selected text
#define VIEWER_BK_SELECTED 3

#define NUMBER_OF_COLORS 34       // number of colors in scheme
#define NUMBER_OF_VIEWERCOLORS 4  // number of colors for viewer
#define NUMBER_OF_CUSTOMCOLORS 16 // user-defined colors in color dialog
