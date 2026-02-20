#pragma once
#include <X11/Xlib.h>
#define VERSION "tilite ver. 1.0"
#define AUTHOR "(C) Lance Borden 2026"
#define LICENSE "Licensed under the GPL v3.0"

#define MF_MIN 0.05f
#define MF_MAX 0.95f
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define UDIST(a, b) abs((int)(a) - (int)(b))
#define CLAMP(x, lo, hi) (((x) < (lo)) ? (lo) : ((x) > (hi)) ? (hi) : (x))

#define MAX_BINDS 256
#define MAX_CLIENTS 99
#define MAX_SCRATCHPADS 32
#define MAX_ITEMS 256
#define MIN_WINDOW_SIZE 20
#define PATH_MAX 4096

/* workspaces */
#define TYPE_WS_CHANGE 0
#define TYPE_WS_MOVE 1
/* fn/cmd */
#define TYPE_FUNC 2
#define TYPE_CMD 3

#define NUM_WORKSPACES 9
#define WORKSPACE_NAMES                                                        \
    "1"                                                                        \
    "\0"                                                                       \
    "2"                                                                        \
    "\0"                                                                       \
    "3"                                                                        \
    "\0"                                                                       \
    "4"                                                                        \
    "\0"                                                                       \
    "5"                                                                        \
    "\0"                                                                       \
    "6"                                                                        \
    "\0"                                                                       \
    "7"                                                                        \
    "\0"                                                                       \
    "8"                                                                        \
    "\0"                                                                       \
    "9"                                                                        \
    "\0"

typedef enum { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_SWAP } DragMode;
typedef void (*event_t)(XEvent *);

typedef union {
    const char **cmd;
    void (*fn)(void);
    int ws;
} action_t;

typedef struct {
    int mods;
    KeySym keysym;
    KeyCode keycode;
    action_t action;
    int type;
} binding_t;

typedef struct client_t {
    Window win;
    int x, y, w, h;
    int orig_x, orig_y, orig_w, orig_h;
    int ws;
    Bool fixed;
    Bool floating;
    Bool fullscreen;
    Bool mapped;
    pid_t pid;
    struct client_t *next;
} client_t;

typedef struct {
    int modkey;
    int gaps;
    int border_width;
    long border_foc_col;
    long border_ufoc_col;
    long border_swap_col;
    int motion_throttle;
    int snap_distance;
    int n_binds;
    int move_window_amt;
    int resize_window_amt;
    Bool new_win_focus;
    Bool warp_cursor;
    Bool floating_on_top;
    binding_t binds[MAX_ITEMS];
    char *to_run[MAX_ITEMS];
} config_t;

typedef struct {
    const char *name;
    void (*fn)(void);
} command_t;

typedef enum {
    ATOM_NET_ACTIVE_WINDOW,
    ATOM_NET_CURRENT_DESKTOP,
    ATOM_NET_SUPPORTED,
    ATOM_NET_WM_STATE,
    ATOM_NET_WM_STATE_FULLSCREEN,
    ATOM_WM_STATE,
    ATOM_NET_WM_WINDOW_TYPE,
    ATOM_NET_WORKAREA,
    ATOM_WM_DELETE_WINDOW,
    ATOM_NET_WM_STRUT,
    ATOM_NET_WM_STRUT_PARTIAL,
    ATOM_NET_SUPPORTING_WM_CHECK,
    ATOM_NET_WM_NAME,
    ATOM_UTF8_STRING,
    ATOM_NET_WM_DESKTOP,
    ATOM_NET_CLIENT_LIST,
    ATOM_NET_FRAME_EXTENTS,
    ATOM_NET_NUMBER_OF_DESKTOPS,
    ATOM_NET_DESKTOP_NAMES,
    ATOM_NET_WM_PID,
    ATOM_NET_WM_WINDOW_TYPE_DOCK,
    ATOM_NET_WM_WINDOW_TYPE_UTILITY,
    ATOM_NET_WM_WINDOW_TYPE_DIALOG,
    ATOM_NET_WM_WINDOW_TYPE_TOOLBAR,
    ATOM_NET_WM_WINDOW_TYPE_SPLASH,
    ATOM_NET_WM_WINDOW_TYPE_POPUP_MENU,
    ATOM_NET_WM_WINDOW_TYPE_MENU,
    ATOM_NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
    ATOM_NET_WM_WINDOW_TYPE_TOOLTIP,
    ATOM_NET_WM_WINDOW_TYPE_NOTIFICATION,
    ATOM_NET_WM_STATE_MODAL,
    ATOM_WM_PROTOCOLS,
    ATOM_COUNT
} atom_type_t;

const char **build_argv(const char *cmd);
client_t *add_client(Window w, int ws);
void apply_fullscreen(client_t *c, Bool on);
void change_workspace(int ws);
int check_parent(pid_t p, pid_t c);
int clean_mask(int mask);
void close_focused(void);
client_t *find_client(Window w);
Window find_toplevel(Window w);
void focus_next(void);
void focus_prev(void);
pid_t get_parent_process(pid_t c);
pid_t get_pid(Window w);
int get_workspace_for_window(Window w);
void grab_button(Mask button, Mask mod, Window w, Bool owner_events,
                 Mask masks);
void grab_keys(void);
void hdl_button(XEvent *xev);
void hdl_button_release(XEvent *xev);
void hdl_client_msg(XEvent *xev);
void hdl_config_ntf(XEvent *xev);
void hdl_config_req(XEvent *xev);
void hdl_dummy(XEvent *xev);
void hdl_destroy_ntf(XEvent *xev);
void hdl_keypress(XEvent *xev);
void hdl_mapping_ntf(XEvent *xev);
void hdl_map_req(XEvent *xev);
void hdl_motion(XEvent *xev);
void hdl_property_ntf(XEvent *xev);
void hdl_unmap_ntf(XEvent *xev);
void init_defaults(void);
Bool is_child_proc(pid_t pid1, pid_t pid2);
void move_focused_next(void);
void move_focused_prev(void);
void move_to_workspace(int ws);
void move_win_down(void);
void move_win_left(void);
void move_win_right(void);
void move_win_up(void);
void other_wm(void);
int other_wm_err(Display *d, XErrorEvent *ee);
long parse_col(const char *hex);
void quit(void);
void resize_win_down(void);
void resize_win_left(void);
void resize_win_right(void);
void resize_win_up(void);
void run(void);
void scan_existing_windows(void);
void select_input(Window w, Mask masks);
void send_wm_take_focus(Window w);
void setup(void);
void setup_atoms(void);
void set_frame_extents(Window w);
void set_input_focus(client_t *c, Bool raise_win, Bool warp);
void set_wm_state(Window w, long state);
int snap_coordinate(int pos, int size, int screen_size, int snap_dist);
void spawn(const char *const *argv);
void swap_clients(client_t *a, client_t *b);
void tile(void);
void toggle_floating(void);
void toggle_floating_global(void);
void toggle_fullscreen(void);
void toggle_monocle(void);
void update_borders(void);
void update_client_desktop_properties(void);
void update_modifier_masks(void);
void update_net_client_list(void);
void update_struts(void);
void update_workarea(void);
void warp_cursor(client_t *c);
Bool window_has_ewmh_state(Window w, Atom state);
void window_set_ewmh_state(Window w, Atom state, Bool add);
int xerr(Display *d, XErrorEvent *ee);
void xev_case(XEvent *xev);
