#include <ctype.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xinerama.h>

#include "config.h"
#include "defs.h"

static Atom atoms[ATOM_COUNT];
static const char *atom_names[ATOM_COUNT] = {
    [ATOM_NET_ACTIVE_WINDOW] = "_NET_ACTIVE_WINDOW",
    [ATOM_NET_CURRENT_DESKTOP] = "_NET_CURRENT_DESKTOP",
    [ATOM_NET_SUPPORTED] = "_NET_SUPPORTED",
    [ATOM_NET_WM_STATE] = "_NET_WM_STATE",
    [ATOM_NET_WM_STATE_FULLSCREEN] = "_NET_WM_STATE_FULLSCREEN",
    [ATOM_WM_STATE] = "WM_STATE",
    [ATOM_NET_WM_WINDOW_TYPE] = "_NET_WM_WINDOW_TYPE",
    [ATOM_NET_WORKAREA] = "_NET_WORKAREA",
    [ATOM_WM_DELETE_WINDOW] = "WM_DELETE_WINDOW",
    [ATOM_NET_WM_STRUT] = "_NET_WM_STRUT",
    [ATOM_NET_WM_STRUT_PARTIAL] = "_NET_WM_STRUT_PARTIAL",
    [ATOM_NET_SUPPORTING_WM_CHECK] = "_NET_SUPPORTING_WM_CHECK",
    [ATOM_NET_WM_NAME] = "_NET_WM_NAME",
    [ATOM_UTF8_STRING] = "UTF8_STRING",
    [ATOM_NET_WM_DESKTOP] = "_NET_WM_DESKTOP",
    [ATOM_NET_CLIENT_LIST] = "_NET_CLIENT_LIST",
    [ATOM_NET_FRAME_EXTENTS] = "_NET_FRAME_EXTENTS",
    [ATOM_NET_NUMBER_OF_DESKTOPS] = "_NET_NUMBER_OF_DESKTOPS",
    [ATOM_NET_DESKTOP_NAMES] = "_NET_DESKTOP_NAMES",
    [ATOM_NET_WM_PID] = "_NET_WM_PID",
    [ATOM_NET_WM_WINDOW_TYPE_DOCK] = "_NET_WM_WINDOW_TYPE_DOCK",
    [ATOM_NET_WM_WINDOW_TYPE_UTILITY] = "_NET_WM_WINDOW_TYPE_UTILITY",
    [ATOM_NET_WM_WINDOW_TYPE_DIALOG] = "_NET_WM_WINDOW_TYPE_DIALOG",
    [ATOM_NET_WM_WINDOW_TYPE_TOOLBAR] = "_NET_WM_WINDOW_TYPE_TOOLBAR",
    [ATOM_NET_WM_WINDOW_TYPE_SPLASH] = "_NET_WM_WINDOW_TYPE_SPLASH",
    [ATOM_NET_WM_WINDOW_TYPE_POPUP_MENU] = "_NET_WM_WINDOW_TYPE_POPUP_MENU",
    [ATOM_NET_WM_WINDOW_TYPE_MENU] = "_NET_WM_WINDOW_TYPE_MENU",
    [ATOM_NET_WM_WINDOW_TYPE_DROPDOWN_MENU] =
        "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
    [ATOM_NET_WM_WINDOW_TYPE_TOOLTIP] = "_NET_WM_WINDOW_TYPE_TOOLTIP",
    [ATOM_NET_WM_WINDOW_TYPE_NOTIFICATION] = "_NET_WM_WINDOW_TYPE_NOTIFICATION",
    [ATOM_NET_WM_STATE_MODAL] = "_NET_WM_STATE_MODAL",
    [ATOM_WM_PROTOCOLS] = "WM_PROTOCOLS",
};

Cursor cursor_normal;
Cursor cursor_move;
Cursor cursor_resize;

client_t *workspaces[NUM_WORKSPACES] = {NULL};
config_t user_config;
bsp_node_t *bsp_roots[NUM_WORKSPACES];
DragMode drag_mode = DRAG_NONE;
client_t *drag_client = NULL;
client_t *swap_target = NULL;
client_t *focused = NULL;
client_t *ws_focused[NUM_WORKSPACES] = {NULL};
event_t evtable[LASTEvent];
Display *dpy;
Window root;
Window wm_check_win;
int current_ws = 0;
long last_motion_time = 0;
Bool global_floating = False;
Bool in_ws_switch = False;
Bool running = False;
Bool monocle = False;

Mask numlock_mask = 0;
Mask mode_switch_mask = 0;

int scr_width;
int scr_height;
int open_windows = 0;
int drag_start_x, drag_start_y;
int drag_orig_x, drag_orig_y, drag_orig_w, drag_orig_h;

int reserve_left = 0;
int reserve_right = 0;
int reserve_top = 0;
int reserve_bottom = 0;

static char **split_cmd(const char *cmd, int *out_argc) {
    enum { NORMAL, IN_QUOTE } state = NORMAL;
    size_t cap = 8, argc = 0, toklen = 0;
    char *token = malloc(strlen(cmd) + 1);
    char **argv = malloc(cap * sizeof *argv);

    if (!token || !argv)
        goto err;

    for (const char *p = cmd; *p; p++) {
        if (state == NORMAL && isspace((unsigned char)*p)) {
            if (toklen) {
                token[toklen] = '\0';
                if (argc + 1 >= cap) {
                    cap *= 2;
                    char **tmp = realloc(argv, cap * sizeof *argv);
                    if (!tmp)
                        goto err;

                    argv = tmp;
                }
                argv[argc++] = strdup(token);
                toklen = 0;
            }
        } else if (*p == '"')
            state = (state == NORMAL) ? IN_QUOTE : NORMAL;
        else if (*p == '\'')
            state = (state == NORMAL) ? IN_QUOTE : NORMAL;
        else
            token[toklen++] = *p;
    }

    if (toklen) {
        token[toklen] = '\0';
        argv[argc++] = strdup(token);
    }
    argv[argc] = NULL;
    *out_argc = argc;
    free(token);
    return argv;

err:
    free(token);
    if (argv) {
        for (size_t i = 0; i < argc; i++)
            free(argv[i]);
        free(argv);
    }
    return NULL;
}

const char **build_argv(const char *cmd) {
    int argc = 0;
    char **tmp = split_cmd(cmd, &argc);
    if (!tmp)
        return NULL;

    return (const char **)tmp;
}

static void load_config(void) {
    user_config.modkey = MODKEY;
    user_config.gaps = CFG_GAPS;
    user_config.border_width = CFG_BORDER_WIDTH;
    user_config.move_window_amt = CFG_MOVE_WINDOW_AMT;
    user_config.resize_window_amt = CFG_RESIZE_WINDOW_AMT;
    user_config.snap_distance = CFG_SNAP_DISTANCE;
    user_config.motion_throttle = CFG_MOTION_THROTTLE;
    user_config.new_win_focus = CFG_NEW_WIN_FOCUS;
    user_config.warp_cursor = CFG_WARP_CURSOR;
    user_config.floating_on_top = CFG_FLOATING_ON_TOP;

    user_config.border_foc_col = parse_col(CFG_FOCUSED_BORDER_COL);
    user_config.border_ufoc_col = parse_col(CFG_UNFOCUSED_BORDER_COL);
    user_config.border_swap_col = parse_col(CFG_SWAP_BORDER_COL);

    binding_t binds[] = {CFG_BINDS};
    user_config.n_binds = (int)(sizeof(binds) / sizeof(binds[0]));
    for (int i = 0; i < user_config.n_binds; i++)
        user_config.binds[i] = binds[i];
}

client_t *add_client(Window w, int ws) {
    client_t *c = malloc(sizeof(client_t));
    if (!c) {
        fprintf(stderr, "tilite: could not alloc memory for client\n");
        return NULL;
    }

    c->win = w;
    c->next = NULL;
    c->ws = ws;
    c->pid = get_pid(w);

    if (!workspaces[ws]) {
        workspaces[ws] = c;
    } else if (focused && focused->ws == ws) {
        /* Insert after focused so the new window splits the focused slot */
        c->next = focused->next;
        focused->next = c;
    } else {
        /* Fallback: append to tail */
        client_t *tail = workspaces[ws];
        while (tail->next)
            tail = tail->next;
        tail->next = c;
    }
    open_windows++;

    /* subscribing to certain events */
    Mask window_masks = EnterWindowMask | LeaveWindowMask | FocusChangeMask |
                        PropertyChangeMask | StructureNotifyMask |
                        ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    select_input(w, window_masks);
    grab_button(Button1, None, w, False, ButtonPressMask);
    grab_button(Button1, user_config.modkey, w, False, ButtonPressMask);
    grab_button(Button1, user_config.modkey | ShiftMask, w, False,
                ButtonPressMask);
    grab_button(Button3, user_config.modkey, w, False, ButtonPressMask);

    /* allow for more graceful exitting */
    Atom protos[] = {atoms[ATOM_WM_DELETE_WINDOW]};
    XSetWMProtocols(dpy, w, protos, 1);

    XWindowAttributes wa;
    XGetWindowAttributes(dpy, w, &wa);
    c->x = wa.x;
    c->y = wa.y;
    c->w = wa.width;
    c->h = wa.height;

    /* set client defaults */
    c->fixed = False;
    c->floating = False;
    c->fullscreen = False;
    c->mapped = True;

    if (global_floating)
        c->floating = True;

    /* Update BSP tree: split the focused client's region for the new client.
     * Do this after floating flag is set so we don't add floating wins to BSP.
     */
    if (!c->floating && !c->fullscreen) {
        client_t *split_target =
            (focused && focused->ws == ws) ? focused : NULL;
        bsp_insert(&bsp_roots[ws], split_target, c);
    }

    /* remember first created client per workspace as a fallback */
    if (!ws_focused[ws])
        ws_focused[ws] = c;

    if (ws == current_ws && !focused) {
        focused = c;
    }

    /* associate client with workspace n */
    long desktop = ws;
    XChangeProperty(dpy, w, atoms[ATOM_NET_WM_DESKTOP], XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&desktop, 1);
    XRaiseWindow(dpy, w);
    return c;
}

void apply_fullscreen(client_t *c, Bool on) {
    if (!c || !c->mapped || c->fullscreen == on)
        return;

    if (on) {
        XWindowAttributes win_attr;
        if (!XGetWindowAttributes(dpy, c->win, &win_attr))
            return;

        c->orig_x = win_attr.x;
        c->orig_y = win_attr.y;
        c->orig_w = win_attr.width;
        c->orig_h = win_attr.height;

        c->fullscreen = True;

        XSetWindowBorderWidth(dpy, c->win, 0);
        XMoveResizeWindow(dpy, c->win, 0, 0, scr_width, scr_height);

        c->x = 0;
        c->y = 0;
        c->w = scr_width;
        c->h = scr_height;

        XRaiseWindow(dpy, c->win);
        window_set_ewmh_state(c->win, atoms[ATOM_NET_WM_STATE_FULLSCREEN],
                              True);
    } else {
        c->fullscreen = False;

        /* restore win attributes */
        XMoveResizeWindow(dpy, c->win, c->orig_x, c->orig_y, c->orig_w,
                          c->orig_h);
        XSetWindowBorderWidth(dpy, c->win, user_config.border_width);
        window_set_ewmh_state(c->win, atoms[ATOM_NET_WM_STATE_FULLSCREEN],
                              False);

        c->x = c->orig_x;
        c->y = c->orig_y;
        c->w = c->orig_w;
        c->h = c->orig_h;

        tile();
        update_borders();
    }
}

void change_workspace(int ws) {
    if (ws >= NUM_WORKSPACES || ws == current_ws)
        return;

    ws_focused[current_ws] = focused;

    in_ws_switch = True;
    XGrabServer(dpy);

    for (client_t *c = workspaces[current_ws]; c; c = c->next) {
        if (c->mapped) {
            XUnmapWindow(dpy, c->win);
        }
    }

    current_ws = ws;
    for (client_t *c = workspaces[current_ws]; c; c = c->next) {
        if (c->mapped) {
            XMapWindow(dpy, c->win);
        }
    }

    tile();

    focused = ws_focused[current_ws];

    if (focused) {
        client_t *found = NULL;
        for (client_t *c = workspaces[current_ws]; c; c = c->next) {
            if (c == focused && c->mapped) {
                found = c;
                break;
            }
        }
        if (!found)
            focused = NULL;
    }

    if (!focused && workspaces[current_ws]) {
        for (client_t *c = workspaces[current_ws]; c; c = c->next) {
            if (!c->mapped)
                continue;
            if (!focused)
                focused = c;
        }
    }

    set_input_focus(focused, False, True);

    long current_desktop = current_ws;
    XChangeProperty(dpy, root, atoms[ATOM_NET_CURRENT_DESKTOP], XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&current_desktop, 1);
    update_client_desktop_properties();

    XUngrabServer(dpy);
    XSync(dpy, False);
    in_ws_switch = False;
}

int check_parent(pid_t p, pid_t c) {
    while (p != c && c != 0) /* walk proc tree until parent found */
        c = get_parent_process(c);

    return (int)c;
}

int clean_mask(int mask) {
    return mask & ~(LockMask | numlock_mask | mode_switch_mask);
}

void close_focused(void) {
    if (!focused)
        return;

    Atom *protocols;
    int n_protocols;
    /* get number of protocols a window possesses and check if any ==
     * WM_DELETE_WINDOW (supports it) */
    if (XGetWMProtocols(dpy, focused->win, &protocols, &n_protocols) &&
        protocols) {
        for (int i = 0; i < n_protocols; i++) {
            if (protocols[i] == atoms[ATOM_WM_DELETE_WINDOW]) {
                XEvent ev = {
                    .xclient = {.type = ClientMessage,
                                .window = focused->win,
                                .message_type = atoms[ATOM_WM_PROTOCOLS],
                                .format = 32}};

                ev.xclient.data.l[0] = atoms[ATOM_WM_DELETE_WINDOW];
                ev.xclient.data.l[1] = CurrentTime;
                XSendEvent(dpy, focused->win, False, NoEventMask, &ev);
                XFree(protocols);
                return;
            }
        }
        XUnmapWindow(dpy, focused->win);
        XFree(protocols);
    }
    XUnmapWindow(dpy, focused->win);
    XKillClient(dpy, focused->win);
}

client_t *find_client(Window w) {
    for (int ws = 0; ws < NUM_WORKSPACES; ws++)
        for (client_t *c = workspaces[ws]; c; c = c->next)
            if (c->win == w)
                return c;

    return NULL;
}

Window find_toplevel(Window w) {
    if (!w || w == None)
        return root;

    Window root_win = None;
    Window parent;
    Window *kids;
    unsigned n_kids;

    while (True) {
        if (w == root_win)
            break;
        if (XQueryTree(dpy, w, &root_win, &parent, &kids, &n_kids) == 0)
            break;
        if (kids)
            XFree(kids);
        if (parent == root_win || parent == None)
            break;
        w = parent;
    }
    return w;
}

void focus_next(void) {
    if (!workspaces[current_ws])
        return;

    client_t *start = focused ? focused : workspaces[current_ws];
    client_t *c = start;

    /* loop until we find a mapped client or return to start */
    do
        c = c->next ? c->next : workspaces[current_ws];
    while (!c->mapped && c != start);

    /* if we return to start: */
    if (!c->mapped)
        return;

    focused = c;
    set_input_focus(focused, True, True);
}

void focus_prev(void) {
    if (!workspaces[current_ws])
        return;

    client_t *start = focused ? focused : workspaces[current_ws];
    client_t *c = start;

    /* loop until we find a mapped client or return to starting point */
    do {
        client_t *p = workspaces[current_ws];
        client_t *prev = NULL;
        while (p && p != c) {
            prev = p;
            p = p->next;
        }

        if (prev) {
            c = prev;
        } else {
            /* wrap to tail */
            p = workspaces[current_ws];
            while (p->next)
                p = p->next;
            c = p;
        }
    } while (!c->mapped && c != start);

    /* this stops invisible windows being detected or focused */
    if (!c->mapped)
        return;

    focused = c;
    set_input_focus(focused, True, True);
}

pid_t get_parent_process(pid_t c) {
    pid_t v = -1;
    FILE *f;
    char buf[256];

    snprintf(buf, sizeof(buf), "/proc/%u/stat", (unsigned)c);
    if (!(f = fopen(buf, "r")))
        return 0;

    int no_error = fscanf(f, "%*u %*s %*c %d", &v);
    (void)no_error;
    fclose(f);
    return (pid_t)v;
}

pid_t get_pid(Window w) {
    pid_t pid = 0;
    Atom actual_type;
    int actual_format;
    unsigned long n_items, bytes_after;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(dpy, w, atoms[ATOM_NET_WM_PID], 0, 1, False,
                           XA_CARDINAL, &actual_type, &actual_format, &n_items,
                           &bytes_after, &prop) == Success &&
        prop) {
        if (actual_format == 32 && n_items == 1)
            pid = *(pid_t *)prop;
        XFree(prop);
    }
    return pid;
}

int get_workspace_for_window(Window w) {
    XClassHint ch = {0};
    if (!XGetClassHint(dpy, w, &ch))
        return current_ws;

    XFree(ch.res_class);
    XFree(ch.res_name);

    return current_ws; /* default */
}

void grab_button(Mask button, Mask mod, Window w, Bool owner_events,
                 Mask masks) {
    if (w == root) /* grabbing for wm */
        XGrabButton(dpy, button, mod, w, owner_events, masks, GrabModeAsync,
                    GrabModeAsync, None, None);
    else /* grabbing for windows */
        XGrabButton(dpy, button, mod, w, owner_events, masks, GrabModeSync,
                    GrabModeAsync, None, None);
}

void grab_keys(void) {
    Mask guards[] = {0,
                     LockMask,
                     numlock_mask,
                     LockMask | numlock_mask,
                     mode_switch_mask,
                     LockMask | mode_switch_mask,
                     numlock_mask | mode_switch_mask,
                     LockMask | numlock_mask | mode_switch_mask};
    XUngrabKey(dpy, AnyKey, AnyModifier, root);

    for (int i = 0; i < user_config.n_binds; i++) {
        binding_t *bind = &user_config.binds[i];

        if ((bind->type == TYPE_WS_CHANGE &&
             bind->mods != user_config.modkey) ||
            (bind->type == TYPE_WS_MOVE &&
             bind->mods != (user_config.modkey | ShiftMask))) {
            continue;
        }

        bind->keycode = XKeysymToKeycode(dpy, bind->keysym);
        if (!bind->keycode)
            continue;

        for (size_t guard = 0; guard < sizeof(guards) / sizeof(guards[0]);
             guard++) {
            XGrabKey(dpy, bind->keycode, bind->mods | guards[guard], root, True,
                     GrabModeAsync, GrabModeAsync);
        }
    }
}

void hdl_button(XEvent *xev) {
    XButtonEvent *xbutton = &xev->xbutton;
    Window w =
        (xbutton->subwindow != None) ? xbutton->subwindow : xbutton->window;
    w = find_toplevel(w);

    Mask left_click = Button1;
    Mask right_click = Button3;

    XAllowEvents(dpy, ReplayPointer, xbutton->time);
    if (!w)
        return;

    client_t *head = workspaces[current_ws];
    for (client_t *c = head; c; c = c->next) {
        if (c->win != w)
            continue;

        Bool is_swap_mode = (xbutton->state & user_config.modkey) &&
                            (xbutton->state & ShiftMask) &&
                            xbutton->button == left_click && !c->floating;
        if (is_swap_mode) {
            drag_client = c;
            drag_start_x = xbutton->x_root;
            drag_start_y = xbutton->y_root;
            drag_orig_x = c->x;
            drag_orig_y = c->y;
            drag_orig_w = c->w;
            drag_orig_h = c->h;
            drag_mode = DRAG_SWAP;
            XGrabPointer(dpy, root, True, ButtonReleaseMask | PointerMotionMask,
                         GrabModeAsync, GrabModeAsync, None, cursor_move,
                         CurrentTime);
            focused = c;
            set_input_focus(focused, False, False);
            XSetWindowBorder(dpy, c->win, user_config.border_swap_col);
            return;
        }

        Bool is_move_resize =
            (xbutton->state & user_config.modkey) &&
            (xbutton->button == left_click || xbutton->button == right_click) &&
            !c->floating;
        if (is_move_resize) {
            focused = c;
            toggle_floating();
        }

        Bool is_single_click = !(xbutton->state & user_config.modkey) &&
                               xbutton->button == left_click;
        if (is_single_click) {
            focused = c;
            set_input_focus(focused, True, False);
            return;
        }

        if (!c->floating)
            return;

        if (c->fixed && xbutton->button == right_click)
            return;

        Cursor cursor =
            (xbutton->button == left_click) ? cursor_move : cursor_resize;
        XGrabPointer(dpy, root, True, ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime);

        drag_client = c;
        drag_start_x = xbutton->x_root;
        drag_start_y = xbutton->y_root;
        drag_orig_x = c->x;
        drag_orig_y = c->y;
        drag_orig_w = c->w;
        drag_orig_h = c->h;
        drag_mode = (xbutton->button == left_click) ? DRAG_MOVE : DRAG_RESIZE;
        focused = c;

        set_input_focus(focused, True, False);
        return;
    }
}

void hdl_button_release(XEvent *xev) {
    (void)xev;

    if (drag_mode == DRAG_SWAP) {
        if (swap_target) {
            XSetWindowBorder(dpy, swap_target->win,
                             (swap_target == focused
                                  ? user_config.border_foc_col
                                  : user_config.border_ufoc_col));
            swap_clients(drag_client, swap_target);
        }
        tile();
        update_borders();
    }

    XUngrabPointer(dpy, CurrentTime);

    drag_mode = DRAG_NONE;
    drag_client = NULL;
    swap_target = NULL;
}

void hdl_client_msg(XEvent *xev) {
    if (xev->xclient.message_type == atoms[ATOM_NET_CURRENT_DESKTOP]) {
        int ws = (int)xev->xclient.data.l[0];
        change_workspace(ws);
        return;
    }

    if (xev->xclient.message_type == atoms[ATOM_NET_WM_STATE]) {
        XClientMessageEvent *client_msg_ev = &xev->xclient;
        Window w = client_msg_ev->window;
        client_t *c = find_client(find_toplevel(w));
        if (!c)
            return;

        /* 0=remove, 1=add, 2=toggle */
        long action = client_msg_ev->data.l[0];
        Atom a1 = (Atom)client_msg_ev->data.l[1];
        Atom a2 = (Atom)client_msg_ev->data.l[2];

        Atom state_atoms[2] = {a1, a2};
        for (int i = 0; i < 2; i++) {
            if (state_atoms[i] == None)
                continue;

            if (state_atoms[i] == atoms[ATOM_NET_WM_STATE_FULLSCREEN]) {
                Bool want = c->fullscreen;
                if (action == 0)
                    want = False;
                else if (action == 1)
                    want = True;
                else if (action == 2)
                    want = !want;

                apply_fullscreen(c, want);

                if (want)
                    XRaiseWindow(dpy, c->win);
            }
        }
        return;
    }
}

void hdl_config_ntf(XEvent *xev) {
    if (xev->xconfigure.window == root) {
        tile();
        update_borders();
    }
}

void hdl_config_req(XEvent *xev) {
    XConfigureRequestEvent *config_ev = &xev->xconfigurerequest;
    client_t *c = NULL;

    for (int i = 0; i < NUM_WORKSPACES && !c; i++)
        for (c = workspaces[i]; c; c = c->next)
            if (c->win == config_ev->window)
                break;

    if (!c || c->floating || c->fullscreen) {
        /* allow client to configure itself */
        XWindowChanges wc = {.x = config_ev->x,
                             .y = config_ev->y,
                             .width = config_ev->width,
                             .height = config_ev->height,
                             .border_width = config_ev->border_width,
                             .sibling = config_ev->above,
                             .stack_mode = config_ev->detail};
        XConfigureWindow(dpy, config_ev->window, config_ev->value_mask, &wc);
        return;
    }
}

void hdl_dummy(XEvent *xev) { (void)xev; }

void hdl_destroy_ntf(XEvent *xev) {
    Window w = xev->xdestroywindow.window;

    for (int i = 0; i < NUM_WORKSPACES; i++) {
        client_t *prev = NULL;
        client_t *c = workspaces[i];

        while (c && c->win != w) {
            prev = c;
            c = c->next;
        }

        if (!c)
            continue;

        for (int ws = 0; ws < NUM_WORKSPACES; ws++)
            if (ws_focused[ws] == c)
                ws_focused[ws] = NULL;

        if (focused == c)
            focused = NULL;

        /* unlink from workspace list */
        if (!prev)
            workspaces[i] = c->next;
        else
            prev->next = c->next;

        /* remove from BSP tree — only if still mapped (unmap handler already
         * removes it for the normal close path) */
        if (c->mapped && !c->floating && !c->fullscreen)
            bsp_remove(&bsp_roots[i], c);

        free(c);
        update_net_client_list();
        open_windows--;

        if (i == current_ws) {
            tile();
            update_borders();

            /* prefer previous window else next */
            client_t *foc_new = NULL;
            if (prev && prev->mapped)
                foc_new = prev;
            else {
                for (client_t *p = workspaces[i]; p; p = p->next) {
                    if (!p->mapped)
                        continue;
                    foc_new = p;
                    break;
                }
            }

            if (foc_new)
                set_input_focus(foc_new, True, True);
            else
                set_input_focus(NULL, False, False);
        }

        return;
    }
}

void hdl_keypress(XEvent *xev) {
    KeyCode code = xev->xkey.keycode;
    int mods = clean_mask(xev->xkey.state);

    for (int i = 0; i < user_config.n_binds; i++) {
        binding_t *bind = &user_config.binds[i];
        if (bind->keycode == code && clean_mask(bind->mods) == mods) {
            switch (bind->type) {
            case TYPE_CMD:
                spawn(bind->action.cmd);
                break;
            case TYPE_FUNC:
                if (bind->action.fn)
                    bind->action.fn();
                break;
            case TYPE_WS_CHANGE:
                change_workspace(bind->action.ws);
                update_net_client_list();
                break;
            case TYPE_WS_MOVE:
                move_to_workspace(bind->action.ws);
                update_net_client_list();
                break;
            }
            return;
        }
    }
}

void hdl_mapping_ntf(XEvent *xev) {
    XRefreshKeyboardMapping(&xev->xmapping);
    update_modifier_masks();
    grab_keys();
}

void hdl_map_req(XEvent *xev) {
    Window w = xev->xmaprequest.window;
    XWindowAttributes win_attr;

    if (!XGetWindowAttributes(dpy, w, &win_attr))
        return;

    /* skips invisible windows */
    if (win_attr.override_redirect || win_attr.width <= 0 ||
        win_attr.height <= 0) {
        XMapWindow(dpy, w);
        return;
    }

    /* check if this window is already managed on any workspace */
    client_t *c = find_client(w);
    if (c) {
        if (c->ws == current_ws) {
            if (!c->mapped) {
                XMapWindow(dpy, w);
                c->mapped = True;
                /* Re-insert into BSP tree beside the focused client */
                if (!c->floating && !c->fullscreen) {
                    client_t *split_target =
                        (focused && focused != c) ? focused : NULL;
                    bsp_insert(&bsp_roots[current_ws], split_target, c);
                }
            }
            if (user_config.new_win_focus) {
                focused = c;
                set_input_focus(c, True, True);
                return; /* set_input_focus already calls update_borders */
            }
            update_borders();
        }
        return;
    }

    Atom type;
    int format;
    unsigned long n_items, after;
    Atom *types = NULL;
    Bool should_float = False;

    if (XGetWindowProperty(dpy, w, atoms[ATOM_NET_WM_WINDOW_TYPE], 0, 4, False,
                           XA_ATOM, &type, &format, &n_items, &after,
                           (unsigned char **)&types) == Success &&
        types) {

        for (unsigned long i = 0; i < n_items; i++) {
            if (types[i] == atoms[ATOM_NET_WM_WINDOW_TYPE_DOCK]) {
                XFree(types);
                XMapWindow(dpy, w);
                return;
            }

            if (types[i] == atoms[ATOM_NET_WM_WINDOW_TYPE_UTILITY] ||
                types[i] == atoms[ATOM_NET_WM_WINDOW_TYPE_DIALOG] ||
                types[i] == atoms[ATOM_NET_WM_WINDOW_TYPE_TOOLBAR] ||
                types[i] == atoms[ATOM_NET_WM_WINDOW_TYPE_SPLASH] ||
                types[i] == atoms[ATOM_NET_WM_WINDOW_TYPE_POPUP_MENU] ||
                types[i] == atoms[ATOM_NET_WM_WINDOW_TYPE_DROPDOWN_MENU] ||
                types[i] == atoms[ATOM_NET_WM_WINDOW_TYPE_MENU] ||
                types[i] == atoms[ATOM_NET_WM_WINDOW_TYPE_TOOLTIP] ||
                types[i] == atoms[ATOM_NET_WM_WINDOW_TYPE_NOTIFICATION]) {
                should_float = True;
                break;
            }
        }
        XFree(types);
    }

    if (open_windows == MAX_CLIENTS) {
        fprintf(stderr, "tilite: max clients reached, ignoring map request\n");
        return;
    }

    int target_ws = get_workspace_for_window(w);
    c = add_client(w, target_ws);
    if (!c)
        return;
    set_wm_state(w, NormalState);

    Window transient;
    if (!should_float && XGetTransientForHint(dpy, w, &transient))
        should_float = True;

    XSizeHints size_hints;
    long supplied_ret;

    if (!should_float &&
        XGetWMNormalHints(dpy, w, &size_hints, &supplied_ret) &&
        (size_hints.flags & PMinSize) && (size_hints.flags & PMaxSize) &&
        size_hints.min_width == size_hints.max_width &&
        size_hints.min_height == size_hints.max_height) {

        should_float = True;
        c->fixed = True;
    }

    if (should_float || global_floating)
        c->floating = True;

    /* center floating windows & set border */
    if (c->floating && !c->fullscreen) {
        int w_ = MAX(c->w, 64), h_ = MAX(c->h, 64);
        int x = (scr_width - w_) / 2, y = (scr_height - h_) / 2;
        c->x = x;
        c->y = y;
        c->w = w_;
        c->h = h_;
        XMoveResizeWindow(dpy, w, x, y, w_, h_);
        XSetWindowBorderWidth(dpy, w, user_config.border_width);
    }

    update_net_client_list();
    if (target_ws != current_ws)
        return;

    /* map & borders */
    if (!global_floating && !c->floating)
        tile();
    else if (c->floating)
        XRaiseWindow(dpy, w);

    if (window_has_ewmh_state(w, atoms[ATOM_NET_WM_STATE_FULLSCREEN])) {
        c->fullscreen = True;
        c->floating = False;
    }

    XMapWindow(dpy, w);
    c->mapped = True;
    if (c->fullscreen)
        apply_fullscreen(c, True);
    set_frame_extents(w);

    if (user_config.new_win_focus) {
        focused = c;
        set_input_focus(focused, True, True);
        return;
    }
    update_borders();
}

void hdl_motion(XEvent *xev) {
    XMotionEvent *motion_ev = &xev->xmotion;

    if ((drag_mode == DRAG_NONE || !drag_client) ||
        (motion_ev->time - last_motion_time <=
         (1000 / (Time)user_config.motion_throttle)))
        return;
    last_motion_time = motion_ev->time;

    if (drag_mode == DRAG_SWAP) {
        Window root_ret, child;
        int rx, ry, wx, wy;
        unsigned int mask;
        XQueryPointer(dpy, root, &root_ret, &child, &rx, &ry, &wx, &wy, &mask);

        client_t *new_target = NULL;

        for (client_t *c = workspaces[current_ws]; c; c = c->next) {
            if (c == drag_client || c->floating)
                continue;
            if (c->win == child) {
                new_target = c;
                break;
            }
            Window root_ret2, parent;
            Window *children;
            unsigned int n_children;
            if (XQueryTree(dpy, child, &root_ret2, &parent, &children,
                           &n_children)) {
                if (children)
                    XFree(children);
                if (parent == c->win) {
                    new_target = c;
                    break;
                }
            }
        }

        if (new_target != swap_target) {
            if (swap_target) {
                XSetWindowBorder(dpy, swap_target->win,
                                 (swap_target == focused
                                      ? user_config.border_foc_col
                                      : user_config.border_ufoc_col));
            }
            if (new_target)
                XSetWindowBorder(dpy, new_target->win,
                                 user_config.border_swap_col);
        }

        swap_target = new_target;
        return;
    } else if (drag_mode == DRAG_MOVE) {
        int dx = motion_ev->x_root - drag_start_x;
        int dy = motion_ev->y_root - drag_start_y;
        int nx = drag_orig_x + dx;
        int ny = drag_orig_y + dy;

        int outer_w = drag_client->w + 2 * user_config.border_width;
        int outer_h = drag_client->h + 2 * user_config.border_width;

        int rel_x = nx;
        int rel_y = ny;

        rel_x = snap_coordinate(rel_x, outer_w, scr_width,
                                user_config.snap_distance);
        rel_y = snap_coordinate(rel_y, outer_h, scr_height,
                                user_config.snap_distance);

        nx = rel_x;
        ny = rel_y;

        if (!drag_client->floating &&
            (UDIST(nx, drag_client->x) > user_config.snap_distance ||
             UDIST(ny, drag_client->y) > user_config.snap_distance)) {
            toggle_floating();
        }

        XMoveWindow(dpy, drag_client->win, nx, ny);
        drag_client->x = nx;
        drag_client->y = ny;
    } else if (drag_mode == DRAG_RESIZE) {
        int dx = motion_ev->x_root - drag_start_x;
        int dy = motion_ev->y_root - drag_start_y;
        int nw = drag_orig_w + dx;
        int nh = drag_orig_h + dy;

        int max_w = (scr_width - drag_client->x);
        int max_h = (scr_height - drag_client->y);

        drag_client->w = CLAMP(nw, MIN_WINDOW_SIZE, max_w);
        drag_client->h = CLAMP(nh, MIN_WINDOW_SIZE, max_h);

        XResizeWindow(dpy, drag_client->win, drag_client->w, drag_client->h);
    }
}

void hdl_property_ntf(XEvent *xev) {
    XPropertyEvent *property_ev = &xev->xproperty;

    if (property_ev->window == root) {
        if (property_ev->atom == atoms[ATOM_NET_CURRENT_DESKTOP]) {
            long *val = NULL;
            Atom actual;
            int fmt;
            unsigned long n;
            unsigned long after;
            if (XGetWindowProperty(dpy, root, atoms[ATOM_NET_CURRENT_DESKTOP],
                                   0, 1, False, XA_CARDINAL, &actual, &fmt, &n,
                                   &after, (unsigned char **)&val) == Success &&
                val) {
                change_workspace((int)val[0]);
                XFree(val);
            }
        } else if (property_ev->atom == atoms[ATOM_NET_WM_STRUT_PARTIAL]) {
            update_struts();
            tile();
            update_borders();
        }
    }

    /* client window properties */
    if (property_ev->atom == atoms[ATOM_NET_WM_STATE]) {
        client_t *c = find_client(find_toplevel(property_ev->window));
        if (!c)
            return;

        Bool want =
            window_has_ewmh_state(c->win, atoms[ATOM_NET_WM_STATE_FULLSCREEN]);
        if (want != c->fullscreen)
            apply_fullscreen(c, want);
    }
}

void hdl_unmap_ntf(XEvent *xev) {
    if (!in_ws_switch) {
        Window w = xev->xunmap.window;
        for (client_t *c = workspaces[current_ws]; c; c = c->next) {
            if (c->win == w && c->mapped) {
                c->mapped = False;
                /* Remove from BSP so tile() doesn't see a stale leaf */
                if (!c->floating && !c->fullscreen)
                    bsp_remove(&bsp_roots[current_ws], c);
                break;
            }
        }
    }

    update_net_client_list();
    tile();
    update_borders();
}

void inc_gaps(void) {
    user_config.gaps++;
    tile();
    update_borders();
}

void init_defaults(void) {
    user_config.modkey = Mod4Mask;
    user_config.gaps = 10;
    user_config.border_width = 1;
    user_config.border_foc_col = parse_col("#c0cbff");
    user_config.border_ufoc_col = parse_col("#555555");
    user_config.border_swap_col = parse_col("#fff4c0");
    user_config.move_window_amt = 10;
    user_config.resize_window_amt = 10;

    user_config.motion_throttle = 60;
    user_config.snap_distance = 5;
    user_config.n_binds = 0;
    user_config.new_win_focus = True;
    user_config.warp_cursor = True;
    user_config.floating_on_top = True;
}

Bool is_child_proc(pid_t parent_pid, pid_t child_pid) {
    if (parent_pid <= 0 || child_pid <= 0)
        return False;

    char path[PATH_MAX];
    FILE *f;
    pid_t current_pid = child_pid;
    int max_iterations = 20;

    while (current_pid > 1 && max_iterations-- > 0) {
        snprintf(path, sizeof(path), "/proc/%d/stat", current_pid);
        f = fopen(path, "r");
        if (!f) {
            fprintf(stderr, "tilite: could not open %s\n", path);
            return False;
        }

        int ppid = 0;
        if (fscanf(f, "%*d %*s %*c %d", &ppid) != 1) {
            fprintf(stderr, "tilite: failed to read ppid from %s\n", path);
            fclose(f);
            return False;
        }
        fclose(f);

        if (ppid == parent_pid)
            return True;

        if (ppid <= 1) {
            fprintf(stderr,
                    "tilite: reached init/kernel, no relationship found\n");
            break;
        }
        current_pid = ppid;
    }
    return False;
}

void move_focused_next(void) {
    if (!focused || !workspaces[current_ws])
        return;

    client_t *prev = NULL;
    client_t *c = workspaces[current_ws];
    while (c && c != focused) {
        prev = c;
        c = c->next;
    }
    if (!c || !c->next)
        return;

    client_t *next = c->next;

    c->next = next->next;
    next->next = c;
    if (prev)
        prev->next = next;
    else
        workspaces[current_ws] = next;

    tile();
    if (user_config.warp_cursor)
        warp_cursor(focused);
    send_wm_take_focus(focused->win);
    update_borders();
}

void move_focused_prev(void) {
    if (!focused || !workspaces[current_ws])
        return;

    client_t *prev = NULL;
    client_t *c = workspaces[current_ws];
    while (c && c != focused) {
        prev = c;
        c = c->next;
    }
    if (!c || !prev)
        return;

    client_t *prev_prev = NULL;
    client_t *p = workspaces[current_ws];
    while (p && p != prev) {
        prev_prev = p;
        p = p->next;
    }

    prev->next = c->next;
    c->next = prev;
    if (prev_prev)
        prev_prev->next = c;
    else
        workspaces[current_ws] = c;

    tile();
    if (user_config.warp_cursor)
        warp_cursor(focused);
    send_wm_take_focus(focused->win);
    update_borders();
}

void move_to_workspace(int ws) {
    if (!focused || ws >= NUM_WORKSPACES || ws == current_ws)
        return;

    client_t *moved = focused;
    int from_ws = current_ws;

    XUnmapWindow(dpy, moved->win);

    /* remove from current list */
    client_t **pp = &workspaces[from_ws];
    while (*pp && *pp != moved)
        pp = &(*pp)->next;

    if (*pp)
        *pp = moved->next;

    /* update BSP trees */
    bsp_remove(&bsp_roots[from_ws], moved);
    if (!moved->floating && !moved->fullscreen)
        bsp_insert(&bsp_roots[ws], NULL, moved);

    /* push to target list */
    moved->next = workspaces[ws];
    workspaces[ws] = moved;
    moved->ws = ws;
    long desktop = ws;
    XChangeProperty(dpy, moved->win, atoms[ATOM_NET_WM_DESKTOP], XA_CARDINAL,
                    32, PropModeReplace, (unsigned char *)&desktop, 1);

    /* remember it as last-focused for the target workspace */
    ws_focused[ws] = moved;

    /* retile current workspace and pick a new focus there */
    tile();
    focused = workspaces[from_ws];
    if (focused)
        set_input_focus(focused, False, False);
    else
        set_input_focus(NULL, False, False);
}

void move_win_down(void) {
    if (!focused || !focused->floating)
        return;

    focused->y += user_config.move_window_amt;
    XMoveWindow(dpy, focused->win, focused->x, focused->y);
}

void move_win_left(void) {
    if (!focused || !focused->floating)
        return;

    focused->x -= user_config.move_window_amt;
    XMoveWindow(dpy, focused->win, focused->x, focused->y);
}

void move_win_right(void) {
    if (!focused || !focused->floating)
        return;
    focused->x += user_config.move_window_amt;
    XMoveWindow(dpy, focused->win, focused->x, focused->y);
}

void move_win_up(void) {
    if (!focused || !focused->floating)
        return;

    focused->y -= user_config.move_window_amt;
    XMoveWindow(dpy, focused->win, focused->x, focused->y);
}

void other_wm(void) {
    XSetErrorHandler(other_wm_err);
    XChangeWindowAttributes(
        dpy, root, CWEventMask,
        &(XSetWindowAttributes){.event_mask = SubstructureRedirectMask});
    XSync(dpy, False);
    XSetErrorHandler(xerr);
    XChangeWindowAttributes(dpy, root, CWEventMask,
                            &(XSetWindowAttributes){.event_mask = 0});
    XSync(dpy, False);
}

int other_wm_err(Display *d, XErrorEvent *ee) {
    fprintf(stderr,
            "can't start because another window manager is already running");
    exit(EXIT_FAILURE);
    return 0;
    (void)d;
    (void)ee;
}

long parse_col(const char *hex) {
    XColor col;
    Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));

    if (!XParseColor(dpy, cmap, hex, &col)) {
        fprintf(stderr, "tilite: cannot parse color %s\n", hex);
        return WhitePixel(dpy, DefaultScreen(dpy));
    }

    if (!XAllocColor(dpy, cmap, &col)) {
        fprintf(stderr, "tilite: cannot allocate color %s\n", hex);
        return WhitePixel(dpy, DefaultScreen(dpy));
    }

    /* possibly unsafe BUT i dont think it can cause any problems.
     * used to make sure borders are opaque with compositor like picom */
    return ((long)col.pixel) | (0xffL << 24);
}

void quit(void) {
    /* Kill all clients on exit...

    for (int ws = 0; ws < NUM_WORKSPACES; ws++) {
        for (Client *c = workspaces[ws]; c; c = c->next) {
            XUnmapWindow(dpy, c->win);
            XKillClient(dpy, c->win);
        }
    }
    */

    XSync(dpy, False);
    XFreeCursor(dpy, cursor_move);
    XFreeCursor(dpy, cursor_normal);
    XFreeCursor(dpy, cursor_resize);
    XCloseDisplay(dpy);
    puts("quitting...");
    running = False;
}

void resize_win_down(void) {
    if (!focused || !focused->floating)
        return;

    int new_h = focused->h + user_config.resize_window_amt;
    int max_h = scr_height - focused->y;
    focused->h = CLAMP(new_h, MIN_WINDOW_SIZE, max_h);
    XResizeWindow(dpy, focused->win, focused->w, focused->h);
}

void resize_win_up(void) {
    if (!focused || !focused->floating)
        return;

    int new_h = focused->h - user_config.resize_window_amt;
    focused->h = CLAMP(new_h, MIN_WINDOW_SIZE, focused->h);
    XResizeWindow(dpy, focused->win, focused->w, focused->h);
}

void resize_win_right(void) {
    if (!focused || !focused->floating)
        return;

    int new_w = focused->w + user_config.resize_window_amt;
    int max_w = scr_width - focused->x;
    focused->w = CLAMP(new_w, MIN_WINDOW_SIZE, max_w);
    XResizeWindow(dpy, focused->win, focused->w, focused->h);
}

void resize_win_left(void) {
    if (!focused || !focused->floating)
        return;

    int new_w = focused->w - user_config.resize_window_amt;
    focused->w = CLAMP(new_w, MIN_WINDOW_SIZE, focused->w);
    XResizeWindow(dpy, focused->win, focused->w, focused->h);
}

void run(void) {
    running = True;
    XEvent xev;
    while (running) {
        XNextEvent(dpy, &xev);
        xev_case(&xev);
    }
}

void scan_existing_windows(void) {
    Window root_return;
    Window parent_return;
    Window *children;
    unsigned int n_children;

    if (XQueryTree(dpy, root, &root_return, &parent_return, &children,
                   &n_children)) {
        for (unsigned int i = 0; i < n_children; i++) {
            XWindowAttributes wa;
            if (!XGetWindowAttributes(dpy, children[i], &wa) ||
                wa.override_redirect || wa.map_state != IsViewable)
                continue;

            XEvent fake_event = {None};
            fake_event.type = MapRequest;
            fake_event.xmaprequest.window = children[i];
            hdl_map_req(&fake_event);
        }
        if (children)
            XFree(children);
    }
}

void select_input(Window w, Mask masks) { XSelectInput(dpy, w, masks); }

void send_wm_take_focus(Window w) {
    Atom wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    Atom wm_take_focus = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
    Atom *protos;
    int n;

    if (XGetWMProtocols(dpy, w, &protos, &n)) {
        for (int i = 0; i < n; i++) {
            if (protos[i] == wm_take_focus) {
                XEvent ev = {.xclient = {.type = ClientMessage,
                                         .window = w,
                                         .message_type = wm_protocols,
                                         .format = 32}};
                ev.xclient.data.l[0] = wm_take_focus;
                ev.xclient.data.l[1] = CurrentTime;
                XSendEvent(dpy, w, False, NoEventMask, &ev);
            }
        }
        XFree(protos);
    }
}

void setup(void) {
    if ((dpy = XOpenDisplay(NULL)) == NULL) {
        fprintf(stderr, "can't open display.\nquitting...");
        exit(EXIT_FAILURE);
    }
    root = XDefaultRootWindow(dpy);

    setup_atoms();
    other_wm();
    init_defaults();
    load_config();
    update_modifier_masks();
    grab_keys();

    cursor_normal = XcursorLibraryLoadCursor(dpy, "left_ptr");
    cursor_move = XcursorLibraryLoadCursor(dpy, "fleur");
    cursor_resize = XcursorLibraryLoadCursor(dpy, "bottom_right_corner");
    XDefineCursor(dpy, root, cursor_normal);

    scr_width = XDisplayWidth(dpy, DefaultScreen(dpy));
    scr_height = XDisplayHeight(dpy, DefaultScreen(dpy));

    /* select events wm should look for on root */
    Mask wm_masks = StructureNotifyMask | SubstructureRedirectMask |
                    SubstructureNotifyMask | KeyPressMask | PropertyChangeMask;
    select_input(root, wm_masks);

    /* grab mouse button events on root window */
    Mask root_click_masks =
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    Mask root_swap_masks =
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    Mask root_resize_masks =
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    grab_button(Button1, user_config.modkey, root, True, root_click_masks);
    grab_button(Button1, user_config.modkey | ShiftMask, root, True,
                root_swap_masks);
    grab_button(Button3, user_config.modkey, root, True, root_resize_masks);
    XSync(dpy, False);

    for (int i = 0; i < LASTEvent; i++)
        evtable[i] = hdl_dummy;
    evtable[ButtonPress] = hdl_button;
    evtable[ButtonRelease] = hdl_button_release;
    evtable[ClientMessage] = hdl_client_msg;
    evtable[ConfigureNotify] = hdl_config_ntf;
    evtable[ConfigureRequest] = hdl_config_req;
    evtable[DestroyNotify] = hdl_destroy_ntf;
    evtable[KeyPress] = hdl_keypress;
    evtable[MappingNotify] = hdl_mapping_ntf;
    evtable[MapRequest] = hdl_map_req;
    evtable[MotionNotify] = hdl_motion;
    evtable[PropertyNotify] = hdl_property_ntf;
    evtable[UnmapNotify] = hdl_unmap_ntf;
    scan_existing_windows();

    /* prevent child processes from becoming zombies */
    signal(SIGCHLD, SIG_IGN);
}

void setup_atoms(void) {
    for (int i = 0; i < ATOM_COUNT; i++)
        atoms[i] = XInternAtom(dpy, atom_names[i], False);

    /* checking window */
    wm_check_win = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    /* root property -> child window */
    XChangeProperty(dpy, root, atoms[ATOM_NET_SUPPORTING_WM_CHECK], XA_WINDOW,
                    32, PropModeReplace, (unsigned char *)&wm_check_win, 1);
    /* child window -> child window */
    XChangeProperty(dpy, wm_check_win, atoms[ATOM_NET_SUPPORTING_WM_CHECK],
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&wm_check_win, 1);
    /* name the wm */
    const char *wmname = "tilite";
    XChangeProperty(dpy, wm_check_win, atoms[ATOM_NET_WM_NAME],
                    atoms[ATOM_UTF8_STRING], 8, PropModeReplace,
                    (const unsigned char *)wmname, strlen(wmname));

    /* workspace setup */
    long num_workspaces = NUM_WORKSPACES;
    XChangeProperty(dpy, root, atoms[ATOM_NET_NUMBER_OF_DESKTOPS], XA_CARDINAL,
                    32, PropModeReplace, (const unsigned char *)&num_workspaces,
                    1);

    const char workspace_names[] = WORKSPACE_NAMES;
    int names_len = sizeof(workspace_names);
    XChangeProperty(dpy, root, atoms[ATOM_NET_DESKTOP_NAMES],
                    atoms[ATOM_UTF8_STRING], 8, PropModeReplace,
                    (const unsigned char *)workspace_names, names_len);

    XChangeProperty(dpy, root, atoms[ATOM_NET_CURRENT_DESKTOP], XA_CARDINAL, 32,
                    PropModeReplace, (const unsigned char *)&current_ws, 1);

    /* load supported list */
    XChangeProperty(dpy, root, atoms[ATOM_NET_SUPPORTED], XA_ATOM, 32,
                    PropModeReplace, (const unsigned char *)atoms, ATOM_COUNT);

    update_workarea();
}

void set_frame_extents(Window w) {
    long extents[4] = {user_config.border_width, user_config.border_width,
                       user_config.border_width, user_config.border_width};
    XChangeProperty(dpy, w, atoms[ATOM_NET_FRAME_EXTENTS], XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)extents, 4);
}

void set_input_focus(client_t *c, Bool raise_win, Bool warp) {
    if (c && c->mapped) {
        focused = c;

        /* update remembered focus */
        if (c->ws >= 0 && c->ws < NUM_WORKSPACES)
            ws_focused[c->ws] = c;

        Window w = find_toplevel(c->win);

        XSetInputFocus(dpy, w, RevertToPointerRoot, CurrentTime);
        send_wm_take_focus(w);

        if (raise_win) {
            /* always raise in monocle, otherwise respect floating_on_top */
            if (monocle || c->floating || !user_config.floating_on_top)
                XRaiseWindow(dpy, w);
        }
        /* EWMH focus hint */
        XChangeProperty(dpy, root, atoms[ATOM_NET_ACTIVE_WINDOW], XA_WINDOW, 32,
                        PropModeReplace, (unsigned char *)&w, 1);

        update_borders();

        if (warp && user_config.warp_cursor)
            warp_cursor(c);
    } else {
        /* no client */
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root, atoms[ATOM_NET_ACTIVE_WINDOW]);

        focused = NULL;
        ws_focused[current_ws] = NULL;
        update_borders();
    }

    XFlush(dpy);
}

void reset_opacity(Window w) {
    Atom atom = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
    XDeleteProperty(dpy, w, atom);
}

void set_opacity(Window w, double opacity) {
    if (opacity < 0.0)
        opacity = 0.0;

    if (opacity > 1.0)
        opacity = 1.0;

    unsigned long op = (unsigned long)(opacity * 0xFFFFFFFFu);
    Atom atom = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
    XChangeProperty(dpy, w, atom, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&op, 1);
}

void set_wm_state(Window w, long state) {
    long data[2] = {state, None}; /* state, icon window */
    XChangeProperty(dpy, w, atoms[ATOM_WM_STATE], atoms[ATOM_WM_STATE], 32,
                    PropModeReplace, (unsigned char *)data, 2);
}

int snap_coordinate(int pos, int size, int screen_size, int snap_dist) {
    if (UDIST(pos, 0) <= snap_dist)
        return 0;
    if (UDIST(pos + size, screen_size) <= snap_dist)
        return screen_size - size;
    return pos;
}

void spawn(const char *const *argv) {
    int argc = 0;
    while (argv[argc])
        argc++;

    int cmd_count = 1;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "|") == 0)
            cmd_count++;
    }

    const char ***commands = malloc(cmd_count * sizeof(char **)); /* *** bruh */
    if (!commands) {
        perror("malloc commands");
        return;
    }

    /* initialize all command pointers to NULL for safe cleanup */
    for (int i = 0; i < cmd_count; i++)
        commands[i] = NULL;

    int cmd_idx = 0;
    int arg_start = 0;
    for (int i = 0; i <= argc; i++) {
        if (!argv[i] || strcmp(argv[i], "|") == 0) {
            int len = i - arg_start;
            const char **cmd_args = malloc((len + 1) * sizeof(char *));

            if (!cmd_args) {
                perror("malloc cmd_args");

                for (int j = 0; j < cmd_idx; j++)
                    free(commands[j]);

                free(commands);
                return;
            }

            for (int j = 0; j < len; j++)
                cmd_args[j] = argv[arg_start + j];

            cmd_args[len] = NULL;
            commands[cmd_idx++] = cmd_args;
            arg_start = i + 1;
        }
    }

    int (*pipes)[2] = malloc(sizeof(int[2]) * (cmd_count - 1));
    if (!pipes) {
        perror("malloc pipes");

        for (int j = 0; j < cmd_count; j++)
            free(commands[j]);

        free(commands);
        return;
    }

    for (int i = 0; i < cmd_count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");

            for (int j = 0; j < cmd_count; j++)
                free(commands[j]);

            free(commands);
            free(pipes);
            return;
        }
    }

    for (int i = 0; i < cmd_count; i++) {
        if (!commands[i] || !commands[i][0])
            continue;

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");

            for (int k = 0; k < cmd_count - 1; k++) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }

            for (int j = 0; j < cmd_count; j++)
                free(commands[j]);

            free(commands);
            free(pipes);
            return;
        }
        if (pid == 0) {
            close(ConnectionNumber(dpy));

            if (i > 0)
                dup2(pipes[i - 1][0], STDIN_FILENO);

            if (i < cmd_count - 1)
                dup2(pipes[i][1], STDOUT_FILENO);

            for (int k = 0; k < cmd_count - 1; k++) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }

            execvp(commands[i][0], (char *const *)(void *)commands[i]);
            fprintf(stderr, "tilite: execvp '%s' failed\n", commands[i][0]);
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < cmd_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (int i = 0; i < cmd_count; i++)
        free(commands[i]);

    free(commands);
    free(pipes);
}

void swap_clients(client_t *a, client_t *b) {
    if (!a || !b || a == b)
        return;

    client_t **head = &workspaces[current_ws];
    client_t **pa = head, **pb = head;

    while (*pa && *pa != a)
        pa = &(*pa)->next;

    while (*pb && *pb != b)
        pb = &(*pb)->next;

    if (!*pa || !*pb)
        return;

    /* if next to it swap */
    if (*pa == b && *pb == a) {
        client_t *tmp = b->next;
        b->next = a;
        a->next = tmp;
        *pa = b;
        return;
    }

    /* full swap */
    client_t *ta = *pa;
    client_t *tb = *pb;
    client_t *ta_next = ta->next;
    client_t *tb_next = tb->next;

    *pa = tb;
    tb->next = ta_next == tb ? ta : ta_next;

    *pb = ta;
    ta->next = tb_next == ta ? tb : tb_next;
}

static bsp_node_t *bsp_make_leaf(client_t *c) {
    bsp_node_t *n = calloc(1, sizeof(bsp_node_t));
    if (!n)
        return NULL;
    n->type = BSP_LEAF;
    n->client = c;
    return n;
}

static bsp_node_t *bsp_find_leaf(bsp_node_t *node, client_t *c) {
    if (!node)
        return NULL;
    if (node->type == BSP_LEAF)
        return (node->client == c) ? node : NULL;
    bsp_node_t *r = bsp_find_leaf(node->first, c);
    return r ? r : bsp_find_leaf(node->second, c);
}

bsp_node_t *bsp_insert(bsp_node_t **root, client_t *old_client,
                       client_t *new_client) {
    bsp_node_t *leaf = NULL;

    if (*root == NULL) {
        *root = bsp_make_leaf(new_client);
        return *root;
    }

    leaf = bsp_find_leaf(*root, old_client);
    if (!leaf) {
        bsp_node_t *split = calloc(1, sizeof(bsp_node_t));
        if (!split)
            return NULL;
        split->type = BSP_SPLIT_V;
        split->first = *root;
        split->second = bsp_make_leaf(new_client);
        if (split->first)
            split->first->parent = split;
        if (split->second)
            split->second->parent = split;
        *root = split;
        return split;
    }

    bsp_node_t *split = calloc(1, sizeof(bsp_node_t));
    if (!split)
        return NULL;

    split->type = BSP_SPLIT_V;
    split->first = bsp_make_leaf(old_client);
    split->second = bsp_make_leaf(new_client);
    if (!split->first || !split->second) {
        free(split->first);
        free(split->second);
        free(split);
        return NULL;
    }
    split->first->parent = split;
    split->second->parent = split;
    split->parent = leaf->parent;

    if (!leaf->parent) {
        *root = split;
    } else {
        bsp_node_t *p = leaf->parent;
        if (p->first == leaf)
            p->first = split;
        else
            p->second = split;
    }
    free(leaf);
    return split;
}

void bsp_remove(bsp_node_t **root, client_t *c) {
    if (!*root)
        return;

    bsp_node_t *leaf = bsp_find_leaf(*root, c);
    if (!leaf)
        return;

    if (!leaf->parent) {
        free(leaf);
        *root = NULL;
        return;
    }

    bsp_node_t *parent = leaf->parent;
    bsp_node_t *sibling =
        (parent->first == leaf) ? parent->second : parent->first;
    bsp_node_t *grandp = parent->parent;

    sibling->parent = grandp;
    if (!grandp) {
        *root = sibling;
    } else {
        if (grandp->first == parent)
            grandp->first = sibling;
        else
            grandp->second = sibling;
    }
    free(leaf);
    free(parent);
}

static void bsp_assign_rects(bsp_node_t *node, int x, int y, int w, int h) {
    if (!node)
        return;

    if (node->type == BSP_LEAF) {
        client_t *c = node->client;
        if (!c || !c->mapped || c->floating || c->fullscreen)
            return;
        int bw = user_config.border_width;
        XWindowChanges wc = {.x = x,
                             .y = y,
                             .width = MAX(1, w - 2 * bw),
                             .height = MAX(1, h - 2 * bw),
                             .border_width = bw};
        if (c->x != wc.x || c->y != wc.y || c->w != wc.width ||
            c->h != wc.height)
            XConfigureWindow(dpy, c->win,
                             CWX | CWY | CWWidth | CWHeight | CWBorderWidth,
                             &wc);
        c->x = wc.x;
        c->y = wc.y;
        c->w = wc.width;
        c->h = wc.height;
        return;
    }

    int gaps = user_config.gaps;

    if (w >= h) {
        int lw = (w - gaps) / 2;
        int rw = w - lw - gaps;
        bsp_assign_rects(node->first, x, y, lw, h);
        bsp_assign_rects(node->second, x + lw + gaps, y, rw, h);
    } else {
        int th = (h - gaps) / 2;
        int bh = h - th - gaps;
        bsp_assign_rects(node->first, x, y, w, th);
        bsp_assign_rects(node->second, x, y + th + gaps, w, bh);
    }
}

void tile(void) {
    update_struts();
    client_t *head = workspaces[current_ws];

    client_t *tileable[MAX_CLIENTS] = {0};
    int n_tileable = 0;
    for (client_t *c = head; c && n_tileable < MAX_CLIENTS; c = c->next)
        if (c->mapped && !c->floating && !c->fullscreen)
            tileable[n_tileable++] = c;

    if (n_tileable == 0)
        return;

    int gaps = user_config.gaps;
    int x = reserve_left + gaps;
    int y = reserve_top + gaps;
    int w = MAX(1, scr_width - reserve_left - reserve_right - 2 * gaps);
    int h = MAX(1, scr_height - reserve_top - reserve_bottom - 2 * gaps);

    if (monocle) {
        for (int i = 0; i < n_tileable; i++) {
            client_t *c = tileable[i];
            int bw = user_config.border_width;
            XWindowChanges wc = {.x = x,
                                 .y = y,
                                 .width = MAX(1, w - 2 * bw),
                                 .height = MAX(1, h - 2 * bw),
                                 .border_width = bw};
            XConfigureWindow(dpy, c->win,
                             CWX | CWY | CWWidth | CWHeight | CWBorderWidth,
                             &wc);
            c->x = wc.x;
            c->y = wc.y;
            c->w = wc.width;
            c->h = wc.height;
        }
        if (focused && focused->mapped && !focused->floating &&
            !focused->fullscreen)
            XRaiseWindow(dpy, focused->win);
        update_borders();
        return;
    }

    bsp_node_t **bsp = &bsp_roots[current_ws];

    if (!*bsp) {
        for (int i = 0; i < n_tileable; i++)
            bsp_insert(bsp, i > 0 ? tileable[i - 1] : NULL, tileable[i]);
    }

    bsp_assign_rects(*bsp, x, y, w, h);
    update_borders();
}

void toggle_floating(void) {
    if (!focused)
        return;

    if (focused->fullscreen) {
        focused->fullscreen = False;
        tile();
        XSetWindowBorderWidth(dpy, focused->win, user_config.border_width);
    }

    focused->floating = !focused->floating;

    if (focused->floating) {
        /* Window is becoming floating: remove from BSP */
        bsp_remove(&bsp_roots[current_ws], focused);
        XWindowAttributes wa;
        if (XGetWindowAttributes(dpy, focused->win, &wa)) {
            focused->x = wa.x;
            focused->y = wa.y;
            focused->w = wa.width;
            focused->h = wa.height;

            XConfigureWindow(dpy, focused->win, CWX | CWY | CWWidth | CWHeight,
                             &(XWindowChanges){.x = focused->x,
                                               .y = focused->y,
                                               .width = focused->w,
                                               .height = focused->h});
        }
    } else {
        bsp_insert(&bsp_roots[current_ws], NULL, focused);
    }
    tile();
    update_borders();

    if (focused->floating)
        set_input_focus(focused, True, False);
}

void toggle_floating_global(void) {
    global_floating = !global_floating;
    Bool any_tiled = False;
    for (client_t *c = workspaces[current_ws]; c; c = c->next) {
        if (!c->floating) {
            any_tiled = True;
            break;
        }
    }

    for (client_t *c = workspaces[current_ws]; c; c = c->next) {
        c->floating = any_tiled;
        if (c->floating) {
            XWindowAttributes wa;
            XGetWindowAttributes(dpy, c->win, &wa);
            c->x = wa.x;
            c->y = wa.y;
            c->w = wa.width;
            c->h = wa.height;

            XConfigureWindow(
                dpy, c->win, CWX | CWY | CWWidth | CWHeight,
                &(XWindowChanges){
                    .x = c->x, .y = c->y, .width = c->w, .height = c->h});
            XRaiseWindow(dpy, c->win);
        }
    }

    tile();
    update_borders();
}

void toggle_fullscreen(void) {
    if (!focused)
        return;

    apply_fullscreen(focused, !focused->fullscreen);
}

void toggle_monocle(void) {
    monocle = !monocle;
    tile();
    update_borders();
    if (focused)
        set_input_focus(focused, True, True);
}

void update_borders(void) {
    for (client_t *c = workspaces[current_ws]; c; c = c->next)
        XSetWindowBorder(dpy, c->win,
                         (c == focused ? user_config.border_foc_col
                                       : user_config.border_ufoc_col));

    if (focused) {
        Window w = focused->win;
        XChangeProperty(dpy, root, atoms[ATOM_NET_ACTIVE_WINDOW], XA_WINDOW, 32,
                        PropModeReplace, (unsigned char *)&w, 1);
    }
}

void update_client_desktop_properties(void) {
    for (int ws = 0; ws < NUM_WORKSPACES; ws++) {
        for (client_t *c = workspaces[ws]; c; c = c->next) {
            long desktop = ws;
            XChangeProperty(dpy, c->win, atoms[ATOM_NET_WM_DESKTOP],
                            XA_CARDINAL, 32, PropModeReplace,
                            (unsigned char *)&desktop, 1);
        }
    }
}

void update_modifier_masks(void) {
    XModifierKeymap *mod_mapping = XGetModifierMapping(dpy);
    KeyCode num = XKeysymToKeycode(dpy, XK_Num_Lock);
    KeyCode mode = XKeysymToKeycode(dpy, XK_Mode_switch);
    numlock_mask = 0;
    mode_switch_mask = 0;

    int n_masks = 8;
    for (int i = 0; i < n_masks; i++) {
        for (int j = 0; j < mod_mapping->max_keypermod; j++) {
            KeyCode keycode =
                mod_mapping->modifiermap[i * mod_mapping->max_keypermod + j];
            if (keycode == num)
                numlock_mask = (1u << i);
            if (keycode == mode)
                mode_switch_mask = (1u << i);
        }
    }
    XFreeModifiermap(mod_mapping);
}

void update_net_client_list(void) {
    Window wins[MAX_CLIENTS];
    int n = 0;
    for (int ws = 0; ws < NUM_WORKSPACES; ws++)
        for (client_t *c = workspaces[ws]; c; c = c->next)
            wins[n++] = c->win;

    XChangeProperty(dpy, root, atoms[ATOM_NET_CLIENT_LIST], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)wins, n);
}

void update_struts(void) {
    reserve_left = 0;
    reserve_right = 0;
    reserve_top = 0;
    reserve_bottom = 0;

    Window root_ret;
    Window parent_ret;
    Window *children = NULL;
    unsigned int n_children = 0;

    if (!XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &n_children))
        return;

    int screen_w = scr_width;
    int screen_h = scr_height;

    for (unsigned int i = 0; i < n_children; i++) {
        Window w = children[i];

        Atom actual_type;
        int actual_format;
        unsigned long n_items, bytes_after;
        Atom *types = NULL;

        if (XGetWindowProperty(dpy, w, atoms[ATOM_NET_WM_WINDOW_TYPE], 0, 4,
                               False, XA_ATOM, &actual_type, &actual_format,
                               &n_items, &bytes_after,
                               (unsigned char **)&types) != Success ||
            !types)
            continue;

        Bool is_dock = False;
        for (unsigned long j = 0; j < n_items; j++) {
            if (types[j] == atoms[ATOM_NET_WM_WINDOW_TYPE_DOCK]) {
                is_dock = True;
                break;
            }
        }
        XFree(types);
        if (!is_dock)
            continue;

        long *str = NULL;
        Atom actual;
        int sfmt;
        unsigned long len;
        unsigned long rem;

        if (XGetWindowProperty(dpy, w, atoms[ATOM_NET_WM_STRUT_PARTIAL], 0, 12,
                               False, XA_CARDINAL, &actual, &sfmt, &len, &rem,
                               (unsigned char **)&str) == Success &&
            str && len >= 12) {

            /*
             ewmh:
             [0] left, [1] right, [2] top, [3] bottom

             [4] left_start_y,   [5] left_end_y
             [6] right_start_y,  [7] right_end_y
             [8] top_start_x,    [9] top_end_x
             [10] bottom_start_x,[11] bottom_end_x

             all coords are in root space.
             */
            long left = str[0];
            long right = str[1];
            long top = str[2];
            long bottom = str[3];
            long left_start_y = str[4];
            long left_end_y = str[5];
            long right_start_y = str[6];
            long right_end_y = str[7];
            long top_start_x = str[8];
            long top_end_x = str[9];
            long bot_start_x = str[10];
            long bot_end_x = str[11];

            XFree(str);

            /* skip empty struts */
            if (!left && !right && !top && !bottom)
                continue;

            if (left > 0) {
                long span_start = left_start_y;
                long span_end = left_end_y;
                if (span_end >= 0 && span_start <= scr_height - 1) {
                    int reserve = (int)MAX(0, left);
                    if (reserve > 0)
                        reserve_left = MAX(reserve_left, reserve);
                }
            }

            if (right > 0) {
                long span_start = right_start_y;
                long span_end = right_end_y;
                if (span_end >= 0 && span_start <= scr_height - 1) {
                    int global_reserved_left = screen_w - (int)right;
                    int overlap = scr_width - global_reserved_left;
                    int reserve = MAX(0, overlap);
                    if (reserve > 0)
                        reserve_right = MAX(reserve_right, reserve);
                }
            }

            if (top > 0) {
                long span_start = top_start_x;
                long span_end = top_end_x;
                if (span_end >= 0 && span_start <= scr_width - 1) {
                    int reserve = (int)MAX(0, top);
                    if (reserve > 0)
                        reserve_top = MAX(reserve_top, reserve);
                }
            }

            if (bottom > 0) {
                long span_start = bot_start_x;
                long span_end = bot_end_x;
                if (span_end >= 0 && span_start <= scr_width - 1) {
                    int global_reserved_top = screen_h - (int)bottom;
                    int overlap = scr_height - global_reserved_top;
                    int reserve = MAX(0, overlap);
                    if (reserve > 0)
                        reserve_bottom = MAX(reserve_bottom, reserve);
                }
            }
        }
    }

    if (children)
        XFree(children);

    update_workarea();
}

void update_workarea(void) {
    long workarea[4];

    workarea[0] = reserve_left;
    workarea[1] = reserve_top;
    workarea[2] = scr_width - reserve_left - reserve_right;
    workarea[3] = scr_height - reserve_top - reserve_bottom;

    XChangeProperty(dpy, root, atoms[ATOM_NET_WORKAREA], XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)workarea, 4);
}

void warp_cursor(client_t *c) {
    if (!c)
        return;

    int center_x = c->x + (c->w / 2);
    int center_y = c->y + (c->h / 2);

    XWarpPointer(dpy, None, root, 0, 0, 0, 0, center_x, center_y);
    XSync(dpy, False);
}

Bool window_has_ewmh_state(Window w, Atom state) {
    Atom type;
    int format;
    unsigned long n_atoms = 0;
    unsigned long unread = 0;
    Atom *found_atoms = NULL;

    if (XGetWindowProperty(dpy, w, atoms[ATOM_NET_WM_STATE], 0, 1024, False,
                           XA_ATOM, &type, &format, &n_atoms, &unread,
                           (unsigned char **)&found_atoms) == Success &&
        found_atoms) {

        for (unsigned long i = 0; i < n_atoms; i++) {
            if (found_atoms[i] == state) {
                XFree(found_atoms);
                return True;
            }
        }
        XFree(found_atoms);
    }
    return False;
}

void window_set_ewmh_state(Window w, Atom state, Bool add) {
    Atom type;
    int format;
    unsigned long n_atoms = 0;
    unsigned long unread = 0;
    Atom *found_atoms = NULL;

    if (XGetWindowProperty(dpy, w, atoms[ATOM_NET_WM_STATE], 0, 1024, False,
                           XA_ATOM, &type, &format, &n_atoms, &unread,
                           (unsigned char **)&found_atoms) != Success) {
        found_atoms = NULL;
        n_atoms = 0;
    }

    /* build new list */
    Atom buf[16];
    Atom *list = buf;
    unsigned long list_len = 0;

    if (found_atoms) {
        for (unsigned long i = 0; i < n_atoms; i++) {
            if (found_atoms[i] != state)
                list[list_len++] = found_atoms[i];
        }
    }
    if (add && list_len < 16)
        list[list_len++] = state;

    if (list_len == 0)
        XDeleteProperty(dpy, w, atoms[ATOM_NET_WM_STATE]);
    else
        XChangeProperty(dpy, w, atoms[ATOM_NET_WM_STATE], XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)list, list_len);

    if (found_atoms)
        XFree(found_atoms);
}

int xerr(Display *d, XErrorEvent *ee) {
    /* ignore noise & non fatal errors */
    const struct {
        int req, code;
    } ignore[] = {
        {0, BadWindow},
        {X_GetGeometry, BadDrawable},
        {X_SetInputFocus, BadMatch},
        {X_ConfigureWindow, BadMatch},
    };

    for (size_t i = 0; i < sizeof(ignore) / sizeof(ignore[0]); i++) {
        if ((ignore[i].req == 0 || ignore[i].req == ee->request_code) &&
            (ignore[i].code == ee->error_code))
            return 0;
    }

    return 0;
    (void)d;
    (void)ee;
}

void xev_case(XEvent *xev) {
    if (xev->type >= 0 && xev->type < LASTEvent)
        evtable[xev->type](xev);
    else
        fprintf(stderr, "tilite: invalid event type: %d\n", xev->type);
}

int main(int ac, char **av) {
    if (ac > 1) {
        if (strcmp(av[1], "-v") == 0 || strcmp(av[1], "--version") == 0) {
            printf("%s\n%s\n%s\n", VERSION, AUTHOR, LICENSE);
            return EXIT_SUCCESS;
        } else {
            printf("usage:\n");
            printf("\t[-v || --version]: See the version of tilite\n");
            return EXIT_SUCCESS;
        }
    }
    setup();
    puts("tilite: starting...");
    run();
    return EXIT_SUCCESS;
}
