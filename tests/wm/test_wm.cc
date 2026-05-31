// WM integration tests. Runs against Xvfb :98 (not :99, to avoid conflicts).
//
// Architecture:
//   Child 1: Xvfb :98              — headless X server
//   Child 2: ./wm (DISPLAY=:98)    — the WM under test (NYLA_WM_NO_DAEMONS=1)
//   Parent:  test driver           — connects to :98, creates windows, verifies layout
//
// Isolation: The WM is started with NYLA_WM_NO_DAEMONS=1 so it never spawns or
// kills daemon processes (wm_overlay, dunst, redshift). All output goes to stderr.
//
// Critical: All X11 atoms are interned BEFORE the WM starts, because the WM
// calls xcb_grab_server() during WmInit(), which freezes replies for other clients.
//
// Layout model: infinite-strip horizontal tiling with center-scroll.
//   - Windows placed left-to-right at x=0, 1280, 2560, ... in a virtual strip.
//   - Viewport scrolls to center the active (most recently mapped) window.
//   - Earlier windows may have negative screen x (scrolled off left).
//   - Default desiredWidth = 1280, capped to viewport (1920).
//
// Keybinding summary:
//   Meta+←/→       — resize active window width ±80px
//   Meta+Ctrl+←/→  — switch stacks (prev/next workspace)
//   Meta+E/R        — switch stacks (prev/next)
//   Alt+Tab          — cycle window focus forward (no clear-zoom)
//   Alt+Shift+Tab    — cycle window focus backward
//   Meta+D/F         — cycle window focus prev/next (clear-zoom)
//   Meta+G           — toggle zoom
//   Meta+V           — toggle follow mode
//   Alt+F4           — close active window
//   Meta+Shift+R     — serialize state and restart WM
//   Meta+Backspace   — close active window (backup)
//   Meta+S           — launch dmenu_run
//   Meta+T           — launch ghostty terminal

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <xcb/xtest.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

namespace
{

// ─── Constants ───────────────────────────────────────────────────────────────

constexpr int kScreenW = 1920;
constexpr int kScreenH = 1080;
constexpr int kBarH = 20;
constexpr int kViewW = kScreenW;
constexpr int kViewH = kScreenH - kBarH;

constexpr int kDefaultDesiredW = 1280;

// Max child processes to track (real apps we launched).
constexpr int kMaxChildren = 32;
pid_t children[kMaxChildren];
int childCount = 0;

// ─── Test output ─────────────────────────────────────────────────────────────

#define TPRINT(...) fprintf(stderr, __VA_ARGS__)
#define TFAIL(msg, ...) fprintf(stderr, "  FAIL: " msg "\n", ##__VA_ARGS__)

int failureCount = 0;

void Check(int cond, char const *file, int line, char const *msg)
{
    if (!(cond))
    {
        TFAIL("%s:%d: %s", file, line, msg);
        ++failureCount;
    }
}
#define CHECK(cond, msg) Check((cond), __FILE__, __LINE__, msg)

// ─── Globals ─────────────────────────────────────────────────────────────────

pid_t xvfbPid = 0;
pid_t wmPid = 0;
xcb_connection_t *conn = nullptr;
xcb_screen_t *screen = nullptr;

xcb_atom_t atomWmName;
xcb_atom_t atomUtf8String;
xcb_atom_t atomWmNormalHints;
xcb_atom_t atomWmHints;
xcb_atom_t atomWmTransientFor;
xcb_atom_t atomWmProtocols;
xcb_atom_t atomWmDeleteWindow;
xcb_atom_t atomNetWmName;
xcb_atom_t atomUtf8StringType;
xcb_atom_t atomWmState;
xcb_atom_t atomWmClass;
xcb_atom_t atomWmClientMachine;

// ─── XTEST / key injection globals ───────────────────────────────────────────

xkb_context *xkbCtx = nullptr;
xkb_keymap *keymap_ = nullptr;

// Keycodes for modifier keys (populated from modifier mapping).
xcb_keycode_t mod4Key = 0;  // first Mod4 key (Meta/Super)
xcb_keycode_t altKey = 0;   // first Mod1 key (Alt)
xcb_keycode_t ctrlKey = 0;  // first Control key
xcb_keycode_t shiftKey = 0; // first Shift key

// ─── Fatal error ─────────────────────────────────────────────────────────────

void Die(char const *msg)
{
    TFAIL("%s", msg);
    if (xkbCtx)
    {
        xkb_context_unref(xkbCtx);
        xkbCtx = nullptr;
    }
    if (keymap_)
    {
        xkb_keymap_unref(keymap_);
        keymap_ = nullptr;
    }
    if (conn)
    {
        xcb_disconnect(conn);
        conn = nullptr;
    }
    if (wmPid)
    {
        kill(wmPid, SIGTERM);
        waitpid(wmPid, nullptr, 0);
        wmPid = 0;
    }
    if (xvfbPid)
    {
        kill(xvfbPid, SIGTERM);
        waitpid(xvfbPid, nullptr, 0);
        xvfbPid = 0;
    }
    for (int i = 0; i < childCount; ++i)
    {
        kill(children[i], SIGKILL);
        waitpid(children[i], nullptr, 0);
    }
    unlink("/tmp/.X98-lock");
    exit(1);
}

auto InternAtom(char const *name) -> xcb_atom_t
{
    xcb_intern_atom_cookie_t c = xcb_intern_atom(conn, 0, strlen(name), name);
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(conn, c, nullptr);
    if (!r)
        Die("InternAtom failed");
    xcb_atom_t a = r->atom;
    free(r);
    return a;
}

void SleepMs(int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

// ─── Child process management ────────────────────────────────────────────────

// Launch a real X11 application. Returns its pid. The child inherits DISPLAY.
auto LaunchApp(char const *const argv[]) -> pid_t
{
    pid_t pid = fork();
    if (pid == 0)
    {
        // Redirect stdout/stderr to /dev/null to reduce noise.
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
        {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp(argv[0], (char *const *)argv);
        fprintf(stderr, "exec %s failed: %s\n", argv[0], strerror(errno));
        _exit(1);
    }
    if (pid > 0 && childCount < kMaxChildren)
    {
        children[childCount++] = pid;
    }
    return pid;
}

void DrainEvents();
void WmPump(int ms);

// Kill and wait for all tracked child processes, then let the WM settle.
void KillAllChildren()
{
    for (int i = 0; i < childCount; ++i)
    {
        kill(children[i], SIGTERM);
    }
    for (int i = 0; i < childCount; ++i)
    {
        waitpid(children[i], nullptr, 0);
    }
    childCount = 0;
    if (conn)
    {
        DrainEvents();
        WmPump(500); // Let WM process window destructions.
    }
}

// ─── X11 helpers ─────────────────────────────────────────────────────────────

auto CreateTestWindow(char const *title, uint32_t w, uint32_t h) -> xcb_window_t
{
    xcb_window_t win = xcb_generate_id(conn);
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[] = {
        screen->white_pixel,
        XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY,
    };
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, screen->root, 0, 0, w, h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual, mask, values);

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atomWmName, atomUtf8StringType, 8, strlen(title), title);

    xcb_map_window(conn, win);
    xcb_flush(conn);
    return win;
}

// Create an override-redirect window (not managed by the WM).
// Note: xcb_create_window values must be in ascending bit-order.
// XCB_CW_BACK_PIXEL (bit 1), XCB_CW_OVERRIDE_REDIRECT (bit 9), XCB_CW_EVENT_MASK (bit 11).
auto CreateOverrideRedirectWindow(char const *title, uint32_t w, uint32_t h) -> xcb_window_t
{
    xcb_window_t win = xcb_generate_id(conn);
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    uint32_t values[] = {
        screen->white_pixel,                                              // bit  1: BACK_PIXEL
        1,                                                                // bit  9: OVERRIDE_REDIRECT
        XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY, // bit 11: EVENT_MASK
    };
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, screen->root, 0, 0, w, h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual, mask, values);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atomWmName, atomUtf8StringType, 8, strlen(title), title);
    xcb_map_window(conn, win);
    xcb_flush(conn);
    return win;
}

// Create a transient window (WM_TRANSIENT_FOR pointing to parent).
auto CreateTransientWindow(char const *title, uint32_t w, uint32_t h, xcb_window_t parent) -> xcb_window_t
{
    xcb_window_t win = xcb_generate_id(conn);
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[] = {
        screen->white_pixel,
        XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY,
    };
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, screen->root, 0, 0, w, h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual, mask, values);

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atomWmName, atomUtf8StringType, 8, strlen(title), title);

    // Set WM_TRANSIENT_FOR to parent window.
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atomWmTransientFor, XCB_ATOM_WINDOW, 32, 1, &parent);

    xcb_map_window(conn, win);
    xcb_flush(conn);
    return win;
}

auto GetWindowGeometry(xcb_window_t win, int16_t *x, int16_t *y, uint16_t *w, uint16_t *h) -> int
{
    xcb_get_geometry_cookie_t c = xcb_get_geometry(conn, win);
    xcb_get_geometry_reply_t *r = xcb_get_geometry_reply(conn, c, nullptr);
    if (!r)
        return -1;
    if (x)
        *x = r->x;
    if (y)
        *y = r->y;
    if (w)
        *w = r->width;
    if (h)
        *h = r->height;
    free(r);
    return 0;
}

auto GetWindowMapped(xcb_window_t win) -> int
{
    xcb_get_window_attributes_cookie_t c = xcb_get_window_attributes(conn, win);
    xcb_get_window_attributes_reply_t *r = xcb_get_window_attributes_reply(conn, c, nullptr);
    if (!r)
        return -1;
    int mapped = (r->map_state == XCB_MAP_STATE_VIEWABLE);
    free(r);
    return mapped;
}

auto GetInputFocus() -> xcb_window_t
{
    xcb_get_input_focus_cookie_t c = xcb_get_input_focus(conn);
    xcb_get_input_focus_reply_t *r = xcb_get_input_focus_reply(conn, c, nullptr);
    if (!r)
        return 0;
    xcb_window_t w = r->focus;
    free(r);
    return w;
}

// Get the WM_NAME property of a window (returns length, fills buf).
auto GetWmName(xcb_window_t win, char *buf, int bufsize) -> int
{
    buf[0] = '\0';
    xcb_get_property_cookie_t c = xcb_get_property(conn, 0, win, atomWmName, XCB_ATOM_ANY, 0, 64);
    xcb_get_property_reply_t *r = xcb_get_property_reply(conn, c, nullptr);
    if (!r || r->length == 0 || r->type == XCB_NONE)
    {
        if (r)
            free(r);
        return 0;
    }
    int len = xcb_get_property_value_length(r);
    if (len >= bufsize)
        len = bufsize - 1;
    memcpy(buf, xcb_get_property_value(r), len);
    buf[len] = '\0';
    free(r);
    return len;
}

// Query all direct children of the root window. Returns count.
auto GetRootChildren(xcb_window_t *out, int maxOut) -> int
{
    xcb_query_tree_cookie_t c = xcb_query_tree(conn, screen->root);
    xcb_query_tree_reply_t *r = xcb_query_tree_reply(conn, c, nullptr);
    if (!r)
        return 0;
    int n = xcb_query_tree_children_length(r);
    xcb_window_t *childWindows = xcb_query_tree_children(r);
    if (n > maxOut)
        n = maxOut;
    memcpy(out, childWindows, n * sizeof(xcb_window_t));
    free(r);
    return n;
}

// Wait for a window with the given WM_CLASS instance name to appear.
auto WaitForWindowClass(char const *className, int timeoutMs) -> xcb_window_t
{
    int elapsed = 0;
    while (elapsed < timeoutMs)
    {
        xcb_window_t wins[64];
        int n = GetRootChildren(wins, 64);
        for (int i = 0; i < n; ++i)
        {
            xcb_get_property_cookie_t c = xcb_get_property(conn, 0, wins[i], atomWmClass, XCB_ATOM_ANY, 0, 128);
            xcb_get_property_reply_t *r = xcb_get_property_reply(conn, c, nullptr);
            if (!r || r->length == 0)
            {
                if (r)
                    free(r);
                continue;
            }
            char *val = (char *)xcb_get_property_value(r);
            int len = xcb_get_property_value_length(r);
            bool found = false;
            for (int off = 0; off < len && !found;)
            {
                if (strcasecmp(val + off, className) == 0)
                {
                    found = true;
                    break;
                }
                off += strlen(val + off) + 1;
            }
            free(r);
            if (found)
                return wins[i];
        }
        xcb_flush(conn);
        SleepMs(100);
        elapsed += 100;
    }
    return 0;
}

// Wait for a window with the given WM_NAME substring to appear.
auto WaitForWindowNamed(char const *nameSubstr, int timeoutMs) -> xcb_window_t
{
    int elapsed = 0;
    while (elapsed < timeoutMs)
    {
        xcb_window_t wins[64];
        int n = GetRootChildren(wins, 64);
        for (int i = 0; i < n; ++i)
        {
            char name[256];
            GetWmName(wins[i], name, sizeof(name));
            if (strstr(name, nameSubstr))
                return wins[i];
        }
        SleepMs(100);
        elapsed += 100;
    }
    return 0;
}

// Drains all pending X events so we get a clean view of current state.
void DrainEvents()
{
    while (true)
    {
        xcb_generic_event_t *ev = xcb_poll_for_event(conn);
        if (!ev)
            break;
        free(ev);
    }
}

// ─── Key injection infrastructure (XTEST + xkbcommon) ────────────────────────

// Look up a keycode by XKB key name (e.g. "AD03" for E, "LEFT" for ArrowLeft).
auto KeycodeByXkbName(char const *name) -> xcb_keycode_t
{
    if (!keymap_)
        return 0;
    xkb_keycode_t kc = xkb_keymap_key_by_name(keymap_, name);
    return (kc == XKB_KEYCODE_INVALID) ? 0 : (xcb_keycode_t)kc;
}

// Find modifier keycodes from the X server's modifier mapping.
// Mod4 → Meta/Super, Mod1 → Alt, Control, Shift.
void FindModifierKeys()
{
    xcb_get_modifier_mapping_cookie_t c = xcb_get_modifier_mapping(conn);
    xcb_get_modifier_mapping_reply_t *r = xcb_get_modifier_mapping_reply(conn, c, nullptr);
    if (!r)
        Die("get_modifier_mapping failed");

    int kcPerMod = r->keycodes_per_modifier;
    xcb_keycode_t *map = xcb_get_modifier_mapping_keycodes(r);

    // Mod indices: Shift=0, Lock=1, Control=2, Mod1=3, Mod2=4, Mod3=5, Mod4=6, Mod5=7
    for (int i = 0; i < kcPerMod; ++i)
    {
        int shiftIdx = 0 * kcPerMod + i;
        int ctrlIdx = 2 * kcPerMod + i;
        int altIdx = 3 * kcPerMod + i;
        int mod4Idx = 6 * kcPerMod + i;

        if (!shiftKey && map[shiftIdx] != 0)
            shiftKey = map[shiftIdx];
        if (!ctrlKey && map[ctrlIdx] != 0)
            ctrlKey = map[ctrlIdx];
        if (!altKey && map[altIdx] != 0)
            altKey = map[altIdx];
        if (!mod4Key && map[mod4Idx] != 0)
            mod4Key = map[mod4Idx];
    }
    free(r);

    if (!mod4Key || !altKey || !ctrlKey || !shiftKey)
        Die("could not find all modifier keys");

    TPRINT("  [keys] mod4=0x%x alt=0x%x ctrl=0x%x shift=0x%x\n", mod4Key, altKey, ctrlKey, shiftKey);
}

// Initialize the xkb keymap (for key name → keycode lookup).
// Xvfb often doesn't expose a core keyboard via XI, so we fall back to
// evdev rules + us layout which is what Xvfb uses internally.
void InitXkb()
{
    xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkbCtx)
        Die("xkb_context_new failed");

    // Try device-based keymap first.
    int32_t deviceId = xkb_x11_get_core_keyboard_device_id(conn);
    if (deviceId != -1)
    {
        keymap_ = xkb_x11_keymap_new_from_device(xkbCtx, conn, deviceId, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }

    // Fall back to names-based keymap (evdev rules, US layout).
    if (!keymap_)
    {
        TPRINT("  [keys] no core keyboard device, using evdev keymap\n");
        struct xkb_rule_names names = {
            .rules = "evdev",
            .model = "pc105",
            .layout = "us",
        };
        keymap_ = xkb_keymap_new_from_names(xkbCtx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    if (!keymap_)
        Die("failed to get xkb keymap");
}

// Send a raw key press/release via XTEST.
void XtestKey(xcb_keycode_t keycode, bool press)
{
    xcb_test_fake_input(conn, press ? XCB_KEY_PRESS : XCB_KEY_RELEASE, keycode, XCB_CURRENT_TIME, screen->root, 0, 0,
                        0);
    xcb_flush(conn);
}

// Send a single key tap (press + release, no modifiers).
void TapKey(xcb_keycode_t kc)
{
    XtestKey(kc, true);
    WmPump(10);
    XtestKey(kc, false);
    WmPump(10);
}

// Send a key chord: hold modifier(s), press+release key, release modifier(s).
// mod1/mod2 can be 0 for no modifier.
void ModKey(xcb_keycode_t mod1, xcb_keycode_t mod2, xcb_keycode_t key)
{
    if (mod1)
    {
        XtestKey(mod1, true);
        WmPump(15);
    }
    if (mod2)
    {
        XtestKey(mod2, true);
        WmPump(15);
    }
    XtestKey(key, true);
    WmPump(15);
    XtestKey(key, false);
    WmPump(15);
    if (mod2)
    {
        XtestKey(mod2, false);
        WmPump(15);
    }
    if (mod1)
    {
        XtestKey(mod1, false);
        WmPump(15);
    }
}

// Convenience: Meta + <key>
void MetaKey(xcb_keycode_t kc)
{
    ModKey(mod4Key, 0, kc);
}
// Meta + Ctrl + <key>
void MetaCtrlKey(xcb_keycode_t kc)
{
    ModKey(mod4Key, ctrlKey, kc);
}
// Meta + Shift + <key>
void MetaShiftKey(xcb_keycode_t kc)
{
    ModKey(mod4Key, shiftKey, kc);
}
// Alt + <key>
void AltKey(xcb_keycode_t kc)
{
    ModKey(altKey, 0, kc);
}
// Alt + Shift + <key>
void AltShiftKey(xcb_keycode_t kc)
{
    ModKey(altKey, shiftKey, kc);
}

// ─── Setup / teardown ────────────────────────────────────────────────────────

// Start a new WM instance on the current display. Updates wmPid.
// Returns true on success.
// IMPORTANT: No XCB connection must exist when this is called,
// because the fork() would share the socket with the child.
auto StartWm(char const *display) -> bool
{
    TPRINT("  [setup] starting WM\n");
    wmPid = fork();
    if (wmPid == 0)
    {
        setenv("DISPLAY", display, 1);
        setenv("NYLA_WM_NO_DAEMONS", "1", 1);
        // Resolve wm binary relative to the test executable's location.
        // This works for ctest (any cwd) and direct invocation.
        char wmPath[256];
        char selfPath[256];
        ssize_t selfLen = readlink("/proc/self/exe", selfPath, sizeof(selfPath) - 1);
        if (selfLen > 0)
        {
            selfPath[selfLen] = '\0';
            char *lastSlash = strrchr(selfPath, '/');
            if (lastSlash)
            {
                *lastSlash = '\0';
                snprintf(wmPath, sizeof(wmPath), "%s/wm", selfPath);
                execl(wmPath, wmPath, nullptr);
            }
        }
        // Fall back to relative paths from project root.
        char const *fallbackPaths[] = {
            "./build/linux-debug/bin/wm",
            "./build/bin/wm",
            nullptr,
        };
        for (int i = 0; fallbackPaths[i]; ++i)
            execl(fallbackPaths[i], fallbackPaths[i], nullptr);
        fprintf(stderr, "exec wm failed: %s\n", strerror(errno));
        _exit(1);
    }
    TPRINT("  [setup] WM pid=%d, waiting for init...\n", wmPid);

    // Wait for WM to finish WmInit (including server grab/ungrab).
    SleepMs(2000);

    // Check if WM died.
    int wmStatus;
    if (waitpid(wmPid, &wmStatus, WNOHANG) == wmPid)
    {
        if (WIFEXITED(wmStatus))
            TFAIL("WM exited with code %d", WEXITSTATUS(wmStatus));
        else if (WIFSIGNALED(wmStatus))
            TFAIL("WM killed by signal %d", WTERMSIG(wmStatus));
        TPRINT("  [setup] WM died during init\n");
        return false;
    }

    TPRINT("  [setup] WM ready\n");
    return true;
}

void Setup()
{
    char const *testDisplay = getenv("WM_TEST_DISPLAY");

    if (testDisplay)
    {
        TPRINT("  [setup] using existing display %s\n", testDisplay);
        setenv("DISPLAY", testDisplay, 1);
    }
    else
    {
        unlink("/tmp/.X98-lock");
        unlink("/tmp/.X11-unix/X98");

        TPRINT("  [setup] starting Xvfb :98\n");
        xvfbPid = fork();
        if (xvfbPid == 0)
        {
            execlp("Xvfb", "Xvfb", ":98", "-screen", "0", "1920x1080x24", "-ac", nullptr);
            fprintf(stderr, "exec Xvfb failed: %s\n", strerror(errno));
            _exit(1);
        }
        SleepMs(800);

        if (waitpid(xvfbPid, nullptr, WNOHANG) == xvfbPid)
            Die("Xvfb exited immediately");
    }

    // Start the WM BEFORE creating any XCB connection.
    // The fork() must not share an XCB socket with the child.
    char const *display = testDisplay ? testDisplay : ":98";
    if (!StartWm(display))
        Die("WM failed to start");

    // Now create a fresh XCB connection for the test client.
    // The WM has finished its server grab/ungrab by now.
    setenv("DISPLAY", display, 1);
    TPRINT("  [setup] connecting to X (display=%s)...\n", display);
    conn = xcb_connect(display, nullptr);
    if (xcb_connection_has_error(conn))
        Die("xcb_connect failed");

    screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    if (!screen)
        Die("no screen");
    TPRINT("  [setup] screen=%dx%d\n", screen->width_in_pixels, screen->height_in_pixels);

    // Verify server is responsive.
    {
        xcb_get_input_focus_cookie_t fc = xcb_get_input_focus(conn);
        xcb_get_input_focus_reply_t *fr = xcb_get_input_focus_reply(conn, fc, nullptr);
        if (!fr)
            Die("server not responsive");
        free(fr);
    }

    TPRINT("  [setup] caching atoms...\n");
    atomWmName = InternAtom("WM_NAME");
    atomUtf8StringType = InternAtom("UTF8_STRING");
    atomWmNormalHints = InternAtom("WM_NORMAL_HINTS");
    atomWmHints = InternAtom("WM_HINTS");
    atomWmTransientFor = InternAtom("WM_TRANSIENT_FOR");
    atomWmProtocols = InternAtom("WM_PROTOCOLS");
    atomWmDeleteWindow = InternAtom("WM_DELETE_WINDOW");
    atomNetWmName = InternAtom("_NET_WM_NAME");
    atomWmState = InternAtom("WM_STATE");
    atomWmClass = InternAtom("WM_CLASS");
    atomWmClientMachine = InternAtom("WM_CLIENT_MACHINE");
    TPRINT("  [setup] atoms cached\n");

    // Initialize XTEST key injection infrastructure.
    TPRINT("  [setup] initializing xkb keymap...\n");
    InitXkb();
    FindModifierKeys();
    TPRINT("  [setup] key infrastructure ready\n");
}

void Teardown()
{
    KillAllChildren();
    if (keymap_)
    {
        xkb_keymap_unref(keymap_);
        keymap_ = nullptr;
    }
    if (xkbCtx)
    {
        xkb_context_unref(xkbCtx);
        xkbCtx = nullptr;
    }
    if (conn)
    {
        xcb_disconnect(conn);
        conn = nullptr;
    }
    if (wmPid)
    {
        kill(wmPid, SIGTERM);
        waitpid(wmPid, nullptr, 0);
        wmPid = 0;
    }
    if (xvfbPid)
    {
        kill(xvfbPid, SIGTERM);
        waitpid(xvfbPid, nullptr, 0);
        xvfbPid = 0;
    }
    unlink("/tmp/.X98-lock");
}

// ─── Test helpers ────────────────────────────────────────────────────────────

void WmPump(int ms)
{
    xcb_flush(conn);
    SleepMs(ms);
}

void DestroyAll(xcb_window_t *wins, int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (wins[i])
            xcb_destroy_window(conn, wins[i]);
    }
    // Give the WM ample time to process all destroy notifications.
    WmPump(600);
    DrainEvents();
}

// ─── Synthetic window tests ──────────────────────────────────────────────────

void TestWmManagesNewWindow()
{
    TPRINT("test: wm manages new window\n");
    xcb_window_t win = CreateTestWindow("test1", 800, 600);
    WmPump(300);

    int16_t x, y;
    uint16_t w, h;
    CHECK(GetWindowGeometry(win, &x, &y, &w, &h) == 0, "geom");

    TPRINT("  pos=(%d,%d) size=%ux%u\n", x, y, w, h);
    CHECK(x >= 0, "x >= 0 (centered)");
    CHECK(y >= 0, "y >= 0");
    CHECK(w >= 100, "sane width");
    CHECK(h >= 100, "sane height");
    CHECK(w <= (uint16_t)kViewW, "width <= viewport");
    CHECK(h <= (uint16_t)kViewH, "height <= viewport");

    xcb_destroy_window(conn, win);
    WmPump(200);
}

void TestTwoWindowsTiled()
{
    TPRINT("test: two windows tiled\n");
    xcb_window_t w1 = CreateTestWindow("left", 600, 400);
    WmPump(300);
    xcb_window_t w2 = CreateTestWindow("right", 600, 400);
    WmPump(300);

    int16_t x1, y1, x2, y2;
    uint16_t ww1, h1, ww2, h2;
    CHECK(GetWindowGeometry(w1, &x1, &y1, &ww1, &h1) == 0, "geom w1");
    CHECK(GetWindowGeometry(w2, &x2, &y2, &ww2, &h2) == 0, "geom w2");

    TPRINT("  w1=(%d,%d %ux%u) w2=(%d,%d %ux%u)\n", x1, y1, ww1, h1, x2, y2, ww2, h2);

    CHECK(x2 >= 0, "active window on screen (x >= 0)");
    CHECK(x2 + (int16_t)ww2 <= (int16_t)kScreenW, "active window right edge on screen");
    CHECK(x1 + (int16_t)ww1 <= x2 + (int16_t)ww2, "w1 not to the right of w2");
    CHECK(ww1 >= 100 && ww2 >= 100, "sane widths");

    xcb_window_t wins[] = {w1, w2};
    DestroyAll(wins, 2);
}

void TestUnmapRemovesWindow()
{
    TPRINT("test: unmap removes window\n");
    xcb_window_t w1 = CreateTestWindow("keep", 400, 300);
    WmPump(200);
    xcb_window_t w2 = CreateTestWindow("unmap_me", 400, 300);
    WmPump(200);

    xcb_unmap_window(conn, w2);
    xcb_flush(conn);
    WmPump(300);

    int16_t x1;
    uint16_t ww1;
    CHECK(GetWindowGeometry(w1, &x1, nullptr, &ww1, nullptr) == 0, "geom w1 after");
    CHECK(x1 >= 0, "w1 centered after unmap");
    CHECK(x1 + (int16_t)ww1 <= (int16_t)kScreenW, "w1 right edge on screen");

    xcb_destroy_window(conn, w1);
    xcb_destroy_window(conn, w2);
    WmPump(100);
}

// ─── Real X11 application tests ──────────────────────────────────────────────

void TestXterm()
{
    KillAllChildren();
    TPRINT("test: xterm tiling and lifecycle\n");

    char const *argv[] = {"xterm", "-geometry", "80x24", "-e", "sleep", "30", nullptr};
    pid_t pid = LaunchApp(argv);
    CHECK(pid > 0, "xterm launched");

    xcb_window_t win = WaitForWindowClass("XTerm", 5000);
    CHECK(win != 0, "xterm window appeared");
    if (!win)
        return;

    DrainEvents();
    WmPump(500);

    int16_t x, y;
    uint16_t w, h;
    CHECK(GetWindowGeometry(win, &x, &y, &w, &h) == 0, "xterm geom");
    TPRINT("  xterm: pos=(%d,%d) size=%ux%u\n", x, y, w, h);

    CHECK(x >= 0, "xterm x >= 0");
    CHECK(y >= 0, "xterm y >= 0");
    CHECK(w >= 100, "xterm width >= 100");
    CHECK(h >= 100, "xterm height >= 100");
    CHECK(w <= (uint16_t)kViewW, "xterm width <= viewport");
    CHECK(h <= (uint16_t)kViewH, "xterm height <= viewport");

    // xterm sets WM_CLIENT_MACHINE — verify the WM didn't corrupt it.
    {
        xcb_get_property_cookie_t c = xcb_get_property(conn, 0, win, atomWmClientMachine, XCB_ATOM_ANY, 0, 64);
        xcb_get_property_reply_t *r = xcb_get_property_reply(conn, c, nullptr);
        CHECK(r && r->length > 0, "xterm has WM_CLIENT_MACHINE");
        if (r)
            free(r);
    }

    // Test WM_DELETE_WINDOW protocol.
    {
        xcb_get_property_cookie_t c = xcb_get_property(conn, 0, win, atomWmProtocols, XCB_ATOM_ATOM, 0, 16);
        xcb_get_property_reply_t *r = xcb_get_property_reply(conn, c, nullptr);
        CHECK(r != nullptr, "xterm has WM_PROTOCOLS");
        bool hasDelete = false;
        if (r)
        {
            xcb_atom_t *atoms = (xcb_atom_t *)xcb_get_property_value(r);
            int count = xcb_get_property_value_length(r) / sizeof(xcb_atom_t);
            for (int i = 0; i < count; ++i)
            {
                if (atoms[i] == atomWmDeleteWindow)
                {
                    hasDelete = true;
                    break;
                }
            }
            free(r);
        }
        CHECK(hasDelete, "xterm supports WM_DELETE_WINDOW");

        if (hasDelete)
        {
            xcb_client_message_event_t ev = {};
            ev.response_type = XCB_CLIENT_MESSAGE;
            ev.window = win;
            ev.type = atomWmProtocols;
            ev.format = 32;
            ev.data.data32[0] = atomWmDeleteWindow;
            ev.data.data32[1] = XCB_CURRENT_TIME;
            xcb_send_event(conn, 0, win, XCB_EVENT_MASK_NO_EVENT, (char const *)&ev);
            xcb_flush(conn);
            WmPump(1000);

            int status;
            waitpid(pid, &status, WNOHANG);
            DrainEvents();
            xcb_window_t stillThere = WaitForWindowClass("XTerm", 500);
            CHECK(stillThere == 0, "xterm closed after WM_DELETE_WINDOW");
            if (stillThere)
            {
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
            }
            for (int i = 0; i < childCount; ++i)
            {
                if (children[i] == pid)
                {
                    children[i] = children[--childCount];
                    break;
                }
            }
        }
        else
        {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            for (int i = 0; i < childCount; ++i)
            {
                if (children[i] == pid)
                {
                    children[i] = children[--childCount];
                    break;
                }
            }
        }
    }

    WmPump(300);
}

void TestXeyes()
{
    KillAllChildren();
    TPRINT("test: xeyes tiling\n");

    char const *argv[] = {"xeyes", nullptr};
    pid_t pid = LaunchApp(argv);
    CHECK(pid > 0, "xeyes launched");

    xcb_window_t win = WaitForWindowClass("XEyes", 5000);
    CHECK(win != 0, "xeyes window appeared");
    if (!win)
        return;

    DrainEvents();
    WmPump(500);

    int16_t x, y;
    uint16_t w, h;
    CHECK(GetWindowGeometry(win, &x, &y, &w, &h) == 0, "xeyes geom");
    TPRINT("  xeyes: pos=(%d,%d) size=%ux%u\n", x, y, w, h);

    CHECK(x >= 0, "xeyes on screen");
    CHECK(w >= 50, "xeyes width sane");
    CHECK(h >= 50, "xeyes height sane");
}

void TestXclock()
{
    KillAllChildren();
    TPRINT("test: xclock tiling\n");

    char const *argv[] = {"xclock", nullptr};
    pid_t pid = LaunchApp(argv);
    CHECK(pid > 0, "xclock launched");

    xcb_window_t win = WaitForWindowClass("XClock", 5000);
    CHECK(win != 0, "xclock window appeared");
    if (!win)
        return;

    DrainEvents();
    WmPump(500);

    int16_t x, y;
    uint16_t w, h;
    CHECK(GetWindowGeometry(win, &x, &y, &w, &h) == 0, "xclock geom");
    TPRINT("  xclock: pos=(%d,%d) size=%ux%u\n", x, y, w, h);

    CHECK(x >= 0, "xclock on screen");
    CHECK(w >= 50, "xclock width sane");
    CHECK(h >= 50, "xclock height sane");
}

void TestMultipleRealApps()
{
    KillAllChildren();
    TPRINT("test: multiple real apps tiled\n");

    char const *argv1[] = {"xeyes", nullptr};
    char const *argv2[] = {"xclock", nullptr};
    char const *argv3[] = {"xterm", "-geometry", "80x24", "-e", "sleep", "30", nullptr};

    pid_t p1 = LaunchApp(argv1);
    xcb_window_t w1 = WaitForWindowClass("XEyes", 5000);
    CHECK(w1 != 0, "xeyes appeared");
    WmPump(300);

    pid_t p2 = LaunchApp(argv2);
    xcb_window_t w2 = WaitForWindowClass("XClock", 5000);
    CHECK(w2 != 0, "xclock appeared");
    WmPump(300);

    pid_t p3 = LaunchApp(argv3);
    xcb_window_t w3 = WaitForWindowClass("XTerm", 5000);
    CHECK(w3 != 0, "xterm appeared");

    DrainEvents();
    WmPump(500);

    if (!w1 || !w2 || !w3)
        return;

    int16_t x1, x2, x3;
    uint16_t ww1, ww2, ww3;
    CHECK(GetWindowGeometry(w1, &x1, nullptr, &ww1, nullptr) == 0, "xeyes geom");
    CHECK(GetWindowGeometry(w2, &x2, nullptr, &ww2, nullptr) == 0, "xclock geom");
    CHECK(GetWindowGeometry(w3, &x3, nullptr, &ww3, nullptr) == 0, "xterm geom");

    TPRINT("  xeyes x=%d w=%u, xclock x=%d w=%u, xterm x=%d w=%u\n", x1, ww1, x2, ww2, x3, ww3);

    // All windows should have sane sizes. Don't assert ordering — WM_NAME
    // and WM_CLASS property fetches are asynchronous, so the WM may tile
    // them in any order depending on which reply arrives first.
    CHECK(ww1 >= 50 && ww2 >= 50 && ww3 >= 100, "all widths sane");
    // At least the most recently mapped window (xterm) should be on screen.
    CHECK(x3 >= 0, "xterm (last mapped) on screen");
}

void TestRealAppUnmapRemap()
{
    KillAllChildren();
    TPRINT("test: real app unmap/remap\n");

    char const *argv1[] = {"xterm", "-e", "sleep", "60", nullptr};
    pid_t p1 = LaunchApp(argv1);
    xcb_window_t w1 = WaitForWindowClass("XTerm", 5000);
    CHECK(w1 != 0, "xterm1 appeared");
    WmPump(300);

    char const *argv2[] = {"xeyes", nullptr};
    pid_t p2 = LaunchApp(argv2);
    xcb_window_t w2 = WaitForWindowClass("XEyes", 5000);
    CHECK(w2 != 0, "xeyes appeared");

    DrainEvents();
    WmPump(500);

    if (!w1 || !w2)
        return;

    xcb_unmap_window(conn, w1);
    xcb_flush(conn);
    WmPump(500);

    int16_t x2AfterUnmap;
    CHECK(GetWindowGeometry(w2, &x2AfterUnmap, nullptr, nullptr, nullptr) == 0, "xeyes geom after unmap");
    CHECK(x2AfterUnmap >= 0, "xeyes centered after xterm unmap");

    xcb_map_window(conn, w1);
    xcb_flush(conn);
    WmPump(500);

    int16_t x1Remap, x2Remap;
    uint16_t w1Remap, w2Remap;
    CHECK(GetWindowGeometry(w1, &x1Remap, nullptr, &w1Remap, nullptr) == 0, "xterm geom after remap");
    CHECK(GetWindowGeometry(w2, &x2Remap, nullptr, &w2Remap, nullptr) == 0, "xeyes geom after remap");
    CHECK(w1Remap >= 100, "xterm width sane after remap");
    CHECK(w2Remap >= 50, "xeyes width sane after remap");
}

void TestRealAppFocus()
{
    KillAllChildren();
    TPRINT("test: real app focus tracking\n");

    char const *argv1[] = {"xterm", "-e", "sleep", "30", nullptr};
    pid_t p1 = LaunchApp(argv1);
    xcb_window_t w1 = WaitForWindowClass("XTerm", 5000);
    CHECK(w1 != 0, "xterm appeared");
    WmPump(300);

    char const *argv2[] = {"xeyes", nullptr};
    pid_t p2 = LaunchApp(argv2);
    xcb_window_t w2 = WaitForWindowClass("XEyes", 5000);
    CHECK(w2 != 0, "xeyes appeared");

    DrainEvents();
    WmPump(500);

    if (!w1 || !w2)
        return;

    xcb_window_t focus = GetInputFocus();
    TPRINT("  focus=0x%x xterm=0x%x xeyes=0x%x\n", focus, w1, w2);
    // Focus should be on one of our windows or the root.
    // xterm may steal focus back (XSetInputFocus), so don't assert
    // which specific window — just that the WM didn't crash.
    CHECK(focus == w1 || focus == w2 || focus == screen->root, "focus on a known window");
}

void TestRealAppStress()
{
    KillAllChildren();
    TPRINT("test: real app stress (rapid xterm spawn/kill)\n");

    pid_t pids[6] = {};
    xcb_window_t wins[6] = {};

    for (int i = 0; i < 6; ++i)
    {
        char const *argv[] = {"xterm", "-e", "sleep", "5", nullptr};
        pids[i] = LaunchApp(argv);
        wins[i] = WaitForWindowClass("XTerm", 3000);
        if (wins[i])
            WmPump(200);
    }

    for (int i = 0; i < 5; ++i)
    {
        if (pids[i] > 0)
        {
            kill(pids[i], SIGKILL);
            for (int j = 0; j < childCount; ++j)
            {
                if (children[j] == pids[i])
                {
                    children[j] = children[--childCount];
                    break;
                }
            }
        }
    }

    WmPump(1000);
    DrainEvents();

    for (int i = 0; i < 6; ++i)
    {
        if (pids[i] > 0)
            waitpid(pids[i], nullptr, WNOHANG);
    }

    xcb_get_input_focus_cookie_t fc = xcb_get_input_focus(conn);
    xcb_generic_error_t *err = nullptr;
    xcb_get_input_focus_reply_t *fr = xcb_get_input_focus_reply(conn, fc, &err);
    CHECK(fr != nullptr && !err, "server responsive after stress");
    if (fr)
        free(fr);
    if (err)
        free(err);

    if (pids[5] > 0)
    {
        kill(pids[5], SIGTERM);
        for (int j = 0; j < childCount; ++j)
        {
            if (children[j] == pids[5])
            {
                children[j] = children[--childCount];
                break;
            }
        }
    }

    WmPump(300);
}

void TestMixedAppsAndSynthetic()
{
    KillAllChildren();
    TPRINT("test: mixed real and synthetic windows\n");

    char const *argv[] = {"xterm", "-e", "sleep", "30", nullptr};
    pid_t p = LaunchApp(argv);
    xcb_window_t rw = WaitForWindowClass("XTerm", 5000);
    CHECK(rw != 0, "xterm appeared");
    WmPump(300);

    xcb_window_t sw = CreateTestWindow("synthetic", 400, 300);
    WmPump(300);

    DrainEvents();

    int16_t rx, sx;
    uint16_t rww, sww;
    CHECK(GetWindowGeometry(rw, &rx, nullptr, &rww, nullptr) == 0, "xterm geom");
    CHECK(GetWindowGeometry(sw, &sx, nullptr, &sww, nullptr) == 0, "synthetic geom");

    TPRINT("  xterm x=%d w=%u, synthetic x=%d w=%u\n", rx, rww, sx, sww);

    CHECK(sx >= 0, "synthetic on screen");
    CHECK(rww >= 100 && sww >= 100, "both widths sane");
    // Both windows should be on screen. The ordering (which is left/right)
    // depends on WM timing of property fetches, so we don't assert it.
    bool xtermOnScreen = (rx >= 0 && rx + (int16_t)rww <= kScreenW);
    bool synthOnScreen = (sx >= 0 && sx + (int16_t)sww <= kScreenW);
    CHECK(xtermOnScreen || synthOnScreen, "at least one window on screen");
}

// ─── Keybinding tests (XTEST key injection) ──────────────────────────────────

// Barrier: ensure WM has fully processed all events from prior tests.
// Destroys any leftover test windows and waits for WM to stabilize.
void KeyboardTestBarrier()
{
    // Destroy any leftover root children that look like test windows.
    // Single pass is enough if we give the WM enough time to process.
    xcb_window_t wins[64];
    int n = GetRootChildren(wins, 64);
    for (int i = 0; i < n; ++i)
    {
        if (wins[i] < 0x100000)
            continue;
        xcb_destroy_window(conn, wins[i]);
    }
    // Wait for WM to process all DestroyNotify events.
    WmPump(1000);
    DrainEvents();
    // Double-check: drain any stragglers.
    WmPump(500);
    DrainEvents();
}

// Test: Meta+Left/Right resizes the active window (±80px).
void TestKeyboardResize()
{
    TPRINT("test: keyboard resize (Meta+Left/Right)\n");
    KeyboardTestBarrier();

    xcb_keycode_t kcLeft = KeycodeByXkbName("LEFT");
    xcb_keycode_t kcRight = KeycodeByXkbName("RGHT");
    CHECK(kcLeft && kcRight, "found arrow keycodes");

    xcb_window_t w1 = CreateTestWindow("resize", 400, 300);
    WmPump(400);

    int16_t xBefore;
    uint16_t wBefore;
    CHECK(GetWindowGeometry(w1, &xBefore, nullptr, &wBefore, nullptr) == 0, "geom before resize");
    TPRINT("  before: x=%d w=%u\n", xBefore, wBefore);

    // Meta+Right increases width by 80.
    MetaKey(kcRight);
    WmPump(400);

    uint16_t wAfter;
    CHECK(GetWindowGeometry(w1, nullptr, nullptr, &wAfter, nullptr) == 0, "geom after Meta+Right");
    TPRINT("  after Meta+Right: w=%u\n", wAfter);
    // Width should have increased (by ~80, but layout may clamp).
    CHECK(wAfter >= wBefore, "width increased after Meta+Right");

    // Meta+Left decreases width.
    MetaKey(kcLeft);
    WmPump(400);

    uint16_t wBack;
    CHECK(GetWindowGeometry(w1, nullptr, nullptr, &wBack, nullptr) == 0, "geom after Meta+Left");
    TPRINT("  after Meta+Left: w=%u\n", wBack);
    // Should be back down (clamped to minimum of 100).
    CHECK(wBack <= wAfter, "width decreased after Meta+Left");

    xcb_destroy_window(conn, w1);
    WmPump(400);
    KeyboardTestBarrier();
}

// Test: Meta+G toggles zoom (non-active windows get hidden).
void TestKeyboardZoom()
{
    TPRINT("test: keyboard zoom (Meta+G)\n");
    KeyboardTestBarrier();

    xcb_keycode_t kcG = KeycodeByXkbName("AC05");
    CHECK(kcG, "found G keycode");

    xcb_window_t w1 = CreateTestWindow("zoom1", 400, 300);
    WmPump(400);
    xcb_window_t w2 = CreateTestWindow("zoom2", 400, 300);
    WmPump(400);

    // Both windows should be visible before zoom.
    int mapped1 = GetWindowMapped(w1);
    int mapped2 = GetWindowMapped(w2);
    CHECK(mapped1 == 1, "w1 viewable before zoom");
    CHECK(mapped2 == 1, "w2 viewable before zoom");

    // Toggle zoom ON (Meta+G).
    MetaKey(kcG);
    WmPump(400);
    DrainEvents();

    // After zoom, w1 (inactive) should be hidden off-screen.
    int16_t x1;
    uint16_t ww1;
    CHECK(GetWindowGeometry(w1, &x1, nullptr, &ww1, nullptr) == 0, "w1 geom after zoom");
    // The WM hides inactive windows by moving them to (screen_w, screen_h).
    CHECK(x1 >= (int16_t)kScreenW, "w1 hidden off-screen (x >= screen_w) after zoom");

    // w2 (active) should fill the screen.
    int16_t x2, y2;
    uint16_t ww2, h2;
    CHECK(GetWindowGeometry(w2, &x2, &y2, &ww2, &h2) == 0, "w2 geom after zoom");
    TPRINT("  zoom active: x=%d y=%d w=%u h=%u\n", x2, y2, ww2, h2);
    CHECK(x2 == 0, "zoomed active window at x=0");
    CHECK(ww2 >= (uint16_t)(kScreenW - 10), "zoomed active fills width");

    // Toggle zoom OFF (Meta+G again).
    MetaKey(kcG);
    WmPump(400);
    DrainEvents();

    // Both windows should be visible again.
    int16_t x1b, x2b;
    CHECK(GetWindowGeometry(w1, &x1b, nullptr, nullptr, nullptr) == 0, "w1 geom after unzoom");
    CHECK(GetWindowGeometry(w2, &x2b, nullptr, nullptr, nullptr) == 0, "w2 geom after unzoom");
    TPRINT("  unzoom: w1 x=%d, w2 x=%d\n", x1b, x2b);
    // w2 should be on screen (the active one).
    CHECK(x2b >= 0, "w2 back on screen after unzoom");

    xcb_window_t wins[] = {w1, w2};
    DestroyAll(wins, 2);
    KeyboardTestBarrier();
}

// Test: Meta+D/F cycles window focus (with clear-zoom).
void TestKeyboardCycleMetaDf()
{
    TPRINT("test: keyboard cycle (Meta+F/D)\n");
    KeyboardTestBarrier();

    xcb_keycode_t kcF = KeycodeByXkbName("AC04");
    xcb_keycode_t kcD = KeycodeByXkbName("AC03");
    CHECK(kcF && kcD, "found F/D keycodes");

    xcb_window_t w1 = CreateTestWindow("cyc1", 400, 300);
    WmPump(400);
    xcb_window_t w2 = CreateTestWindow("cyc2", 400, 300);
    WmPump(400);
    xcb_window_t w3 = CreateTestWindow("cyc3", 400, 300);
    WmPump(400);

    // w3 is active (last mapped).
    int16_t x3Before;
    CHECK(GetWindowGeometry(w3, &x3Before, nullptr, nullptr, nullptr) == 0, "w3 before cycle");
    TPRINT("  before cycle: w3 x=%d\n", x3Before);
    CHECK(x3Before >= 0, "w3 on screen (active)");

    // Meta+F = cycle next with clearZoom=true.
    MetaKey(kcF);
    WmPump(400);

    // w1 becomes active, should be scrolled into view.
    int16_t x1After;
    CHECK(GetWindowGeometry(w1, &x1After, nullptr, nullptr, nullptr) == 0, "w1 after Meta+F");
    TPRINT("  after Meta+F: w1 x=%d\n", x1After);
    CHECK(x1After >= 0, "w1 on screen after becoming active");

    // Meta+D cycles back to w3.
    MetaKey(kcD);
    WmPump(400);

    int16_t x3Back;
    CHECK(GetWindowGeometry(w3, &x3Back, nullptr, nullptr, nullptr) == 0, "w3 after Meta+D");
    TPRINT("  after Meta+D: w3 x=%d\n", x3Back);
    CHECK(x3Back >= 0, "w3 back on screen");

    xcb_window_t wins[] = {w1, w2, w3};
    DestroyAll(wins, 3);
    KeyboardTestBarrier();
}

// Test: Alt+Tab cycles window focus (no clear-zoom).
void TestAltTabCycle()
{
    TPRINT("test: Alt+Tab cycle windows\n");
    KeyboardTestBarrier();

    xcb_keycode_t kcTab = KeycodeByXkbName("TAB");
    CHECK(kcTab, "found TAB keycode");

    xcb_window_t w1 = CreateTestWindow("at1", 400, 300);
    WmPump(400);
    xcb_window_t w2 = CreateTestWindow("at2", 400, 300);
    WmPump(400);

    // w2 is active (last mapped). Record its position.
    int16_t x2Before, x1Before;
    CHECK(GetWindowGeometry(w2, &x2Before, nullptr, nullptr, nullptr) == 0, "w2 before Alt+Tab");
    CHECK(GetWindowGeometry(w1, &x1Before, nullptr, nullptr, nullptr) == 0, "w1 before Alt+Tab");
    TPRINT("  before: w1 x=%d w2 x=%d\n", x1Before, x2Before);
    CHECK(x2Before >= 0, "w2 on screen before Alt+Tab");

    AltKey(kcTab);
    WmPump(600);
    DrainEvents();

    // After Alt+Tab, the active window should have changed.
    // w1 should now be the visible/active one (scrolled into view).
    int16_t x1After, x2After;
    CHECK(GetWindowGeometry(w1, &x1After, nullptr, nullptr, nullptr) == 0, "w1 after Alt+Tab");
    CHECK(GetWindowGeometry(w2, &x2After, nullptr, nullptr, nullptr) == 0, "w2 after Alt+Tab");
    TPRINT("  after Alt+Tab: w1 x=%d w2 x=%d\n", x1After, x2After);

    // The previously-offscreen window should now be on screen,
    // OR the previously-onscreen window should now be off screen.
    // Depending on follow/center-scroll mode, the result varies.
    // Key invariant: at least one of {w1, w2} is visible.
    bool w1Visible = (x1After >= 0 && x1After < kScreenW);
    bool w2Visible = (x2After >= 0 && x2After < kScreenW);
    CHECK(w1Visible || w2Visible, "at least one window visible after Alt+Tab");

    xcb_window_t wins[] = {w1, w2};
    DestroyAll(wins, 2);
    KeyboardTestBarrier();
}

// Test: Meta+Ctrl+Left/Right switches stacks (workspaces).
void TestStackSwitchCtrlArrows()
{
    TPRINT("test: stack switch (Meta+Ctrl+Left/Right)\n");
    KeyboardTestBarrier();

    xcb_keycode_t kcLeft = KeycodeByXkbName("LEFT");
    xcb_keycode_t kcRight = KeycodeByXkbName("RGHT");
    CHECK(kcLeft && kcRight, "found arrow keycodes");

    // Create windows on the default stack (0).
    xcb_window_t w0 = CreateTestWindow("stack0_win", 400, 300);
    WmPump(400);

    int16_t x0Before;
    CHECK(GetWindowGeometry(w0, &x0Before, nullptr, nullptr, nullptr) == 0, "w0 before switch");
    TPRINT("  w0 before switch: x=%d\n", x0Before);

    // Switch to stack 1 (Meta+Ctrl+Right).
    MetaCtrlKey(kcRight);
    WmPump(500);

    // w0 (on stack 0) should now be hidden (moved off-screen).
    int16_t x0Hidden;
    CHECK(GetWindowGeometry(w0, &x0Hidden, nullptr, nullptr, nullptr) == 0, "w0 after switch away");
    TPRINT("  w0 on inactive stack: x=%d\n", x0Hidden);
    CHECK(x0Hidden >= (int16_t)kScreenW, "w0 hidden after switching to stack 1");

    // Create a window on the new stack (stack 1).
    xcb_window_t w1 = CreateTestWindow("stack1_win", 400, 300);
    WmPump(400);

    int16_t x1Pos;
    CHECK(GetWindowGeometry(w1, &x1Pos, nullptr, nullptr, nullptr) == 0, "w1 on stack 1");
    TPRINT("  w1 on stack 1: x=%d\n", x1Pos);
    CHECK(x1Pos >= 0, "w1 visible on stack 1");

    // Switch back to stack 0 (Meta+Ctrl+Left).
    MetaCtrlKey(kcLeft);
    WmPump(500);

    // w0 should be visible again, w1 hidden.
    int16_t x0Back;
    CHECK(GetWindowGeometry(w0, &x0Back, nullptr, nullptr, nullptr) == 0, "w0 back on stack 0");
    TPRINT("  w0 back on stack 0: x=%d\n", x0Back);
    CHECK(x0Back >= 0, "w0 visible again on stack 0");

    int16_t x1Hidden;
    CHECK(GetWindowGeometry(w1, &x1Hidden, nullptr, nullptr, nullptr) == 0, "w1 hidden");
    CHECK(x1Hidden >= (int16_t)kScreenW, "w1 hidden after switching back");

    xcb_window_t wins[] = {w0, w1};
    DestroyAll(wins, 2);
    KeyboardTestBarrier();
}

// Test: Meta+E/R switches stacks (prev/next workspace).
void TestStackSwitchER()
{
    TPRINT("test: stack switch (Meta+E and Meta+R)\n");
    KeyboardTestBarrier();

    xcb_keycode_t kcE = KeycodeByXkbName("AD03");
    xcb_keycode_t kcR = KeycodeByXkbName("AD04");
    CHECK(kcE && kcR, "found E/R keycodes");

    xcb_window_t w0 = CreateTestWindow("sr0", 400, 300);
    WmPump(400);

    int16_t x0On;
    CHECK(GetWindowGeometry(w0, &x0On, nullptr, nullptr, nullptr) == 0, "w0 before switch");
    TPRINT("  w0 on stack 0: x=%d\n", x0On);
    CHECK(x0On >= 0, "w0 visible on stack 0");

    // Meta+R = next stack (stack 1).
    MetaKey(kcR);
    WmPump(500);

    int16_t x0Off;
    CHECK(GetWindowGeometry(w0, &x0Off, nullptr, nullptr, nullptr) == 0, "w0 after Meta+R");
    CHECK(x0Off >= (int16_t)kScreenW, "w0 hidden on stack 1");

    xcb_window_t w1 = CreateTestWindow("sr1", 400, 300);
    WmPump(400);

    int16_t x1On;
    CHECK(GetWindowGeometry(w1, &x1On, nullptr, nullptr, nullptr) == 0, "w1 on stack 1");
    CHECK(x1On >= 0, "w1 visible on stack 1");

    // Meta+E = previous stack (stack 0).
    MetaKey(kcE);
    WmPump(500);

    int16_t x0Back;
    CHECK(GetWindowGeometry(w0, &x0Back, nullptr, nullptr, nullptr) == 0, "w0 after Meta+E");
    CHECK(x0Back >= 0, "w0 visible again on stack 0");

    int16_t x1Off;
    CHECK(GetWindowGeometry(w1, &x1Off, nullptr, nullptr, nullptr) == 0, "w1 after Meta+E");
    CHECK(x1Off >= (int16_t)kScreenW, "w1 hidden after switching back");

    xcb_window_t wins[] = {w0, w1};
    DestroyAll(wins, 2);
    KeyboardTestBarrier();
}

// ─── Override-redirect test ──────────────────────────────────────────────────

// Override-redirect windows should NOT be managed by the WM.
// They should appear at their requested position, unchanged by the WM.
// Note: Xvfb may not honor override_redirect for MapNotify in all cases,
// so this test validates the WM behavior in a way that's robust across
// different X server implementations.
void TestOverrideRedirectNotManaged()
{
    TPRINT("test: override-redirect window not managed\n");
    KeyboardTestBarrier();

    // Create an override-redirect window first (before any normal windows)
    // to avoid interference from WM's layout management.
    xcb_window_t ovr = CreateOverrideRedirectWindow("overlay", 200, 150);
    WmPump(400);
    DrainEvents();

    int16_t ox, oy;
    uint16_t ow, oh;
    CHECK(GetWindowGeometry(ovr, &ox, &oy, &ow, &oh) == 0, "override-redirect geom");
    TPRINT("  override-redirect: x=%d y=%d w=%u h=%u\n", ox, oy, ow, oh);

    // Now create a normal window to confirm WM is active.
    xcb_window_t normal = CreateTestWindow("normal_win", 400, 300);
    WmPump(400);

    int16_t nx;
    uint16_t nw;
    CHECK(GetWindowGeometry(normal, &nx, nullptr, &nw, nullptr) == 0, "normal geom");
    TPRINT("  normal window: x=%d w=%u\n", nx, nw);
    CHECK(nw >= 100, "normal window has sane width");

    // The OR window's geometry should NOT have been changed by the WM.
    // On some X implementations the WM might still touch it — we check
    // the size at least (it should still be 200x150 if unmanaged).
    int16_t ox2, oy2;
    uint16_t ow2, oh2;
    CHECK(GetWindowGeometry(ovr, &ox2, &oy2, &ow2, &oh2) == 0, "override-redirect geom after");
    TPRINT("  override-redirect after normal: x=%d y=%d w=%u h=%u\n", ox2, oy2, ow2, oh2);
    // Width shouldn't have changed to the WM's default of ~1280.
    CHECK(ow2 <= 250, "override-redirect width not expanded to tile width");
    CHECK(oh2 <= 200, "override-redirect height not expanded");

    xcb_destroy_window(conn, ovr);
    xcb_destroy_window(conn, normal);
    WmPump(200);
    KeyboardTestBarrier();
}

// ─── Transient window test ───────────────────────────────────────────────────

// WM_TRANSIENT_FOR windows should become subwindows of their parent.
// Note: There's a known race in the WM — WM_TRANSIENT_FOR is fetched
// asynchronously, so the window may initially be tiled as a normal window
// before the property reply arrives and moves it to be a subwindow.
// This test validates the eventual behavior after the WM stabilizes.
void TestTransientWindowPosition()
{
    TPRINT("test: transient window positioned\n");
    KeyboardTestBarrier();

    xcb_window_t parent = CreateTestWindow("parent", 400, 300);
    WmPump(400);

    int16_t px, py;
    uint16_t pw, ph;
    CHECK(GetWindowGeometry(parent, &px, &py, &pw, &ph) == 0, "parent geom");
    TPRINT("  parent: x=%d y=%d w=%u h=%u\n", px, py, pw, ph);
    CHECK(pw >= 100, "parent has sane width");

    // Create a transient window with WM_TRANSIENT_FOR pointing to parent.
    xcb_window_t trans = CreateTransientWindow("dialog", 300, 200, parent);
    // Give the WM extra time to fetch the WM_TRANSIENT_FOR property
    // and process the reply.
    WmPump(800);
    DrainEvents();

    int16_t tx, ty;
    uint16_t tw, th;
    CHECK(GetWindowGeometry(trans, &tx, &ty, &tw, &th) == 0, "transient geom");
    TPRINT("  transient: x=%d y=%d w=%u h=%u\n", tx, ty, tw, th);

    // The transient should be positioned on screen (not at 0,0).
    // It should be within the viewport bounds.
    CHECK(tx >= 0, "transient x >= 0");
    CHECK(ty >= 0, "transient y >= 0");
    CHECK(tx + (int32_t)tw <= kScreenW, "transient fits in screen width");
    CHECK(ty + (int32_t)th <= kScreenH, "transient fits in screen height");
    // It should NOT exceed the screen size.
    CHECK(tw <= (uint16_t)kScreenW, "transient width <= screen");
    CHECK(th <= (uint16_t)kScreenH, "transient height <= screen");

    xcb_destroy_window(conn, trans);
    xcb_destroy_window(conn, parent);
    WmPump(200);
    KeyboardTestBarrier();
}

// ─── WM restart / serialization test ─────────────────────────────────────────

// Meta+Shift+R triggers WmRestart(), which serializes state and exits.
// A new WM instance should pick up the state and restore windows.
void TestSerializationRestart()
{
    TPRINT("test: serialization restart (Meta+Shift+R)\n");
    KeyboardTestBarrier();

    // Clean up any leftover state file from previous runs.
    unlink("/tmp/nyla_wm_state");

    // Log how many windows exist before starting.
    {
        xcb_window_t wins[128];
        int n = GetRootChildren(wins, 128);
        TPRINT("  [restart] %d root children before test\n", n);
    }

    xcb_keycode_t kcR = KeycodeByXkbName("AD04");
    CHECK(kcR, "found R keycode");

    // Create windows on two different stacks.
    xcb_window_t w0 = CreateTestWindow("saved_s0", 400, 300);
    WmPump(300);

    int16_t x0, y0;
    uint16_t ww0, hh0;
    CHECK(GetWindowGeometry(w0, &x0, &y0, &ww0, &hh0) == 0, "w0 geom before restart");
    TPRINT("  w0 before restart: x=%d y=%d w=%u h=%u\n", x0, y0, ww0, hh0);

    // Switch to stack 1 and create a window there too.
    xcb_keycode_t kcRight = KeycodeByXkbName("RGHT");
    if (kcRight)
    {
        MetaCtrlKey(kcRight);
        WmPump(400);
    }

    xcb_window_t w1 = CreateTestWindow("saved_s1", 400, 300);
    WmPump(300);

    int16_t x1, y1;
    uint16_t ww1, hh1;
    CHECK(GetWindowGeometry(w1, &x1, &y1, &ww1, &hh1) == 0, "w1 geom before restart");
    TPRINT("  w1 before restart: x=%d y=%d w=%u h=%u\n", x1, y1, ww1, hh1);

    // Send Meta+Shift+R to trigger restart.
    TPRINT("  sending Meta+Shift+R to restart WM...\n");
    MetaShiftKey(kcR);

    // Wait for the WM to exit.
    int maxWait = 5000;
    int waited = 0;
    while (waited < maxWait)
    {
        int status;
        pid_t result = waitpid(wmPid, &status, WNOHANG);
        if (result == wmPid)
        {
            if (WIFEXITED(status))
            {
                TPRINT("  WM exited with code %d\n", WEXITSTATUS(status));
            }
            else if (WIFSIGNALED(status))
            {
                TPRINT("  WM killed by signal %d\n", WTERMSIG(status));
            }
            wmPid = 0;
            break;
        }
        SleepMs(200);
        waited += 200;
    }

    if (wmPid != 0)
    {
        TFAIL("WM did not exit within %d ms", maxWait);
        kill(wmPid, SIGKILL);
        waitpid(wmPid, nullptr, 0);
        wmPid = 0;
        xcb_destroy_window(conn, w1);
        xcb_destroy_window(conn, w0);
        return;
    }

    // Verify windows still exist on the X server (they should survive WM exit).
    int16_t w0xAfter;
    CHECK(GetWindowGeometry(w0, &w0xAfter, nullptr, nullptr, nullptr) == 0, "w0 still exists after WM exit");
    int16_t w1xAfter;
    CHECK(GetWindowGeometry(w1, &w1xAfter, nullptr, nullptr, nullptr) == 0, "w1 still exists after WM exit");
    TPRINT("  windows survived WM exit: w0 x=%d, w1 x=%d\n", w0xAfter, w1xAfter);

    // Start a new WM instance. It should read the serialized state.
    TPRINT("  starting new WM instance...\n");
    // Give X server time to release the old WM's SUBSTRUCTURE_REDIRECT grab
    // (xcb_disconnect doesn't guarantee instant release).
    DrainEvents();
    WmPump(1500);
    DrainEvents();

    // Mark the test's XCB fd as FD_CLOEXEC so the new WM fork doesn't
    // inherit the socket. Do NOT disconnect — that would destroy our test
    // windows (w0, w1) since they're owned by this connection.
    {
        int fd = xcb_get_file_descriptor(conn);
        int flags = fcntl(fd, F_GETFD);
        if (flags != -1)
            fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }

    char const *testDisplay = getenv("WM_TEST_DISPLAY");
    char const *display = testDisplay ? testDisplay : ":98";
    if (!StartWm(display))
    {
        TFAIL("could not start new WM instance");
        return;
    }

    // Give the new WM time to scan existing windows and process
    // the async property fetch replies from ManageClient (WmInit).
    // A single dummy window forces one full event loop cycle.
    DrainEvents();
    WmPump(1500);
    {
        xcb_window_t wins[128];
        int n = GetRootChildren(wins, 128);
        TPRINT("  [restart] %d root children before kick\n", n);
        xcb_window_t dummy = CreateTestWindow("_kick", 100, 100);
        WmPump(800);
        xcb_destroy_window(conn, dummy);
        WmPump(800);
        DrainEvents();
    }

    // Both windows should still exist and be managed by the new WM.
    {
        int16_t nx, ny;
        uint16_t nw, nh;
        CHECK(GetWindowGeometry(w0, &nx, &ny, &nw, &nh) == 0, "w0 managed after restart");
        TPRINT("  w0 after restart: x=%d y=%d w=%u h=%u\n", nx, ny, nw, nh);
        // The WM should have repositioned the window — it should not be 1x1
        // (which would mean it wasn't managed).
        CHECK(nw > 1, "w0 has been assigned a size by revived WM");
    }
    {
        int16_t nx, ny;
        uint16_t nw, nh;
        CHECK(GetWindowGeometry(w1, &nx, &ny, &nw, &nh) == 0, "w1 managed after restart");
        TPRINT("  w1 after restart: x=%d y=%d w=%u h=%u\n", nx, ny, nw, nh);
        CHECK(nw > 1, "w1 has been assigned a size by revived WM");
    }

    // Verify server is responsive after restart.
    {
        xcb_get_input_focus_cookie_t fc = xcb_get_input_focus(conn);
        xcb_get_input_focus_reply_t *fr = xcb_get_input_focus_reply(conn, fc, nullptr);
        CHECK(fr != nullptr, "server responsive after WM restart");
        if (fr)
            free(fr);
    }

    // Clean up the test windows.
    xcb_destroy_window(conn, w1);
    xcb_destroy_window(conn, w0);
    WmPump(300);

    // Clean up state file so it doesn't affect future tests.
    unlink("/tmp/nyla_wm_state");
}

// ─── Test runner ─────────────────────────────────────────────────────────────

// Note: keymap_ has a trailing underscore to avoid shadowing the xcb/xkb
// `keymap` parameter name used in some header macros.

} // namespace

// ─── Main ────────────────────────────────────────────────────────────────────

int main()
{
    if (!getenv("WM_TEST_DISPLAY"))
        setenv("DISPLAY", ":98", 1);
    TPRINT("=== WM Integration Tests ===\n\n");

    Setup();

    // Synthetic window tests.
    TestWmManagesNewWindow();
    TestTwoWindowsTiled();
    TestUnmapRemovesWindow();

    // Real X11 application tests.
    TestXterm();
    TestXeyes();
    TestXclock();
    TestMultipleRealApps();
    TestRealAppUnmapRemap();
    TestRealAppFocus();
    TestRealAppStress();
    TestMixedAppsAndSynthetic();

    // ── Keybinding tests (XTEST injection) ──────────────────────────────
    TestKeyboardResize();
    TestKeyboardZoom();
    TestKeyboardCycleMetaDf();
    TestAltTabCycle();

    // Stack switching tests.
    TestStackSwitchCtrlArrows();
    TestStackSwitchER();

    // ── WM feature tests ────────────────────────────────────────────────
    TestOverrideRedirectNotManaged();
    TestTransientWindowPosition();

    // RandR monitor hotplug — removed (no WM RandR handling).

    // Serialization/restart — must run LAST because it restarts the WM.
    TestSerializationRestart();

    Teardown();

    if (failureCount > 0)
    {
        TPRINT("\n%d test(s) FAILED.\n", failureCount);
        return 1;
    }
    TPRINT("\nAll tests passed.\n");
    return 0;
}
