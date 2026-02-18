#pragma once
#include <X11/X.h>
#include <X11/keysym.h>
#include "defs.h"

#define MODKEY Mod4Mask

#define CFG_FOCUSED_BORDER_COL   "#89B4FA"
#define CFG_UNFOCUSED_BORDER_COL "#1E1E2E"
#define CFG_SWAP_BORDER_COL      "#1E1E2E"

#define CFG_GAPS                5
#define CFG_BORDER_WIDTH        3
#define CFG_MOVE_WINDOW_AMT     50
#define CFG_RESIZE_WINDOW_AMT   50
#define CFG_SNAP_DISTANCE       5
#define CFG_MOTION_THROTTLE     60
#define CFG_NEW_WIN_FOCUS       True
#define CFG_WARP_CURSOR         True
#define CFG_FLOATING_ON_TOP     True

#define CFG_BINDS \
    /* Application launchers */ \
    { MODKEY,                XK_Return, 0, { .cmd = build_argv("kitty") },                                            TYPE_CMD  }, \
    { MODKEY,                XK_w,      0, { .cmd = build_argv("surf git.bance.dev") },                               TYPE_CMD  }, \
    { MODKEY,                XK_space,  0, { .cmd = build_argv("dmenu_run") },                                        TYPE_CMD  }, \
    { MODKEY,                XK_equal,  0, { .cmd = build_argv("pactl set-sink-volume @DEFAULT_SINK@ +5%") },         TYPE_CMD  }, \
    { MODKEY,                XK_minus,  0, { .cmd = build_argv("pactl set-sink-volume @DEFAULT_SINK@ -5%") },         TYPE_CMD  }, \
    { MODKEY,                XK_0,      0, { .cmd = build_argv("pactl set-sink-mute @DEFAULT_SINK@ toggle") },        TYPE_CMD  }, \
    /* Window management */ \
    { MODKEY,                XK_q,      0, { .fn = close_focused    }, TYPE_FUNC }, \
    { MODKEY|ShiftMask,      XK_e,      0, { .fn = quit             }, TYPE_FUNC }, \
    { MODKEY,                XK_m,      0, { .fn = toggle_monocle   }, TYPE_FUNC }, \
    /* Focus */ \
    { MODKEY,                XK_j,      0, { .fn = focus_next       }, TYPE_FUNC }, \
    { MODKEY,                XK_k,      0, { .fn = focus_prev       }, TYPE_FUNC }, \
    /* Movement */ \
    { MODKEY|ShiftMask,      XK_j,      0, { .fn = move_focused_next  }, TYPE_FUNC }, \
    { MODKEY|ShiftMask,      XK_k,      0, { .fn = move_focused_prev  }, TYPE_FUNC }, \
    /* Keyboard window movement */ \
    { MODKEY,                XK_Up,     0, { .fn = move_win_up       }, TYPE_FUNC }, \
    { MODKEY,                XK_Down,   0, { .fn = move_win_down     }, TYPE_FUNC }, \
    { MODKEY,                XK_Left,   0, { .fn = move_win_left     }, TYPE_FUNC }, \
    { MODKEY,                XK_Right,  0, { .fn = move_win_right    }, TYPE_FUNC }, \
    /* Keyboard window resize */ \
    { MODKEY|ShiftMask,      XK_Up,     0, { .fn = resize_win_up     }, TYPE_FUNC }, \
    { MODKEY|ShiftMask,      XK_Down,   0, { .fn = resize_win_down   }, TYPE_FUNC }, \
    { MODKEY|ShiftMask,      XK_Left,   0, { .fn = resize_win_left   }, TYPE_FUNC }, \
    { MODKEY|ShiftMask,      XK_Right,  0, { .fn = resize_win_right  }, TYPE_FUNC }, \
    /* Floating / fullscreen */ \
    { MODKEY,                XK_f,      0, { .fn = toggle_floating        }, TYPE_FUNC }, \
    { MODKEY|ShiftMask,      XK_space,  0, { .fn = toggle_floating_global }, TYPE_FUNC }, \
    { MODKEY|ShiftMask,      XK_f,      0, { .fn = toggle_fullscreen      }, TYPE_FUNC }, \
    /* Workspaces 1–9 */ \
    { MODKEY,           XK_1, 0, { .ws = 0 }, TYPE_WS_CHANGE }, \
    { MODKEY|ShiftMask, XK_1, 0, { .ws = 0 }, TYPE_WS_MOVE   }, \
    { MODKEY,           XK_2, 0, { .ws = 1 }, TYPE_WS_CHANGE }, \
    { MODKEY|ShiftMask, XK_2, 0, { .ws = 1 }, TYPE_WS_MOVE   }, \
    { MODKEY,           XK_3, 0, { .ws = 2 }, TYPE_WS_CHANGE }, \
    { MODKEY|ShiftMask, XK_3, 0, { .ws = 2 }, TYPE_WS_MOVE   }, \
    { MODKEY,           XK_4, 0, { .ws = 3 }, TYPE_WS_CHANGE }, \
    { MODKEY|ShiftMask, XK_4, 0, { .ws = 3 }, TYPE_WS_MOVE   }, \
    { MODKEY,           XK_5, 0, { .ws = 4 }, TYPE_WS_CHANGE }, \
    { MODKEY|ShiftMask, XK_5, 0, { .ws = 4 }, TYPE_WS_MOVE   }, \
    { MODKEY,           XK_6, 0, { .ws = 5 }, TYPE_WS_CHANGE }, \
    { MODKEY|ShiftMask, XK_6, 0, { .ws = 5 }, TYPE_WS_MOVE   }, \
    { MODKEY,           XK_7, 0, { .ws = 6 }, TYPE_WS_CHANGE }, \
    { MODKEY|ShiftMask, XK_7, 0, { .ws = 6 }, TYPE_WS_MOVE   }, \
    { MODKEY,           XK_8, 0, { .ws = 7 }, TYPE_WS_CHANGE }, \
    { MODKEY|ShiftMask, XK_8, 0, { .ws = 7 }, TYPE_WS_MOVE   }, \
    { MODKEY,           XK_9, 0, { .ws = 8 }, TYPE_WS_CHANGE }, \
    { MODKEY|ShiftMask, XK_9, 0, { .ws = 8 }, TYPE_WS_MOVE   },
