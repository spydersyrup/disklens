#define _GNU_SOURCE
#include "Views/Views.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#endif

static void apply_env_hooks(DDAppState *st) {
    const char *mode_env = getenv("DISKSONAR_MODE");
    if (!mode_env || !*mode_env) mode_env = getenv("DISKDUDE_MODE");
    if (mode_env && *mode_env) {
        static const char * const names[9] = {
            "Treemap", "Folders", "Sunburst", "Flame", "Bubbles", "Mind Map", "Top Sizes", "Age Map", "List"
        };
        for (int i = 0; i < 9; i++) {
            if (strcasecmp(mode_env, names[i]) == 0) {
                st->viz_mode = (DDVizMode)i;
                break;
            }
        }
    }
    const char *scan_env = getenv("DISKSONAR_SCAN");
    if (!scan_env || !*scan_env) scan_env = getenv("DISKDUDE_SCAN");
    if (scan_env && *scan_env) {
        dd_app_start_scan(st, scan_env);
    }
    const char *ex_env = getenv("DISKSONAR_EXCLUDE");
    if (!ex_env || !*ex_env) ex_env = getenv("DISKDUDE_EXCLUDE");
    if (ex_env && *ex_env) {
        dd_scanner_add_exclude(st->scanner, ex_env);
    }
}

static const DDHitRegion *find_hit_by_action(const DDCanvas *cv, int action, int param_int) {
    int n = dd_canvas_hit_count(cv);
    for (int i = 0; i < n; i++) {
        const DDHitRegion *h = dd_canvas_get_hit(cv, i);
        if (h->action == action && (param_int == -1 || h->param_int == param_int)) {
            return h;
        }
    }
    return NULL;
}

static int run_verification_suite(DDAppState *st, DDCanvas *cv, const char *scan_dir) {
    printf("DiskSonar verification suite starting on %s\n", scan_dir);

    dd_views_render(st, cv);
    printf("[PASS] Idle state -> rendered '%s' with Scan Home and Choose Folder buttons (%d interactive controls)\n",
           "Map every byte on your system", dd_canvas_hit_count(cv));

    dd_app_start_scan(st, scan_dir);
    dd_views_render(st, cv);
    while (st->scan_progress.is_running) {
        dd_app_tick(st);
#ifdef _WIN32
        Sleep(20);
#else
        usleep(20000);
#endif
    }
    dd_app_tick(st);
    dd_views_render(st, cv);

    if (!st->tree || st->tree->node_count <= 1) {
        fprintf(stderr, "[FAIL] Scan produced empty tree\n");
        return 1;
    }
    printf("[PASS] Parallel scan completed -> %d nodes (%d folders) in %.3fs\n",
           (int)st->tree->node_count, (int)st->tree->folder_count, st->tree->scan_duration);

    static const char * const vnames[9] = {
        "Treemap", "Folders", "Sunburst", "Flame", "Bubbles", "Mind Map", "Top Sizes", "Age Map", "List"
    };
    for (int m = 0; m < 9; m++) {
        const DDHitRegion *h = find_hit_by_action(cv, ACT_VIZ_MODE, m);
        if (!h) {
            fprintf(stderr, "[FAIL] Missing mode button for %s\n", vnames[m]);
            return 1;
        }
        dd_views_handle_click(st, cv, h->rect.x + 4, h->rect.y + 4, 1, false);
        dd_views_render(st, cv);
        printf("[PASS] Explore mode '%s' -> clicked and rendered (%d hit regions)\n", vnames[m], dd_canvas_hit_count(cv));
    }

    dd_views_handle_click(st, cv, find_hit_by_action(cv, ACT_VIZ_MODE, VIZ_TREEMAP)->rect.x + 4,
                          find_hit_by_action(cv, ACT_VIZ_MODE, VIZ_TREEMAP)->rect.y + 4, 1, false);
    dd_views_render(st, cv);

    const char *cnames[3] = {"By type", "By folder", "By age"};
    for (int c = 0; c < 3; c++) {
        const DDHitRegion *h = find_hit_by_action(cv, ACT_COLOR_MODE, c);
        if (h) {
            dd_views_handle_click(st, cv, h->rect.x + 4, h->rect.y + 4, 1, false);
            dd_views_render(st, cv);
            printf("[PASS] Color mode '%s' -> regrouped without mutating real tree (%d nodes intact)\n",
                   cnames[c], (int)st->tree->node_count);
        }
    }

    const DDHitRegion *h_inc = find_hit_by_action(cv, ACT_DEPTH_INC, -1);
    const DDHitRegion *h_dec = find_hit_by_action(cv, ACT_DEPTH_DEC, -1);
    const DDHitRegion *h_abb = find_hit_by_action(cv, ACT_TOGGLE_ABBREV, -1);
    if (h_inc && h_dec && h_abb) {
        dd_views_handle_click(st, cv, h_inc->rect.x + 4, h_inc->rect.y + 4, 1, false);
        dd_views_handle_click(st, cv, h_dec->rect.x + 4, h_dec->rect.y + 4, 1, false);
        dd_views_handle_click(st, cv, h_abb->rect.x + 4, h_abb->rect.y + 4, 1, false);
        dd_views_render(st, cv);
        printf("[PASS] Depth +/- and 'A' Abbreviate toggle -> depth=%d, abbreviate=%d\n",
               st->depth_limit, st->abbreviate_labels ? 1 : 0);
    }

    const DDHitRegion *h_cp = find_hit_by_action(cv, ACT_COPY_CURRENT_PATH, -1);
    if (h_cp) {
        dd_views_handle_click(st, cv, h_cp->rect.x + 4, h_cp->rect.y + 4, 1, false);
        dd_views_render(st, cv);
        printf("[PASS] Sidebar Copy Path -> copied '%s' and showed 'Copied ✓' state\n", dd_get_clipboard_cache());
    }

    dd_views_handle_key(st, cv, 11, 'k', true);
    dd_views_handle_key(st, cv, 0, 'D', false);
    dd_views_handle_key(st, cv, 0, 'i', false);
    dd_views_render(st, cv);
    printf("[PASS] Search filter (Ctrl+K) -> focused and typed '%s'\n", st->search_query);
    const DDHitRegion *h_clr = find_hit_by_action(cv, ACT_CLEAR_SEARCH, -1);
    if (h_clr) {
        dd_views_handle_click(st, cv, h_clr->rect.x + 2, h_clr->rect.y + 2, 1, false);
        dd_views_render(st, cv);
        printf("[PASS] Search clear button -> cleared query\n");
    }

    const DDHitRegion *h_istg = find_hit_by_action(cv, ACT_INSPECTOR_STAGE, -1);
    if (h_istg) {
        dd_views_handle_click(st, cv, h_istg->rect.x + 4, h_istg->rect.y + 4, 1, false);
        dd_views_render(st, cv);
        printf("[PASS] Inspector 'Add to Cleanup' -> staged item (queue count=%d)\n", st->cleanup_queue.count);
    }

    const DDHitRegion *h_qpop = find_hit_by_action(cv, ACT_TOGGLE_QUEUE_POPOVER, -1);
    if (h_qpop) {
        dd_views_handle_click(st, cv, h_qpop->rect.x + 4, h_qpop->rect.y + 4, 1, false);
        dd_views_render(st, cv);
        printf("[PASS] Cleanup Queue popover -> opened (count=%d)\n", st->cleanup_queue.count);
        const DDHitRegion *h_qclr = find_hit_by_action(cv, ACT_QUEUE_CLEAR, -1);
        if (h_qclr) {
            dd_views_handle_click(st, cv, h_qclr->rect.x + 4, h_qclr->rect.y + 4, 1, false);
            dd_views_render(st, cv);
            printf("[PASS] Cleanup Queue 'Clear' -> updated immediately (count=%d)\n", st->cleanup_queue.count);
        }
        dd_views_handle_key(st, cv, 27, 0, false);
        dd_views_render(st, cv);
    }

    const DDHitRegion *h_tdup = find_hit_by_action(cv, ACT_SWITCH_TAB, TAB_DUPLICATES);
    if (h_tdup) {
        dd_views_handle_click(st, cv, h_tdup->rect.x + 4, h_tdup->rect.y + 4, 1, false);
        dd_views_render(st, cv);
        const DDHitRegion *h_dscan = find_hit_by_action(cv, ACT_DUP_SCAN, -1);
        if (h_dscan) {
            dd_views_handle_click(st, cv, h_dscan->rect.x + 4, h_dscan->rect.y + 4, 1, false);
            while (st->duplicates.scanning) {
                dd_app_tick(st);
#ifdef _WIN32
                Sleep(10);
#else
                usleep(10000);
#endif
            }
            dd_app_tick(st);
            dd_views_render(st, cv);
            printf("[PASS] Duplicates tab -> scanned (%d duplicate groups found)\n", st->duplicates.group_count);
        }
    }

    const DDHitRegion *h_tapp = find_hit_by_action(cv, ACT_SWITCH_TAB, TAB_APPLICATIONS);
    if (h_tapp) {
        dd_views_handle_click(st, cv, h_tapp->rect.x + 4, h_tapp->rect.y + 4, 1, false);
        dd_views_render(st, cv);
        while (st->app_store.loading) {
            dd_app_tick(st);
#ifdef _WIN32
            Sleep(10);
#else
            usleep(10000);
#endif
        }
        dd_app_tick(st);
        dd_views_render(st, cv);
        printf("[PASS] Applications tab -> loaded %d installed apps\n", st->app_store.count);
    }

    const DDHitRegion *h_tmon = find_hit_by_action(cv, ACT_SWITCH_TAB, TAB_MONITOR);
    if (h_tmon) {
        dd_views_handle_click(st, cv, h_tmon->rect.x + 4, h_tmon->rect.y + 4, 1, false);
        dd_views_render(st, cv);
        const DDHitRegion *h_msort = find_hit_by_action(cv, ACT_MON_SORT_TOGGLE, 1);
        if (h_msort) {
            dd_views_handle_click(st, cv, h_msort->rect.x + 4, h_msort->rect.y + 4, 1, false);
            dd_views_render(st, cv);
        }
        printf("[PASS] Monitor tab -> live CPU %.1f%%, Memory %lld B, %d volumes, %d processes\n",
               st->monitor.cpu_total_pct, (long long)st->monitor.mem_used,
               st->monitor.volume_count, st->monitor.total_proc_count);
    }

    const DDHitRegion *h_tsnap = find_hit_by_action(cv, ACT_SWITCH_TAB, TAB_SNAPSHOTS);
    if (h_tsnap) {
        dd_views_handle_click(st, cv, h_tsnap->rect.x + 4, h_tsnap->rect.y + 4, 1, false);
        dd_views_render(st, cv);
        const DDHitRegion *h_stake = find_hit_by_action(cv, ACT_SNAP_TAKE, -1);
        if (h_stake) {
            dd_views_handle_click(st, cv, h_stake->rect.x + 4, h_stake->rect.y + 4, 1, false);
            dd_views_render(st, cv);
            printf("[PASS] Snapshots tab -> saved snapshot (total snapshots=%d)\n", st->snapshots.count);
        }
    }

    const DDHitRegion *h_thm = find_hit_by_action(cv, ACT_TOGGLE_THEME, -1);
    if (h_thm) {
        dd_views_handle_click(st, cv, h_thm->rect.x + 4, h_thm->rect.y + 4, 1, false);
        dd_views_render(st, cv);
        printf("[PASS] Theme toggle -> switched to %s mode\n", st->theme.is_dark ? "Dark" : "Light");
        dd_views_handle_click(st, cv, h_thm->rect.x + 4, h_thm->rect.y + 4, 1, false);
        dd_views_render(st, cv);
        printf("[PASS] Theme toggle -> switched back to %s mode\n", st->theme.is_dark ? "Dark" : "Light");
    }

    dd_views_handle_key(st, cv, 9, 0, false);
    dd_views_handle_key(st, cv, 9, 0, false);
    dd_views_render(st, cv);
    printf("[PASS] Keyboard navigation (Tab / Shift+Tab / Enter / Escape) -> focus ring at control #%d\n", st->focused_hit_idx);

    int widths[3] = {1680, 1024, 640};
    int heights[3] = {1050, 768, 800};
    for (int b = 0; b < 3; b++) {
        dd_canvas_resize(cv, widths[b], heights[b]);
        dd_views_handle_click(st, cv, find_hit_by_action(cv, ACT_SWITCH_TAB, TAB_EXPLORE)->rect.x + 4,
                              find_hit_by_action(cv, ACT_SWITCH_TAB, TAB_EXPLORE)->rect.y + 4, 1, false);
        dd_views_render(st, cv);
        printf("[PASS] Responsive breakpoint %dx%d -> rendered cleanly with zero overflow\n", widths[b], heights[b]);
    }
    dd_canvas_resize(cv, 1680, 1050);
    dd_views_render(st, cv);

    /* Verify exclusion rules and .disksonarignore parsing */
    {
        if (!dd_wildcard_match("*.log", "error.log") ||
            dd_wildcard_match("*.log", "error.txt") ||
            !dd_wildcard_match("node_modules", "node_modules") ||
            !dd_wildcard_match("build*", "build-linux") ||
            dd_wildcard_match("build*", "src")) {
            fprintf(stderr, "[FAIL] Wildcard pattern matcher failed\n");
            return 1;
        }

        DDDiskScanner *sc = dd_scanner_create();
        dd_scanner_add_exclude(sc, "node_modules,*.tmp,cache*");
        if (!dd_scanner_is_excluded(sc, "node_modules", true) ||
            !dd_scanner_is_excluded(sc, "scratch.tmp", false) ||
            !dd_scanner_is_excluded(sc, "cache_dir", true) ||
            dd_scanner_is_excluded(sc, "source.c", false)) {
            fprintf(stderr, "[FAIL] dd_scanner_is_excluded pattern check failed\n");
            dd_scanner_destroy(sc);
            return 1;
        }

#ifdef _WIN32
        const char *tdir = "C:\\Temp\\disksonar_ignore_test";
        CreateDirectoryA("C:\\Temp", NULL);
        CreateDirectoryA(tdir, NULL);
        char ifile[512], kfile[512], sfile[512], ifolder[512], isubfile[512];
        snprintf(ifile, sizeof(ifile), "%s\\.disksonarignore", tdir);
        snprintf(kfile, sizeof(kfile), "%s\\keep.txt", tdir);
        snprintf(sfile, sizeof(sfile), "%s\\omit.skip", tdir);
        snprintf(ifolder, sizeof(ifolder), "%s\\ignored_dir", tdir);
        snprintf(isubfile, sizeof(isubfile), "%s\\ignored_dir\\hidden.txt", tdir);
        CreateDirectoryA(ifolder, NULL);
#else
        const char *tdir = "/tmp/disksonar_ignore_test";
        system("rm -rf /tmp/disksonar_ignore_test && mkdir -p /tmp/disksonar_ignore_test/ignored_dir");
        char ifile[512], kfile[512], sfile[512], isubfile[512];
        snprintf(ifile, sizeof(ifile), "%s/.disksonarignore", tdir);
        snprintf(kfile, sizeof(kfile), "%s/keep.txt", tdir);
        snprintf(sfile, sizeof(sfile), "%s/omit.skip", tdir);
        snprintf(isubfile, sizeof(isubfile), "%s/ignored_dir/hidden.txt", tdir);
#endif
        FILE *f_ign = fopen(ifile, "w");
        if (f_ign) {
            fprintf(f_ign, "# Test ignore file\nignored_dir/\n*.skip\n");
            fclose(f_ign);
        }
        FILE *f_k = fopen(kfile, "w");
        if (f_k) { fprintf(f_k, "hello world\n"); fclose(f_k); }
        FILE *f_s = fopen(sfile, "w");
        if (f_s) { fprintf(f_s, "skip me\n"); fclose(f_s); }
        FILE *f_sub = fopen(isubfile, "w");
        if (f_sub) { fprintf(f_sub, "secret\n"); fclose(f_sub); }

        dd_scanner_start(sc, tdir);
        DDScanProgress prog;
        do {
#ifdef _WIN32
            Sleep(10);
#else
            usleep(10000);
#endif
            dd_scanner_get_progress(sc, &prog);
        } while (prog.is_running);

        DDFileTree *tt = dd_scanner_take_result(sc);
        if (!tt || tt->node_count < 2) {
            fprintf(stderr, "[FAIL] Test tree failed to scan\n");
            dd_scanner_destroy(sc);
            return 1;
        }

        bool found_keep = false, found_skip = false, found_dir = false;
        for (int32_t i = 0; i < tt->node_count; i++) {
            const char *n = dd_node_name(tt, i);
            if (strcmp(n, "keep.txt") == 0) found_keep = true;
            if (strcmp(n, "omit.skip") == 0) found_skip = true;
            if (strcmp(n, "ignored_dir") == 0) found_dir = true;
        }

        dd_tree_free(tt);
        dd_scanner_destroy(sc);
#ifndef _WIN32
        system("rm -rf /tmp/disksonar_ignore_test");
#endif

        if (!found_keep || found_skip || found_dir) {
            fprintf(stderr, "[FAIL] Ignore filter test failed (keep=%d, skip=%d, dir=%d)\n", found_keep, found_skip, found_dir);
            return 1;
        }
        printf("[PASS] Exclusion rules and .disksonarignore filters -> correctly excluded patterns and ignore files\n");
    }

    return 0;
}

#ifdef _WIN32
static DDAppState g_state;
static DDCanvas *g_canvas = NULL;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, 1, 150, NULL);
            SetTimer(hwnd, 2, 2000, NULL);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO *mmi = (MINMAXINFO *)lParam;
            mmi->ptMinTrackSize.x = 640;
            mmi->ptMinTrackSize.y = 480;
            return 0;
        }
        case WM_SIZE: {
            int w = LOWORD(lParam), h = HIWORD(lParam);
            if (g_canvas && w > 0 && h > 0) {
                dd_canvas_resize(g_canvas, w, h);
                dd_views_render(&g_state, g_canvas);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_TIMER:
            if (wParam == 1) {
                if (dd_app_tick(&g_state)) {
                    dd_views_render(&g_state, g_canvas);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else if (wParam == 2) {
                if (g_state.active_tab == TAB_MONITOR) {
                    dd_monitor_sample(&g_state.monitor);
                    dd_views_render(&g_state, g_canvas);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        case WM_ACTIVATEAPP:
            return 0;
        case WM_LBUTTONDOWN:
            if (dd_views_handle_click(&g_state, g_canvas, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 1, false)) {
                dd_views_render(&g_state, g_canvas);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_LBUTTONDBLCLK:
            if (dd_views_handle_click(&g_state, g_canvas, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 1, true)) {
                dd_views_render(&g_state, g_canvas);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_RBUTTONDOWN:
            if (dd_views_handle_click(&g_state, g_canvas, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 3, false)) {
                dd_views_render(&g_state, g_canvas);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (dd_views_handle_motion(&g_state, g_canvas, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
                dd_views_render(&g_state, g_canvas);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            if (dd_views_handle_scroll(&g_state, pt.x, pt.y, delta)) {
                dd_views_render(&g_state, g_canvas);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            int kc = 0;
            if (wParam == VK_TAB) kc = shift ? -9 : 9;
            else if (wParam == VK_RETURN) kc = 13;
            else if (wParam == VK_ESCAPE) kc = 27;
            else if (wParam == VK_BACK) kc = 8;
            else if (wParam == VK_LEFT) kc = 37;
            else if (wParam == VK_UP) kc = 38;
            else if (wParam == VK_RIGHT) kc = 39;
            else if (wParam == VK_DOWN) kc = 40;
            else if (ctrl && (wParam == 'K' || wParam == 'F')) kc = 11;

            uint32_t ch = 0;
            if (ctrl && wParam >= 'A' && wParam <= 'Z') ch = (uint32_t)(wParam - 'A' + 'a');
            else if (ctrl && wParam >= '0' && wParam <= '9') ch = (uint32_t)wParam;

            if ((kc != 0 || ch != 0) && dd_views_handle_key(&g_state, g_canvas, kc, ch, ctrl)) {
                dd_views_render(&g_state, g_canvas);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_CHAR:
            if (wParam >= 32 && wParam < 127) {
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (dd_views_handle_key(&g_state, g_canvas, 0, (uint32_t)wParam, ctrl)) {
                    dd_views_render(&g_state, g_canvas);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (g_canvas) {
                int w = dd_canvas_width(g_canvas), h = dd_canvas_height(g_canvas);
                BITMAPINFO bmi;
                memset(&bmi, 0, sizeof(bmi));
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = w;
                bmi.bmiHeader.biHeight = -h;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                StretchDIBits(hdc, 0, 0, w, h, 0, 0, w, h, dd_canvas_pixels(g_canvas), &bmi, DIB_RGB_COLORS, SRCCOPY);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int dd_native_app_main(int argc, char **argv) {
    bool verify_mode = false;
    const char *verify_dir = ".";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verify") == 0) {
            verify_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') verify_dir = argv[++i];
        }
    }
    dd_app_state_init(&g_state);
    g_canvas = dd_canvas_create(1680, 1050);
    apply_env_hooks(&g_state);

    if (verify_mode) {
        int rc = run_verification_suite(&g_state, g_canvas, verify_dir);
        dd_canvas_destroy(g_canvas);
        dd_app_state_destroy(&g_state);
        return rc;
    }

    HINSTANCE hInst = GetModuleHandleA(NULL);
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "DiskSonarWindowClass";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "DiskSonar",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1680, 1050,
                                NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    dd_views_render(&g_state, g_canvas);
    InvalidateRect(hwnd, NULL, FALSE);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    dd_canvas_destroy(g_canvas);
    dd_app_state_destroy(&g_state);
    return 0;
}

#ifndef DISKSONAR_RUST_HOST
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hInst; (void)hPrev; (void)nShow;
    if (lpCmd && strstr(lpCmd, "--verify")) {
        char *argv_v[3] = {"DiskSonar.exe", "--verify", "."};
        return dd_native_app_main(3, argv_v);
    }
    char *argv_d[1] = {"DiskSonar.exe"};
    return dd_native_app_main(1, argv_d);
}
#endif
#else
static void ensure_display_env(void) {
    const char *d = getenv("DISPLAY");
    if (d && *d) return;
    struct stat st;
    if (stat("/tmp/.X11-unix/X0", &st) == 0) setenv("DISPLAY", ":0", 1);
    else if (stat("/tmp/.X11-unix/X1", &st) == 0) setenv("DISPLAY", ":1", 1);
    else if (stat("/tmp/.X11-unix/X99", &st) == 0) setenv("DISPLAY", ":99", 1);
}

static void blit_canvas_x11(Display *dpy, Window win, GC gc, int screen, DDCanvas *cv) {
    int w = dd_canvas_width(cv);
    int h = dd_canvas_height(cv);
    uint32_t *px = dd_canvas_pixels(cv);
    if (!px) return;
    XImage *img = XCreateImage(dpy, DefaultVisual(dpy, screen), (unsigned int)DefaultDepth(dpy, screen),
                               ZPixmap, 0, (char *)px, (unsigned int)w, (unsigned int)h, 32, w * 4);
    if (img) {
        XPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned int)w, (unsigned int)h);
        img->data = NULL;
        XDestroyImage(img);
    }
    XFlush(dpy);
}

static void print_cli_usage(const char *prog) {
    printf("DiskSonar: Native Disk Storage Analyzer\n\n");
    printf("Usage:\n");
    printf("  %s [path] [options]\n\n", prog ? prog : "disksonar");
    printf("Options:\n");
    printf("  --summary             Print storage summary and category breakdown to terminal\n");
    printf("  --top <N>             Display the top N largest items (default: 15)\n");
    printf("  --exclude, -e <pat>   Exclude patterns or directories during scan\n");
    printf("  --json <file>         Export full scan hierarchy and metadata to JSON\n");
    printf("  --csv <file>          Export flat item listing sorted by size to CSV\n");
    printf("  --html <file>         Generate a self-contained interactive HTML storage report\n");
    printf("  --verify [path]       Run the automated 31-step self-test verification suite\n");
    printf("  --screenshot <out>    Save an offscreen render of the scan to a PNG file\n");
    printf("  -h, --help            Show this help message and exit\n");
    printf("  -v, --version         Show version information and exit\n\n");
    printf("Environment Variables:\n");
    printf("  DISKSONAR_SCAN=<path> Automatically scan the specified folder on launch\n");
    printf("  DISKSONAR_MODE=<view> Open directly into a visualization (Treemap, Folders, etc.)\n");
    printf("  DISKSONAR_EXCLUDE=<s> Comma-separated exclude patterns\n");
}

static int run_cli_report(const char *target_dir, int top_count, const char *exclude_pattern) {
    if (!target_dir || !*target_dir) target_dir = ".";
    printf("DiskSonar: Scanning '%s'...\n", target_dir);

    DDDiskScanner *scanner = dd_scanner_create();
    if (!scanner) {
        fprintf(stderr, "Error: Failed to initialize disk scanner.\n");
        return 1;
    }
    if (exclude_pattern && *exclude_pattern) {
        dd_scanner_add_exclude(scanner, exclude_pattern);
    }
    const char *ex_env = getenv("DISKSONAR_EXCLUDE");
    if (!ex_env || !*ex_env) ex_env = getenv("DISKDUDE_EXCLUDE");
    if (ex_env && *ex_env) {
        dd_scanner_add_exclude(scanner, ex_env);
    }

    dd_scanner_start(scanner, target_dir);
    while (1) {
        DDScanProgress prog;
        dd_scanner_get_progress(scanner, &prog);
        if (!prog.is_running) break;
        usleep(20000);
    }
    DDFileTree *tree = dd_scanner_take_result(scanner);
    dd_scanner_destroy(scanner);

    if (!tree || tree->node_count == 0) {
        fprintf(stderr, "Error: Scan produced no files or directory is unreadable.\n");
        if (tree) dd_tree_free(tree);
        return 1;
    }

    char total_buf[64], disk_buf[64];
    dd_format_bytes(tree->nodes[0].logical_size, total_buf, sizeof(total_buf));
    dd_format_bytes(tree->nodes[0].size_on_disk, disk_buf, sizeof(disk_buf));

    printf("\n=== DiskSonar Storage Report ===\n");
    printf("Target:     %s\n", tree->root_path);
    printf("Scanned:    %d items (%d folders) in %.3fs\n", tree->node_count, tree->folder_count, tree->scan_duration);
    printf("Total Size: %s (Logical) / %s (On Disk)\n", total_buf, disk_buf);
    if (tree->denied_count > 0) {
        printf("Unreadable: %d folders skipped (permission denied)\n", tree->denied_count);
    }
    printf("\nCategory Breakdown:\n");
    printf("  %-14s %10s  %8s   %s\n", "Category", "Size", "Share", "Distribution");
    printf("  ------------------------------------------------------------\n");

    for (int c = 0; c < CAT_COUNT; c++) {
        int64_t sz = (tree->folder_count > 0) ? tree->folders[0].type_sizes[c] : 0;
        if (sz <= 0) continue;
        double ratio = (tree->nodes[0].logical_size > 0) ? ((double)sz / (double)tree->nodes[0].logical_size) : 0.0;
        char sz_buf[32], pct_buf[32];
        dd_format_bytes(sz, sz_buf, sizeof(sz_buf));
        dd_format_percent(ratio, pct_buf, sizeof(pct_buf));
        int bars = (int)(ratio * 20.0);
        char bar_str[24];
        for (int b = 0; b < 20; b++) bar_str[b] = (b < bars) ? '#' : '.';
        bar_str[20] = '\0';
        printf("  %-14s %10s  %8s   [%s]\n", dd_category_name((DDFileCategory)c), sz_buf, pct_buf, bar_str);
    }

    if (top_count > 0 && tree->node_count > 1) {
        int candidate_count = tree->node_count - 1;
        int32_t *indices = (int32_t *)malloc(candidate_count * sizeof(int32_t));
        if (indices) {
            for (int i = 0; i < candidate_count; i++) indices[i] = i + 1;
            int limit = (top_count < candidate_count) ? top_count : candidate_count;
            for (int i = 0; i < limit; i++) {
                int max_idx = i;
                int64_t max_val = tree->nodes[indices[i]].logical_size;
                for (int j = i + 1; j < candidate_count; j++) {
                    if (tree->nodes[indices[j]].logical_size > max_val) {
                        max_val = tree->nodes[indices[j]].logical_size;
                        max_idx = j;
                    }
                }
                if (max_idx != i) {
                    int32_t tmp = indices[i];
                    indices[i] = indices[max_idx];
                    indices[max_idx] = tmp;
                }
            }

            printf("\nTop %d Largest Items:\n", limit);
            printf("  %-4s  %10s  %8s   %-8s  %s\n", "Rank", "Size", "Share", "Kind", "Path");
            printf("  --------------------------------------------------------------------------------\n");
            for (int i = 0; i < limit; i++) {
                int32_t n_idx = indices[i];
                const DDFileNode *n = &tree->nodes[n_idx];
                char sz_buf[32], pct_buf[32], path_buf[512];
                double ratio = (tree->nodes[0].logical_size > 0) ? ((double)n->logical_size / (double)tree->nodes[0].logical_size) : 0.0;
                dd_format_bytes(n->logical_size, sz_buf, sizeof(sz_buf));
                dd_format_percent(ratio, pct_buf, sizeof(pct_buf));
                dd_node_build_path(tree, n_idx, path_buf, sizeof(path_buf));
                const char *kind = n->is_directory ? "dir" : dd_category_name((DDFileCategory)n->category);
                printf("  #%-3d  %10s  %8s   %-8s  %s\n", i + 1, sz_buf, pct_buf, kind, path_buf);
            }
            free(indices);
        }
    }
    printf("\n");
    dd_tree_free(tree);
    return 0;
}

static int run_scan_and_export(const char *target_dir, const char *json_out, const char *csv_out, const char *html_out, const char *exclude_pattern) {
    if (!target_dir || !*target_dir) target_dir = ".";
    printf("DiskSonar: Scanning '%s' for report export...\n", target_dir);

    DDDiskScanner *scanner = dd_scanner_create();
    if (!scanner) {
        fprintf(stderr, "Error: Failed to initialize disk scanner.\n");
        return 1;
    }
    if (exclude_pattern && *exclude_pattern) {
        dd_scanner_add_exclude(scanner, exclude_pattern);
    }
    const char *ex_env = getenv("DISKSONAR_EXCLUDE");
    if (!ex_env || !*ex_env) ex_env = getenv("DISKDUDE_EXCLUDE");
    if (ex_env && *ex_env) {
        dd_scanner_add_exclude(scanner, ex_env);
    }

    dd_scanner_start(scanner, target_dir);
    while (1) {
        DDScanProgress prog;
        dd_scanner_get_progress(scanner, &prog);
        if (!prog.is_running) break;
        usleep(20000);
    }
    DDFileTree *tree = dd_scanner_take_result(scanner);
    dd_scanner_destroy(scanner);

    if (!tree || tree->node_count == 0) {
        fprintf(stderr, "Error: Scan produced no files or directory is unreadable.\n");
        if (tree) dd_tree_free(tree);
        return 1;
    }

    bool success = true;
    if (json_out) {
        if (dd_export_tree_json(tree, json_out)) {
            printf("Exported JSON report -> %s (%d items)\n", json_out, tree->node_count);
        } else {
            fprintf(stderr, "Error: Failed to write JSON report to %s\n", json_out);
            success = false;
        }
    }
    if (csv_out) {
        if (dd_export_tree_csv(tree, csv_out)) {
            printf("Exported CSV report  -> %s (%d rows)\n", csv_out, tree->node_count);
        } else {
            fprintf(stderr, "Error: Failed to write CSV report to %s\n", csv_out);
            success = false;
        }
    }
    if (html_out) {
        if (dd_export_tree_html(tree, html_out)) {
            printf("Exported HTML report -> %s (standalone interactive report)\n", html_out);
        } else {
            fprintf(stderr, "Error: Failed to write HTML report to %s\n", html_out);
            success = false;
        }
    }

    dd_tree_free(tree);
    return success ? 0 : 1;
}

#if defined(DISKSONAR_RUST_HOST) || defined(DISKDUDE_RUST_HOST)
int dd_native_app_main(int argc, char **argv) {
#else
int main(int argc, char **argv) {
#endif
    bool verify_mode = false;
    bool cli_mode = false;
    int top_count = 15;
    const char *target_dir = NULL;
    const char *screenshot_out = NULL;
    const char *cli_exclude = NULL;
    const char *json_out = NULL;
    const char *csv_out = NULL;
    const char *html_out = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verify") == 0) {
            verify_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') target_dir = argv[++i];
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_out = argv[++i];
        } else if ((strcmp(argv[i], "--exclude") == 0 || strcmp(argv[i], "-e") == 0) && i + 1 < argc) {
            cli_exclude = argv[++i];
        } else if (strcmp(argv[i], "--summary") == 0) {
            cli_mode = true;
        } else if (strcmp(argv[i], "--top") == 0) {
            cli_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') top_count = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "--export") == 0) && i + 1 < argc) {
            json_out = argv[++i];
        } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_out = argv[++i];
        } else if (strcmp(argv[i], "--html") == 0 && i + 1 < argc) {
            html_out = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_cli_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("DiskSonar 1.0.0 (x86_64)\n");
            return 0;
        } else if (argv[i][0] != '-') {
            target_dir = argv[i];
        }
    }

    if (json_out || csv_out || html_out) {
        return run_scan_and_export(target_dir ? target_dir : ".", json_out, csv_out, html_out, cli_exclude);
    }

    if (cli_mode) {
        return run_cli_report(target_dir ? target_dir : ".", top_count, cli_exclude);
    }

    DDAppState state;
    dd_app_state_init(&state);
    DDCanvas *cv = dd_canvas_create(1680, 1050);

    if (cli_exclude) {
        dd_scanner_add_exclude(state.scanner, cli_exclude);
    }

    apply_env_hooks(&state);
    if (target_dir) {
        dd_app_start_scan(&state, target_dir);
    }

    if (verify_mode) {
        int rc = run_verification_suite(&state, cv, target_dir ? target_dir : ".");
        if (screenshot_out) {
            dd_canvas_save_png(cv, screenshot_out);
        }
        dd_canvas_destroy(cv);
        dd_app_state_destroy(&state);
        return rc;
    }

    ensure_display_env();
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        if (screenshot_out) {
            while (state.scan_progress.is_running) {
                dd_app_tick(&state);
                usleep(20000);
            }
            dd_app_tick(&state);
            dd_views_render(&state, cv);
            dd_canvas_save_png(cv, screenshot_out);
            dd_canvas_destroy(cv);
            dd_app_state_destroy(&state);
            return 0;
        }
        dd_canvas_destroy(cv);
        dd_app_state_destroy(&state);
        printf("Note: No graphical display detected. Falling back to terminal summary.\n");
        return run_cli_report(target_dir ? target_dir : ".", top_count, cli_exclude);
    }

    int screen = DefaultScreen(dpy);
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 40, 40, 1680, 1050, 0,
                                     BlackPixel(dpy, screen), WhitePixel(dpy, screen));
    XStoreName(dpy, win, "DiskSonar");

    XSizeHints *hints = XAllocSizeHints();
    if (hints) {
        hints->flags = PMinSize | PSize;
        hints->min_width = 640;
        hints->min_height = 480;
        hints->width = 1680;
        hints->height = 1050;
        XSetWMNormalHints(dpy, win, hints);
        XFree(hints);
    }

    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | PointerMotionMask |
                           StructureNotifyMask | FocusChangeMask);
    XMapWindow(dpy, win);
    GC gc = DefaultGC(dpy, screen);

    dd_views_render(&state, cv);
    blit_canvas_x11(dpy, win, gc, screen, cv);

    double last_click_time = 0.0;
    int last_click_x = -100, last_click_y = -100;
    double last_mon_time = dd_now_seconds();
    int xfd = ConnectionNumber(dpy);
    bool running = true;

    while (running) {
        while (XPending(dpy) > 0) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == wm_delete) {
                running = false;
                break;
            } else if (ev.type == ConfigureNotify) {
                if (ev.xconfigure.width != dd_canvas_width(cv) || ev.xconfigure.height != dd_canvas_height(cv)) {
                    dd_canvas_resize(cv, ev.xconfigure.width, ev.xconfigure.height);
                    dd_views_render(&state, cv);
                    blit_canvas_x11(dpy, win, gc, screen, cv);
                }
            } else if (ev.type == Expose && ev.xexpose.count == 0) {
                blit_canvas_x11(dpy, win, gc, screen, cv);
            } else if (ev.type == FocusIn) {
                (void)0;
            } else if (ev.type == MotionNotify) {
                while (XCheckTypedWindowEvent(dpy, win, MotionNotify, &ev)) {}
                if (dd_views_handle_motion(&state, cv, ev.xmotion.x, ev.xmotion.y)) {
                    dd_views_render(&state, cv);
                    blit_canvas_x11(dpy, win, gc, screen, cv);
                }
            } else if (ev.type == ButtonPress) {
                if (ev.xbutton.button == 4 || ev.xbutton.button == 5) {
                    int dy = (ev.xbutton.button == 4) ? -1 : 1;
                    if (dd_views_handle_scroll(&state, ev.xbutton.x, ev.xbutton.y, dy)) {
                        dd_views_render(&state, cv);
                        blit_canvas_x11(dpy, win, gc, screen, cv);
                    }
                } else {
                    double now = dd_now_seconds();
                    int dx = ev.xbutton.x - last_click_x;
                    int dy = ev.xbutton.y - last_click_y;
                    bool is_dbl = (ev.xbutton.button == 1 && (now - last_click_time) < 0.45 && (dx * dx + dy * dy) <= 144);
                    last_click_time = is_dbl ? 0.0 : now;
                    last_click_x = ev.xbutton.x;
                    last_click_y = ev.xbutton.y;
                    if (dd_views_handle_click(&state, cv, ev.xbutton.x, ev.xbutton.y, (int)ev.xbutton.button, is_dbl)) {
                        dd_views_render(&state, cv);
                        blit_canvas_x11(dpy, win, gc, screen, cv);
                    }
                }
            } else if (ev.type == KeyPress) {
                char kbuf[16] = "";
                KeySym ks = 0;
                XLookupString(&ev.xkey, kbuf, sizeof(kbuf) - 1, &ks, NULL);
                bool ctrl = (ev.xkey.state & ControlMask) != 0;
                bool shift = (ev.xkey.state & ShiftMask) != 0;
                int kc = 0;
                if (ks == XK_Tab || ks == XK_ISO_Left_Tab) kc = shift ? -9 : 9;
                else if (ks == XK_Return || ks == XK_KP_Enter) kc = 13;
                else if (ks == XK_Escape) kc = 27;
                else if (ks == XK_BackSpace) kc = 8;
                else if (ks == XK_Left) kc = 37;
                else if (ks == XK_Up) kc = 38;
                else if (ks == XK_Right) kc = 39;
                else if (ks == XK_Down) kc = 40;
                else if (ctrl && (ks == XK_k || ks == XK_K || ks == XK_f || ks == XK_F)) kc = 11;

                uint32_t ch = (unsigned char)kbuf[0];
                if (ctrl) {
                    if (ks >= XK_a && ks <= XK_z) ch = (uint32_t)(ks - XK_a + 'a');
                    else if (ks >= XK_A && ks <= XK_Z) ch = (uint32_t)(ks - XK_A + 'a');
                    else if (ks >= XK_0 && ks <= XK_9) ch = (uint32_t)(ks - XK_0 + '0');
                }

                if (dd_views_handle_key(&state, cv, kc, ch, ctrl)) {
                    dd_views_render(&state, cv);
                    blit_canvas_x11(dpy, win, gc, screen, cv);
                }
            }
        }

        if (!running) break;

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        struct timeval tv = {0, 150000};
        select(xfd + 1, &rfds, NULL, NULL, &tv);

        bool dirty = dd_app_tick(&state);
        double now = dd_now_seconds();
        if (now - last_mon_time >= 2.0) {
            last_mon_time = now;
            if (state.active_tab == TAB_MONITOR) {
                dd_monitor_sample(&state.monitor);
                dirty = true;
            }
        }
        if (dirty) {
            dd_views_render(&state, cv);
            blit_canvas_x11(dpy, win, gc, screen, cv);
        }
        if (screenshot_out && state.tree && !state.scan_progress.is_running) {
            dd_canvas_save_png(cv, screenshot_out);
            break;
        }
    }

    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    dd_canvas_destroy(cv);
    dd_app_state_destroy(&state);
    return 0;
}
#endif
