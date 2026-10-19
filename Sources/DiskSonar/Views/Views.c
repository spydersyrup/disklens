#define _GNU_SOURCE
#include "Views.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool str_contains_ci(const char *haystack, const char *needle) {
    if (!needle || !*needle) return true;
    if (!haystack || !*haystack) return false;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

static DDColor node_color_for_mode(const DDAppState *st, int32_t node_idx, int top_branch_idx, int depth) {
    const DDTheme *th = &st->theme;
    if (!st->tree || node_idx < 0 || node_idx >= st->tree->node_count) return th->categories[CAT_OTHER];
    const DDFileNode *n = &st->tree->nodes[node_idx];

    if (st->color_mode == COLOR_BY_TYPE) {
        DDFileCategory cat = dd_node_dominant_category(st->tree, node_idx);
        return dd_color_shade(th->categories[cat], (depth > 2) ? 0.08 * (depth - 2) : 0.0);
    } else if (st->color_mode == COLOR_BY_FOLDER) {
        int fam = (top_branch_idx >= 0) ? (top_branch_idx % 8) : (node_idx % 8);
        double shade = (depth > 1) ? ((depth % 3) * 0.07 - 0.04) : 0.0;
        return dd_color_shade(th->folder_families[fam], shade);
    } else {
        double now = dd_now_seconds();
        double days = (n->modified_stamp > 0.0 && now > n->modified_stamp) ? ((now - n->modified_stamp) / 86400.0) : 0.0;
        int b = 0;
        if (days <= 7.0) b = 0;
        else if (days <= 30.0) b = 1;
        else if (days <= 90.0) b = 2;
        else if (days <= 365.0) b = 3;
        else if (days <= 730.0) b = 4;
        else b = 5;
        return th->age_buckets[b];
    }
}

static DDIconType category_icon(DDFileCategory cat, bool is_dir) {
    if (is_dir) return ICON_FOLDER;
    return (DDIconType)(ICON_VIDEO + (int)cat);
}

static void draw_ink_button(DDCanvas *cv, const DDTheme *th, DDRect r, const char *label, int action, int p1, int p2, const char *pstr) {
    dd_canvas_round_rect(cv, r, 8.0, th->ink, th->control_border, 1.0);
    dd_canvas_text_center(cv, r, label, 13.0, true, false, th->on_ink);
    dd_canvas_register_hit(cv, action, p1, p2, pstr, label, r, true);
}

static void draw_outline_button(DDCanvas *cv, const DDTheme *th, DDRect r, const char *label, bool active, int action, int p1, int p2, const char *pstr) {
    DDColor fill = active ? th->ink : th->card;
    DDColor fg = active ? th->on_ink : th->text;
    dd_canvas_round_rect(cv, r, 8.0, fill, th->control_border, 1.0);
    dd_canvas_text_center(cv, r, label, 12.5, active, false, fg);
    dd_canvas_register_hit(cv, action, p1, p2, pstr, label, r, true);
}

static void draw_square_icon_button(DDCanvas *cv, const DDTheme *th, DDRect r, DDIconType icon, bool active, const char *label, int action, int p1) {
    DDColor fill = active ? th->ink : th->chip;
    DDColor fg = active ? th->on_ink : th->text;
    dd_canvas_round_rect(cv, r, 8.0, fill, th->control_border, 1.0);
    double isz = 16.0;
    dd_canvas_draw_icon(cv, icon, r.x + (r.w - isz) * 0.5, r.y + (r.h - isz) * 0.5, isz, fg);
    dd_canvas_register_hit(cv, action, p1, 0, NULL, label, r, true);
}

static void draw_section_caption(DDCanvas *cv, const DDTheme *th, double x, double y, const char *title) {
    dd_canvas_text(cv, x, y, title, 11.0, true, false, th->text_tertiary);
}

static bool is_folder_expanded(const DDAppState *st, int32_t node_idx) {
    for (int i = 0; i < st->expanded_count; i++) {
        if (st->expanded_folders[i] == node_idx) return true;
    }
    return false;
}

static void toggle_folder_expanded(DDAppState *st, int32_t node_idx) {
    for (int i = 0; i < st->expanded_count; i++) {
        if (st->expanded_folders[i] == node_idx) {
            for (int j = i; j < st->expanded_count - 1; j++) {
                st->expanded_folders[j] = st->expanded_folders[j + 1];
            }
            st->expanded_count--;
            return;
        }
    }
    if (st->expanded_count < 512) {
        st->expanded_folders[st->expanded_count++] = node_idx;
    }
}

static void set_current_folder(DDAppState *st, int32_t folder_idx) {
    if (!st || !st->tree || folder_idx < 0 || folder_idx >= st->tree->node_count) return;
    if (!st->tree->nodes[folder_idx].is_directory) {
        if (st->tree->nodes[folder_idx].parent_idx >= 0) {
            folder_idx = st->tree->nodes[folder_idx].parent_idx;
        } else {
            return;
        }
    }
    st->current_folder = folder_idx;
    st->selected_node = folder_idx;
    st->hovered_node = -1;
    st->scroll_y_main = 0;
    st->top_sizes_cache.valid = false;
    st->age_report.valid = false;
    dd_quick_wins_compute_for(st->tree, folder_idx, &st->quick_wins);
}

static void clamp_scroll(int *val, double content_h, double view_h, int *out_max) {
    int m = (content_h > view_h) ? (int)(content_h - view_h + 24.0) : 0;
    if (m < 0) m = 0;
    if (out_max) *out_max = m;
    if (*val > m) *val = m;
    if (*val < 0) *val = 0;
}

static bool is_node_staged(const DDAppState *st, int32_t node_idx) {
    if (!st || !st->tree || node_idx < 0 || node_idx >= st->tree->node_count) return false;
    if (st->cleanup_queue.count == 0) return false;
    bool has_unindexed = false;
    for (int i = 0; i < st->cleanup_queue.count; i++) {
        int32_t qidx = st->cleanup_queue.items[i].node_idx;
        if (qidx == node_idx) return true;
        if (qidx < 0) has_unindexed = true;
    }
    if (!has_unindexed) return false;
    char p[512];
    dd_node_build_path(st->tree, node_idx, p, sizeof(p));
    return dd_queue_contains_path(&st->cleanup_queue, p);
}

void dd_app_state_init(DDAppState *st) {
    memset(st, 0, sizeof(*st));
    dd_settings_load(&st->settings);
    dd_theme_init(&st->theme, st->settings.is_dark);
    st->scanner = dd_scanner_create();
    st->active_tab = TAB_EXPLORE;
    st->viz_mode = VIZ_FOLDERS;
    st->color_mode = COLOR_BY_FOLDER;
    st->depth_limit = 7;
    st->abbreviate_labels = false;
    st->show_inspector = true;
    st->show_sidebar_drawer = false;
    st->current_folder = 0;
    st->selected_node = 0;
    st->hovered_node = -1;
    st->focused_hit_idx = -1;
    st->last_win_w = 1680;
    st->last_win_h = 1050;
    dd_queue_init(&st->cleanup_queue);
    dd_get_primary_volume("/", &st->primary_vol);
    dd_monitor_init(&st->monitor);
    dd_snapshots_load_index(&st->snapshots);
}

void dd_app_state_destroy(DDAppState *st) {
    if (!st) return;
    dd_apps_cancel(&st->app_store);
    dd_scanner_destroy(st->scanner);
    dd_tree_free(st->tree);
    dd_duplicates_free(&st->duplicates);
}

void dd_app_start_scan(DDAppState *st, const char *path) {
    if (!st || !path || !*path) return;
    dd_duplicates_free(&st->duplicates);
    st->active_tab = TAB_EXPLORE;
    st->denied_banner_dismissed = false;
    st->hovered_node = -1;
    st->scroll_y_main = 0;
    dd_get_primary_volume(path, &st->primary_vol);
    dd_settings_add_recent(&st->settings, path);
    dd_scanner_start(st->scanner, path);
    dd_scanner_get_progress(st->scanner, &st->scan_progress);
}

bool dd_app_tick(DDAppState *st) {
    if (!st) return false;
    bool changed = false;

    bool dup_scanning = (st->duplicates.scanning != 0);
    if (dup_scanning || st->prev_dup_scanning != dup_scanning) {
        st->prev_dup_scanning = dup_scanning;
        changed = true;
    }

    bool apps_loading = (st->app_store.loading != 0);
    if (apps_loading || st->prev_apps_loading != apps_loading) {
        st->prev_apps_loading = apps_loading;
        changed = true;
    }

    DDScanProgress prev = st->scan_progress;
    dd_scanner_get_progress(st->scanner, &st->scan_progress);
    if (st->scan_progress.is_running || prev.is_running != st->scan_progress.is_running ||
        prev.files_scanned != st->scan_progress.files_scanned) {
        changed = true;
    }

    if (st->scan_progress.is_finished) {
        DDFileTree *new_tree = dd_scanner_take_result(st->scanner);
        if (new_tree) {
            dd_duplicates_free(&st->duplicates);
            if (st->tree) dd_tree_free_async(st->tree);
            st->tree = new_tree;
            st->current_folder = 0;
            st->selected_node = 0;
            st->hovered_node = -1;
            st->expanded_count = 0;
            st->expanded_folders[st->expanded_count++] = 0;
            st->top_sizes_cache.valid = false;
            st->age_report.valid = false;
            dd_quick_wins_compute_for(st->tree, 0, &st->quick_wins);
            dd_get_primary_volume(st->tree->root_path, &st->primary_vol);
            changed = true;
        }
    }
    return changed;
}

static void render_top_bar(DDAppState *st, DDCanvas *cv, int W) {
    const DDTheme *th = &st->theme;
    DDRect bar = {0, 0, (double)W, 56.0};
    dd_canvas_fill_rect(cv, bar, th->panel);
    dd_canvas_fill_rect(cv, (DDRect){0, 55.0, (double)W, 1.0}, th->divider);

    double x = 16.0;
    if (W < 860) {
        draw_outline_button(cv, th, (DDRect){x, 12.0, 58.0, 32.0}, "Menu", st->show_sidebar_drawer, ACT_TOGGLE_SIDEBAR_DRAWER, 0, 0, NULL);
        x += 66.0;
    } else {
        dd_canvas_text(cv, x, 34.0, "DiskSonar", 15.0, true, false, th->text);
        x += 84.0;
    }

    const char *tab_names[5] = {"Explore", "Duplicates", "Applications", "Monitor", "Snapshots"};
    const char *tab_short[5] = {"Exp", "Dup", "Apps", "Mon", "Snap"};
    bool compact_tabs = (W < 1180);

    for (int t = 0; t < 5; t++) {
        bool active = ((int)st->active_tab == t);
        const char *lbl = (compact_tabs && !active) ? tab_short[t] : tab_names[t];
        double tw = dd_canvas_measure_text(cv, lbl, 12.5, active, false) + (compact_tabs ? 20.0 : 26.0);
        DDRect tr = {x, 12.0, tw, 32.0};
        DDColor fill = active ? th->ink : th->chip;
        DDColor fg = active ? th->on_ink : th->text;
        dd_canvas_round_rect(cv, tr, 16.0, fill, active ? th->control_border : th->border, 1.0);
        dd_canvas_text_center(cv, tr, lbl, 12.5, active, false, fg);
        dd_canvas_register_hit(cv, ACT_SWITCH_TAB, t, 0, NULL, tab_names[t], tr, true);
        x += tw + 6.0;
    }

    double right_x = (double)W - 14.0;

    DDRect insp_btn = {right_x - 34.0, 11.0, 34.0, 34.0};
    draw_square_icon_button(cv, th, insp_btn, ICON_SIDEBAR_RIGHT, st->show_inspector && st->active_tab == TAB_EXPLORE, "Toggle Inspector", ACT_TOGGLE_INSPECTOR, 0);
    right_x -= 42.0;

    DDRect theme_btn = {right_x - 34.0, 11.0, 34.0, 34.0};
    draw_square_icon_button(cv, th, theme_btn, th->is_dark ? ICON_SUN : ICON_MOON, false, "Toggle Theme", ACT_TOGGLE_THEME, 0);
    right_x -= 42.0;

    DDRect help_btn = {right_x - 34.0, 11.0, 34.0, 34.0};
    dd_canvas_round_rect(cv, help_btn, 8.0, st->show_shortcuts_help ? th->ink : th->card, th->control_border, 1.0);
    dd_canvas_text_center(cv, help_btn, "?", 14.0, true, false, st->show_shortcuts_help ? th->on_ink : th->text);
    dd_canvas_register_hit(cv, ACT_TOGGLE_SHORTCUTS_HELP, 0, 0, NULL, "Shortcuts Help (?)", help_btn, true);
    right_x -= 42.0;

    double q_w = (W < 960) ? 74.0 : 148.0;
    DDRect q_btn = {right_x - q_w, 11.0, q_w, 34.0};
    dd_canvas_round_rect(cv, q_btn, 17.0, st->show_queue_popover ? th->ink : th->card, th->control_border, 1.0);
    DDColor q_fg = st->show_queue_popover ? th->on_ink : th->text;
    dd_canvas_draw_icon(cv, ICON_TRASH, q_btn.x + 10.0, q_btn.y + 9.0, 16.0, q_fg);
    if (W >= 960) {
        dd_canvas_text(cv, q_btn.x + 30.0, q_btn.y + 22.0, "Cleanup Queue", 12.0, true, false, q_fg);
    }
    if (st->cleanup_queue.count > 0) {
        char bstr[16];
        snprintf(bstr, sizeof(bstr), "%d", st->cleanup_queue.count);
        double bw = dd_canvas_measure_text(cv, bstr, 11.0, true, false) + 12.0;
        if (bw < 20.0) bw = 20.0;
        DDRect badge = {q_btn.x + q_btn.w - bw - 6.0, q_btn.y + 7.0, bw, 20.0};
        dd_canvas_round_rect(cv, badge, 10.0, th->used, th->on_ink, 1.0);
        dd_canvas_text_center(cv, badge, bstr, 11.0, true, false, dd_color_hex(0xFFFFFF, 1.0));
    }
    dd_canvas_register_hit(cv, ACT_TOGGLE_QUEUE_POPOVER, 0, 0, NULL, "Cleanup Queue", q_btn, true);
    right_x -= (q_w + 10.0);

    double search_w = (W >= 1280) ? 210.0 : (W >= 960 ? 150.0 : 110.0);
    DDRect s_box = {right_x - search_w, 11.0, search_w, 34.0};
    dd_canvas_round_rect(cv, s_box, 17.0, th->card, st->search_focused ? th->ink : th->control_border, st->search_focused ? 2.0 : 1.0);
    dd_canvas_draw_icon(cv, ICON_SEARCH, s_box.x + 10.0, s_box.y + 9.0, 15.0, th->text_secondary);
    if (st->search_query[0]) {
        dd_canvas_text_clipped(cv, (DDRect){s_box.x + 28.0, s_box.y, s_box.w - 54.0, s_box.h}, st->search_query, 12.0, false, false, th->text);
        DDRect clr = {s_box.x + s_box.w - 26.0, s_box.y + 7.0, 20.0, 20.0};
        dd_canvas_draw_icon(cv, ICON_CLOSE, clr.x + 3.0, clr.y + 3.0, 14.0, th->text_secondary);
        dd_canvas_register_hit(cv, ACT_CLEAR_SEARCH, 0, 0, NULL, "Clear Filter", clr, true);
    } else {
        dd_canvas_text_clipped(cv, (DDRect){s_box.x + 28.0, s_box.y, s_box.w - 64.0, s_box.h}, "Filter by name...", 12.0, false, false, th->text_tertiary);
        if (search_w >= 150.0) {
            dd_canvas_text_Right(cv, s_box.x + s_box.w - 10.0, s_box.y + 21.0, "Ctrl+K", 10.5, false, true, th->text_tertiary);
        }
    }
    dd_canvas_register_hit(cv, ACT_FOCUS_SEARCH, 0, 0, NULL, "Filter by name", (DDRect){s_box.x, s_box.y, s_box.w - 28.0, s_box.h}, true);
    right_x -= (search_w + 12.0);

    if (st->active_tab == TAB_EXPLORE && st->tree && x + 80.0 < right_x) {
        dd_canvas_fill_rect(cv, (DDRect){x + 2.0, 14.0, 1.0, 28.0}, th->divider);
        x += 12.0;

        bool can_back = (st->current_folder > 0);
        DDRect back_r = {x, 13.0, 28.0, 30.0};
        dd_canvas_round_rect(cv, back_r, 6.0, th->chip, th->border, 1.0);
        dd_canvas_draw_icon(cv, ICON_CHEVRON_LEFT, back_r.x + 6.0, back_r.y + 7.0, 16.0, can_back ? th->text : th->text_tertiary);
        if (can_back) {
            dd_canvas_register_hit(cv, ACT_NAV_BACK, 0, 0, NULL, "Navigate Up", back_r, true);
        }
        x += 34.0;

        int32_t chain[64];
        int clen = 0;
        int32_t cur = st->current_folder;
        while (cur >= 0 && clen < 64) {
            chain[clen++] = cur;
            cur = st->tree->nodes[cur].parent_idx;
        }
        int start = clen > 4 ? 4 : clen;
        for (int i = start - 1; i >= 0; i--) {
            int32_t nidx = chain[i];
            const char *nm = dd_node_name(st->tree, nidx);
            double nw = dd_canvas_measure_text(cv, nm, 12.0, i == 0, false) + 16.0;
            if (nw > 140.0) nw = 140.0;
            if (x + nw > right_x) break;
            DDRect cr = {x, 14.0, nw, 28.0};
            dd_canvas_round_rect(cv, cr, 6.0, (i == 0) ? th->card : th->chip, th->border, 1.0);
            dd_canvas_text_clipped(cv, (DDRect){cr.x + 8.0, cr.y, cr.w - 16.0, cr.h}, nm, 12.0, i == 0, false, th->text);
            dd_canvas_register_hit(cv, ACT_BREADCRUMB, nidx, 0, NULL, nm, cr, true);
            x += nw + 4.0;
            if (i > 0 && x + 12.0 < right_x) {
                dd_canvas_text(cv, x, 32.0, "/", 12.0, false, false, th->text_tertiary);
                x += 10.0;
            }
        }
    }
}

static void render_sidebar(DDAppState *st, DDCanvas *cv, DDRect sb) {
    const DDTheme *th = &st->theme;
    dd_canvas_push_clip(cv, sb);
    dd_canvas_fill_rect(cv, sb, th->panel);
    dd_canvas_fill_rect(cv, (DDRect){sb.x + sb.w - 1.0, sb.y, 1.0, sb.h}, th->divider);

    double x = sb.x + 14.0;
    double w = sb.w - 28.0;
    double y0 = sb.y + 14.0 - st->scroll_y_sidebar;
    double y = y0;
    double now = dd_now_seconds();

    draw_ink_button(cv, th, (DDRect){x, y, w, 38.0}, "Scan Full System", ACT_SCAN_ROOT, 0, 0, NULL);
    y += 46.0;

    double hw = (w - 8.0) * 0.5;
    draw_outline_button(cv, th, (DDRect){x, y, hw, 34.0}, "Home", false, ACT_SCAN_HOME, 0, 0, NULL);
    draw_outline_button(cv, th, (DDRect){x + hw + 8.0, y, hw, 34.0}, "Folder...", false, ACT_SCAN_PICK, 0, 0, NULL);
    y += 46.0;

    if (st->settings.recent_count > 0) {
        draw_section_caption(cv, th, x, y + 10.0, "RECENT");
        y += 18.0;
        for (int i = 0; i < st->settings.recent_count; i++) {
            DDRect rr = {x, y, w, 26.0};
            dd_canvas_round_rect(cv, rr, 6.0, th->card, th->border, 1.0);
            char trunc[96];
            dd_truncate_middle(st->settings.recent_paths[i], 36, trunc, sizeof(trunc));
            dd_canvas_draw_icon(cv, ICON_FOLDER, rr.x + 8.0, rr.y + 6.0, 14.0, th->ink);
            dd_canvas_text_clipped(cv, (DDRect){rr.x + 26.0, rr.y, rr.w - 32.0, rr.h}, trunc, 11.5, false, true, th->text);
            dd_canvas_register_hit(cv, ACT_SCAN_RECENT, i, 0, st->settings.recent_paths[i], trunc, rr, true);
            y += 30.0;
        }
        y += 8.0;
    }

    draw_section_caption(cv, th, x, y + 10.0, "DISK STORAGE");
    y += 18.0;
    DDRect disk_card = {x, y, w, 108.0};
    dd_canvas_round_rect(cv, disk_card, 10.0, th->card, th->border, 1.0);

    double ring_cx = x + 48.0, ring_cy = y + 54.0;
    double used_ratio = (st->primary_vol.total_bytes > 0)
        ? ((double)st->primary_vol.used_bytes / (double)st->primary_vol.total_bytes) : 0.0;
    if (used_ratio > 1.0) used_ratio = 1.0;
    dd_canvas_arc_ring(cv, ring_cx, ring_cy, 24.0, 34.0, 0.0, 2.0 * M_PI, th->track, dd_color_hex(0,0));
    if (used_ratio > 0.01) {
        dd_canvas_arc_ring(cv, ring_cx, ring_cy, 24.0, 34.0, -M_PI * 0.5, -M_PI * 0.5 + used_ratio * 2.0 * M_PI, th->ink, dd_color_hex(0,0));
    }
    char pct_str[32];
    snprintf(pct_str, sizeof(pct_str), "%.0f%%", used_ratio * 100.0);
    dd_canvas_text_center(cv, (DDRect){ring_cx - 24.0, ring_cy - 14.0, 48.0, 18.0}, pct_str, 11.5, true, false, th->text);
    dd_canvas_text_center(cv, (DDRect){ring_cx - 24.0, ring_cy + 1.0, 48.0, 14.0}, "USED", 8.5, true, false, th->text_tertiary);

    double tx = x + 96.0;
    dd_canvas_text_clipped(cv, (DDRect){tx, y + 8.0, w - 104.0, 18.0}, st->primary_vol.name, 12.5, true, false, th->text);
    char b_tot[32], b_used[32], b_free[32];
    dd_format_bytes(st->primary_vol.total_bytes, b_tot, sizeof(b_tot));
    dd_format_bytes(st->primary_vol.used_bytes, b_used, sizeof(b_used));
    dd_format_bytes(st->primary_vol.free_bytes, b_free, sizeof(b_free));

    dd_canvas_text(cv, tx, y + 44.0, "Total", 11.5, false, false, th->text_secondary);
    dd_canvas_text_Right(cv, x + w - 12.0, y + 44.0, b_tot, 11.5, true, true, th->text);
    dd_canvas_text(cv, tx, y + 66.0, "Used", 11.5, false, false, th->used_text);
    dd_canvas_text_Right(cv, x + w - 12.0, y + 66.0, b_used, 11.5, true, true, th->used_text);
    dd_canvas_text(cv, tx, y + 88.0, "Free", 11.5, false, false, th->free_text);
    dd_canvas_text_Right(cv, x + w - 12.0, y + 88.0, b_free, 11.5, true, true, th->free_text);
    y += 120.0;

    if (st->tree) {
        draw_section_caption(cv, th, x, y + 10.0, "CURRENT VIEW");
        y += 18.0;
        DDRect cv_card = {x, y, w, 92.0};
        dd_canvas_round_rect(cv, cv_card, 10.0, th->card, th->border, 1.0);
        const char *fname = dd_node_name(st->tree, st->current_folder);
        char dur_str[32], dur_lbl[48];
        dd_format_duration(st->tree->scan_duration, dur_str, sizeof(dur_str));
        snprintf(dur_lbl, sizeof(dur_lbl), "%s scan", dur_str);
        dd_canvas_text_clipped(cv, (DDRect){x + 10.0, y + 8.0, w - 90.0, 20.0}, fname, 13.0, true, false, th->text);
        dd_canvas_text_Right(cv, x + w - 10.0, y + 22.0, dur_lbl, 11.0, false, true, th->text_tertiary);

        char full_p[512], trunc_p[96];
        dd_node_build_path(st->tree, st->current_folder, full_p, sizeof(full_p));
        dd_truncate_middle(full_p, 38, trunc_p, sizeof(trunc_p));
        dd_canvas_text_clipped(cv, (DDRect){x + 10.0, y + 30.0, w - 20.0, 18.0}, trunc_p, 11.0, false, true, th->text_secondary);

        double bw = (w - 26.0) * 0.5;
        draw_outline_button(cv, th, (DDRect){x + 10.0, y + 54.0, bw, 28.0}, "Reveal", false, ACT_REVEAL_CURRENT, 0, 0, NULL);
        const char *cp_lbl = (now < st->copy_confirm_until) ? "Copied ✓" : "Copy Path";
        draw_outline_button(cv, th, (DDRect){x + 16.0 + bw, y + 54.0, bw, 28.0}, cp_lbl, now < st->copy_confirm_until, ACT_COPY_CURRENT_PATH, 0, 0, NULL);
        y += 104.0;

        char qw_hdr[64], qw_sz[32];
        dd_format_bytes(st->quick_wins.grand_total, qw_sz, sizeof(qw_sz));
        snprintf(qw_hdr, sizeof(qw_hdr), "QUICK WINS (%s)", qw_sz);
        draw_section_caption(cv, th, x, y + 10.0, qw_hdr);
        y += 18.0;

        if (st->quick_wins.group_count == 0) {
            DDRect empty_qw = {x, y, w, 36.0};
            dd_canvas_round_rect(cv, empty_qw, 8.0, th->card, th->border, 1.0);
            dd_canvas_text_center(cv, empty_qw, "No junk or large media found in this folder", 11.5, false, false, th->text_secondary);
            y += 44.0;
        } else {
            for (int i = 0; i < st->quick_wins.group_count; i++) {
                const DDQuickWinGroup *g = &st->quick_wins.groups[i];
                bool all_staged = (g->item_count > 0);
                for (int k = 0; k < g->item_count && k < 16; k++) {
                    if (!is_node_staged(st, g->items[k])) {
                        all_staged = false;
                        break;
                    }
                }
                DDRect gr = {x, y, w, 34.0};
                dd_canvas_round_rect(cv, gr, 8.0, th->card, th->border, 1.0);
                dd_canvas_draw_icon(cv, ICON_FOLDER, x + 8.0, y + 9.0, 15.0, th->ink);
                char g_title[96], g_sz[32];
                snprintf(g_title, sizeof(g_title), "%s (%d)", g->title, g->item_count);
                dd_format_bytes(g->total_size, g_sz, sizeof(g_sz));
                dd_canvas_text_clipped(cv, (DDRect){x + 28.0, y, w - 120.0, 34.0}, g_title, 12.0, false, false, th->text);
                dd_canvas_text_Right(cv, x + w - 34.0, y + 21.0, g_sz, 11.5, true, true, th->text_secondary);

                dd_canvas_register_hit(cv, ACT_QUICK_WIN_CLICK, i, 0, NULL, g->title, (DDRect){x, y, w - 30.0, 34.0}, true);

                DDRect stg_btn = {x + w - 28.0, y + 5.0, 24.0, 24.0};
                dd_canvas_round_rect(cv, stg_btn, 6.0, all_staged ? th->ink : th->chip, th->control_border, 1.0);
                dd_canvas_draw_icon(cv, all_staged ? ICON_CHECK : ICON_PLUS, stg_btn.x + 5.0, stg_btn.y + 5.0, 14.0, all_staged ? th->on_ink : th->text);
                dd_canvas_register_hit(cv, ACT_QUICK_WIN_STAGE_ALL, i, 0, NULL, "Stage Quick Win Group", stg_btn, true);
                y += 38.0;
            }
            y += 6.0;
        }

        draw_section_caption(cv, th, x, y + 10.0, "FILE TYPES");
        y += 18.0;
        int32_t root_fidx = st->tree->nodes[st->current_folder].folder_idx;
        if (root_fidx >= 0) {
            const DDFolderExtra *fe = &st->tree->folders[root_fidx];
            int64_t tot_sz = st->tree->nodes[st->current_folder].size_on_disk;
            DDRect bar_r = {x, y, w, 12.0};
            dd_canvas_round_rect(cv, bar_r, 6.0, th->track, th->border, 1.0);
            if (tot_sz > 0) {
                double bx = x;
                for (int c = 0; c < CAT_COUNT; c++) {
                    double cw = (double)fe->type_sizes[c] / (double)tot_sz * w;
                    if (cw >= 1.0) {
                        dd_canvas_fill_rect(cv, (DDRect){bx, y, cw, 12.0}, th->categories[c]);
                        bx += cw;
                    }
                }
            }
            y += 20.0;
            for (int c = 0; c < CAT_COUNT; c++) {
                if (fe->type_sizes[c] <= 0 && c != CAT_OTHER) continue;
                dd_canvas_circle(cv, x + 6.0, y + 9.0, 4.5, th->categories[c], th->border, 0.5);
                dd_canvas_text(cv, x + 16.0, y + 13.0, dd_category_name((DDFileCategory)c), 11.5, false, false, th->text);
                char csz[32];
                dd_format_bytes(fe->type_sizes[c], csz, sizeof(csz));
                dd_canvas_text_Right(cv, x + w, y + 13.0, csz, 11.5, false, true, th->text_secondary);
                y += 19.0;
            }
        }
    }
    clamp_scroll(&st->scroll_y_sidebar, y - y0, sb.h, &st->max_scroll_sidebar);
    dd_canvas_pop_clip(cv);
}

typedef struct {
    int32_t node_idx;
    int64_t size;
    int top_branch;
    char synth_name[64];
    DDColor synth_col;
    bool is_synth;
} VizItem;

static void draw_treemap_cells(DDAppState *st, DDCanvas *cv, DDRect box, int32_t folder_node_idx, int depth, int max_depth, int branch_idx) {
    if (box.w * box.h < 6.0 || box.w < 2.0 || box.h < 2.0 || depth > max_depth) return;
    const DDFileTree *tree = st->tree;
    const DDTheme *th = &st->theme;
    const DDFileNode *fn = &tree->nodes[folder_node_idx];
    if (!fn->is_directory || fn->folder_idx < 0) return;
    const DDFolderExtra *fe = &tree->folders[fn->folder_idx];
    if (fe->child_count == 0) return;

    int count = fe->child_count < 64 ? fe->child_count : 64;
    int64_t sum = 0;
    for (int i = 0; i < count; i++) {
        int32_t cidx = fe->children[i];
        if (!tree->nodes[cidx].is_deleted) sum += tree->nodes[cidx].size_on_disk;
    }
    if (sum <= 0) return;

    DDRect rem = box;
    int64_t rem_bytes = sum;

    int i = 0;
    while (i < count && rem.w >= 2.0 && rem.h >= 2.0 && rem_bytes > 0) {
        bool horiz_split = (rem.w >= rem.h);
        double short_side = horiz_split ? rem.h : rem.w;
        if (short_side < 2.0) break;

        int row_end = i;
        int64_t row_bytes = 0;
        double best_aspect = 1e18;

        for (int j = i; j < count; j++) {
            int32_t cidx = fe->children[j];
            int64_t csz = tree->nodes[cidx].size_on_disk;
            if (csz <= 0 || tree->nodes[cidx].is_deleted) continue;
            int64_t cand_bytes = row_bytes + csz;
            double strip_thick = ((double)cand_bytes / (double)rem_bytes) * (horiz_split ? rem.w : rem.h);
            if (strip_thick < 1.0) strip_thick = 1.0;

            double worst = 0.0;
            for (int k = i; k <= j; k++) {
                int64_t ksz = tree->nodes[fe->children[k]].size_on_disk;
                if (ksz <= 0) continue;
                double klen = ((double)ksz / (double)cand_bytes) * short_side;
                if (klen < 0.5) klen = 0.5;
                double asp = (strip_thick > klen) ? (strip_thick / klen) : (klen / strip_thick);
                if (asp > worst) worst = asp;
            }
            if (worst <= best_aspect || j == i) {
                best_aspect = worst;
                row_bytes = cand_bytes;
                row_end = j;
            } else {
                break;
            }
        }

        if (row_bytes <= 0) break;
        double strip_frac = (double)row_bytes / (double)rem_bytes;
        if (strip_frac > 1.0) strip_frac = 1.0;
        double strip_thick = strip_frac * (horiz_split ? rem.w : rem.h);

        double pos = horiz_split ? rem.y : rem.x;
        for (int k = i; k <= row_end; k++) {
            int32_t cidx = fe->children[k];
            const DDFileNode *cn = &tree->nodes[cidx];
            if (cn->is_deleted || cn->size_on_disk <= 0) continue;
            double clen = ((double)cn->size_on_disk / (double)row_bytes) * short_side;
            DDRect cell = horiz_split
                ? (DDRect){rem.x, pos, strip_thick, clen}
                : (DDRect){pos, rem.y, clen, strip_thick};
            pos += clen;

            if (cell.w * cell.h < 6.0) continue;

            const char *nm = dd_node_name(tree, cidx);
            int top_b = (depth == 1) ? k : branch_idx;
            DDColor col = node_color_for_mode(st, cidx, top_b, depth);
            if (st->search_query[0] && !cn->is_directory && !str_contains_ci(nm, st->search_query)) {
                col.a = 0.28;
            }
            DDRect pad = {cell.x + 1.0, cell.y + 1.0, cell.w - 2.0, cell.h - 2.0};
            if (pad.w <= 1.0 || pad.h <= 1.0) pad = cell;

            bool is_sel = (st->selected_node == cidx);
            bool is_hov = (st->hovered_node == cidx);
            bool is_match = (st->search_query[0] && str_contains_ci(nm, st->search_query));
            dd_canvas_round_rect(cv, pad, 4.0, col,
                                 (is_sel || is_hov || is_match) ? th->pastel_text_primary : dd_color_hex(0x0F172A, 0.22),
                                 (is_sel || is_hov || is_match) ? 2.0 : 0.8);
            dd_canvas_add_viz_rect(cv, cidx, pad);

            char ab[16];
            if (st->abbreviate_labels) {
                dd_abbreviate_label(nm, ab, sizeof(ab));
                nm = ab;
            }

            if (cn->is_directory && depth < max_depth && pad.w > 36.0 && pad.h > 28.0) {
                dd_canvas_text_clipped(cv, (DDRect){pad.x + 5.0, pad.y + 2.0, pad.w - 10.0, 16.0}, nm, 10.5, true, false, th->pastel_text_primary);
                DDRect inner = {pad.x + 3.0, pad.y + 18.0, pad.w - 6.0, pad.h - 21.0};
                draw_treemap_cells(st, cv, inner, cidx, depth + 1, max_depth, top_b);
            } else if (pad.w > 32.0 && pad.h > 18.0) {
                dd_canvas_text_clipped(cv, (DDRect){pad.x + 5.0, pad.y + 3.0, pad.w - 10.0, 16.0}, nm, 11.0, true, false, th->pastel_text_primary);
                if (pad.h > 34.0 && pad.w > 48.0) {
                    char sz_s[32];
                    dd_format_bytes(cn->size_on_disk, sz_s, sizeof(sz_s));
                    dd_canvas_text_clipped(cv, (DDRect){pad.x + 5.0, pad.y + 19.0, pad.w - 10.0, 14.0}, sz_s, 10.0, false, true, th->pastel_text_secondary);
                }
            }
        }

        if (horiz_split) {
            rem.x += strip_thick;
            rem.w -= strip_thick;
        } else {
            rem.y += strip_thick;
            rem.h -= strip_thick;
        }
        rem_bytes -= row_bytes;
        i = row_end + 1;
    }
}

static void render_viz_treemap(DDAppState *st, DDCanvas *cv, DDRect area) {
    dd_canvas_clear_viz_hits(cv);
    st->max_scroll_main = 0;
    DDRect pad = {area.x + 12.0, area.y + 8.0, area.w - 24.0, area.h - 16.0};
    draw_treemap_cells(st, cv, pad, st->current_folder, 1, st->depth_limit, 0);
}

static void render_viz_folders(DDAppState *st, DDCanvas *cv, DDRect area) {
    const DDTheme *th = &st->theme;
    const DDFileTree *tree = st->tree;
    dd_canvas_clear_viz_hits(cv);
    dd_canvas_push_clip(cv, area);

    int32_t fidx = tree->nodes[st->current_folder].folder_idx;
    if (fidx < 0) {
        dd_canvas_pop_clip(cv);
        return;
    }
    const DDFolderExtra *fe = &tree->folders[fidx];

    double x0 = area.x + 16.0;
    double avail_w = area.w - 32.0;
    int cols = (int)(avail_w / 260.0);
    if (cols < 1) cols = 1;
    if (cols > 4) cols = 4;
    double gap = 12.0;
    double card_w = (avail_w - gap * (cols - 1)) / cols;
    double card_h = 92.0;
    double y0 = area.y + 12.0 - st->scroll_y_main;
    double y = y0;

    draw_section_caption(cv, th, x0, y + 10.0, "FOLDERS  (CLICK TO OPEN)");
    y += 20.0;

    int col = 0;
    int folder_shown = 0;
    for (int32_t i = 0; i < fe->child_count && folder_shown < 400; i++) {
        int32_t cidx = fe->children[i];
        const DDFileNode *cn = &tree->nodes[cidx];
        if (!cn->is_directory || cn->is_deleted) continue;
        const char *nm = dd_node_name(tree, cidx);
        if (!str_contains_ci(nm, st->search_query)) continue;

        double cx = x0 + col * (card_w + gap);
        DDRect cr = {cx, y, card_w, card_h};
        if (cr.y + cr.h >= area.y && cr.y <= area.y + area.h) {
            DDColor fill = th->folder_families[folder_shown % 8];
            bool hov = (st->hovered_node == cidx);
            bool sel = (st->selected_node == cidx);
            bool staged = is_node_staged(st, cidx);
            dd_canvas_folder_card(cv, cr, fill, (sel || hov) ? th->pastel_text_primary : dd_color_hex(0x0F172A, 0.28), hov);
            dd_canvas_add_viz_rect(cv, cidx, (DDRect){cr.x, cr.y, cr.w - 34.0, cr.h});

            dd_canvas_text_clipped(cv, (DDRect){cr.x + 12.0, cr.y + 14.0, cr.w - 44.0, 22.0}, nm, 13.0, true, false, th->pastel_text_primary);
            DDRect sbtn = {cr.x + cr.w - 32.0, cr.y + 13.0, 22.0, 22.0};
            dd_canvas_round_rect(cv, sbtn, 5.0, staged ? th->ink : dd_color_hex(0xFFFFFF, 0.45), dd_color_hex(0x0F172A, 0.25), 0.8);
            dd_canvas_draw_icon(cv, staged ? ICON_CHECK : ICON_PLUS, sbtn.x + 4.0, sbtn.y + 4.0, 14.0, staged ? th->on_ink : th->pastel_text_primary);
            dd_canvas_register_hit(cv, ACT_STAGE_NODE, cidx, 0, NULL, "Stage Folder", sbtn, true);

            dd_canvas_fill_rect(cv, (DDRect){cr.x + 12.0, cr.y + 44.0, cr.w - 24.0, 1.0}, dd_color_hex(0x0F172A, 0.15));

            DDFileCategory top_cats[3];
            int tc_count = 0;
            dd_node_top_categories(tree, cidx, 3, top_cats, &tc_count);
            for (int d = 0; d < tc_count; d++) {
                dd_canvas_circle(cv, cr.x + 16.0 + d * 9.0, cr.y + 66.0, 5.0, th->categories[top_cats[d]], th->pastel_text_primary, 0.8);
            }
            int32_t cfidx = cn->folder_idx;
            int32_t nitems = (cfidx >= 0) ? tree->folders[cfidx].child_count : 0;
            char items_s[48], sz_s[32];
            snprintf(items_s, sizeof(items_s), "%d item%s", (int)nitems, nitems == 1 ? "" : "s");
            dd_format_bytes(cn->size_on_disk, sz_s, sizeof(sz_s));
            dd_canvas_text(cv, cr.x + 18.0 + tc_count * 10.0, cr.y + 71.0, items_s, 11.5, false, false, th->pastel_text_secondary);
            dd_canvas_text_Right(cv, cr.x + cr.w - 12.0, cr.y + 71.0, sz_s, 13.0, true, true, th->pastel_text_primary);
        }
        folder_shown++;
        col++;
        if (col >= cols) {
            col = 0;
            y += card_h + gap;
        }
    }
    if (col > 0) y += card_h + gap;
    if (folder_shown == 0) {
        dd_canvas_text(cv, x0, y + 16.0, "No subfolders match the current filter.", 12.0, false, false, th->text_secondary);
        y += 32.0;
    }

    y += 12.0;
    draw_section_caption(cv, th, x0, y + 10.0, "FILES");
    y += 20.0;

    col = 0;
    int files_shown = 0;
    double tile_h = 62.0;
    for (int32_t i = 0; i < fe->child_count && files_shown < 600; i++) {
        int32_t cidx = fe->children[i];
        const DDFileNode *cn = &tree->nodes[cidx];
        if (cn->is_directory || cn->is_deleted) continue;
        const char *nm = dd_node_name(tree, cidx);
        if (!str_contains_ci(nm, st->search_query)) continue;

        double cx = x0 + col * (card_w + gap);
        DDRect tr = {cx, y, card_w, tile_h};
        if (tr.y + tr.h >= area.y && tr.y <= area.y + area.h) {
            bool sel = (st->selected_node == cidx);
            bool hov = (st->hovered_node == cidx);
            bool staged = is_node_staged(st, cidx);
            dd_canvas_round_rect(cv, tr, 8.0, th->card, (sel || hov) ? th->ink : th->border, (sel || hov) ? 2.0 : 1.0);
            dd_canvas_add_viz_rect(cv, cidx, (DDRect){tr.x, tr.y, tr.w - 34.0, tr.h});

            DDRect ibox = {tr.x + 10.0, tr.y + 13.0, 36.0, 36.0};
            dd_canvas_round_rect(cv, ibox, 8.0, th->categories[cn->category], dd_color_hex(0x0F172A, 0.2), 1.0);
            dd_canvas_draw_icon(cv, category_icon((DDFileCategory)cn->category, false), ibox.x + 10.0, ibox.y + 10.0, 16.0, th->pastel_text_primary);

            char sz_s[32], age_s[48], meta_s[96];
            dd_format_bytes(cn->size_on_disk, sz_s, sizeof(sz_s));
            dd_format_relative_age(cn->modified_stamp, age_s, sizeof(age_s));
            snprintf(meta_s, sizeof(meta_s), "%s · %s", dd_category_name((DDFileCategory)cn->category), age_s);

            dd_canvas_text_clipped(cv, (DDRect){tr.x + 54.0, tr.y + 10.0, tr.w - 150.0, 20.0}, nm, 12.5, true, false, th->text);
            dd_canvas_text_clipped(cv, (DDRect){tr.x + 54.0, tr.y + 30.0, tr.w - 150.0, 18.0}, meta_s, 11.0, false, false, th->text_secondary);
            dd_canvas_text_Right(cv, tr.x + tr.w - 36.0, tr.y + 36.0, sz_s, 12.0, true, true, th->text);

            DDRect sbtn = {tr.x + tr.w - 30.0, tr.y + 19.0, 22.0, 22.0};
            dd_canvas_round_rect(cv, sbtn, 5.0, staged ? th->ink : th->chip, th->control_border, 0.8);
            dd_canvas_draw_icon(cv, staged ? ICON_CHECK : ICON_PLUS, sbtn.x + 4.0, sbtn.y + 4.0, 14.0, staged ? th->on_ink : th->text);
            dd_canvas_register_hit(cv, ACT_STAGE_NODE, cidx, 0, NULL, "Stage File", sbtn, true);
        }
        files_shown++;
        col++;
        if (col >= cols) {
            col = 0;
            y += tile_h + 10.0;
        }
    }
    if (col > 0) y += tile_h + 10.0;
    if (files_shown == 0) {
        dd_canvas_text(cv, x0, y + 16.0, "No files in this directory match the current view.", 12.0, false, false, th->text_secondary);
        y += 32.0;
    }

    clamp_scroll(&st->scroll_y_main, y - y0, area.h, &st->max_scroll_main);
    dd_canvas_pop_clip(cv);
}

static void draw_sunburst_arcs(DDAppState *st, DDCanvas *cv, double cx, double cy, double r_base, double ring_w,
                               int32_t folder_idx, int depth, int max_depth, double a0, double a1, int branch_idx) {
    if (depth > max_depth || a1 - a0 < 0.008) return;
    const DDFileTree *tree = st->tree;
    const DDTheme *th = &st->theme;
    const DDFileNode *fn = &tree->nodes[folder_idx];
    if (!fn->is_directory || fn->folder_idx < 0) return;
    const DDFolderExtra *fe = &tree->folders[fn->folder_idx];
    int64_t psize = fn->size_on_disk;
    if (psize <= 0) return;

    double r_in = r_base + (depth - 1) * ring_w;
    double r_out = r_in + ring_w - 2.0;
    double cur_a = a0;

    for (int32_t i = 0; i < fe->child_count && i < 64; i++) {
        int32_t cidx = fe->children[i];
        const DDFileNode *cn = &tree->nodes[cidx];
        if (cn->is_deleted || cn->size_on_disk <= 0) continue;
        double span = ((double)cn->size_on_disk / (double)psize) * (a1 - a0);
        if (span < 0.012) break;

        const char *nm = dd_node_name(tree, cidx);
        int top_b = (depth == 1) ? i : branch_idx;
        DDColor col = node_color_for_mode(st, cidx, top_b, depth);
        if (st->search_query[0] && !cn->is_directory && !str_contains_ci(nm, st->search_query)) {
            col.a = 0.28;
        }
        bool sel = (st->selected_node == cidx || st->hovered_node == cidx);
        dd_canvas_arc_ring(cv, cx, cy, r_in, r_out, cur_a, cur_a + span - 0.004, col,
                           sel ? th->pastel_text_primary : dd_color_hex(0x0F172A, 0.25));
        dd_canvas_add_viz_arc(cv, cidx, cx, cy, r_in, r_out, cur_a, cur_a + span);

        double mid_a = cur_a + span * 0.5;
        double mid_r = (r_in + r_out) * 0.5;
        if (span * mid_r > 42.0 && ring_w > 20.0) {
            char ab[16];
            if (st->abbreviate_labels || span * mid_r < 75.0) {
                dd_abbreviate_label(nm, ab, sizeof(ab));
                nm = ab;
            }
            double lx = cx + cos(mid_a) * mid_r;
            double ly = cy + sin(mid_a) * mid_r;
            dd_canvas_text_center(cv, (DDRect){lx - 34.0, ly - 9.0, 68.0, 18.0}, nm, 10.5, true, false, th->pastel_text_primary);
        }

        if (cn->is_directory && depth < max_depth) {
            draw_sunburst_arcs(st, cv, cx, cy, r_base, ring_w, cidx, depth + 1, max_depth, cur_a, cur_a + span, top_b);
        }
        cur_a += span;
    }
}

static void render_viz_sunburst(DDAppState *st, DDCanvas *cv, DDRect area) {
    const DDTheme *th = &st->theme;
    dd_canvas_clear_viz_hits(cv);
    st->max_scroll_main = 0;
    double cx = area.x + area.w * 0.5;
    double cy = area.y + area.h * 0.5;
    double max_r = fmin(area.w, area.h) * 0.46;
    double r_center = max_r * 0.24;
    if (r_center < 48.0) r_center = 48.0;
    int levels = st->depth_limit < 7 ? st->depth_limit : 7;
    double ring_w = (max_r - r_center) / (double)levels;

    draw_sunburst_arcs(st, cv, cx, cy, r_center + 4.0, ring_w, st->current_folder, 1, levels, -M_PI * 0.5, 1.5 * M_PI, 0);

    dd_canvas_circle(cv, cx, cy, r_center, th->card, th->border, 1.5);
    if (st->current_folder > 0) {
        dd_canvas_register_hit(cv, ACT_NAV_BACK, 0, 0, NULL, "Navigate Up",
                               (DDRect){cx - r_center * 0.75, cy - r_center * 0.75, r_center * 1.5, r_center * 1.5}, true);
    } else {
        dd_canvas_add_viz_circle(cv, st->current_folder, cx, cy, r_center);
    }
    const char *rname = dd_node_name(st->tree, st->current_folder);
    char rsz[32];
    dd_format_bytes(st->tree->nodes[st->current_folder].size_on_disk, rsz, sizeof(rsz));
    dd_canvas_text_clipped(cv, (DDRect){cx - r_center + 8.0, cy - 22.0, (r_center - 8.0) * 2.0, 20.0}, rname, 12.0, true, false, th->text);
    dd_canvas_text_center(cv, (DDRect){cx - r_center, cy - 2.0, r_center * 2.0, 18.0}, rsz, 11.5, true, true, th->ink);
    if (st->current_folder > 0) {
        dd_canvas_text_center(cv, (DDRect){cx - r_center, cy + 15.0, r_center * 2.0, 16.0}, "↑ Click for Parent", 9.5, true, false, th->text_secondary);
    }
}

static void draw_flame_bars(DDAppState *st, DDCanvas *cv, double x0, double w0, double y_top, double row_h,
                            int32_t folder_idx, int depth, int max_depth, int branch_idx) {
    if (depth > max_depth || w0 < 1.0) return;
    const DDFileTree *tree = st->tree;
    const DDTheme *th = &st->theme;
    const DDFileNode *fn = &tree->nodes[folder_idx];
    if (!fn->is_directory || fn->folder_idx < 0) return;
    const DDFolderExtra *fe = &tree->folders[fn->folder_idx];
    int64_t psize = fn->size_on_disk;
    if (psize <= 0) return;

    double cx = x0;
    double ry = y_top + (depth - 1) * (row_h + 3.0);
    for (int32_t i = 0; i < fe->child_count && i < 96; i++) {
        int32_t cidx = fe->children[i];
        const DDFileNode *cn = &tree->nodes[cidx];
        if (cn->is_deleted || cn->size_on_disk <= 0) continue;
        double bw = ((double)cn->size_on_disk / (double)psize) * w0;
        if (bw < 1.0) break;

        const char *nm = dd_node_name(tree, cidx);
        int top_b = (depth == 1) ? i : branch_idx;
        DDColor col = node_color_for_mode(st, cidx, top_b, depth);
        if (st->search_query[0] && !cn->is_directory && !str_contains_ci(nm, st->search_query)) {
            col.a = 0.28;
        }
        DDRect br = {cx, ry, bw > 2.0 ? bw - 1.0 : bw, row_h};
        bool sel = (st->selected_node == cidx || st->hovered_node == cidx);
        dd_canvas_round_rect(cv, br, 4.0, col, sel ? th->pastel_text_primary : dd_color_hex(0x0F172A, 0.2), sel ? 2.0 : 0.8);
        dd_canvas_add_viz_rect(cv, cidx, br);

        if (br.w > 28.0) {
            char ab[16];
            if (st->abbreviate_labels) {
                dd_abbreviate_label(nm, ab, sizeof(ab));
                nm = ab;
            }
            dd_canvas_text_clipped(cv, (DDRect){br.x + 5.0, br.y, br.w - 10.0, br.h}, nm, 11.0, true, false, th->pastel_text_primary);
        }
        if (cn->is_directory && depth < max_depth) {
            draw_flame_bars(st, cv, cx, bw, y_top, row_h, cidx, depth + 1, max_depth, top_b);
        }
        cx += bw;
    }
}

static void render_viz_flame(DDAppState *st, DDCanvas *cv, DDRect area) {
    const DDTheme *th = &st->theme;
    dd_canvas_clear_viz_hits(cv);
    st->max_scroll_main = 0;
    double x0 = area.x + 14.0;
    double w0 = area.w - 28.0;
    int levels = st->depth_limit;
    double row_h = (area.h - 56.0) / (double)levels - 3.0;
    if (row_h > 38.0) row_h = 38.0;
    if (row_h < 22.0) row_h = 22.0;

    DDRect root_bar = {x0, area.y + 10.0, w0, 30.0};
    dd_canvas_round_rect(cv, root_bar, 6.0, th->ink, th->control_border, 1.0);
    char rlbl[256];
    if (st->current_folder > 0) {
        snprintf(rlbl, sizeof(rlbl), "↑ %s  (click to navigate up)", dd_node_name(st->tree, st->current_folder));
        dd_canvas_register_hit(cv, ACT_NAV_BACK, 0, 0, NULL, "Navigate Up", root_bar, true);
    } else {
        snprintf(rlbl, sizeof(rlbl), "%s", dd_node_name(st->tree, st->current_folder));
        dd_canvas_add_viz_rect(cv, st->current_folder, root_bar);
    }
    dd_canvas_text_clipped(cv, (DDRect){root_bar.x + 10.0, root_bar.y, root_bar.w - 20.0, root_bar.h},
                           rlbl, 12.5, true, false, th->on_ink);

    draw_flame_bars(st, cv, x0, w0, area.y + 44.0, row_h, st->current_folder, 1, levels, 0);
}

static void draw_packed_bubbles(DDAppState *st, DDCanvas *cv, double cx, double cy, double R, int32_t folder_idx, int depth, int branch_idx) {
    if (R < 5.0 || depth > 3) return;
    const DDFileTree *tree = st->tree;
    const DDTheme *th = &st->theme;
    const DDFileNode *fn = &tree->nodes[folder_idx];
    if (!fn->is_directory || fn->folder_idx < 0) return;
    const DDFolderExtra *fe = &tree->folders[fn->folder_idx];
    int64_t psize = fn->size_on_disk;
    if (psize <= 0 || fe->child_count == 0) return;

    int count = fe->child_count < 9 ? fe->child_count : 9;
    for (int i = 0; i < count; i++) {
        int32_t cidx = fe->children[i];
        const DDFileNode *cn = &tree->nodes[cidx];
        if (cn->is_deleted || cn->size_on_disk <= 0) continue;
        double ratio = (double)cn->size_on_disk / (double)psize;
        double bx = cx, by = cy, cr = R * 0.65;

        if (count == 1) {
            cr = R * 0.72;
        } else if (i == 0) {
            cr = R * (0.24 + 0.10 * sqrt(ratio));
            if (cr > R * 0.34) cr = R * 0.34;
            bx = cx;
            by = cy;
        } else {
            int ring_n = count - 1;
            double ang = -M_PI * 0.5 + (i - 1) * (2.0 * M_PI / (double)ring_n);
            double ring_r = R * 0.64;
            double max_ring_cr = (ring_n > 1) ? fmin(R * 0.21, ring_r * sin(M_PI / (double)ring_n) * 0.88) : (R * 0.24);
            cr = sqrt(ratio) * R * 0.48;
            if (cr < 7.0) cr = 7.0;
            if (cr > max_ring_cr) cr = max_ring_cr;
            bx = cx + cos(ang) * ring_r;
            by = cy + sin(ang) * ring_r;
        }

        const char *nm = dd_node_name(tree, cidx);
        int top_b = (depth == 1) ? i : branch_idx;
        DDColor col = node_color_for_mode(st, cidx, top_b, depth);
        col.a = cn->is_directory ? 0.62 : 0.92;
        if (st->search_query[0] && !cn->is_directory && !str_contains_ci(nm, st->search_query)) {
            col.a = 0.22;
        }
        bool sel = (st->selected_node == cidx || st->hovered_node == cidx);
        dd_canvas_circle(cv, bx, by, cr, col, sel ? th->pastel_text_primary : dd_color_hex(0x0F172A, 0.35), sel ? 2.0 : 1.0);
        dd_canvas_add_viz_circle(cv, cidx, bx, by, cr);

        if (cn->is_directory && depth < 3 && cr > 28.0) {
            draw_packed_bubbles(st, cv, bx, by, cr * 0.86, cidx, depth + 1, top_b);
        }
        if (cr > 18.0 && (!cn->is_directory || depth >= 3 || cr <= 28.0)) {
            char ab[16];
            if (st->abbreviate_labels || cr < 32.0) {
                dd_abbreviate_label(nm, ab, sizeof(ab));
                nm = ab;
            }
            dd_canvas_text_clipped(cv, (DDRect){bx - cr + 4.0, by - 9.0, (cr - 4.0) * 2.0, 18.0}, nm, 10.5, true, false, th->pastel_text_primary);
        }
    }
}

static void render_viz_bubbles(DDAppState *st, DDCanvas *cv, DDRect area) {
    const DDTheme *th = &st->theme;
    dd_canvas_clear_viz_hits(cv);
    st->max_scroll_main = 0;
    double cx = area.x + area.w * 0.5;
    double cy = area.y + area.h * 0.5;
    double R = fmin(area.w, area.h) * 0.45;
    dd_canvas_circle(cv, cx, cy, R, th->chip, th->border, 1.5);
    dd_canvas_add_viz_circle(cv, st->current_folder, cx, cy, R);
    draw_packed_bubbles(st, cv, cx, cy, R * 0.94, st->current_folder, 1, 0);
}

static void render_viz_mindmap(DDAppState *st, DDCanvas *cv, DDRect area) {
    const DDTheme *th = &st->theme;
    const DDFileTree *tree = st->tree;
    dd_canvas_clear_viz_hits(cv);
    st->max_scroll_main = 0;

    double rx = area.x + 90.0;
    double ry = area.y + area.h * 0.5;
    int32_t root_idx = st->current_folder;
    int32_t fidx = tree->nodes[root_idx].folder_idx;
    if (fidx < 0) return;
    const DDFolderExtra *fe = &tree->folders[fidx];
    int64_t rsize = tree->nodes[root_idx].size_on_disk;
    if (rsize <= 0) rsize = 1;

    int l1_count = fe->child_count < 10 ? fe->child_count : 10;
    double col1_x = area.x + area.w * 0.44;
    double col2_x = area.x + area.w * 0.78;
    double l2_step = (l1_count > 1) ? fmin(22.0, ((area.h - 88.0) / (double)(l1_count - 1)) / 3.1) : 22.0;

    for (int i = 0; i < l1_count; i++) {
        int32_t cidx = fe->children[i];
        const DDFileNode *cn = &tree->nodes[cidx];
        if (cn->is_deleted) continue;
        double frac = (l1_count == 1) ? 0.5 : ((double)i / (double)(l1_count - 1));
        double cy = area.y + 44.0 + frac * (area.h - 88.0);
        double share = (double)cn->size_on_disk / (double)rsize;
        double lw = 1.5 + share * 10.0;
        DDColor col = node_color_for_mode(st, cidx, i, 1);

        dd_canvas_bezier(cv, rx, ry, (rx + col1_x) * 0.5, ry, (rx + col1_x) * 0.5, cy, col1_x, cy, col, lw);

        if (cn->is_directory && cn->folder_idx >= 0) {
            const DDFolderExtra *cfe = &tree->folders[cn->folder_idx];
            int l2_n = cfe->child_count < 3 ? cfe->child_count : 3;
            for (int j = 0; j < l2_n; j++) {
                int32_t gcidx = cfe->children[j];
                const DDFileNode *gcn = &tree->nodes[gcidx];
                if (gcn->is_deleted) continue;
                double gcy = cy + (j - (l2_n - 1) * 0.5) * l2_step;
                if (gcy < area.y + 18.0 || gcy > area.y + area.h - 18.0) continue;
                DDColor gcol = node_color_for_mode(st, gcidx, i, 2);
                dd_canvas_bezier(cv, col1_x, cy, (col1_x + col2_x) * 0.5, cy, (col1_x + col2_x) * 0.5, gcy, col2_x, gcy, gcol, 1.6);
                dd_canvas_circle(cv, col2_x, gcy, 6.0, gcol, th->pastel_text_primary, 1.0);
                dd_canvas_add_viz_circle(cv, gcidx, col2_x, gcy, 10.0);

                const char *gnm = dd_node_name(tree, gcidx);
                char gab[16];
                if (st->abbreviate_labels) {
                    dd_abbreviate_label(gnm, gab, sizeof(gab));
                    gnm = gab;
                }
                dd_canvas_text_clipped(cv, (DDRect){col2_x + 12.0, gcy - 9.0, area.x + area.w - col2_x - 20.0, 18.0}, gnm, 11.0, false, false, th->text);
            }
        }

        double rad = 7.0 + sqrt(share) * 14.0;
        dd_canvas_circle(cv, col1_x, cy, rad, col, th->pastel_text_primary, 1.4);
        dd_canvas_add_viz_circle(cv, cidx, col1_x, cy, rad + 4.0);

        const char *nm = dd_node_name(tree, cidx);
        char ab[16], sz_s[32], lbl[160];
        if (st->abbreviate_labels) {
            dd_abbreviate_label(nm, ab, sizeof(ab));
            nm = ab;
        }
        dd_format_bytes(cn->size_on_disk, sz_s, sizeof(sz_s));
        snprintf(lbl, sizeof(lbl), "%s (%s)", nm, sz_s);
        DDRect lbox = {col1_x + rad + 8.0, cy - 11.0, 170.0, 22.0};
        dd_canvas_round_rect(cv, lbox, 6.0, th->card, th->border, 1.0);
        dd_canvas_text_clipped(cv, (DDRect){lbox.x + 6.0, lbox.y, lbox.w - 12.0, lbox.h}, lbl, 11.0, true, false, th->text);
        dd_canvas_add_viz_rect(cv, cidx, lbox);
    }

    dd_canvas_circle(cv, rx, ry, 26.0, th->ink, th->control_border, 1.5);
    if (st->current_folder > 0) {
        dd_canvas_register_hit(cv, ACT_NAV_BACK, 0, 0, NULL, "Navigate Up", (DDRect){rx - 26.0, ry - 26.0, 52.0, 52.0}, true);
    } else {
        dd_canvas_add_viz_circle(cv, root_idx, rx, ry, 26.0);
    }
    dd_canvas_text_clipped(cv, (DDRect){rx - 40.0, ry + 30.0, 100.0, 20.0}, dd_node_name(tree, root_idx), 12.0, true, false, th->text);
}

static void render_viz_topsizes(DDAppState *st, DDCanvas *cv, DDRect area) {
    const DDTheme *th = &st->theme;
    const DDFileTree *tree = st->tree;
    dd_canvas_clear_viz_hits(cv);
    dd_canvas_push_clip(cv, area);

    dd_top_sizes_compute(tree, st->current_folder, &st->top_sizes_cache);

    double x0 = area.x + 16.0;
    double w0 = area.w - 32.0;
    double y = area.y + 10.0;

    const char *scopes[3] = {"In this folder", "Biggest files anywhere", "Biggest folders anywhere"};
    double sx = x0;
    for (int s = 0; s < 3; s++) {
        bool act = ((int)st->top_scope == s);
        double sw = dd_canvas_measure_text(cv, scopes[s], 12.0, act, false) + 24.0;
        draw_outline_button(cv, th, (DDRect){sx, y, sw, 30.0}, scopes[s], act, ACT_TOP_SCOPE, s, 0, NULL);
        sx += sw + 8.0;
    }
    y += 40.0;

    DDRect list_area = {area.x, y, area.w, area.h - (y - area.y)};
    dd_canvas_push_clip(cv, list_area);
    double y0 = y - st->scroll_y_main;
    y = y0;

    int count = 0;
    int32_t folder_fidx = tree->nodes[st->current_folder].folder_idx;
    if (st->top_scope == SCOPE_THIS_FOLDER && folder_fidx >= 0) {
        count = tree->folders[folder_fidx].child_count;
        if (count > 200) count = 200;
    } else if (st->top_scope == SCOPE_FILES_ANYWHERE) {
        count = st->top_sizes_cache.top_files_count;
    } else if (st->top_scope == SCOPE_FOLDERS_ANYWHERE) {
        count = st->top_sizes_cache.top_folders_count;
    }

    int64_t max_sz = 1;
    int64_t folder_total = tree->nodes[st->current_folder].size_on_disk;
    if (folder_total <= 0) folder_total = 1;

    int rank = 1;
    for (int i = 0; i < count; i++) {
        int32_t nidx = -1;
        if (st->top_scope == SCOPE_THIS_FOLDER) nidx = tree->folders[folder_fidx].children[i];
        else if (st->top_scope == SCOPE_FILES_ANYWHERE) nidx = st->top_sizes_cache.top_files[i].node_idx;
        else nidx = st->top_sizes_cache.top_folders[i].node_idx;

        if (nidx < 0 || tree->nodes[nidx].is_deleted) continue;
        const DDFileNode *n = &tree->nodes[nidx];
        const char *nm = dd_node_name(tree, nidx);
        if (!str_contains_ci(nm, st->search_query)) continue;
        if (rank == 1 && n->size_on_disk > 0) max_sz = n->size_on_disk;

        DDRect row = {x0, y, w0, 38.0};
        if (row.y + row.h >= list_area.y && row.y <= list_area.y + list_area.h) {
            bool sel = (st->selected_node == nidx || st->hovered_node == nidx);
            bool staged = is_node_staged(st, nidx);
            dd_canvas_round_rect(cv, row, 8.0, th->card, sel ? th->ink : th->border, sel ? 1.8 : 1.0);
            dd_canvas_add_viz_rect(cv, nidx, (DDRect){x0, y, w0 - 40.0, 38.0});

            char rstr[16];
            snprintf(rstr, sizeof(rstr), "#%d", rank);
            dd_canvas_text(cv, x0 + 10.0, y + 24.0, rstr, 11.5, true, true, th->text_tertiary);

            DDFileCategory cat = dd_node_dominant_category(tree, nidx);
            dd_canvas_draw_icon(cv, category_icon(cat, n->is_directory), x0 + 42.0, y + 11.0, 16.0, th->ink);

            double bar_x = x0 + 66.0;
            double bar_max_w = w0 - 284.0;
            if (bar_max_w < 80.0) bar_max_w = 80.0;
            double fill_w = ((double)n->size_on_disk / (double)max_sz) * bar_max_w;
            if (fill_w < 6.0) fill_w = 6.0;
            dd_canvas_round_rect(cv, (DDRect){bar_x, y + 6.0, fill_w, 26.0}, 6.0, th->categories[cat], dd_color_hex(0x0F172A, 0.18), 0.8);

            char label_buf[256];
            if (st->top_scope != SCOPE_THIS_FOLDER && n->parent_idx >= 0) {
                snprintf(label_buf, sizeof(label_buf), "%s  (%s)", nm, dd_node_name(tree, n->parent_idx));
            } else {
                snprintf(label_buf, sizeof(label_buf), "%s", nm);
            }
            dd_canvas_text_clipped(cv, (DDRect){bar_x + 8.0, y + 6.0, bar_max_w - 16.0, 26.0}, label_buf, 12.0, true, false, th->pastel_text_primary);

            char kind_s[48], pct_s[24], sz_s[32];
            if (n->is_directory && n->folder_idx >= 0) {
                snprintf(kind_s, sizeof(kind_s), "%d files", (int)tree->folders[n->folder_idx].file_count);
            } else {
                snprintf(kind_s, sizeof(kind_s), "%s", dd_category_name(cat));
            }
            double share = (double)n->size_on_disk / (double)folder_total;
            if (share > 1.0) share = 1.0;
            dd_format_percent(share, pct_s, sizeof(pct_s));
            dd_format_bytes(n->size_on_disk, sz_s, sizeof(sz_s));

            dd_canvas_text_Right(cv, x0 + w0 - 152.0, y + 24.0, kind_s, 11.0, false, false, th->text_secondary);
            dd_canvas_text_Right(cv, x0 + w0 - 102.0, y + 24.0, pct_s, 11.0, false, true, th->text_tertiary);
            dd_canvas_text_Right(cv, x0 + w0 - 42.0, y + 24.0, sz_s, 12.0, true, true, th->text);

            DDRect sbtn = {x0 + w0 - 32.0, y + 7.0, 24.0, 24.0};
            dd_canvas_round_rect(cv, sbtn, 6.0, staged ? th->ink : th->chip, th->control_border, 1.0);
            dd_canvas_draw_icon(cv, staged ? ICON_CHECK : ICON_PLUS, sbtn.x + 5.0, sbtn.y + 5.0, 14.0, staged ? th->on_ink : th->text);
            dd_canvas_register_hit(cv, ACT_STAGE_NODE, nidx, 0, NULL, "Stage Item", sbtn, true);
        }
        rank++;
        y += 42.0;
    }

    clamp_scroll(&st->scroll_y_main, y - y0, list_area.h, &st->max_scroll_main);
    dd_canvas_pop_clip(cv);
    dd_canvas_pop_clip(cv);
}

static void render_viz_agemap(DDAppState *st, DDCanvas *cv, DDRect area) {
    const DDTheme *th = &st->theme;
    const DDFileTree *tree = st->tree;
    dd_canvas_clear_viz_hits(cv);
    dd_canvas_push_clip(cv, area);

    dd_age_report_compute(tree, st->current_folder, &st->age_report);
    const DDAgeReport *rep = &st->age_report;

    double x0 = area.x + 16.0;
    double w0 = area.w - 32.0;
    double y0 = area.y + 12.0 - st->scroll_y_main;
    double y = y0;

    dd_canvas_text(cv, x0, y + 14.0, "How old are these bytes?", 14.0, true, false, th->text);
    y += 24.0;

    const char *b_names[6] = {"Last 7 days", "8-30 days", "1-3 months", "3-12 months", "1-2 years", "Over 2 years"};
    int64_t max_b = 1;
    for (int b = 0; b < 6; b++) if (rep->bucket_bytes[b] > max_b) max_b = rep->bucket_bytes[b];

    double bw = (w0 - 50.0) / 6.0;
    for (int b = 0; b < 6; b++) {
        DDRect bc = {x0 + b * (bw + 10.0), y, bw, 74.0};
        dd_canvas_round_rect(cv, bc, 8.0, th->card, th->border, 1.0);
        dd_canvas_fill_rect(cv, (DDRect){bc.x + 8.0, bc.y + 8.0, (bc.w - 16.0) * ((double)rep->bucket_bytes[b] / (double)max_b), 8.0}, th->age_buckets[b]);
        dd_canvas_text_clipped(cv, (DDRect){bc.x + 8.0, bc.y + 22.0, bc.w - 16.0, 18.0}, b_names[b], 11.0, false, false, th->text_secondary);
        char bsz[32];
        dd_format_bytes(rep->bucket_bytes[b], bsz, sizeof(bsz));
        dd_canvas_text_clipped(cv, (DDRect){bc.x + 8.0, bc.y + 44.0, bc.w - 16.0, 20.0}, bsz, 13.0, true, true, th->text);
    }
    y += 92.0;

    static const char * const months[12] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char hm_hdr[128];
    if (rep->busiest_bytes > 0) {
        char bus_sz[32];
        dd_format_bytes(rep->busiest_bytes, bus_sz, sizeof(bus_sz));
        snprintf(hm_hdr, sizeof(hm_hdr), "Bytes by last-modified month  (Busiest: %s %d, %s)", months[rep->busiest_month], rep->busiest_year, bus_sz);
    } else {
        snprintf(hm_hdr, sizeof(hm_hdr), "Bytes by last-modified month");
    }
    dd_canvas_text(cv, x0, y + 14.0, hm_hdr, 14.0, true, false, th->text);
    y += 24.0;

    double cell_w = (w0 - 60.0) / 12.0;
    for (int m = 0; m < 12; m++) {
        dd_canvas_text(cv, x0 + 52.0 + m * cell_w, y + 12.0, months[m], 10.5, false, false, th->text_tertiary);
    }
    y += 18.0;

    int64_t hm_max = rep->busiest_bytes > 0 ? rep->busiest_bytes : 1;
    for (int yr = 0; yr < rep->year_count; yr++) {
        char ystr[16];
        snprintf(ystr, sizeof(ystr), "%d", rep->years[yr]);
        dd_canvas_text(cv, x0, y + 16.0, ystr, 11.0, false, true, th->text_secondary);
        for (int m = 0; m < 12; m++) {
            double intensity = (double)rep->heatmap[yr][m] / (double)hm_max;
            DDColor c = (intensity <= 0.0) ? th->track : dd_color_hex(0xFF6B4A, 0.22 + 0.78 * sqrt(intensity));
            dd_canvas_round_rect(cv, (DDRect){x0 + 50.0 + m * cell_w, y + 2.0, cell_w - 4.0, 18.0}, 4.0, c, th->border, 0.6);
        }
        y += 22.0;
    }
    y += 18.0;

    dd_canvas_text(cv, x0, y + 18.0, "Big & Untouched (>= 40 MB, not modified in over 1 year)", 14.0, true, false, th->text);
    if (rep->big_untouched_count > 0) {
        char btn_lbl[64];
        snprintf(btn_lbl, sizeof(btn_lbl), "Stage top %d for cleanup", rep->big_untouched_count);
        draw_outline_button(cv, th, (DDRect){x0 + w0 - 200.0, y, 200.0, 28.0}, btn_lbl, false, ACT_STAGE_TOP_UNTOUCHED, 0, 0, NULL);
    }
    y += 36.0;

    if (rep->big_untouched_count == 0) {
        DDRect emp = {x0, y, w0, 44.0};
        dd_canvas_round_rect(cv, emp, 8.0, th->card, th->border, 1.0);
        dd_canvas_text_center(cv, emp, "No files >= 40 MB older than 1 year in this folder.", 12.0, false, false, th->text_secondary);
        y += 50.0;
    } else {
        for (int i = 0; i < rep->big_untouched_count; i++) {
            int32_t nidx = rep->big_untouched[i].node_idx;
            if (nidx < 0 || tree->nodes[nidx].is_deleted) continue;
            const char *nm = dd_node_name(tree, nidx);
            if (!str_contains_ci(nm, st->search_query)) continue;
            DDRect r = {x0, y, w0, 36.0};
            if (r.y + r.h >= area.y && r.y <= area.y + area.h) {
                const DDFileNode *n = &tree->nodes[nidx];
                char sz_s[32], age_s[48];
                dd_format_bytes(n->size_on_disk, sz_s, sizeof(sz_s));
                dd_format_relative_age(n->modified_stamp, age_s, sizeof(age_s));
                bool staged = is_node_staged(st, nidx);

                dd_canvas_round_rect(cv, r, 8.0, th->card, th->border, 1.0);
                dd_canvas_add_viz_rect(cv, nidx, (DDRect){x0, y, w0 - 44.0, 36.0});
                dd_canvas_text_clipped(cv, (DDRect){x0 + 12.0, y, w0 - 210.0, 36.0}, nm, 12.0, true, false, th->text);
                dd_canvas_text_Right(cv, x0 + w0 - 118.0, y + 23.0, age_s, 11.0, false, false, th->text_secondary);
                dd_canvas_text_Right(cv, x0 + w0 - 46.0, y + 23.0, sz_s, 12.0, true, true, th->text);

                DDRect sbtn = {x0 + w0 - 34.0, y + 5.0, 26.0, 26.0};
                dd_canvas_round_rect(cv, sbtn, 6.0, staged ? th->ink : th->chip, th->control_border, 1.0);
                dd_canvas_draw_icon(cv, staged ? ICON_CHECK : ICON_PLUS, sbtn.x + 6.0, sbtn.y + 6.0, 14.0, staged ? th->on_ink : th->text);
                dd_canvas_register_hit(cv, ACT_STAGE_NODE, nidx, 1, NULL, "Toggle Stage Untouched File", sbtn, true);
            }
            y += 40.0;
        }
    }

    clamp_scroll(&st->scroll_y_main, y - y0, area.h, &st->max_scroll_main);
    dd_canvas_pop_clip(cv);
}

static void draw_list_outline(DDAppState *st, DDCanvas *cv, DDRect area, double x0, double w0, double *io_y, int32_t folder_idx, int depth) {
    const DDFileTree *tree = st->tree;
    const DDTheme *th = &st->theme;
    int32_t fidx = tree->nodes[folder_idx].folder_idx;
    if (fidx < 0) return;
    const DDFolderExtra *fe = &tree->folders[fidx];
    int64_t psize = tree->nodes[folder_idx].size_on_disk;
    if (psize <= 0) psize = 1;

    int limit = fe->child_count < 300 ? fe->child_count : 300;
    int32_t sorted[300];
    int scount = 0;
    for (int i = 0; i < limit; i++) {
        int32_t cidx = fe->children[i];
        if (!tree->nodes[cidx].is_deleted) sorted[scount++] = cidx;
    }

    if (st->list_sort_col != 0 || st->list_sort_asc) {
        for (int i = 0; i < scount - 1; i++) {
            for (int j = i + 1; j < scount; j++) {
                const DDFileNode *na = &tree->nodes[sorted[i]];
                const DDFileNode *nb = &tree->nodes[sorted[j]];
                int cmp = 0;
                if (st->list_sort_col == 1) {
                    cmp = strcasecmp(dd_node_name(tree, sorted[i]), dd_node_name(tree, sorted[j]));
                } else if (st->list_sort_col == 2) {
                    cmp = (nb->modified_stamp > na->modified_stamp) - (nb->modified_stamp < na->modified_stamp);
                } else {
                    cmp = (nb->size_on_disk > na->size_on_disk) - (nb->size_on_disk < na->size_on_disk);
                }
                if (st->list_sort_asc) cmp = -cmp;
                if (cmp > 0) {
                    int32_t t = sorted[i];
                    sorted[i] = sorted[j];
                    sorted[j] = t;
                }
            }
        }
    }

    for (int i = 0; i < scount; i++) {
        int32_t cidx = sorted[i];
        const DDFileNode *cn = &tree->nodes[cidx];
        const char *nm = dd_node_name(tree, cidx);
        if (st->search_query[0] && !cn->is_directory && !str_contains_ci(nm, st->search_query)) continue;

        double y = *io_y;
        DDRect row = {x0, y, w0, 32.0};
        if (row.y + row.h >= area.y && row.y <= area.y + area.h) {
            bool sel = (st->selected_node == cidx || st->hovered_node == cidx);
            bool staged = is_node_staged(st, cidx);
            dd_canvas_round_rect(cv, row, 6.0, sel ? th->chip : th->card, sel ? th->ink : th->border, sel ? 1.5 : 0.8);
            dd_canvas_add_viz_rect(cv, cidx, (DDRect){x0, y, w0 - 38.0, 32.0});

            double indent = x0 + 10.0 + depth * 20.0;
            bool has_children = (cn->is_directory && cn->folder_idx >= 0 && tree->folders[cn->folder_idx].child_count > 0);
            bool exp = is_folder_expanded(st, cidx);
            if (has_children) {
                DDRect disc = {indent, y + 6.0, 20.0, 20.0};
                dd_canvas_draw_icon(cv, exp ? ICON_CHEVRON_DOWN : ICON_CHEVRON_RIGHT, disc.x + 3.0, disc.y + 3.0, 14.0, th->text_secondary);
                dd_canvas_register_hit(cv, ACT_TOGGLE_LIST_EXPAND, cidx, 0, NULL, "Expand Folder", disc, true);
            }
            indent += 22.0;

            DDFileCategory cat = dd_node_dominant_category(tree, cidx);
            dd_canvas_draw_icon(cv, category_icon(cat, cn->is_directory), indent, y + 8.0, 15.0, cn->is_directory ? th->ink : th->categories[cat]);
            dd_canvas_text_clipped(cv, (DDRect){indent + 20.0, y, w0 - (indent - x0) - 310.0, 32.0}, nm, 12.0, cn->is_directory, false, th->text);

            char mod_s[48];
            dd_format_relative_age(cn->modified_stamp, mod_s, sizeof(mod_s));
            dd_canvas_text_Right(cv, x0 + w0 - 216.0, y + 21.0, mod_s, 11.0, false, false, th->text_secondary);

            double share = (double)cn->size_on_disk / (double)psize;
            if (share > 1.0) share = 1.0;
            DDRect mbar = {x0 + w0 - 198.0, y + 11.0, 56.0, 10.0};
            dd_canvas_round_rect(cv, mbar, 5.0, th->track, th->border, 0.5);
            if (share > 0.01) {
                dd_canvas_round_rect(cv, (DDRect){mbar.x, mbar.y, mbar.w * share, mbar.h}, 5.0, th->ink, dd_color_hex(0,0), 0);
            }

            char pct_s[24], sz_s[32];
            dd_format_percent(share, pct_s, sizeof(pct_s));
            dd_format_bytes(cn->size_on_disk, sz_s, sizeof(sz_s));
            dd_canvas_text_Right(cv, x0 + w0 - 104.0, y + 21.0, pct_s, 11.0, false, true, th->text_secondary);
            dd_canvas_text_Right(cv, x0 + w0 - 40.0, y + 21.0, sz_s, 12.0, true, true, th->text);

            DDRect sbtn = {x0 + w0 - 30.0, y + 5.0, 22.0, 22.0};
            dd_canvas_round_rect(cv, sbtn, 5.0, staged ? th->ink : th->chip, th->control_border, 0.8);
            dd_canvas_draw_icon(cv, staged ? ICON_CHECK : ICON_PLUS, sbtn.x + 4.0, sbtn.y + 4.0, 14.0, staged ? th->on_ink : th->text);
            dd_canvas_register_hit(cv, ACT_STAGE_NODE, cidx, 0, NULL, "Stage Item", sbtn, true);
        }
        *io_y += 35.0;

        if (cn->is_directory && is_folder_expanded(st, cidx) && depth < 8) {
            draw_list_outline(st, cv, area, x0, w0, io_y, cidx, depth + 1);
        }
    }
}

static void render_viz_list(DDAppState *st, DDCanvas *cv, DDRect area) {
    const DDTheme *th = &st->theme;
    dd_canvas_clear_viz_hits(cv);
    dd_canvas_push_clip(cv, area);

    double x0 = area.x + 16.0;
    double w0 = area.w - 32.0;
    double hdr_y = area.y + 8.0;

    draw_outline_button(cv, th, (DDRect){x0, hdr_y, 96.0, 26.0},
                        st->list_sort_col == 1 ? (st->list_sort_asc ? "Name ↑" : "Name ↓") : "Name",
                        st->list_sort_col == 1, ACT_LIST_SORT, 1, 0, NULL);
    draw_outline_button(cv, th, (DDRect){x0 + w0 - 296.0, hdr_y, 88.0, 26.0},
                        st->list_sort_col == 2 ? (st->list_sort_asc ? "Modified ↑" : "Modified ↓") : "Modified",
                        st->list_sort_col == 2, ACT_LIST_SORT, 2, 0, NULL);
    draw_outline_button(cv, th, (DDRect){x0 + w0 - 134.0, hdr_y, 96.0, 26.0},
                        st->list_sort_col == 0 ? (st->list_sort_asc ? "Size ↑" : "Size ↓") : "Size",
                        st->list_sort_col == 0, ACT_LIST_SORT, 0, 0, NULL);

    DDRect list_r = {area.x, hdr_y + 32.0, area.w, area.h - 40.0};
    dd_canvas_push_clip(cv, list_r);
    double y0 = list_r.y + 4.0 - st->scroll_y_main;
    double y = y0;
    draw_list_outline(st, cv, list_r, x0, w0, &y, st->current_folder, 0);
    clamp_scroll(&st->scroll_y_main, y - y0, list_r.h, &st->max_scroll_main);
    dd_canvas_pop_clip(cv);
    dd_canvas_pop_clip(cv);
}

static void render_explore_tab(DDAppState *st, DDCanvas *cv, DDRect main_r) {
    const DDTheme *th = &st->theme;
    dd_canvas_fill_rect(cv, main_r, th->background);

    if (st->scan_progress.is_running) {
        double cx = main_r.x + main_r.w * 0.5;
        double cy = main_r.y + main_r.h * 0.45;
        double ang = fmod(dd_now_seconds() * 4.0, 2.0 * M_PI);
        dd_canvas_arc_ring(cv, cx, cy - 44.0, 20.0, 28.0, ang, ang + 4.2, th->ink, dd_color_hex(0,0));
        dd_canvas_text_center(cv, (DDRect){cx - 160.0, cy - 4.0, 320.0, 28.0}, "Scanning...", 18.0, true, false, th->text);

        char b_sz[32], counter[128], trunc_p[96];
        dd_format_bytes(st->scan_progress.bytes_scanned, b_sz, sizeof(b_sz));
        snprintf(counter, sizeof(counter), "%lld files · %s", (long long)st->scan_progress.files_scanned, b_sz);
        dd_canvas_text_center(cv, (DDRect){cx - 200.0, cy + 28.0, 400.0, 24.0}, counter, 14.0, true, true, th->ink);

        dd_truncate_middle(st->scan_progress.current_path, 60, trunc_p, sizeof(trunc_p));
        dd_canvas_text_center(cv, (DDRect){cx - 260.0, cy + 56.0, 520.0, 20.0}, trunc_p, 11.5, false, true, th->text_secondary);

        draw_outline_button(cv, th, (DDRect){cx - 60.0, cy + 92.0, 120.0, 34.0}, "Cancel Scan", false, ACT_CANCEL_SCAN, 0, 0, NULL);
        return;
    }

    if (!st->tree || st->tree->node_count == 0) {
        double cx = main_r.x + main_r.w * 0.5;
        double cy = main_r.y + main_r.h * 0.44;
        dd_canvas_draw_icon(cv, ICON_FOLDER, cx - 24.0, cy - 72.0, 48.0, th->ink);
        dd_canvas_text_center(cv, (DDRect){cx - 240.0, cy - 12.0, 480.0, 32.0}, "Map every byte on your system", 21.0, true, false, th->text);
        dd_canvas_text_center(cv, (DDRect){cx - 240.0, cy + 22.0, 480.0, 22.0}, "Choose your Home folder or any directory to explore 9 interactive views.", 13.0, false, false, th->text_secondary);
        draw_ink_button(cv, th, (DDRect){cx - 148.0, cy + 62.0, 140.0, 38.0}, "Scan Home", ACT_SCAN_HOME, 0, 0, NULL);
        draw_outline_button(cv, th, (DDRect){cx + 8.0, cy + 62.0, 140.0, 38.0}, "Choose Folder...", false, ACT_SCAN_PICK, 0, 0, NULL);
        return;
    }

    double y = main_r.y + 12.0;
    double x = main_r.x + 16.0;
    double w = main_r.w - 32.0;

    if (st->tree->denied_count > 0 && !st->denied_banner_dismissed) {
        DDRect ban = {x, y, w, 38.0};
        dd_canvas_round_rect(cv, ban, 8.0, th->chip, th->control_border, 1.0);
        dd_canvas_draw_icon(cv, ICON_LOCK, x + 12.0, y + 11.0, 16.0, th->ink);
        char msg[256];
        snprintf(msg, sizeof(msg), "%d folder%s couldn't be read: sizes may be incomplete (e.g. %s)",
                 (int)st->tree->denied_count, st->tree->denied_count == 1 ? "" : "s",
                 st->tree->denied_sample_count > 0 ? st->tree->denied_samples[0] : "restricted directory");
        dd_canvas_text_clipped(cv, (DDRect){x + 34.0, y, w - 284.0, 38.0}, msg, 11.5, false, false, th->text);
        draw_outline_button(cv, th, (DDRect){x + w - 244.0, y + 5.0, 72.0, 28.0}, "Rescan", false, ACT_RESCAN_CURRENT, 0, 0, NULL);
        draw_outline_button(cv, th, (DDRect){x + w - 166.0, y + 5.0, 128.0, 28.0}, "Permissions Help", false, ACT_OPEN_PERMS_HELP, 0, 0, NULL);
        draw_square_icon_button(cv, th, (DDRect){x + w - 32.0, y + 5.0, 28.0, 28.0}, ICON_CLOSE, false, "Dismiss Notice", ACT_DISMISS_DENIED, 0);
        y += 46.0;
    }

    const DDFileNode *cur_n = &st->tree->nodes[st->current_folder];
    const DDFolderExtra *cur_fe = (cur_n->folder_idx >= 0) ? &st->tree->folders[cur_n->folder_idx] : NULL;
    const char *fname = dd_node_name(st->tree, st->current_folder);
    char sz_str[32], hdr_meta[128];
    dd_format_bytes(cur_n->size_on_disk, sz_str, sizeof(sz_str));
    snprintf(hdr_meta, sizeof(hdr_meta), "%s  ·  %d files  ·  %d folders",
             sz_str, cur_fe ? (int)cur_fe->file_count : 0, cur_fe ? (int)cur_fe->folder_count : 0);

    dd_canvas_text_clipped(cv, (DDRect){x, y, w * 0.45, 28.0}, fname, 19.0, true, false, th->text);
    double fn_w = dd_canvas_measure_text(cv, fname, 19.0, true, false);
    if (fn_w > w * 0.45) fn_w = w * 0.45;
    dd_canvas_text_clipped(cv, (DDRect){x + fn_w + 14.0, y + 2.0, w - fn_w - 56.0, 26.0}, hdr_meta, 13.0, false, true, th->text_secondary);
    draw_square_icon_button(cv, th, (DDRect){x + w - 32.0, y, 30.0, 30.0}, ICON_REVEAL, false, "Reveal in File Manager", ACT_REVEAL_CURRENT, 0);
    y += 38.0;

    static const char * const mode_names[9] = {
        "Treemap", "Folders", "Sunburst", "Flame", "Bubbles", "Mind Map", "Top Sizes", "Age Map", "List"
    };
    static const char * const mode_captions[9] = {
        "Every file as a rectangle, sized by bytes",
        "Browse folder by folder, sized as you go",
        "Rings radiating out from the scan root",
        "Depth top to bottom, size left to right",
        "Nested bubbles, one per folder",
        "Branches from the root, sized by weight",
        "The biggest items, ranked",
        "Where your bytes sit on a timeline",
        "Every item as an expandable outline"
    };

    double mx = x;
    for (int m = 0; m < 9; m++) {
        bool act = ((int)st->viz_mode == m);
        double mw = dd_canvas_measure_text(cv, mode_names[m], 11.5, act, false) + 18.0;
        DDRect mr = {mx, y, mw, 28.0};
        dd_canvas_round_rect(cv, mr, 14.0, act ? th->ink : th->chip, act ? th->control_border : th->border, 1.0);
        dd_canvas_text_center(cv, mr, mode_names[m], 11.5, act, false, act ? th->on_ink : th->text);
        dd_canvas_register_hit(cv, ACT_VIZ_MODE, m, 0, NULL, mode_names[m], mr, true);
        mx += mw + 4.0;
    }
    y += 34.0;

    bool has_color_mode = (st->viz_mode == VIZ_TREEMAP || st->viz_mode == VIZ_SUNBURST ||
                           st->viz_mode == VIZ_FLAME || st->viz_mode == VIZ_BUBBLES || st->viz_mode == VIZ_MINDMAP);
    bool has_depth = (st->viz_mode == VIZ_TREEMAP || st->viz_mode == VIZ_SUNBURST || st->viz_mode == VIZ_FLAME);

    dd_canvas_text_clipped(cv, (DDRect){x, y, w * 0.42, 26.0}, mode_captions[st->viz_mode], 12.0, false, false, th->text_secondary);

    double ctrl_x = x + w;
    if (has_color_mode) {
        DDRect ab_r = {ctrl_x - 30.0, y, 30.0, 26.0};
        draw_outline_button(cv, th, ab_r, "A", st->abbreviate_labels, ACT_TOGGLE_ABBREV, 0, 0, NULL);
        ctrl_x -= 38.0;
    }
    if (has_depth) {
        char d_lbl[32];
        snprintf(d_lbl, sizeof(d_lbl), "Depth %d", st->depth_limit);
        draw_outline_button(cv, th, (DDRect){ctrl_x - 26.0, y, 26.0, 26.0}, "+", false, ACT_DEPTH_INC, 0, 0, NULL);
        dd_canvas_text_center(cv, (DDRect){ctrl_x - 90.0, y, 62.0, 26.0}, d_lbl, 11.5, true, true, th->text);
        draw_outline_button(cv, th, (DDRect){ctrl_x - 118.0, y, 26.0, 26.0}, "-", false, ACT_DEPTH_DEC, 0, 0, NULL);
        ctrl_x -= 128.0;
    }
    if (has_color_mode) {
        const char *cm_names[3] = {"By type", "By folder", "By age"};
        for (int c = 2; c >= 0; c--) {
            bool act = ((int)st->color_mode == c);
            double cw = 68.0;
            draw_outline_button(cv, th, (DDRect){ctrl_x - cw, y, cw, 26.0}, cm_names[c], act, ACT_COLOR_MODE, c, 0, NULL);
            ctrl_x -= (cw + 4.0);
        }
    }
    y += 32.0;

    dd_canvas_fill_rect(cv, (DDRect){main_r.x, y, main_r.w, 1.0}, th->divider);
    y += 1.0;

    DDRect viz_area = {main_r.x, y, main_r.w, main_r.y + main_r.h - y};

    switch (st->viz_mode) {
        case VIZ_TREEMAP:  render_viz_treemap(st, cv, viz_area); break;
        case VIZ_FOLDERS:  render_viz_folders(st, cv, viz_area); break;
        case VIZ_SUNBURST: render_viz_sunburst(st, cv, viz_area); break;
        case VIZ_FLAME:    render_viz_flame(st, cv, viz_area); break;
        case VIZ_BUBBLES:  render_viz_bubbles(st, cv, viz_area); break;
        case VIZ_MINDMAP:  render_viz_mindmap(st, cv, viz_area); break;
        case VIZ_TOPSIZES: render_viz_topsizes(st, cv, viz_area); break;
        case VIZ_AGEMAP:   render_viz_agemap(st, cv, viz_area); break;
        case VIZ_LIST:     render_viz_list(st, cv, viz_area); break;
        default: break;
    }

    if (has_color_mode && (st->color_mode == COLOR_BY_TYPE || st->color_mode == COLOR_BY_AGE)) {
        static const char * const age_names[6] = {"<7d", "7-30d", "1-3m", "3-12m", "1-2y", ">2y"};
        int n_leg = (st->color_mode == COLOR_BY_TYPE) ? CAT_COUNT : 6;
        double leg_w = (st->color_mode == COLOR_BY_TYPE) ? 550.0 : 340.0;
        if (leg_w < viz_area.w - 24.0) {
            DDRect lr = {viz_area.x + viz_area.w - leg_w - 12.0, viz_area.y + viz_area.h - 34.0, leg_w, 26.0};
            dd_canvas_round_rect(cv, lr, 13.0, th->card, th->border, 1.0);
            double lx = lr.x + 12.0;
            double step = (lr.w - 16.0) / (double)n_leg;
            for (int g = 0; g < n_leg; g++) {
                DDColor gc = (st->color_mode == COLOR_BY_TYPE) ? th->categories[g] : th->age_buckets[g];
                const char *gn = (st->color_mode == COLOR_BY_TYPE) ? dd_category_name((DDFileCategory)g) : age_names[g];
                dd_canvas_circle(cv, lx + 4.0, lr.y + 13.0, 4.5, gc, th->border, 0.5);
                dd_canvas_text(cv, lx + 12.0, lr.y + 17.0, gn, 10.0, false, false, th->text_secondary);
                lx += step;
            }
        }
    }

    if (st->hovered_node >= 0 && st->hovered_node < st->tree->node_count) {
        const DDFileNode *hn = &st->tree->nodes[st->hovered_node];
        const char *hname = dd_node_name(st->tree, st->hovered_node);
        char hsz[32], hpct[24], hchip[256];
        dd_format_bytes(hn->size_on_disk, hsz, sizeof(hsz));
        int64_t rtot = st->tree->nodes[0].size_on_disk > 0 ? st->tree->nodes[0].size_on_disk : 1;
        dd_format_percent((double)hn->size_on_disk / (double)rtot, hpct, sizeof(hpct));
        if (hn->is_directory && hn->folder_idx >= 0) {
            snprintf(hchip, sizeof(hchip), "%s  ·  %s (%s of scan)  ·  %d files",
                     hname, hsz, hpct, (int)st->tree->folders[hn->folder_idx].file_count);
        } else {
            snprintf(hchip, sizeof(hchip), "%s  ·  %s (%s of scan)", hname, hsz, hpct);
        }
        double cw = dd_canvas_measure_text(cv, hchip, 11.5, true, false) + 38.0;
        if (cw > viz_area.w - 32.0) cw = viz_area.w - 32.0;
        DDRect chip_r = {viz_area.x + 14.0, viz_area.y + viz_area.h - 40.0, cw, 30.0};
        dd_canvas_round_rect(cv, chip_r, 15.0, th->card, th->control_border, 1.2);
        dd_canvas_draw_icon(cv, category_icon(dd_node_dominant_category(st->tree, st->hovered_node), hn->is_directory),
                            chip_r.x + 10.0, chip_r.y + 7.0, 15.0, th->ink);
        dd_canvas_text_clipped(cv, (DDRect){chip_r.x + 30.0, chip_r.y, chip_r.w - 38.0, chip_r.h}, hchip, 11.5, true, false, th->text);
    }
}

static void render_inspector(DDAppState *st, DDCanvas *cv, DDRect ins) {
    const DDTheme *th = &st->theme;
    dd_canvas_push_clip(cv, ins);
    dd_canvas_fill_rect(cv, ins, th->panel);
    dd_canvas_fill_rect(cv, (DDRect){ins.x, ins.y, 1.0, ins.h}, th->divider);

    if (!st->tree || st->tree->node_count == 0) {
        dd_canvas_text_center(cv, ins, "Run a scan to inspect files and folders.", 12.5, false, false, th->text_secondary);
        dd_canvas_pop_clip(cv);
        return;
    }

    int32_t target = (st->selected_node >= 0 && st->selected_node < st->tree->node_count) ? st->selected_node : st->current_folder;
    const DDFileNode *n = &st->tree->nodes[target];
    const char *name = dd_node_name(st->tree, target);
    DDFileCategory cat = dd_node_dominant_category(st->tree, target);

    int child_rows = 0;
    if (n->is_directory && n->folder_idx >= 0) {
        const DDFolderExtra *fe = &st->tree->folders[n->folder_idx];
        child_rows = fe->child_count < 8 ? fe->child_count : 8;
    }
    double content_h = 440.0 + (child_rows > 0 ? (24.0 + child_rows * 32.0) : 0.0);
    clamp_scroll(&st->scroll_y_inspector, content_h, ins.h - 24.0, &st->max_scroll_inspector);

    double x = ins.x + 16.0;
    double w = ins.w - 32.0;
    double y = ins.y + 16.0 - st->scroll_y_inspector;

    DDRect ibox = {x, y, 42.0, 42.0};
    dd_canvas_round_rect(cv, ibox, 10.0, th->categories[cat], dd_color_hex(0x0F172A, 0.25), 1.0);
    dd_canvas_draw_icon(cv, category_icon(cat, n->is_directory), ibox.x + 11.0, ibox.y + 11.0, 20.0, th->pastel_text_primary);

    dd_canvas_text_clipped(cv, (DDRect){x + 52.0, y + 2.0, w - 52.0, 22.0}, name, 14.5, true, false, th->text);
    dd_canvas_text(cv, x + 52.0, y + 36.0, n->is_directory ? "Folder" : dd_category_name(cat), 12.0, false, false, th->text_secondary);
    y += 52.0;

    char full_p[512], trunc_p[96];
    dd_node_build_path(st->tree, target, full_p, sizeof(full_p));
    dd_truncate_middle(full_p, 42, trunc_p, sizeof(trunc_p));
    DDRect pbox = {x, y, w, 28.0};
    dd_canvas_round_rect(cv, pbox, 6.0, th->card, th->border, 1.0);
    dd_canvas_text_clipped(cv, (DDRect){x + 8.0, y, w - 16.0, 28.0}, trunc_p, 11.0, false, true, th->text_secondary);
    y += 38.0;

    char sz_s[32], pct_s[32], scan_sub[64];
    dd_format_bytes(n->size_on_disk, sz_s, sizeof(sz_s));
    int64_t rtot = st->tree->nodes[0].size_on_disk > 0 ? st->tree->nodes[0].size_on_disk : 1;
    dd_format_percent((double)n->size_on_disk / (double)rtot, pct_s, sizeof(pct_s));
    snprintf(scan_sub, sizeof(scan_sub), "%s of scan", pct_s);

    dd_canvas_text(cv, x, y + 22.0, sz_s, 22.0, true, true, th->text);
    dd_canvas_text_Right(cv, x + w, y + 20.0, scan_sub, 12.5, true, false, th->ink);
    y += 34.0;

    bool staged = dd_queue_contains_path(&st->cleanup_queue, full_p);
    const char *stg_lbl = staged ? "✓ Staged for Cleanup" : "Add to Cleanup";
    draw_outline_button(cv, th, (DDRect){x, y, w, 36.0}, stg_lbl, !staged, ACT_INSPECTOR_STAGE, target, 0, NULL);
    y += 44.0;

    double bw = (w - 8.0) * 0.5;
    draw_outline_button(cv, th, (DDRect){x, y, bw, 30.0}, "Reveal", false, ACT_INSPECTOR_REVEAL, target, 0, NULL);
    draw_outline_button(cv, th, (DDRect){x + bw + 8.0, y, bw, 30.0}, "Quick Look", false, ACT_INSPECTOR_QUICKLOOK, target, 0, NULL);
    y += 36.0;
    const char *cp_lbl = (dd_now_seconds() < st->inspector_copy_until) ? "Copied ✓" : "Copy Path";
    const char *foc_lbl = (target == st->current_folder && n->parent_idx >= 0) ? "Parent Folder" : (n->is_directory ? "Open Folder" : "Focus Parent");
    draw_outline_button(cv, th, (DDRect){x, y, bw, 30.0}, foc_lbl, false, ACT_INSPECTOR_FOCUS, target, 0, NULL);
    draw_outline_button(cv, th, (DDRect){x + bw + 8.0, y, bw, 30.0}, cp_lbl, dd_now_seconds() < st->inspector_copy_until, ACT_INSPECTOR_COPY, target, 0, NULL);
    y += 42.0;

    draw_section_caption(cv, th, x, y + 10.0, "DETAILS");
    y += 18.0;
    DDRect dcard = {x, y, w, 162.0};
    dd_canvas_round_rect(cv, dcard, 10.0, th->card, th->border, 1.0);

    char log_s[32], sav_s[32], par_s[24], mod_s[48], crt_s[48], cnt_s[64];
    dd_format_bytes(n->logical_size, log_s, sizeof(log_s));
    dd_format_bytes(dd_node_compression_savings(st->tree, target), sav_s, sizeof(sav_s));
    dd_format_percent(dd_node_share_of_parent(st->tree, target), par_s, sizeof(par_s));
    dd_format_relative_age(n->modified_stamp, mod_s, sizeof(mod_s));
    dd_format_date(n->created_stamp, crt_s, sizeof(crt_s));
    if (n->is_directory && n->folder_idx >= 0) {
        snprintf(cnt_s, sizeof(cnt_s), "%d files / %d folders",
                 (int)st->tree->folders[n->folder_idx].file_count, (int)st->tree->folders[n->folder_idx].folder_count);
    } else {
        snprintf(cnt_s, sizeof(cnt_s), "%s file", dd_category_name(cat));
    }

    struct { const char *k; const char *v; bool green; } rows[7] = {
        {"Size on disk", sz_s, false},
        {"Logical size", log_s, false},
        {"Compressed by", sav_s, true},
        {n->is_directory ? "Files / Folders" : "Kind", cnt_s, false},
        {"Of parent", par_s, false},
        {"Modified", mod_s, false},
        {"Created", crt_s, false}
    };
    for (int r = 0; r < 7; r++) {
        double ry = y + 20.0 + r * 21.0;
        dd_canvas_text(cv, x + 10.0, ry, rows[r].k, 11.5, false, false, th->text_secondary);
        dd_canvas_text_Right(cv, x + w - 10.0, ry, rows[r].v, 11.5, rows[r].green, true, rows[r].green ? th->free_text : th->text);
    }
    y += 174.0;

    if (n->is_directory && n->folder_idx >= 0 && st->tree->folders[n->folder_idx].child_count > 0) {
        draw_section_caption(cv, th, x, y + 10.0, "LARGEST INSIDE");
        y += 18.0;
        const DDFolderExtra *fe = &st->tree->folders[n->folder_idx];
        int lim = fe->child_count < 8 ? fe->child_count : 8;
        for (int i = 0; i < lim; i++) {
            int32_t cidx = fe->children[i];
            const DDFileNode *cn = &st->tree->nodes[cidx];
            if (cn->is_deleted) continue;
            DDRect cr = {x, y, w, 28.0};
            dd_canvas_round_rect(cv, cr, 6.0, (st->selected_node == cidx) ? th->chip : th->card,
                                 (st->selected_node == cidx) ? th->control_border : th->border, 1.0);
            char csz[32];
            dd_format_bytes(cn->size_on_disk, csz, sizeof(csz));
            dd_canvas_draw_icon(cv, category_icon(dd_node_dominant_category(st->tree, cidx), cn->is_directory),
                                x + 8.0, y + 7.0, 14.0, cn->is_directory ? th->ink : th->text_secondary);
            dd_canvas_text_clipped(cv, (DDRect){x + 26.0, y, w - 106.0, 28.0}, dd_node_name(st->tree, cidx), 11.5, false, false, th->text);
            dd_canvas_text_Right(cv, x + w - 10.0, y + 19.0, csz, 11.0, true, true, th->text_secondary);
            dd_canvas_register_hit(cv, ACT_SELECT_NODE, cidx, 1, NULL, dd_node_name(st->tree, cidx), cr, true);
            y += 32.0;
        }
    }

    dd_canvas_pop_clip(cv);
}

static void render_duplicates_tab(DDAppState *st, DDCanvas *cv, DDRect area) {
    const DDTheme *th = &st->theme;
    dd_canvas_push_clip(cv, area);
    dd_canvas_fill_rect(cv, area, th->background);

    double x = area.x + 20.0;
    double w = area.w - 40.0;
    double y = area.y + 18.0;

    dd_canvas_text(cv, x, y + 20.0, "Duplicate Finder", 20.0, true, false, th->text);
    if (st->tree) {
        if (st->duplicates.scanning) {
            draw_outline_button(cv, th, (DDRect){x + w - 120.0, y, 120.0, 34.0}, "Cancel", false, ACT_DUP_CANCEL, 0, 0, NULL);
        } else {
            if (st->duplicates.has_run && st->duplicates.group_count > 0) {
                draw_outline_button(cv, th, (DDRect){x + w - 378.0, y, 198.0, 34.0},
                                    "Keep Newest in All Groups", false, ACT_DUP_KEEP_ALL_NEWEST, 0, 0, NULL);
            }
            draw_ink_button(cv, th, (DDRect){x + w - 170.0, y, 170.0, 34.0}, "Scan for Duplicates", ACT_DUP_SCAN, 0, 0, NULL);
        }
    }
    y += 44.0;

    if (!st->tree) {
        dd_canvas_text_center(cv, area, "Scan a folder on the Explore tab first, then run Duplicate Finder.", 13.5, false, false, th->text_secondary);
        dd_canvas_pop_clip(cv);
        return;
    }
    if (st->duplicates.scanning) {
        int pass = st->duplicates.pass_num;
        int done = st->duplicates.progress_done;
        int tot  = st->duplicates.progress_total;
        char status_s[160];
        if (pass <= 1) {
            snprintf(status_s, sizeof(status_s), "Pass 1 of 3: Grouping candidate files by exact byte size...");
        } else if (pass == 2) {
            snprintf(status_s, sizeof(status_s), "Pass 2 of 3: Hashing 64 KB samples (%d / %d candidates)...", done, tot);
        } else {
            snprintf(status_s, sizeof(status_s), "Pass 3 of 3: Verifying SHA-256 content (%d / %d candidates)...", done, tot);
        }
        double cw = fmin(480.0, w - 40.0);
        DDRect card = {x + (w - cw) * 0.5, area.y + (area.h - 110.0) * 0.45, cw, 100.0};
        dd_canvas_round_rect(cv, card, 12.0, th->card, th->border, 1.0);
        dd_canvas_text_center(cv, (DDRect){card.x + 16.0, card.y + 18.0, card.w - 32.0, 24.0}, status_s, 13.0, true, false, th->text);
        DDRect pbar = {card.x + 24.0, card.y + 56.0, card.w - 48.0, 10.0};
        dd_canvas_round_rect(cv, pbar, 5.0, th->track, dd_color_hex(0,0), 0);
        double ratio = (tot > 0) ? ((double)done / (double)tot) : 0.25;
        if (ratio < 0.05) ratio = 0.05;
        if (ratio > 1.0) ratio = 1.0;
        dd_canvas_round_rect(cv, (DDRect){pbar.x, pbar.y, pbar.w * ratio, pbar.h}, 5.0, th->ink, dd_color_hex(0,0), 0);
        dd_canvas_pop_clip(cv);
        return;
    }
    if (!st->duplicates.has_run) {
        dd_canvas_text_center(cv, area, "Click 'Scan for Duplicates' to run 3-pass size + 64 KB sample + SHA-256 verification.", 13.5, false, false, th->text_secondary);
        dd_canvas_pop_clip(cv);
        return;
    }

    char rec_s[32], sum_s[128];
    dd_format_bytes(st->duplicates.total_reclaimable, rec_s, sizeof(rec_s));
    snprintf(sum_s, sizeof(sum_s), "%s could be reclaimed across %d duplicate group%s",
             rec_s, st->duplicates.group_count, st->duplicates.group_count == 1 ? "" : "s");
    dd_canvas_text(cv, x, y + 16.0, sum_s, 14.0, true, false, th->ink);
    y += 28.0;

    DDRect list_r = {area.x, y, area.w, area.h - (y - area.y)};
    double total_h = 20.0;
    for (int g = 0; g < st->duplicates.group_count; g++) {
        const DDDuplicateGroup *grp = &st->duplicates.groups[g];
        bool match = !st->search_query[0];
        for (int j = 0; j < grp->count && !match; j++) {
            if (str_contains_ci(grp->paths[j], st->search_query)) match = true;
        }
        if (match) total_h += 34.0 + grp->count * 34.0 + 18.0;
    }
    clamp_scroll(&st->scroll_y_main, total_h, list_r.h, &st->max_scroll_main);

    dd_canvas_push_clip(cv, list_r);
    y -= st->scroll_y_main;

    if (st->duplicates.group_count == 0) {
        dd_canvas_text(cv, x, y + 30.0, "No duplicate files found in the scanned tree.", 13.0, false, false, th->text_secondary);
    } else {
        for (int g = 0; g < st->duplicates.group_count; g++) {
            const DDDuplicateGroup *grp = &st->duplicates.groups[g];
            bool match = !st->search_query[0];
            for (int j = 0; j < grp->count && !match; j++) {
                if (str_contains_ci(grp->paths[j], st->search_query)) match = true;
            }
            if (!match) continue;

            double gh = 34.0 + grp->count * 34.0 + 8.0;
            DDRect gc = {x, y, w, gh};
            if (gc.y + gc.h >= list_r.y && gc.y <= list_r.y + list_r.h) {
                dd_canvas_round_rect(cv, gc, 10.0, th->card, th->border, 1.0);
                char fsz[32], wsz[32], ghdr[256];
                dd_format_bytes(grp->file_size, fsz, sizeof(fsz));
                dd_format_bytes(grp->wasted_bytes, wsz, sizeof(wsz));
                snprintf(ghdr, sizeof(ghdr), "%d copies · %s each · %s reclaimable · SHA-256 %.12s...",
                         grp->count, fsz, wsz, grp->hash_hex);
                dd_canvas_text(cv, x + 12.0, y + 22.0, ghdr, 12.0, true, true, th->text);

                int staged_in_grp = 0;
                for (int j = 0; j < grp->count; j++) {
                    if (dd_queue_contains_path(&st->cleanup_queue, grp->paths[j])) staged_in_grp++;
                }

                for (int j = 0; j < grp->count; j++) {
                    double ry = y + 34.0 + j * 34.0;
                    bool is_stg = dd_queue_contains_path(&st->cleanup_queue, grp->paths[j]);
                    bool is_kept = (!is_stg && staged_in_grp == grp->count - 1);
                    char trunc[120];
                    dd_truncate_middle(grp->paths[j], 68, trunc, sizeof(trunc));
                    dd_canvas_text_clipped(cv, (DDRect){x + 16.0, ry, w - 210.0, 30.0}, trunc, 11.5,
                                           is_kept, true, is_stg ? th->used_text : (is_kept ? th->free_text : th->text_secondary));
                    const char *blbl = is_kept ? "✓ Keeping this copy" : (is_stg ? "✓ Staged (Keep instead)" : "Keep this, stage the rest");
                    draw_outline_button(cv, th, (DDRect){x + w - 180.0, ry + 3.0, 168.0, 26.0},
                                        blbl, is_kept, ACT_DUP_KEEP_ONE, g, j, NULL);
                }
            }
            y += gh + 10.0;
        }
    }

    dd_canvas_pop_clip(cv);
    dd_canvas_pop_clip(cv);
}

static void render_applications_tab(DDAppState *st, DDCanvas *cv, DDRect area) {
    const DDTheme *th = &st->theme;
    dd_canvas_push_clip(cv, area);
    dd_canvas_fill_rect(cv, area, th->background);

    if (!st->app_store.loaded && !st->app_store.loading) {
        dd_apps_start_load(&st->app_store);
    }

    double x = area.x + 20.0;
    double w = area.w - 40.0;
    double y = area.y + 18.0;

    char tot_s[32], hdr_s[128];
    dd_format_bytes(st->app_store.grand_total, tot_s, sizeof(tot_s));
    if (st->app_store.loading && !st->app_store.loaded) {
        snprintf(hdr_s, sizeof(hdr_s), "Applications (Scanning installed apps...)");
    } else {
        snprintf(hdr_s, sizeof(hdr_s), "Applications (%d installed · %s total)", st->app_store.count, tot_s);
    }
    dd_canvas_text(cv, x, y + 22.0, hdr_s, 19.0, true, false, th->text);
    draw_outline_button(cv, th, (DDRect){x + w - 100.0, y, 100.0, 32.0},
                        st->app_store.loading ? "Scanning..." : "Refresh",
                        st->app_store.loading != 0, ACT_APPS_REFRESH, 0, 0, NULL);
    y += 42.0;

    if (st->app_store.loading && !st->app_store.loaded) {
        dd_canvas_text_center(cv, area, "Scanning installed applications and calculating leftover cache sizes...", 13.5, false, false, th->text_secondary);
        dd_canvas_pop_clip(cv);
        return;
    }

    DDRect list_r = {area.x, y, area.w, area.h - (y - area.y)};
    double total_h = 20.0;
    for (int i = 0; i < st->app_store.count; i++) {
        const DDInstalledApp *app = &st->app_store.apps[i];
        if (!str_contains_ci(app->name, st->search_query)) continue;
        total_h += (app->expanded ? (52.0 + (1 + app->leftover_count) * 28.0 + 10.0) : 52.0) + 8.0;
    }
    clamp_scroll(&st->scroll_y_main, total_h, list_r.h, &st->max_scroll_main);

    dd_canvas_push_clip(cv, list_r);
    y -= st->scroll_y_main;

    for (int i = 0; i < st->app_store.count; i++) {
        const DDInstalledApp *app = &st->app_store.apps[i];
        if (!str_contains_ci(app->name, st->search_query)) continue;
        double rh = app->expanded ? (52.0 + (1 + app->leftover_count) * 28.0 + 10.0) : 52.0;
        DDRect ar = {x, y, w, rh};
        if (ar.y + ar.h >= list_r.y && ar.y <= list_r.y + list_r.h) {
            dd_canvas_round_rect(cv, ar, 10.0, th->card, th->border, 1.0);
            dd_canvas_draw_icon(cv, app->expanded ? ICON_CHEVRON_DOWN : ICON_CHEVRON_RIGHT, x + 12.0, y + 18.0, 16.0, th->text_secondary);
            dd_canvas_draw_icon(cv, ICON_APPS, x + 34.0, y + 16.0, 20.0, th->ink);

            char meta[256], used_s[48], tot_b[32], left_b[48];
            dd_format_relative_age(app->last_used_stamp, used_s, sizeof(used_s));
            snprintf(meta, sizeof(meta), "%s · v%s · Last used: %s", app->bundle_id, app->version, used_s);
            dd_format_bytes(app->total_size, tot_b, sizeof(tot_b));

            dd_canvas_text_clipped(cv, (DDRect){x + 62.0, y + 6.0, w - 340.0, 22.0}, app->name, 13.5, true, false, th->text);
            dd_canvas_text_clipped(cv, (DDRect){x + 62.0, y + 26.0, w - 340.0, 18.0}, meta, 11.0, false, false, th->text_secondary);

            if (app->leftovers_size > 0) {
                char lsz[32];
                dd_format_bytes(app->leftovers_size, lsz, sizeof(lsz));
                snprintf(left_b, sizeof(left_b), "+%s leftovers", lsz);
                dd_canvas_text_Right(cv, x + w - 200.0, y + 31.0, left_b, 11.0, true, false, th->ink);
            }
            dd_canvas_text_Right(cv, x + w - 116.0, y + 31.0, tot_b, 13.0, true, true, th->text);

            dd_canvas_register_hit(cv, ACT_APP_TOGGLE_EXPAND, i, 0, NULL, app->name, (DDRect){x, y, w - 110.0, 52.0}, true);
            bool app_staged = dd_queue_contains_path(&st->cleanup_queue, app->bundle_path);
            draw_outline_button(cv, th, (DDRect){x + w - 102.0, y + 12.0, 90.0, 28.0},
                                app_staged ? "✓ Staged" : "Uninstall...", app_staged, ACT_APP_ASK_UNINSTALL, i, 0, NULL);

            if (app->expanded) {
                double ly = y + 54.0;
                char bsz[32];
                dd_format_bytes(app->bundle_size, bsz, sizeof(bsz));
                dd_canvas_text(cv, x + 62.0, ly + 16.0, "App bundle", 11.5, true, false, th->text);
                dd_canvas_text_clipped(cv, (DDRect){x + 190.0, ly, w - 340.0, 24.0}, app->bundle_path, 11.0, false, true, th->text_secondary);
                dd_canvas_text_Right(cv, x + w - 116.0, ly + 16.0, bsz, 11.5, false, true, th->text);
                ly += 28.0;

                for (int l = 0; l < app->leftover_count; l++) {
                    char lsz[32];
                    dd_format_bytes(app->leftovers[l].size, lsz, sizeof(lsz));
                    bool l_staged = dd_queue_contains_path(&st->cleanup_queue, app->leftovers[l].path);
                    dd_canvas_text(cv, x + 62.0, ly + 16.0, app->leftovers[l].category, 11.5, false, false, th->ink);
                    dd_canvas_text_clipped(cv, (DDRect){x + 190.0, ly, w - 340.0, 24.0}, app->leftovers[l].path, 11.0, false, true, th->text_secondary);
                    dd_canvas_text_Right(cv, x + w - 116.0, ly + 16.0, lsz, 11.5, false, true, th->text);
                    draw_outline_button(cv, th, (DDRect){x + w - 102.0, ly + 1.0, 90.0, 22.0},
                                        l_staged ? "✓ Staged" : "Stage", l_staged, ACT_APP_STAGE_LEFTOVER, i, l, NULL);
                    ly += 28.0;
                }
            }
        }
        y += rh + 8.0;
    }

    dd_canvas_pop_clip(cv);
    dd_canvas_pop_clip(cv);
}

static void draw_sparkline(DDCanvas *cv, DDRect box, const double *vals, int count, double max_val, DDColor col) {
    if (count < 2 || box.w < 10.0 || box.h < 8.0) return;
    if (max_val <= 0.0) {
        for (int i = 0; i < count; i++) if (vals[i] > max_val) max_val = vals[i];
        if (max_val <= 0.0) max_val = 100.0;
    }
    double px = box.x;
    double py = box.y + box.h - (vals[0] / max_val) * box.h;
    for (int i = 1; i < count; i++) {
        double nx = box.x + ((double)i / 119.0) * box.w;
        double v = vals[i] / max_val;
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        double ny = box.y + box.h - v * box.h;
        dd_canvas_line(cv, px, py, nx, ny, col, 1.8);
        px = nx; py = ny;
    }
}

static void render_monitor_tab(DDAppState *st, DDCanvas *cv, DDRect area) {
    const DDTheme *th = &st->theme;
    const DDSystemMonitor *mon = &st->monitor;
    dd_canvas_push_clip(cv, area);
    dd_canvas_fill_rect(cv, area, th->background);

    double x = area.x + 20.0;
    double w = area.w - 40.0;
    double hdr_y = area.y + 18.0;

    dd_canvas_text(cv, x, hdr_y + 22.0, "Monitor", 20.0, true, false, th->text);
    DDRect list_r = {area.x, hdr_y + 36.0, area.w, area.h - 54.0};

    bool single_col = (w < 680.0);
    double cw = single_col ? w : (w - 16.0) * 0.5;
    double ch = 156.0;
    const DDProcessInfo *plist = st->mon_sort_by_mem ? mon->procs_by_mem : mon->procs_by_cpu;
    int pcount = st->mon_sort_by_mem ? mon->procs_mem_count : mon->procs_cpu_count;
    int pmax = st->mon_show_all_procs ? pcount : (pcount < 15 ? pcount : 15);
    int vis_procs = 0;
    for (int i = 0; i < pmax; i++) {
        if (str_contains_ci(plist[i].name, st->search_query)) vis_procs++;
    }
    double cards_h = single_col ? (ch * 4.0 + 42.0) : (ch * 2.0 + 14.0);
    double total_h = cards_h + 90.0 + vis_procs * 31.0 + 44.0;
    clamp_scroll(&st->scroll_y_main, total_h, list_r.h, &st->max_scroll_main);

    dd_canvas_push_clip(cv, list_r);
    double y = list_r.y + 4.0 - st->scroll_y_main;

    DDRect c_cpu = {x, y, cw, ch};
    DDRect c_mem = single_col ? (DDRect){x, y + ch + 14.0, cw, ch} : (DDRect){x + cw + 16.0, y, cw, ch};
    double y_row2 = single_col ? (y + (ch + 14.0) * 2.0) : (y + ch + 14.0);
    DDRect c_net = {x, y_row2, cw, ch};
    DDRect c_sto = single_col ? (DDRect){x, y_row2 + ch + 14.0, cw, ch} : (DDRect){x + cw + 16.0, y_row2, cw, ch};

    dd_canvas_round_rect(cv, c_cpu, 10.0, th->card, th->border, 1.0);
    draw_section_caption(cv, th, c_cpu.x + 14.0, c_cpu.y + 20.0, "CPU");
    char cpu_big[32], cpu_sub[128];
    snprintf(cpu_big, sizeof(cpu_big), "%.1f%%", mon->cpu_total_pct);
    snprintf(cpu_sub, sizeof(cpu_sub), "USER %.1f%%   ·   SYSTEM %.1f%%   ·   LOAD %.2f",
             mon->cpu_user_pct, mon->cpu_sys_pct, mon->load_avg[0]);
    dd_canvas_text(cv, c_cpu.x + 14.0, c_cpu.y + 52.0, cpu_big, 24.0, true, true, th->text);
    draw_sparkline(cv, (DDRect){c_cpu.x + 14.0, c_cpu.y + 64.0, c_cpu.w - 28.0, 54.0}, mon->cpu_history, mon->history_count, 100.0, th->ink);
    dd_canvas_text(cv, c_cpu.x + 14.0, c_cpu.y + 142.0, cpu_sub, 11.0, false, true, th->text_secondary);

    dd_canvas_round_rect(cv, c_mem, 10.0, th->card, th->border, 1.0);
    draw_section_caption(cv, th, c_mem.x + 14.0, c_mem.y + 20.0, "MEMORY");
    char m_used[32], m_wire[32], m_comp[32], m_tot[32], m_sub[160];
    dd_format_bytes(mon->mem_used, m_used, sizeof(m_used));
    dd_format_bytes(mon->mem_wired, m_wire, sizeof(m_wire));
    dd_format_bytes(mon->mem_compressed, m_comp, sizeof(m_comp));
    dd_format_bytes(mon->mem_total, m_tot, sizeof(m_tot));
    dd_canvas_text(cv, c_mem.x + 14.0, c_mem.y + 52.0, m_used, 24.0, true, true, th->text);

    DDRect mbar = {c_mem.x + 14.0, c_mem.y + 64.0, c_mem.w - 28.0, 12.0};
    dd_canvas_round_rect(cv, mbar, 6.0, th->track, th->border, 0.8);
    if (mon->mem_total > 0) {
        double w_w = ((double)mon->mem_wired / (double)mon->mem_total) * mbar.w;
        double w_c = ((double)mon->mem_compressed / (double)mon->mem_total) * mbar.w;
        double w_o = ((double)mon->mem_other_used / (double)mon->mem_total) * mbar.w;
        dd_canvas_fill_rect(cv, (DDRect){mbar.x, mbar.y, w_w, mbar.h}, th->used);
        dd_canvas_fill_rect(cv, (DDRect){mbar.x + w_w, mbar.y, w_c, mbar.h}, th->categories[CAT_DOCUMENT]);
        dd_canvas_fill_rect(cv, (DDRect){mbar.x + w_w + w_c, mbar.y, w_o, mbar.h}, th->ink);
    }
    draw_sparkline(cv, (DDRect){c_mem.x + 14.0, c_mem.y + 84.0, c_mem.w - 28.0, 38.0}, mon->mem_history, mon->history_count, 100.0, th->ink);
    snprintf(m_sub, sizeof(m_sub), "WIRED %s   ·   CACHED %s   ·   TOTAL %s", m_wire, m_comp, m_tot);
    dd_canvas_text(cv, c_mem.x + 14.0, c_mem.y + 142.0, m_sub, 11.0, false, true, th->text_secondary);

    dd_canvas_round_rect(cv, c_net, 10.0, th->card, th->border, 1.0);
    draw_section_caption(cv, th, c_net.x + 14.0, c_net.y + 20.0, "NETWORK");
    char ndown[32], nup[32], nsin[32], nbig[48], nsub[160];
    dd_format_bytes(mon->net_down_per_sec, ndown, sizeof(ndown));
    dd_format_bytes(mon->net_up_per_sec, nup, sizeof(nup));
    dd_format_bytes(mon->net_session_in, nsin, sizeof(nsin));
    snprintf(nbig, sizeof(nbig), "%s/s down", ndown);
    snprintf(nsub, sizeof(nsub), "DOWN %s/s   ·   UP %s/s   ·   SESSION IN %s", ndown, nup, nsin);
    dd_canvas_text(cv, c_net.x + 14.0, c_net.y + 58.0, nbig, 24.0, true, true, th->text);
    dd_canvas_text(cv, c_net.x + 14.0, c_net.y + 142.0, nsub, 11.0, false, true, th->text_secondary);

    dd_canvas_round_rect(cv, c_sto, 10.0, th->card, th->border, 1.0);
    draw_section_caption(cv, th, c_sto.x + 14.0, c_sto.y + 20.0, "STORAGE");
    int vshow = (st->mon_show_all_vols || mon->volume_count <= 3) ? mon->volume_count : 3;
    double vy = c_sto.y + 30.0;
    for (int v = 0; v < vshow && v < 4; v++) {
        const DDVolumeInfo *vi = &mon->volumes[v];
        char vsz[64], vu[32], vt[32];
        dd_format_bytes(vi->used_bytes, vu, sizeof(vu));
        dd_format_bytes(vi->total_bytes, vt, sizeof(vt));
        snprintf(vsz, sizeof(vsz), "%s / %s", vu, vt);
        dd_canvas_text_clipped(cv, (DDRect){c_sto.x + 14.0, vy, c_sto.w * 0.55, 16.0}, vi->name, 11.0, true, false, th->text);
        dd_canvas_text_Right(cv, c_sto.x + c_sto.w - 14.0, vy + 12.0, vsz, 10.5, false, true, th->text_secondary);
        DDRect vbar = {c_sto.x + 14.0, vy + 16.0, c_sto.w - 28.0, 6.0};
        dd_canvas_round_rect(cv, vbar, 3.0, th->track, dd_color_hex(0,0), 0);
        double vr = vi->total_bytes > 0 ? ((double)vi->used_bytes / (double)vi->total_bytes) : 0.0;
        dd_canvas_round_rect(cv, (DDRect){vbar.x, vbar.y, vbar.w * vr, vbar.h}, 3.0, th->ink, dd_color_hex(0,0), 0);
        vy += 26.0;
    }
    if (mon->volume_count > 3) {
        char more_v[48];
        snprintf(more_v, sizeof(more_v), st->mon_show_all_vols ? "Show fewer volumes" : "Show %d more volumes", mon->volume_count - 3);
        draw_outline_button(cv, th, (DDRect){c_sto.x + 14.0, c_sto.y + 122.0, 150.0, 24.0}, more_v, false, ACT_MON_TOGGLE_VOLUMES, 0, 0, NULL);
    }

    y = (single_col ? (c_sto.y + ch) : (y_row2 + ch)) + 22.0;

    dd_canvas_text(cv, x, y + 20.0, "Top Processes", 16.0, true, false, th->text);
    draw_outline_button(cv, th, (DDRect){x + w - 196.0, y - 2.0, 92.0, 28.0}, "By CPU", !st->mon_sort_by_mem, ACT_MON_SORT_TOGGLE, 0, 0, NULL);
    draw_outline_button(cv, th, (DDRect){x + w - 98.0, y - 2.0, 98.0, 28.0}, "By Memory", st->mon_sort_by_mem, ACT_MON_SORT_TOGGLE, 1, 0, NULL);
    y += 36.0;

    draw_section_caption(cv, th, x + 12.0, y + 12.0, "PROCESS");
    draw_section_caption(cv, th, x + w - 250.0, y + 12.0, "PID");
    draw_section_caption(cv, th, x + w - 160.0, y + 12.0, "CPU");
    draw_section_caption(cv, th, x + w - 70.0, y + 12.0, "MEMORY");
    y += 20.0;

    for (int i = 0; i < pmax; i++) {
        if (!str_contains_ci(plist[i].name, st->search_query)) continue;
        DDRect pr = {x, y, w, 28.0};
        dd_canvas_round_rect(cv, pr, 6.0, th->card, th->border, 0.8);
        char pid_s[24], cpu_s[24], mem_s[32];
        snprintf(pid_s, sizeof(pid_s), "%d", plist[i].pid);
        snprintf(cpu_s, sizeof(cpu_s), "%.1f%%", plist[i].cpu_percent);
        dd_format_bytes(plist[i].rss_bytes, mem_s, sizeof(mem_s));
        dd_canvas_text_clipped(cv, (DDRect){x + 12.0, y, w - 280.0, 28.0}, plist[i].name, 12.0, false, false, th->text);
        dd_canvas_text(cv, x + w - 250.0, y + 19.0, pid_s, 11.5, false, true, th->text_secondary);
        dd_canvas_text(cv, x + w - 160.0, y + 19.0, cpu_s, 11.5, false, true, th->text);
        dd_canvas_text_Right(cv, x + w - 12.0, y + 19.0, mem_s, 11.5, true, true, th->text);
        y += 31.0;
    }

    if (pcount > 15) {
        char exp_lbl[64];
        snprintf(exp_lbl, sizeof(exp_lbl), st->mon_show_all_procs ? "Show top 15" : "Show all %d", pcount);
        draw_outline_button(cv, th, (DDRect){x, y + 4.0, 140.0, 30.0}, exp_lbl, false, ACT_MON_TOGGLE_PROCS, 0, 0, NULL);
    }

    dd_canvas_pop_clip(cv);
    dd_canvas_pop_clip(cv);
}

static void render_snapshots_tab(DDAppState *st, DDCanvas *cv, DDRect area) {
    const DDTheme *th = &st->theme;
    dd_canvas_push_clip(cv, area);
    dd_canvas_fill_rect(cv, area, th->background);

    double x = area.x + 20.0;
    double w = area.w - 40.0;
    double hdr_y = area.y + 18.0;

    dd_canvas_text(cv, x, hdr_y + 22.0, "Snapshots", 20.0, true, false, th->text);
    bool can_snap = (st->tree != NULL && !st->scan_progress.is_running && !st->snapshots.saving);
    if (can_snap) {
        draw_ink_button(cv, th, (DDRect){x + w - 170.0, hdr_y, 170.0, 34.0}, "Take Snapshot Now", ACT_SNAP_TAKE, 0, 0, NULL);
    } else {
        dd_canvas_round_rect(cv, (DDRect){x + w - 170.0, hdr_y, 170.0, 34.0}, 8.0, th->chip, th->border, 1.0);
        dd_canvas_text_center(cv, (DDRect){x + w - 170.0, hdr_y, 170.0, 34.0}, "Take Snapshot Now", 12.5, true, false, th->text_tertiary);
    }

    DDRect list_r = {area.x, hdr_y + 44.0, area.w, area.h - 62.0};
    if (st->snapshots.count == 0) {
        dd_canvas_text(cv, x, list_r.y + 24.0, "No snapshots saved yet. Scan a folder and click 'Take Snapshot Now' to record folder sizes >= 1 MB.", 13.0, false, false, th->text_secondary);
        dd_canvas_pop_clip(cv);
        return;
    }

    if (st->snapshots.before_idx >= 0 && st->snapshots.after_idx >= 0 && !st->snapshots.diff_valid) {
        dd_snapshots_compute_diff(&st->snapshots);
    }

    double total_h = st->snapshots.count * 52.0 + 90.0 + st->snapshots.diff_count * 33.0;
    clamp_scroll(&st->scroll_y_main, total_h, list_r.h, &st->max_scroll_main);

    dd_canvas_push_clip(cv, list_r);
    double y = list_r.y + 4.0 - st->scroll_y_main;

    for (int i = 0; i < st->snapshots.count; i++) {
        const DDSnapshotSummary *s = &st->snapshots.summaries[i];
        DDRect sr = {x, y, w, 46.0};
        dd_canvas_round_rect(cv, sr, 8.0, th->card, th->border, 1.0);
        char sz_s[32], dt_s[48], meta[128];
        dd_format_bytes(s->total_bytes, sz_s, sizeof(sz_s));
        dd_format_date(s->timestamp, dt_s, sizeof(dt_s));
        snprintf(meta, sizeof(meta), "%s  ·  %s  ·  %d folders >= 1 MB", sz_s, dt_s, (int)s->folder_count);

        dd_canvas_text_clipped(cv, (DDRect){x + 12.0, y + 4.0, w - 260.0, 20.0}, s->root_path, 12.5, true, true, th->text);
        dd_canvas_text_clipped(cv, (DDRect){x + 12.0, y + 23.0, w - 260.0, 18.0}, meta, 11.0, false, false, th->text_secondary);

        draw_outline_button(cv, th, (DDRect){x + w - 220.0, y + 9.0, 68.0, 28.0}, "Before", st->snapshots.before_idx == i, ACT_SNAP_SET_BEFORE, i, 0, NULL);
        draw_outline_button(cv, th, (DDRect){x + w - 144.0, y + 9.0, 68.0, 28.0}, "After", st->snapshots.after_idx == i, ACT_SNAP_SET_AFTER, i, 0, NULL);
        draw_outline_button(cv, th, (DDRect){x + w - 68.0, y + 9.0, 58.0, 28.0}, "Delete", false, ACT_SNAP_DELETE, i, 0, NULL);
        y += 52.0;
    }

    if (st->snapshots.before_idx >= 0 && st->snapshots.after_idx >= 0) {
        y += 12.0;
        dd_canvas_text(cv, x, y + 18.0, "Before vs After Comparison", 15.0, true, false, th->text);
        y += 26.0;
        if (st->snapshots.roots_differ) {
            dd_canvas_text(cv, x, y + 16.0, "Note: The selected Before and After snapshots have different root paths.", 12.0, true, false, th->ink);
            y += 24.0;
        }
        if (st->snapshots.diff_count == 0) {
            dd_canvas_text(cv, x, y + 18.0, "No folder size changes >= 1 MB detected between these two snapshots.", 12.5, false, false, th->text_secondary);
        } else {
            for (int d = 0; d < st->snapshots.diff_count; d++) {
                const DDSnapshotDiffRow *dr = &st->snapshots.diff_rows[d];
                DDRect rr = {x, y, w, 30.0};
                dd_canvas_round_rect(cv, rr, 6.0, th->card, th->border, 0.8);
                dd_canvas_text_clipped(cv, (DDRect){x + 12.0, y, w - 150.0, 30.0}, dr->path, 11.5, false, true, th->text);
                char dsz[32], dlbl[48];
                dd_format_bytes(dr->delta >= 0 ? dr->delta : -dr->delta, dsz, sizeof(dsz));
                snprintf(dlbl, sizeof(dlbl), "%s%s", dr->delta >= 0 ? "+" : "-", dsz);
                dd_canvas_text_Right(cv, x + w - 12.0, y + 20.0, dlbl, 12.0, true, true, dr->delta >= 0 ? th->used_text : th->free_text);
                y += 33.0;
            }
        }
    }

    dd_canvas_pop_clip(cv);
    dd_canvas_pop_clip(cv);
}

static void render_queue_popover(DDAppState *st, DDCanvas *cv, int W) {
    const DDTheme *th = &st->theme;
    double pw = 460.0, ph = 520.0;
    if (pw > W - 24.0) pw = W - 24.0;
    DDRect pop = {(double)W - pw - 16.0, 62.0, pw, ph};

    dd_canvas_round_rect(cv, (DDRect){pop.x + 3.0, pop.y + 5.0, pop.w, pop.h}, 12.0, dd_color_hex(0x000000, 0.25), dd_color_hex(0,0), 0);
    dd_canvas_round_rect(cv, pop, 12.0, th->background, th->control_border, 1.5);
    dd_canvas_push_clip(cv, pop);
    dd_canvas_register_hit(cv, ACT_NONE, 0, 0, NULL, "Cleanup Queue Popover", pop, false);

    double x = pop.x + 16.0, w = pop.w - 32.0, y = pop.y + 14.0;
    char tot_s[32], sub_s[64];
    dd_format_bytes(st->cleanup_queue.total_size, tot_s, sizeof(tot_s));
    snprintf(sub_s, sizeof(sub_s), "%s staged (%d items)", tot_s, st->cleanup_queue.count);

    dd_canvas_text(cv, x, y + 18.0, "Cleanup Queue", 15.0, true, false, th->text);
    dd_canvas_text(cv, x, y + 36.0, sub_s, 11.5, false, true, th->text_secondary);

    if (st->cleanup_queue.count > 0) {
        draw_outline_button(cv, th, (DDRect){x + w - 196.0, y + 6.0, 62.0, 30.0}, "Clear", false, ACT_QUEUE_CLEAR, 0, 0, NULL);
        DDRect tr_btn = {x + w - 128.0, y + 6.0, 128.0, 30.0};
        dd_canvas_round_rect(cv, tr_btn, 8.0, th->used, th->control_border, 1.0);
        dd_canvas_text_center(cv, tr_btn, "Move to Trash...", 12.0, true, false, dd_color_hex(0xFFFFFF, 1.0));
        dd_canvas_register_hit(cv, ACT_QUEUE_ASK_TRASH, 0, 0, NULL, "Move to Trash", tr_btn, true);
    } else {
        dd_canvas_round_rect(cv, (DDRect){x + w - 196.0, y + 6.0, 62.0, 30.0}, 8.0, th->chip, th->border, 1.0);
        dd_canvas_text_center(cv, (DDRect){x + w - 196.0, y + 6.0, 62.0, 30.0}, "Clear", 12.0, false, false, th->text_tertiary);
        dd_canvas_round_rect(cv, (DDRect){x + w - 128.0, y + 6.0, 128.0, 30.0}, 8.0, th->chip, th->border, 1.0);
        dd_canvas_text_center(cv, (DDRect){x + w - 128.0, y + 6.0, 128.0, 30.0}, "Move to Trash...", 12.0, false, false, th->text_tertiary);
    }
    y += 48.0;
    dd_canvas_fill_rect(cv, (DDRect){pop.x, y, pop.w, 1.0}, th->divider);
    y += 8.0;

    if (st->cleanup_queue.count == 0) {
        st->max_scroll_popover = 0;
        st->scroll_y_popover = 0;
        double cy = pop.y + pop.h * 0.52;
        dd_canvas_draw_icon(cv, ICON_TRASH, pop.x + pop.w * 0.5 - 16.0, cy - 36.0, 32.0, th->text_tertiary);
        dd_canvas_text_center(cv, (DDRect){x, cy + 4.0, w, 24.0}, "Nothing staged yet...", 14.0, true, false, th->text);
        dd_canvas_text_center(cv, (DDRect){x, cy + 28.0, w, 20.0}, "Stage items from Explore, Quick Wins, Duplicates, or Apps.", 11.5, false, false, th->text_secondary);
    } else {
        DDRect list_r = {pop.x, y, pop.w, pop.y + pop.h - y - 6.0};
        double total_h = st->cleanup_queue.count * 44.0 + 8.0;
        clamp_scroll(&st->scroll_y_popover, total_h, list_r.h, &st->max_scroll_popover);
        dd_canvas_push_clip(cv, list_r);
        y -= st->scroll_y_popover;
        for (int i = 0; i < st->cleanup_queue.count; i++) {
            const DDCleanupItem *it = &st->cleanup_queue.items[i];
            DDRect ir = {x, y, w, 40.0};
            if (ir.y + ir.h >= list_r.y && ir.y <= list_r.y + list_r.h) {
                dd_canvas_round_rect(cv, ir, 8.0, th->card, th->border, 1.0);
                dd_canvas_text_clipped(cv, (DDRect){x + 10.0, y + 3.0, w - 110.0, 20.0}, it->name, 12.5, true, false, th->text);
                dd_canvas_text_clipped(cv, (DDRect){x + 10.0, y + 20.0, w - 110.0, 18.0}, it->reason, 11.0, false, false, th->text_secondary);
                char isz[32];
                dd_format_bytes(it->size, isz, sizeof(isz));
                dd_canvas_text_Right(cv, x + w - 34.0, y + 25.0, isz, 12.0, true, true, th->text);

                DDRect rm = {x + w - 28.0, y + 8.0, 22.0, 24.0};
                dd_canvas_draw_icon(cv, ICON_CLOSE, rm.x + 4.0, rm.y + 5.0, 14.0, th->text_secondary);
                dd_canvas_register_hit(cv, ACT_QUEUE_REMOVE_ITEM, i, 0, NULL, "Remove from Queue", rm, true);
            }
            y += 44.0;
        }
        dd_canvas_pop_clip(cv);
    }

    dd_canvas_pop_clip(cv);
}

static void render_modals(DDAppState *st, DDCanvas *cv, int W, int H) {
    const DDTheme *th = &st->theme;

    bool any_modal = st->show_ctx_menu || st->show_trash_confirm || st->show_uninstall_modal ||
                     st->show_quicklook || st->show_trash_error || st->show_shortcuts_help;
    if (any_modal) {
        dd_canvas_clear_hits(cv);
        dd_canvas_clear_viz_hits(cv);
        dd_canvas_register_hit(cv, ACT_CLOSE_MODAL, 0, 0, NULL, "Close Modal", (DDRect){0, 0, (double)W, (double)H}, false);
    }

    if (st->show_ctx_menu) {
        DDRect cm = {st->ctx_x, st->ctx_y, 210.0, 168.0};
        if (cm.x + cm.w > W - 10.0) cm.x = W - cm.w - 10.0;
        if (cm.y + cm.h > H - 10.0) cm.y = H - cm.h - 10.0;
        dd_canvas_round_rect(cv, cm, 10.0, th->card, th->control_border, 1.5);
        dd_canvas_register_hit(cv, ACT_NONE, 0, 0, NULL, "Context Menu", cm, false);
        double y = cm.y + 8.0;
        bool is_staged = is_node_staged(st, st->ctx_node_idx);
        struct { const char *lbl; int act; } items[5] = {
            {"Open", ACT_CTX_OPEN},
            {"Quick Look", ACT_CTX_QUICKLOOK},
            {"Reveal in File Manager", ACT_CTX_REVEAL},
            {"Copy Path", ACT_CTX_COPY},
            {is_staged ? "Remove from Cleanup" : "Add to Cleanup", ACT_CTX_STAGE}
        };
        for (int i = 0; i < 5; i++) {
            DDRect ir = {cm.x + 6.0, y, cm.w - 12.0, 28.0};
            dd_canvas_round_rect(cv, ir, 6.0, th->chip, dd_color_hex(0,0), 0);
            dd_canvas_text(cv, ir.x + 10.0, ir.y + 19.0, items[i].lbl, 12.0, false, false, th->text);
            dd_canvas_register_hit(cv, items[i].act, st->ctx_node_idx, st->ctx_qw_group, NULL, items[i].lbl, ir, true);
            y += 30.0;
        }
    }

    if (st->show_trash_confirm) {
        dd_canvas_fill_rect(cv, (DDRect){0, 0, (double)W, (double)H}, dd_color_hex(0x000000, 0.45));
        DDRect md = {(W - 420.0) * 0.5, (H - 190.0) * 0.5, 420.0, 190.0};
        dd_canvas_round_rect(cv, md, 12.0, th->card, th->control_border, 1.5);
        dd_canvas_register_hit(cv, ACT_NONE, 0, 0, NULL, "Trash Confirm Dialog", md, false);
        char title[96], body[160], sz_s[32];
        dd_format_bytes(st->cleanup_queue.total_size, sz_s, sizeof(sz_s));
        snprintf(title, sizeof(title), "Move %d item%s to Trash?", st->cleanup_queue.count, st->cleanup_queue.count == 1 ? "" : "s");
        snprintf(body, sizeof(body), "This frees %s. Items go to the system Trash and are never permanently deleted.", sz_s);
        dd_canvas_text(cv, md.x + 20.0, md.y + 38.0, title, 16.0, true, false, th->text);
        dd_canvas_text_clipped(cv, (DDRect){md.x + 20.0, md.y + 56.0, md.w - 40.0, 48.0}, body, 12.5, false, false, th->text_secondary);

        draw_outline_button(cv, th, (DDRect){md.x + md.w - 230.0, md.y + 136.0, 96.0, 34.0}, "Cancel", false, ACT_CONFIRM_TRASH_CANCEL, 0, 0, NULL);
        DDRect yes_r = {md.x + md.w - 124.0, md.y + 136.0, 104.0, 34.0};
        dd_canvas_round_rect(cv, yes_r, 8.0, th->used, th->control_border, 1.0);
        dd_canvas_text_center(cv, yes_r, "Move to Trash", 12.5, true, false, dd_color_hex(0xFFFFFF, 1.0));
        dd_canvas_register_hit(cv, ACT_CONFIRM_TRASH_YES, 0, 0, NULL, "Confirm Move to Trash", yes_r, true);
    }

    if (st->show_uninstall_modal && st->uninstall_app_idx >= 0 && st->uninstall_app_idx < st->app_store.count) {
        const DDInstalledApp *app = &st->app_store.apps[st->uninstall_app_idx];
        dd_canvas_fill_rect(cv, (DDRect){0, 0, (double)W, (double)H}, dd_color_hex(0x000000, 0.45));
        DDRect md = {(W - 460.0) * 0.5, (H - 210.0) * 0.5, 460.0, 210.0};
        dd_canvas_round_rect(cv, md, 12.0, th->card, th->control_border, 1.5);
        dd_canvas_register_hit(cv, ACT_NONE, 0, 0, NULL, "Uninstall Dialog", md, false);
        char title[160], body[200], tsz[32];
        dd_format_bytes(app->total_size, tsz, sizeof(tsz));
        snprintf(title, sizeof(title), "Uninstall %s?", app->name);
        snprintf(body, sizeof(body), "Bundle + %d leftover group(s) total %s. Choose whether to move to Trash now or stage in Cleanup Queue.",
                 app->leftover_count, tsz);
        dd_canvas_text(cv, md.x + 20.0, md.y + 38.0, title, 16.0, true, false, th->text);
        dd_canvas_text_clipped(cv, (DDRect){md.x + 20.0, md.y + 58.0, md.w - 40.0, 50.0}, body, 12.0, false, false, th->text_secondary);

        draw_outline_button(cv, th, (DDRect){md.x + 20.0, md.y + 152.0, 90.0, 34.0}, "Cancel", false, ACT_CLOSE_MODAL, 0, 0, NULL);
        draw_outline_button(cv, th, (DDRect){md.x + 120.0, md.y + 152.0, 172.0, 34.0}, "Add to Cleanup Queue", false, ACT_APP_UNINSTALL_QUEUE, st->uninstall_app_idx, 0, NULL);
        DDRect tr_r = {md.x + 302.0, md.y + 152.0, 138.0, 34.0};
        dd_canvas_round_rect(cv, tr_r, 8.0, th->used, th->control_border, 1.0);
        dd_canvas_text_center(cv, tr_r, "Move to Trash", 12.5, true, false, dd_color_hex(0xFFFFFF, 1.0));
        dd_canvas_register_hit(cv, ACT_APP_UNINSTALL_TRASH, st->uninstall_app_idx, 0, NULL, "Move App to Trash", tr_r, true);
    }

    if (st->show_quicklook && st->tree && st->quicklook_node >= 0 && st->quicklook_node < st->tree->node_count) {
        const DDFileNode *qn = &st->tree->nodes[st->quicklook_node];
        dd_canvas_fill_rect(cv, (DDRect){0, 0, (double)W, (double)H}, dd_color_hex(0x000000, 0.45));
        DDRect md = {(W - 500.0) * 0.5, (H - 250.0) * 0.5, 500.0, 250.0};
        dd_canvas_round_rect(cv, md, 12.0, th->card, th->control_border, 1.5);
        dd_canvas_register_hit(cv, ACT_NONE, 0, 0, NULL, "Quick Look Dialog", md, false);
        char qpath[512], qsz[32], qmod[64];
        dd_node_build_path(st->tree, st->quicklook_node, qpath, sizeof(qpath));
        dd_format_bytes(qn->size_on_disk, qsz, sizeof(qsz));
        dd_format_date(qn->modified_stamp, qmod, sizeof(qmod));

        dd_canvas_text(cv, md.x + 20.0, md.y + 36.0, "Quick Look Preview", 15.0, true, false, th->text);
        dd_canvas_text_clipped(cv, (DDRect){md.x + 20.0, md.y + 58.0, md.w - 40.0, 24.0}, dd_node_name(st->tree, st->quicklook_node), 14.0, true, false, th->ink);
        dd_canvas_text_clipped(cv, (DDRect){md.x + 20.0, md.y + 88.0, md.w - 40.0, 22.0}, qpath, 11.5, false, true, th->text_secondary);
        dd_canvas_text(cv, md.x + 20.0, md.y + 132.0, "Size on disk:", 12.0, false, false, th->text_secondary);
        dd_canvas_text(cv, md.x + 130.0, md.y + 132.0, qsz, 12.0, true, true, th->text);

        if (qn->is_directory && qn->folder_idx >= 0) {
            const DDFolderExtra *fe = &st->tree->folders[qn->folder_idx];
            char qcont[64];
            snprintf(qcont, sizeof(qcont), "(%d files, %d folders)", fe->file_count, fe->folder_count);
            dd_canvas_text(cv, md.x + 220.0, md.y + 132.0, qcont, 11.5, false, false, th->text_tertiary);
        }

        dd_canvas_text(cv, md.x + 20.0, md.y + 156.0, "Last modified:", 12.0, false, false, th->text_secondary);
        dd_canvas_text(cv, md.x + 130.0, md.y + 156.0, qmod, 12.0, false, true, th->text);

        bool staged = is_node_staged(st, st->quicklook_node);
        draw_outline_button(cv, th, (DDRect){md.x + 20.0, md.y + 196.0, 116.0, 34.0}, "Reveal in Files", false, ACT_INSPECTOR_REVEAL, st->quicklook_node, 0, NULL);
        draw_outline_button(cv, th, (DDRect){md.x + 144.0, md.y + 196.0, 96.0, 34.0}, "Copy Path", false, ACT_INSPECTOR_COPY, st->quicklook_node, 0, NULL);
        draw_outline_button(cv, th, (DDRect){md.x + 248.0, md.y + 196.0, staged ? 116.0 : 106.0, 34.0}, staged ? "Staged ✓" : "Add to Queue", staged, ACT_INSPECTOR_STAGE, st->quicklook_node, 0, NULL);
        draw_outline_button(cv, th, (DDRect){md.x + md.w - 100.0, md.y + 196.0, 80.0, 34.0}, "Close (Esc)", false, ACT_CLOSE_MODAL, 0, 0, NULL);
    }

    if (st->show_shortcuts_help) {
        dd_canvas_fill_rect(cv, (DDRect){0, 0, (double)W, (double)H}, dd_color_hex(0x000000, 0.45));
        DDRect md = {(W - 540.0) * 0.5, (H - 430.0) * 0.5, 540.0, 430.0};
        dd_canvas_round_rect(cv, md, 12.0, th->card, th->control_border, 1.5);
        dd_canvas_register_hit(cv, ACT_NONE, 0, 0, NULL, "Shortcuts Help", md, false);

        dd_canvas_text(cv, md.x + 24.0, md.y + 34.0, "Keyboard Shortcuts", 16.0, true, false, th->text);
        dd_canvas_text(cv, md.x + 24.0, md.y + 56.0, "Fast navigation and inspection controls", 12.0, false, false, th->text_secondary);

        double sy = md.y + 86.0;
        static const struct { const char *key; const char *desc; } scs[] = {
            {"Arrow Keys", "Navigate files and folders in Explore view"},
            {"Enter / Right", "Drill down into selected folder"},
            {"Backspace / Left", "Navigate up to parent directory"},
            {"Space", "Toggle Quick Look file preview"},
            {"Ctrl+F / Ctrl+K", "Focus filter search box"},
            {"Ctrl+1 .. Ctrl+5", "Switch tabs (Explore, Dups, Apps, Monitor, Snaps)"},
            {"1 .. 9", "Switch Explore mode (Treemap, Folders, Sunburst, etc.)"},
            {"+ / -", "Increase or decrease visualization depth limit"},
            {"A", "Toggle compact label abbreviation on / off"},
            {"Ctrl+R", "Rescan current folder"},
            {"Ctrl+O", "Choose new folder to scan"},
            {"Ctrl+D", "Toggle Dark and Light theme"},
            {"Ctrl+I", "Toggle Inspector panel sidebar"},
            {"Escape", "Close popovers and clear search filter"}
        };
        int num_scs = (int)(sizeof(scs) / sizeof(scs[0]));
        for (int i = 0; i < num_scs; i++) {
            double row_y = sy + i * 21.0;
            dd_canvas_text(cv, md.x + 24.0, row_y, scs[i].key, 11.5, true, true, th->ink);
            dd_canvas_text(cv, md.x + 160.0, row_y, scs[i].desc, 11.5, false, false, th->text);
        }

        draw_outline_button(cv, th, (DDRect){md.x + md.w - 110.0, md.y + md.h - 44.0, 86.0, 32.0}, "Close (Esc)", false, ACT_CLOSE_MODAL, 0, 0, NULL);
    }

    if (st->show_trash_error) {
        dd_canvas_fill_rect(cv, (DDRect){0, 0, (double)W, (double)H}, dd_color_hex(0x000000, 0.45));
        DDRect md = {(W - 440.0) * 0.5, (H - 180.0) * 0.5, 440.0, 180.0};
        dd_canvas_round_rect(cv, md, 12.0, th->card, th->control_border, 1.5);
        dd_canvas_register_hit(cv, ACT_NONE, 0, 0, NULL, "Error Dialog", md, false);
        dd_canvas_text(cv, md.x + 20.0, md.y + 38.0, st->trash_error_msg, 14.0, true, false, th->used_text);
        dd_canvas_text(cv, md.x + 20.0, md.y + 68.0, "Check filesystem permissions for the restricted items.", 12.0, false, false, th->text_secondary);
        draw_outline_button(cv, th, (DDRect){md.x + md.w - 110.0, md.y + 128.0, 90.0, 34.0}, "Dismiss", false, ACT_CLOSE_MODAL, 0, 0, NULL);
    }
}

void dd_views_render(DDAppState *st, DDCanvas *cv) {
    if (!st || !cv) return;
    int W = dd_canvas_width(cv);
    int H = dd_canvas_height(cv);
    st->last_win_w = W;
    st->last_win_h = H;
    const DDTheme *th = &st->theme;

    dd_canvas_begin_frame(cv, th->background);
    render_top_bar(st, cv, W);

    double body_y = 56.0;
    double body_h = (double)H - body_y;

    bool wide = (W >= 1280);
    bool medium = (W >= 860 && W < 1280);
    double sb_w = wide ? 340.0 : (medium ? 250.0 : 0.0);
    double ins_w = (st->active_tab == TAB_EXPLORE && st->show_inspector) ? (wide ? 382.0 : (medium ? 300.0 : 0.0)) : 0.0;

    if (sb_w > 0.0) {
        render_sidebar(st, cv, (DDRect){0, body_y, sb_w, body_h});
    }

    DDRect main_r = {sb_w, body_y, (double)W - sb_w - ins_w, body_h};
    switch (st->active_tab) {
        case TAB_EXPLORE:      render_explore_tab(st, cv, main_r); break;
        case TAB_DUPLICATES:   render_duplicates_tab(st, cv, main_r); break;
        case TAB_APPLICATIONS: render_applications_tab(st, cv, main_r); break;
        case TAB_MONITOR:      render_monitor_tab(st, cv, main_r); break;
        case TAB_SNAPSHOTS:    render_snapshots_tab(st, cv, main_r); break;
    }

    if (ins_w > 0.0) {
        render_inspector(st, cv, (DDRect){(double)W - ins_w, body_y, ins_w, body_h});
    }

    if (W < 860 && st->show_sidebar_drawer) {
        dd_canvas_fill_rect(cv, (DDRect){0, body_y, (double)W, body_h}, dd_color_hex(0x000000, 0.35));
        render_sidebar(st, cv, (DDRect){0, body_y, fmin(320.0, W * 0.85), body_h});
    }

    if (st->show_queue_popover) {
        render_queue_popover(st, cv, W);
    }

    render_modals(st, cv, W, H);

    if (st->focused_hit_idx >= 0 && st->focused_hit_idx < dd_canvas_hit_count(cv)) {
        const DDHitRegion *fh = dd_canvas_get_hit(cv, st->focused_hit_idx);
        if (fh && fh->focusable) {
            DDRect fr = {fh->rect.x - 2.0, fh->rect.y - 2.0, fh->rect.w + 4.0, fh->rect.h + 4.0};
            dd_canvas_round_rect(cv, fr, 9.0, dd_color_hex(0,0), th->focus_ring, 2.2);
        }
    }

    dd_canvas_end_frame(cv);
}

static void stage_node_by_idx(DDAppState *st, int32_t node_idx, const char *reason, bool toggle) {
    if (!st->tree || node_idx < 0 || node_idx >= st->tree->node_count) return;
    char path[512];
    dd_node_build_path(st->tree, node_idx, path, sizeof(path));
    if (toggle && dd_queue_contains_path(&st->cleanup_queue, path)) {
        dd_queue_unstage_path(&st->cleanup_queue, path);
    } else {
        dd_queue_stage(&st->cleanup_queue, path, dd_node_name(st->tree, node_idx),
                       st->tree->nodes[node_idx].size_on_disk, reason, node_idx);
    }
}

static void execute_commit_queue_to_trash(DDAppState *st) {
    if (st->cleanup_queue.count <= 0) return;
    int count = st->cleanup_queue.count;
    char (*paths)[512] = (char (*)[512])calloc((size_t)count, 512);
    if (!paths) return;
    for (int i = 0; i < count; i++) {
        snprintf(paths[i], 512, "%s", st->cleanup_queue.items[i].path);
    }
    DDTrashResult res;
    dd_trash_move_paths(paths, count, &res);

    if (st->tree) {
        dd_tree_prune_paths(st->tree, paths, count, &st->current_folder, &st->selected_node);
        dd_quick_wins_compute_for(st->tree, st->current_folder, &st->quick_wins);
        st->top_sizes_cache.valid = false;
        st->age_report.valid = false;
    }
    dd_duplicates_prune(&st->duplicates, paths, count);
    dd_apps_prune(&st->app_store, paths, count);
    dd_queue_clear(&st->cleanup_queue);

    st->show_trash_confirm = false;
    if (res.failed_count > 0) {
        snprintf(st->trash_error_msg, sizeof(st->trash_error_msg), "Couldn't trash %d item(s)", res.failed_count);
        st->show_trash_error = true;
    } else {
        st->show_queue_popover = false;
    }
    free(paths);
}

bool dd_views_handle_action(DDAppState *st, const DDHitRegion *hit) {
    if (!st || !hit) return false;
    if (hit->action == ACT_NONE) return true;
    st->show_ctx_menu = false;

    switch ((DDActionCode)hit->action) {
        case ACT_SWITCH_TAB:
            st->active_tab = (DDTab)hit->param_int;
            st->scroll_y_main = 0;
            if (st->active_tab == TAB_MONITOR) dd_monitor_sample(&st->monitor);
            return true;
        case ACT_TOGGLE_SIDEBAR_DRAWER:
            st->show_sidebar_drawer = !st->show_sidebar_drawer;
            return true;
        case ACT_SCAN_ROOT: {
            char root[512];
            dd_get_root_dir(root, sizeof(root));
            dd_app_start_scan(st, root);
            st->show_sidebar_drawer = false;
            return true;
        }
        case ACT_SCAN_HOME: {
            char home[512];
            dd_get_home_dir(home, sizeof(home));
            dd_app_start_scan(st, home);
            st->show_sidebar_drawer = false;
            return true;
        }
        case ACT_SCAN_PICK: {
            char picked[512];
            if (dd_pick_folder_dialog(picked, sizeof(picked))) {
                dd_app_start_scan(st, picked);
            }
            st->show_sidebar_drawer = false;
            return true;
        }
        case ACT_SCAN_RECENT:
            if (hit->param_str[0]) dd_app_start_scan(st, hit->param_str);
            st->show_sidebar_drawer = false;
            return true;
        case ACT_RESCAN_CURRENT:
            if (st->tree) {
                char root[512];
                snprintf(root, sizeof(root), "%s", st->tree->root_path);
                dd_app_start_scan(st, root);
            }
            return true;
        case ACT_CANCEL_SCAN:
            dd_scanner_cancel(st->scanner);
            dd_scanner_get_progress(st->scanner, &st->scan_progress);
            return true;
        case ACT_BREADCRUMB:
            set_current_folder(st, hit->param_int);
            return true;
        case ACT_NAV_BACK:
            if (st->tree && st->current_folder > 0) {
                int32_t p = st->tree->nodes[st->current_folder].parent_idx;
                if (p >= 0) set_current_folder(st, p);
            }
            return true;
        case ACT_FOCUS_SEARCH:
            st->search_focused = true;
            return true;
        case ACT_CLEAR_SEARCH:
            st->search_query[0] = '\0';
            st->scroll_y_main = 0;
            return true;
        case ACT_TOGGLE_QUEUE_POPOVER:
            st->show_queue_popover = !st->show_queue_popover;
            return true;
        case ACT_TOGGLE_THEME:
            st->settings.is_dark = !st->settings.is_dark;
            st->settings.has_theme_pref = true;
            dd_theme_init(&st->theme, st->settings.is_dark);
            dd_settings_save(&st->settings);
            return true;
        case ACT_TOGGLE_INSPECTOR:
            st->show_inspector = !st->show_inspector;
            return true;
        case ACT_REVEAL_CURRENT:
            if (st->tree) {
                char p[512];
                dd_node_build_path(st->tree, st->current_folder, p, sizeof(p));
                dd_reveal_in_file_manager(p);
            }
            return true;
        case ACT_COPY_CURRENT_PATH:
            if (st->tree) {
                char p[512];
                dd_node_build_path(st->tree, st->current_folder, p, sizeof(p));
                dd_copy_to_clipboard(p);
                st->copy_confirm_until = dd_now_seconds() + 1.0;
            }
            return true;
        case ACT_QUICK_WIN_CLICK:
            if (st->tree && hit->param_int >= 0 && hit->param_int < st->quick_wins.group_count) {
                int32_t bidx = st->quick_wins.groups[hit->param_int].biggest_node_idx;
                if (bidx >= 0) {
                    st->active_tab = TAB_EXPLORE;
                    if (st->tree->nodes[bidx].is_directory) {
                        set_current_folder(st, bidx);
                    } else if (st->tree->nodes[bidx].parent_idx >= 0) {
                        set_current_folder(st, st->tree->nodes[bidx].parent_idx);
                    }
                    st->selected_node = bidx;
                }
            }
            return true;
        case ACT_QUICK_WIN_STAGE_ALL:
            if (st->tree && hit->param_int >= 0 && hit->param_int < st->quick_wins.group_count) {
                const DDQuickWinGroup *g = &st->quick_wins.groups[hit->param_int];
                bool all_staged = (g->item_count > 0);
                for (int i = 0; i < g->item_count; i++) {
                    if (!is_node_staged(st, g->items[i])) {
                        all_staged = false;
                        break;
                    }
                }
                char reason[96];
                snprintf(reason, sizeof(reason), "Quick Win: %s", g->title);
                for (int i = 0; i < g->item_count; i++) {
                    if (all_staged) {
                        char path[512];
                        dd_node_build_path(st->tree, g->items[i], path, sizeof(path));
                        dd_queue_unstage_path(&st->cleanup_queue, path);
                    } else {
                        stage_node_by_idx(st, g->items[i], reason, false);
                    }
                }
            }
            return true;
        case ACT_VIZ_MODE:
            st->viz_mode = (DDVizMode)hit->param_int;
            st->scroll_y_main = 0;
            return true;
        case ACT_COLOR_MODE:
            st->color_mode = (DDColorMode)hit->param_int;
            return true;
        case ACT_DEPTH_DEC:
            if (st->depth_limit > 2) st->depth_limit--;
            return true;
        case ACT_DEPTH_INC:
            if (st->depth_limit < 10) st->depth_limit++;
            return true;
        case ACT_TOGGLE_ABBREV:
            st->abbreviate_labels = !st->abbreviate_labels;
            return true;
        case ACT_DISMISS_DENIED:
            st->denied_banner_dismissed = true;
            return true;
        case ACT_OPEN_PERMS_HELP:
            dd_open_permissions_help();
            return true;
        case ACT_TOP_SCOPE:
            st->top_scope = (DDTopScope)hit->param_int;
            st->scroll_y_main = 0;
            return true;
        case ACT_STAGE_NODE:
            stage_node_by_idx(st, hit->param_int, hit->param_int2 == 1 ? "Big & Untouched" : "Staged from Explore", true);
            return true;
        case ACT_STAGE_TOP_UNTOUCHED:
            for (int i = 0; i < st->age_report.big_untouched_count; i++) {
                stage_node_by_idx(st, st->age_report.big_untouched[i].node_idx, "Big & Untouched", false);
            }
            return true;
        case ACT_TOGGLE_LIST_EXPAND:
            toggle_folder_expanded(st, hit->param_int);
            return true;
        case ACT_LIST_SORT:
            if (st->list_sort_col == hit->param_int) {
                st->list_sort_asc = !st->list_sort_asc;
            } else {
                st->list_sort_col = hit->param_int;
                st->list_sort_asc = (hit->param_int == 1);
            }
            return true;
        case ACT_SELECT_NODE:
            if (st->tree && hit->param_int >= 0 && hit->param_int < st->tree->node_count) {
                if (hit->param_int2 == 1 && st->selected_node == hit->param_int && st->tree->nodes[hit->param_int].is_directory) {
                    set_current_folder(st, hit->param_int);
                } else {
                    st->selected_node = hit->param_int;
                }
            }
            return true;
        case ACT_INSPECTOR_REVEAL:
            if (st->tree) {
                char p[512];
                dd_node_build_path(st->tree, hit->param_int, p, sizeof(p));
                dd_reveal_in_file_manager(p);
            }
            return true;
        case ACT_INSPECTOR_QUICKLOOK:
            st->quicklook_node = hit->param_int;
            st->show_quicklook = true;
            return true;
        case ACT_INSPECTOR_FOCUS:
            if (st->tree && hit->param_int >= 0 && hit->param_int < st->tree->node_count) {
                if (hit->param_int == st->current_folder && st->tree->nodes[hit->param_int].parent_idx >= 0) {
                    set_current_folder(st, st->tree->nodes[hit->param_int].parent_idx);
                } else if (st->tree->nodes[hit->param_int].is_directory) {
                    set_current_folder(st, hit->param_int);
                } else if (st->tree->nodes[hit->param_int].parent_idx >= 0) {
                    set_current_folder(st, st->tree->nodes[hit->param_int].parent_idx);
                    st->selected_node = hit->param_int;
                }
            }
            return true;
        case ACT_INSPECTOR_COPY:
            if (st->tree) {
                char p[512];
                dd_node_build_path(st->tree, hit->param_int, p, sizeof(p));
                dd_copy_to_clipboard(p);
                st->inspector_copy_until = dd_now_seconds() + 1.0;
            }
            return true;
        case ACT_INSPECTOR_STAGE:
            stage_node_by_idx(st, hit->param_int, "Staged from Inspector", true);
            return true;
        case ACT_QUEUE_CLEAR:
            dd_queue_clear(&st->cleanup_queue);
            return true;
        case ACT_QUEUE_REMOVE_ITEM:
            dd_queue_remove_at(&st->cleanup_queue, hit->param_int);
            return true;
        case ACT_QUEUE_ASK_TRASH:
            if (st->cleanup_queue.count > 0) st->show_trash_confirm = true;
            return true;
        case ACT_CONFIRM_TRASH_CANCEL:
            st->show_trash_confirm = false;
            return true;
        case ACT_CONFIRM_TRASH_YES:
            execute_commit_queue_to_trash(st);
            return true;
        case ACT_CLOSE_MODAL:
            st->show_quicklook = false;
            st->show_uninstall_modal = false;
            st->show_trash_error = false;
            st->show_trash_confirm = false;
            st->show_shortcuts_help = false;
            return true;
        case ACT_TOGGLE_SHORTCUTS_HELP:
            st->show_shortcuts_help = !st->show_shortcuts_help;
            return true;
        case ACT_DUP_SCAN:
            if (st->tree) dd_duplicates_start(st->tree, st->current_folder, &st->duplicates);
            return true;
        case ACT_DUP_CANCEL:
            dd_duplicates_cancel(&st->duplicates);
            return true;
        case ACT_DUP_KEEP_ONE:
            if (hit->param_int >= 0 && hit->param_int < st->duplicates.group_count) {
                const DDDuplicateGroup *g = &st->duplicates.groups[hit->param_int];
                if (hit->param_int2 >= 0 && hit->param_int2 < g->count) {
                    dd_queue_unstage_path(&st->cleanup_queue, g->paths[hit->param_int2]);
                }
                for (int j = 0; j < g->count; j++) {
                    if (j != hit->param_int2) {
                        dd_queue_stage(&st->cleanup_queue, g->paths[j], NULL, g->file_size, "Duplicate", g->node_indices[j]);
                    }
                }
            }
            return true;
        case ACT_DUP_KEEP_ALL_NEWEST:
            for (int gi = 0; gi < st->duplicates.group_count; gi++) {
                const DDDuplicateGroup *g = &st->duplicates.groups[gi];
                int newest_j = 0;
                double newest_ts = -1.0;
                for (int j = 0; j < g->count; j++) {
                    int32_t nidx = g->node_indices[j];
                    double ts = (st->tree && nidx >= 0 && nidx < st->tree->node_count)
                                ? st->tree->nodes[nidx].modified_stamp : 0.0;
                    if (ts > newest_ts) {
                        newest_ts = ts;
                        newest_j = j;
                    }
                }
                dd_queue_unstage_path(&st->cleanup_queue, g->paths[newest_j]);
                for (int j = 0; j < g->count; j++) {
                    if (j != newest_j) {
                        dd_queue_stage(&st->cleanup_queue, g->paths[j], NULL, g->file_size, "Duplicate", g->node_indices[j]);
                    }
                }
            }
            return true;
        case ACT_APPS_REFRESH:
            dd_apps_start_load(&st->app_store);
            return true;
        case ACT_APP_TOGGLE_EXPAND:
            if (hit->param_int >= 0 && hit->param_int < st->app_store.count) {
                st->app_store.apps[hit->param_int].expanded = !st->app_store.apps[hit->param_int].expanded;
            }
            return true;
        case ACT_APP_STAGE_LEFTOVER:
            if (hit->param_int >= 0 && hit->param_int < st->app_store.count) {
                const DDInstalledApp *app = &st->app_store.apps[hit->param_int];
                int l = hit->param_int2;
                if (l >= 0 && l < app->leftover_count) {
                    if (dd_queue_contains_path(&st->cleanup_queue, app->leftovers[l].path)) {
                        dd_queue_unstage_path(&st->cleanup_queue, app->leftovers[l].path);
                    } else {
                        char rl[160];
                        snprintf(rl, sizeof(rl), "%s: %s", app->name, app->leftovers[l].category);
                        dd_queue_stage(&st->cleanup_queue, app->leftovers[l].path, NULL, app->leftovers[l].size, rl, -1);
                    }
                }
            }
            return true;
        case ACT_APP_ASK_UNINSTALL:
            st->uninstall_app_idx = hit->param_int;
            st->show_uninstall_modal = true;
            return true;
        case ACT_APP_UNINSTALL_QUEUE:
            if (hit->param_int >= 0 && hit->param_int < st->app_store.count) {
                const DDInstalledApp *app = &st->app_store.apps[hit->param_int];
                char r1[160];
                snprintf(r1, sizeof(r1), "App bundle: %s", app->name);
                dd_queue_stage(&st->cleanup_queue, app->bundle_path, app->name, app->bundle_size, r1, -1);
                for (int l = 0; l < app->leftover_count; l++) {
                    char rl[160];
                    snprintf(rl, sizeof(rl), "%s: %s", app->name, app->leftovers[l].category);
                    dd_queue_stage(&st->cleanup_queue, app->leftovers[l].path, NULL, app->leftovers[l].size, rl, -1);
                }
                st->show_uninstall_modal = false;
            }
            return true;
        case ACT_APP_UNINSTALL_TRASH:
            if (hit->param_int >= 0 && hit->param_int < st->app_store.count) {
                const DDInstalledApp *app = &st->app_store.apps[hit->param_int];
                DDTrashResult res;
                dd_app_terminate_and_trash(app, &res);
                st->show_uninstall_modal = false;
                if (res.failed_count > 0) {
                    snprintf(st->trash_error_msg, sizeof(st->trash_error_msg), "Permission denied trashing %s", app->name);
                    st->show_trash_error = true;
                } else {
                    dd_apps_start_load(&st->app_store);
                }
            }
            return true;
        case ACT_MON_SORT_TOGGLE:
            st->mon_sort_by_mem = (hit->param_int != 0);
            return true;
        case ACT_MON_TOGGLE_VOLUMES:
            st->mon_show_all_vols = !st->mon_show_all_vols;
            return true;
        case ACT_MON_TOGGLE_PROCS:
            st->mon_show_all_procs = !st->mon_show_all_procs;
            return true;
        case ACT_SNAP_TAKE:
            if (st->tree) dd_snapshots_take(&st->snapshots, st->tree);
            return true;
        case ACT_SNAP_SET_BEFORE:
            st->snapshots.before_idx = (st->snapshots.before_idx == hit->param_int) ? -1 : hit->param_int;
            st->snapshots.diff_valid = false;
            return true;
        case ACT_SNAP_SET_AFTER:
            st->snapshots.after_idx = (st->snapshots.after_idx == hit->param_int) ? -1 : hit->param_int;
            st->snapshots.diff_valid = false;
            return true;
        case ACT_SNAP_DELETE:
            dd_snapshots_delete(&st->snapshots, hit->param_int);
            return true;
        case ACT_CTX_OPEN:
            if (st->tree && hit->param_int >= 0 && hit->param_int < st->tree->node_count) {
                if (st->tree->nodes[hit->param_int].is_directory) {
                    set_current_folder(st, hit->param_int);
                } else {
                    st->quicklook_node = hit->param_int;
                    st->show_quicklook = true;
                }
            }
            return true;
        case ACT_CTX_QUICKLOOK:
            st->quicklook_node = hit->param_int;
            st->show_quicklook = true;
            return true;
        case ACT_CTX_REVEAL:
            if (st->tree && hit->param_int >= 0) {
                char p[512];
                dd_node_build_path(st->tree, hit->param_int, p, sizeof(p));
                dd_reveal_in_file_manager(p);
            }
            return true;
        case ACT_CTX_COPY:
            if (st->tree && hit->param_int >= 0) {
                char p[512];
                dd_node_build_path(st->tree, hit->param_int, p, sizeof(p));
                dd_copy_to_clipboard(p);
            }
            return true;
        case ACT_CTX_STAGE:
            stage_node_by_idx(st, hit->param_int, "Staged from Context Menu", true);
            return true;
        default:
            return false;
    }
}

bool dd_views_handle_click(DDAppState *st, DDCanvas *cv, double mx, double my, int button, bool is_double) {
    if (!st || !cv) return false;
    st->search_focused = false;

    int hit_idx = dd_canvas_hit_test(cv, mx, my);
    if (button == 1 && hit_idx >= 0) {
        const DDHitRegion *h = dd_canvas_get_hit(cv, hit_idx);
        st->focused_hit_idx = hit_idx;
        return dd_views_handle_action(st, h);
    }

    st->show_ctx_menu = false;

    if (st->show_queue_popover) {
        int W = dd_canvas_width(cv);
        if (mx < W - 476.0 || my > 582.0 || my < 62.0) {
            st->show_queue_popover = false;
            return true;
        }
        return false;
    }

    if (st->active_tab == TAB_EXPLORE && st->tree) {
        int vhit = dd_canvas_viz_hit_test(cv, mx, my);
        if (vhit >= 0) {
            const DDVizHitItem *vi = dd_canvas_get_viz_hit(cv, vhit);
            int32_t nidx = vi->cell_node_idx;
            if (nidx < 0 || nidx >= st->tree->node_count) return false;
            if (button == 3) {
                st->selected_node = nidx;
                st->ctx_node_idx = nidx;
                st->ctx_x = mx;
                st->ctx_y = my;
                st->show_ctx_menu = true;
                return true;
            }
            bool was_selected = (st->selected_node == nidx);
            st->selected_node = nidx;
            bool single_click_drills = (st->viz_mode == VIZ_FOLDERS || st->viz_mode == VIZ_SUNBURST || st->viz_mode == VIZ_FLAME);
            if (is_double || (single_click_drills && st->tree->nodes[nidx].is_directory) ||
                (was_selected && st->tree->nodes[nidx].is_directory && nidx != st->current_folder)) {
                if (st->tree->nodes[nidx].is_directory) {
                    set_current_folder(st, nidx);
                } else {
                    st->quicklook_node = nidx;
                    st->show_quicklook = true;
                }
            }
            return true;
        }
    }

    return false;
}

bool dd_views_handle_motion(DDAppState *st, DDCanvas *cv, double mx, double my) {
    if (!st || !cv) return false;
    st->mouse_x = mx;
    st->mouse_y = my;
    int32_t prev_hov = st->hovered_node;
    st->hovered_node = -1;
    if (st->active_tab == TAB_EXPLORE && st->tree && !st->show_queue_popover) {
        int vhit = dd_canvas_viz_hit_test(cv, mx, my);
        if (vhit >= 0) {
            st->hovered_node = dd_canvas_get_viz_hit(cv, vhit)->cell_node_idx;
        }
    }
    return prev_hov != st->hovered_node;
}

bool dd_views_handle_scroll(DDAppState *st, double mx, double my, int delta_y) {
    if (!st) return false;
    int W = st->last_win_w > 0 ? st->last_win_w : 1280;

    if (st->show_queue_popover && mx >= W - 476.0 && my >= 62.0 && my <= 582.0) {
        st->scroll_y_popover += delta_y * 28.0;
        if (st->scroll_y_popover < 0) st->scroll_y_popover = 0;
        if (st->scroll_y_popover > st->max_scroll_popover) st->scroll_y_popover = st->max_scroll_popover;
        return true;
    }

    bool wide = (W >= 1280);
    bool medium = (W >= 860 && W < 1280);
    double sb_w = wide ? 340.0 : (medium ? 250.0 : (st->show_sidebar_drawer ? 320.0 : 0.0));
    double ins_w = (st->active_tab == TAB_EXPLORE && st->show_inspector) ? (wide ? 382.0 : (medium ? 300.0 : 0.0)) : 0.0;

    if (sb_w > 0.0 && mx < sb_w) {
        st->scroll_y_sidebar += delta_y * 28.0;
        if (st->scroll_y_sidebar < 0) st->scroll_y_sidebar = 0;
        if (st->scroll_y_sidebar > st->max_scroll_sidebar) st->scroll_y_sidebar = st->max_scroll_sidebar;
    } else if (ins_w > 0.0 && mx >= W - ins_w) {
        st->scroll_y_inspector += delta_y * 28.0;
        if (st->scroll_y_inspector < 0) st->scroll_y_inspector = 0;
        if (st->scroll_y_inspector > st->max_scroll_inspector) st->scroll_y_inspector = st->max_scroll_inspector;
    } else {
        st->scroll_y_main += delta_y * 36.0;
        if (st->scroll_y_main < 0) st->scroll_y_main = 0;
        if (st->scroll_y_main > st->max_scroll_main) st->scroll_y_main = st->max_scroll_main;
    }
    return true;
}

bool dd_views_handle_key(DDAppState *st, DDCanvas *cv, int key_code, uint32_t ch, bool ctrl) {
    if (!st || !cv) return false;

    if (ctrl && (ch == 'k' || ch == 'K' || ch == 'f' || ch == 'F' || key_code == 11)) {
        st->search_focused = true;
        return true;
    }

    if (key_code == 27) {
        if (st->show_ctx_menu || st->show_quicklook || st->show_uninstall_modal ||
            st->show_trash_confirm || st->show_trash_error || st->show_queue_popover ||
            st->show_sidebar_drawer || st->show_shortcuts_help) {
            st->show_ctx_menu = false;
            st->show_quicklook = false;
            st->show_uninstall_modal = false;
            st->show_trash_confirm = false;
            st->show_trash_error = false;
            st->show_queue_popover = false;
            st->show_sidebar_drawer = false;
            st->show_shortcuts_help = false;
            return true;
        }
        if (st->search_focused || st->search_query[0]) {
            st->search_query[0] = '\0';
            st->search_focused = false;
            st->scroll_y_main = 0;
            return true;
        }
        return false;
    }

    if (st->search_focused) {
        if (key_code == 8) {
            size_t l = strlen(st->search_query);
            if (l > 0) {
                st->search_query[l - 1] = '\0';
                st->scroll_y_main = 0;
            }
            return true;
        }
        if (ch >= 32 && ch < 127) {
            size_t l = strlen(st->search_query);
            if (l + 1 < sizeof(st->search_query)) {
                st->search_query[l] = (char)ch;
                st->search_query[l + 1] = '\0';
                st->scroll_y_main = 0;
            }
            return true;
        }
    }

    if (ctrl) {
        if (ch >= '1' && ch <= '5') {
            st->active_tab = (DDTab)(ch - '1');
            st->scroll_y_main = 0;
            if (st->active_tab == TAB_MONITOR) dd_monitor_sample(&st->monitor);
            return true;
        }
        if (ch == 'r' || ch == 'R') {
            if (st->tree) {
                char root[512];
                snprintf(root, sizeof(root), "%s", st->tree->root_path);
                dd_app_start_scan(st, root);
                return true;
            }
        }
        if (ch == 'o' || ch == 'O') {
            char picked[512];
            if (dd_pick_folder_dialog(picked, sizeof(picked))) {
                dd_app_start_scan(st, picked);
                return true;
            }
        }
        if (ch == 'd' || ch == 'D') {
            st->settings.is_dark = !st->settings.is_dark;
            st->settings.has_theme_pref = true;
            dd_theme_init(&st->theme, st->settings.is_dark);
            dd_settings_save(&st->settings);
            return true;
        }
        if (ch == 'i' || ch == 'I') {
            st->show_inspector = !st->show_inspector;
            return true;
        }
    }

    if (ch == '?') {
        st->show_shortcuts_help = !st->show_shortcuts_help;
        return true;
    }

    /* Backspace or Left Arrow: navigate up to parent folder */
    if (key_code == 8 || key_code == 37) {
        if (st->tree && st->current_folder > 0) {
            int32_t p = st->tree->nodes[st->current_folder].parent_idx;
            if (p >= 0) {
                set_current_folder(st, p);
                return true;
            }
        }
    }

    /* Right Arrow: drill into selected folder */
    if (key_code == 39) {
        if (st->tree && st->selected_node >= 0 && st->selected_node < st->tree->node_count) {
            if (st->tree->nodes[st->selected_node].is_directory) {
                set_current_folder(st, st->selected_node);
                return true;
            }
        }
    }

    /* Up / Down Arrows: navigate sibling items in Explore view */
    if (st->tree && st->active_tab == TAB_EXPLORE && (key_code == 38 || key_code == 40)) {
        int32_t fidx = st->tree->nodes[st->current_folder].folder_idx;
        if (fidx >= 0) {
            const DDFolderExtra *fe = &st->tree->folders[fidx];
            if (fe->child_count > 0) {
                int cur_k = -1;
                for (int32_t i = 0; i < fe->child_count; i++) {
                    if (fe->children[i] == st->selected_node) {
                        cur_k = (int)i;
                        break;
                    }
                }
                if (key_code == 40) { /* Down */
                    int next_k = (cur_k + 1 < fe->child_count) ? cur_k + 1 : 0;
                    st->selected_node = fe->children[next_k];
                } else { /* Up */
                    int prev_k = (cur_k > 0) ? cur_k - 1 : fe->child_count - 1;
                    st->selected_node = fe->children[prev_k];
                }
                return true;
            }
        }
    }

    /* Explore view shortcuts */
    if (st->active_tab == TAB_EXPLORE) {
        if (ch >= '1' && ch <= '9') {
            st->viz_mode = (DDVizMode)(ch - '1');
            st->scroll_y_main = 0;
            return true;
        }
        if (ch == '+' || ch == '=') {
            if (st->depth_limit < 10) {
                st->depth_limit++;
                return true;
            }
        }
        if (ch == '-' || ch == '_') {
            if (st->depth_limit > 2) {
                st->depth_limit--;
                return true;
            }
        }
        if (ch == 'a' || ch == 'A') {
            st->abbreviate_labels = !st->abbreviate_labels;
            return true;
        }
    }

    /* Space: Quick Look preview */
    if (ch == ' ') {
        if (st->focused_hit_idx >= 0 && st->focused_hit_idx < dd_canvas_hit_count(cv)) {
            const DDHitRegion *h = dd_canvas_get_hit(cv, st->focused_hit_idx);
            return dd_views_handle_action(st, h);
        }
        if (st->tree && st->selected_node >= 0 && st->selected_node < st->tree->node_count) {
            st->quicklook_node = st->selected_node;
            st->show_quicklook = !st->show_quicklook;
            return true;
        }
    }

    if (key_code == 9) {
        int n = dd_canvas_hit_count(cv);
        if (n > 0) {
            st->focused_hit_idx = (st->focused_hit_idx + 1) % n;
            return true;
        }
    }
    if (key_code == -9) {
        int n = dd_canvas_hit_count(cv);
        if (n > 0) {
            if (st->focused_hit_idx <= 0) st->focused_hit_idx = n - 1;
            else st->focused_hit_idx = (st->focused_hit_idx - 1) % n;
            return true;
        }
    }

    if (key_code == 13) {
        if (st->focused_hit_idx >= 0 && st->focused_hit_idx < dd_canvas_hit_count(cv)) {
            const DDHitRegion *h = dd_canvas_get_hit(cv, st->focused_hit_idx);
            return dd_views_handle_action(st, h);
        }
        if (st->tree && st->selected_node >= 0 && st->selected_node < st->tree->node_count) {
            if (st->tree->nodes[st->selected_node].is_directory) {
                set_current_folder(st, st->selected_node);
                return true;
            }
        }
    }
    return false;
}

