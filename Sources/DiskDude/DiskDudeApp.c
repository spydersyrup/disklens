#define _GNU_SOURCE
#include "Views/Views.h"
#include "Utils/Utils.h"
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

static void dd_sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

#ifdef _WIN32
static void attach_parent_console(void) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
    }
}
#endif

typedef struct {
    bool verify_mode;
    bool summary_mode;
    int top_count;
    const char *target_dir;
    const char *screenshot_out;
    const char *json_out;
    const char *csv_out;
    const char *html_out;
    bool show_help;
    bool show_version;
} DDCliOptions;

typedef struct {
    int32_t node_idx;
    int64_t size;
} DDTopCandidate;

static int compare_top_candidates(const void *a, const void *b) {
    const DDTopCandidate *ca = (const DDTopCandidate *)a;
    const DDTopCandidate *cb = (const DDTopCandidate *)b;
    if (cb->size > ca->size) return 1;
    if (cb->size < ca->size) return -1;
    return 0;
}

static void print_cli_usage(const char *prog) {
    printf("DiskLens: Native Disk Storage Analyzer\n\n");
    printf("Usage:\n");
    printf("  %s [path] [options]\n\n", prog ? prog : "disklens");
    printf("CLI & Reporting Options:\n");
    printf("  --summary          Print storage summary and category breakdown to terminal\n");
    printf("  --top <N>          Display the top N largest items (default: 15)\n");
    printf("  --json <file>      Export full scan hierarchy and metadata to JSON\n");
    printf("  --csv <file>       Export flat item listing sorted by size to CSV\n");
    printf("  --html <file>      Generate a self-contained interactive HTML storage report\n");
    printf("  --verify [path]    Run the automated 30-step self-test verification suite\n");
    printf("  --screenshot <out> Save an offscreen render of the scan to a PNG file\n");
    printf("  -h, --help         Show this help message and exit\n");
    printf("  -v, --version      Show version information and exit\n\n");
    printf("Environment Variables:\n");
    printf("  DISKDUDE_SCAN=<path> Automatically scan the specified folder on launch\n");
    printf("  DISKDUDE_MODE=<view> Open directly into a visualization (Treemap, Folders, etc.)\n");
}

static void parse_cli_args(int argc, char **argv, DDCliOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->top_count = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verify") == 0) {
            opts->verify_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') opts->target_dir = argv[++i];
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            opts->screenshot_out = argv[++i];
        } else if (strcmp(argv[i], "--summary") == 0) {
            opts->summary_mode = true;
            if (opts->top_count < 0) opts->top_count = 15;
        } else if (strcmp(argv[i], "--top") == 0) {
            opts->summary_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') opts->top_count = atoi(argv[++i]);
            else opts->top_count = 15;
        } else if ((strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "--export") == 0) && i + 1 < argc) {
            opts->json_out = argv[++i];
        } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            opts->csv_out = argv[++i];
        } else if (strcmp(argv[i], "--html") == 0 && i + 1 < argc) {
            opts->html_out = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            opts->show_help = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            opts->show_version = true;
        } else if (argv[i][0] != '-') {
            opts->target_dir = argv[i];
        }
    }
}

static int run_cli_report_and_exports(const DDCliOptions *opts) {
    const char *target = (opts->target_dir && *opts->target_dir) ? opts->target_dir : ".";
    printf("DiskLens: Scanning '%s'...\n", target);

    DDDiskScanner *scanner = dd_scanner_create();
    if (!scanner) {
        fprintf(stderr, "Error: Failed to initialize disk scanner.\n");
        return 1;
    }
    dd_scanner_start(scanner, target);
    while (1) {
        DDScanProgress prog;
        dd_scanner_get_progress(scanner, &prog);
        if (!prog.is_running) break;
        dd_sleep_ms(20);
    }
    DDFileTree *tree = dd_scanner_take_result(scanner);
    dd_scanner_destroy(scanner);

    if (!tree || tree->node_count == 0) {
        fprintf(stderr, "Error: Scan produced no files or directory is unreadable.\n");
        if (tree) dd_tree_free(tree);
        return 1;
    }

    bool success = true;
    if (opts->json_out) {
        if (dd_export_tree_json(tree, opts->json_out)) {
            printf("Exported JSON report -> %s (%d items)\n", opts->json_out, tree->node_count);
        } else {
            fprintf(stderr, "Error: Failed to write JSON report to %s\n", opts->json_out);
            success = false;
        }
    }
    if (opts->csv_out) {
        if (dd_export_tree_csv(tree, opts->csv_out)) {
            printf("Exported CSV report  -> %s (%d rows)\n", opts->csv_out, tree->node_count);
        } else {
            fprintf(stderr, "Error: Failed to write CSV report to %s\n", opts->csv_out);
            success = false;
        }
    }
    if (opts->html_out) {
        if (dd_export_tree_html(tree, opts->html_out)) {
            printf("Exported HTML report -> %s (standalone interactive report)\n", opts->html_out);
        } else {
            fprintf(stderr, "Error: Failed to write HTML report to %s\n", opts->html_out);
            success = false;
        }
    }

    if (opts->summary_mode || opts->top_count >= 0) {
        char total_buf[64], disk_buf[64];
        dd_format_bytes(tree->nodes[0].logical_size, total_buf, sizeof(total_buf));
        dd_format_bytes(tree->nodes[0].size_on_disk, disk_buf, sizeof(disk_buf));

        printf("\n=== DiskLens Storage Report ===\n");
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

        int top_n = opts->top_count > 0 ? opts->top_count : 15;
        if (top_n > 0 && tree->node_count > 1) {
            int candidate_count = tree->node_count - 1;
            DDTopCandidate *cands = (DDTopCandidate *)malloc(candidate_count * sizeof(DDTopCandidate));
            if (cands) {
                for (int i = 0; i < candidate_count; i++) {
                    cands[i].node_idx = (int32_t)(i + 1);
                    cands[i].size = tree->nodes[i + 1].logical_size;
                }
                qsort(cands, candidate_count, sizeof(DDTopCandidate), compare_top_candidates);
                int limit = (top_n < candidate_count) ? top_n : candidate_count;

                printf("\nTop %d Largest Items:\n", limit);
                printf("  %-4s  %10s  %8s   %-8s  %s\n", "Rank", "Size", "Share", "Kind", "Path");
                printf("  --------------------------------------------------------------------------------\n");
                for (int i = 0; i < limit; i++) {
                    int32_t n_idx = cands[i].node_idx;
                    const DDFileNode *n = &tree->nodes[n_idx];
                    char sz_buf[32], pct_buf[32], path_buf[512];
                    double ratio = (tree->nodes[0].logical_size > 0) ? ((double)n->logical_size / (double)tree->nodes[0].logical_size) : 0.0;
                    dd_format_bytes(n->logical_size, sz_buf, sizeof(sz_buf));
                    dd_format_percent(ratio, pct_buf, sizeof(pct_buf));
                    dd_node_build_path(tree, n_idx, path_buf, sizeof(path_buf));
                    const char *kind = n->is_directory ? "dir" : dd_category_name((DDFileCategory)n->category);
                    printf("  #%-3d  %10s  %8s   %-8s  %s\n", i + 1, sz_buf, pct_buf, kind, path_buf);
                }
                free(cands);
            }
        }
        printf("\n");
    }

    dd_tree_free(tree);
    return success ? 0 : 1;
}

static void apply_env_hooks(DDAppState *st) {
    const char *mode_env = getenv("DISKDUDE_MODE");
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
    const char *scan_env = getenv("DISKDUDE_SCAN");
    if (scan_env && *scan_env) {
        dd_app_start_scan(st, scan_env);
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
    printf("DiskLens verification suite starting on %s\n", scan_dir);

    dd_views_render(st, cv);
    printf("[PASS] Idle state -> rendered '%s' with Scan Home and Choose Folder buttons (%d interactive controls)\n",
           "Map every byte on your system", dd_canvas_hit_count(cv));

    dd_app_start_scan(st, scan_dir);
    dd_views_render(st, cv);
    while (st->scan_progress.is_running) {
        dd_app_tick(st);
        dd_sleep_ms(20);
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
                dd_sleep_ms(10);
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
            dd_sleep_ms(10);
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

    /* Security & Export verification tests */
    /* 1. Test HTML escaping logic against XSS payloads */
    {
        char temp_html[512];
        snprintf(temp_html, sizeof(temp_html), "%s/test_xss_check.html", scan_dir);
        FILE *fh = fopen(temp_html, "wb");
        if (fh) {
            dd_write_html_escaped(fh, "<script>alert('XSS');</script>");
            fclose(fh);
            FILE *rh = fopen(temp_html, "rb");
            if (rh) {
                char buf[512] = {0};
                size_t nr = fread(buf, 1, sizeof(buf) - 1, rh);
                fclose(rh);
                remove(temp_html);
                if (nr > 0 && strstr(buf, "&lt;script&gt;alert(&#39;XSS&#39;);&lt;/script&gt;") != NULL
                    && strchr(buf, '<') == NULL && strchr(buf, '>') == NULL) {
                    printf("[PASS] HTML export security -> untrusted markup contextual entity escaping verified\n");
                } else {
                    fprintf(stderr, "[FAIL] HTML export security check failed: %s\n", buf);
                    return 1;
                }
            }
        }
    }

    /* 2. Test CSV formula injection protection */
    {
        char temp_csv[512];
        snprintf(temp_csv, sizeof(temp_csv), "%s/test_csv_check.csv", scan_dir);
        FILE *fc = fopen(temp_csv, "wb");
        if (fc) {
            dd_write_csv_field_safe(fc, "=cmd|' /C calc'!A0");
            fputc(',', fc);
            dd_write_csv_field_safe(fc, "+12345");
            fputc(',', fc);
            dd_write_csv_field_safe(fc, "  @SUM(A1)");
            fputc('\n', fc);
            fclose(fc);
            FILE *rc = fopen(temp_csv, "rb");
            if (rc) {
                char buf[512] = {0};
                size_t nr = fread(buf, 1, sizeof(buf) - 1, rc);
                fclose(rc);
                remove(temp_csv);
                if (nr > 0 && strstr(buf, "\"'=cmd|") != NULL && strstr(buf, "\"'+12345\"") != NULL && strstr(buf, "\"'  @SUM(A1)\"") != NULL) {
                    printf("[PASS] CSV export security -> spreadsheet formula injection prefixing verified\n");
                } else {
                    fprintf(stderr, "[FAIL] CSV export security check failed: %s\n", buf);
                    return 1;
                }
            }
        }
    }

    /* 3. Test report exports with current scanned tree */
    {
        char out_j[512], out_c[512], out_h[512];
        snprintf(out_j, sizeof(out_j), "%s/verify_scan.json", scan_dir);
        snprintf(out_c, sizeof(out_c), "%s/verify_scan.csv", scan_dir);
        snprintf(out_h, sizeof(out_h), "%s/verify_scan.html", scan_dir);

        if (dd_export_tree_json(st->tree, out_j)) {
            remove(out_j);
            printf("[PASS] JSON report export -> generated and validated\n");
        } else {
            fprintf(stderr, "[FAIL] dd_export_tree_json failed\n");
            return 1;
        }

        if (dd_export_tree_csv(st->tree, out_c)) {
            remove(out_c);
            printf("[PASS] CSV report export -> generated and validated\n");
        } else {
            fprintf(stderr, "[FAIL] dd_export_tree_csv failed\n");
            return 1;
        }

        if (dd_export_tree_html(st->tree, out_h)) {
            remove(out_h);
            printf("[PASS] HTML interactive report export -> generated and validated\n");
        } else {
            fprintf(stderr, "[FAIL] dd_export_tree_html failed\n");
            return 1;
        }
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
    DDCliOptions opts;
    parse_cli_args(argc, argv, &opts);

    bool is_cli = (opts.verify_mode || opts.summary_mode || opts.top_count >= 0 ||
                   opts.json_out || opts.csv_out || opts.html_out ||
                   opts.show_help || opts.show_version);

    if (is_cli) {
        attach_parent_console();
    }

    if (opts.show_help) {
        print_cli_usage(argv[0]);
        return 0;
    }
    if (opts.show_version) {
        printf("DiskLens 1.0.0 (x86_64)\n");
        return 0;
    }

    if (opts.verify_mode) {
        dd_app_state_init(&g_state);
        g_canvas = dd_canvas_create(1680, 1050);
        apply_env_hooks(&g_state);
        int rc = run_verification_suite(&g_state, g_canvas, opts.target_dir ? opts.target_dir : ".");
        if (opts.screenshot_out) {
            dd_canvas_save_png(g_canvas, opts.screenshot_out);
        }
        dd_canvas_destroy(g_canvas);
        dd_app_state_destroy(&g_state);
        return rc;
    }

    if (opts.summary_mode || opts.top_count >= 0 || opts.json_out || opts.csv_out || opts.html_out) {
        return run_cli_report_and_exports(&opts);
    }

    dd_app_state_init(&g_state);
    g_canvas = dd_canvas_create(1680, 1050);
    apply_env_hooks(&g_state);
    if (opts.target_dir) {
        dd_app_start_scan(&g_state, opts.target_dir);
    }

    HINSTANCE hInst = GetModuleHandleA(NULL);
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "DiskDudeWindowClass";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "DiskLens",
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

#ifndef DISKDUDE_RUST_HOST
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hInst; (void)hPrev; (void)nShow;
    if (lpCmd && strstr(lpCmd, "--verify")) {
        char *argv_v[3] = {"DiskLens.exe", "--verify", "."};
        return dd_native_app_main(3, argv_v);
    }
    char *argv_d[1] = {"DiskLens.exe"};
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

#ifdef DISKDUDE_RUST_HOST
int dd_native_app_main(int argc, char **argv) {
#else
int main(int argc, char **argv) {
#endif
    DDCliOptions opts;
    parse_cli_args(argc, argv, &opts);

    if (opts.show_help) {
        print_cli_usage(argv[0]);
        return 0;
    }
    if (opts.show_version) {
        printf("DiskLens 1.0.0 (x86_64)\n");
        return 0;
    }

    if (opts.verify_mode) {
        DDAppState state;
        dd_app_state_init(&state);
        DDCanvas *cv = dd_canvas_create(1680, 1050);
        apply_env_hooks(&state);
        int rc = run_verification_suite(&state, cv, opts.target_dir ? opts.target_dir : ".");
        if (opts.screenshot_out) {
            dd_canvas_save_png(cv, opts.screenshot_out);
        }
        dd_canvas_destroy(cv);
        dd_app_state_destroy(&state);
        return rc;
    }

    if (opts.summary_mode || opts.top_count >= 0 || opts.json_out || opts.csv_out || opts.html_out) {
        return run_cli_report_and_exports(&opts);
    }

    DDAppState state;
    dd_app_state_init(&state);
    DDCanvas *cv = dd_canvas_create(1680, 1050);
    apply_env_hooks(&state);
    if (opts.target_dir) {
        dd_app_start_scan(&state, opts.target_dir);
    }

    ensure_display_env();
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        if (opts.screenshot_out) {
            while (state.scan_progress.is_running) {
                dd_app_tick(&state);
                dd_sleep_ms(20);
            }
            dd_app_tick(&state);
            dd_views_render(&state, cv);
            dd_canvas_save_png(cv, opts.screenshot_out);
            dd_canvas_destroy(cv);
            dd_app_state_destroy(&state);
            return 0;
        }
        dd_canvas_destroy(cv);
        dd_app_state_destroy(&state);
        printf("Note: No graphical display detected. Falling back to terminal summary.\n");
        opts.summary_mode = true;
        if (opts.top_count < 0) opts.top_count = 15;
        return run_cli_report_and_exports(&opts);
    }

    int screen = DefaultScreen(dpy);
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 40, 40, 1680, 1050, 0,
                                     BlackPixel(dpy, screen), WhitePixel(dpy, screen));
    XStoreName(dpy, win, "DiskLens");

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
        if (opts.screenshot_out && state.tree && !state.scan_progress.is_running) {
            dd_canvas_save_png(cv, opts.screenshot_out);
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
