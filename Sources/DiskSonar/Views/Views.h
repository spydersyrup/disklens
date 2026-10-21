#ifndef DISKSONAR_VIEWS_H
#define DISKSONAR_VIEWS_H

#include "../Theme/Theme.h"
#include "../Utils/Utils.h"
#include "../Utils/Canvas.h"
#include "../Models/Models.h"
#include "../Scanning/DiskScanner.h"

typedef enum {
    TAB_EXPLORE = 0,
    TAB_DUPLICATES = 1,
    TAB_APPLICATIONS = 2,
    TAB_MONITOR = 3,
    TAB_SNAPSHOTS = 4
} DDTab;

typedef enum {
    VIZ_TREEMAP = 0,
    VIZ_FOLDERS = 1,
    VIZ_SUNBURST = 2,
    VIZ_FLAME = 3,
    VIZ_BUBBLES = 4,
    VIZ_MINDMAP = 5,
    VIZ_TOPSIZES = 6,
    VIZ_AGEMAP = 7,
    VIZ_LIST = 8,
    VIZ_COUNT = 9
} DDVizMode;

typedef enum {
    COLOR_BY_TYPE = 0,
    COLOR_BY_FOLDER = 1,
    COLOR_BY_AGE = 2
} DDColorMode;

typedef enum {
    SCOPE_THIS_FOLDER = 0,
    SCOPE_FILES_ANYWHERE = 1,
    SCOPE_FOLDERS_ANYWHERE = 2
} DDTopScope;

typedef enum {
    ACT_NONE = 0,
    ACT_SWITCH_TAB,
    ACT_SCAN_ROOT,
    ACT_SCAN_HOME,
    ACT_SCAN_PICK,
    ACT_SCAN_RECENT,
    ACT_CANCEL_SCAN,
    ACT_BREADCRUMB,
    ACT_NAV_BACK,
    ACT_FOCUS_SEARCH,
    ACT_CLEAR_SEARCH,
    ACT_TOGGLE_QUEUE_POPOVER,
    ACT_TOGGLE_THEME,
    ACT_TOGGLE_INSPECTOR,
    ACT_REVEAL_CURRENT,
    ACT_COPY_CURRENT_PATH,
    ACT_QUICK_WIN_CLICK,
    ACT_QUICK_WIN_STAGE_ALL,
    ACT_VIZ_MODE,
    ACT_COLOR_MODE,
    ACT_DEPTH_DEC,
    ACT_DEPTH_INC,
    ACT_DEPTH_SET,
    ACT_TOGGLE_ABBREV,
    ACT_DISMISS_DENIED,
    ACT_OPEN_PERMS_HELP,
    ACT_TOP_SCOPE,
    ACT_STAGE_NODE,
    ACT_STAGE_TOP_UNTOUCHED,
    ACT_TOGGLE_LIST_EXPAND,
    ACT_SELECT_NODE,
    ACT_OPEN_NODE,
    ACT_INSPECTOR_REVEAL,
    ACT_INSPECTOR_QUICKLOOK,
    ACT_INSPECTOR_FOCUS,
    ACT_INSPECTOR_COPY,
    ACT_INSPECTOR_STAGE,
    ACT_QUEUE_CLEAR,
    ACT_QUEUE_REMOVE_ITEM,
    ACT_QUEUE_ASK_TRASH,
    ACT_CONFIRM_TRASH_YES,
    ACT_CONFIRM_TRASH_CANCEL,
    ACT_CLOSE_MODAL,
    ACT_DUP_SCAN,
    ACT_DUP_KEEP_ONE,
    ACT_APPS_REFRESH,
    ACT_APP_TOGGLE_EXPAND,
    ACT_APP_ASK_UNINSTALL,
    ACT_APP_UNINSTALL_TRASH,
    ACT_APP_UNINSTALL_QUEUE,
    ACT_MON_SORT_TOGGLE,
    ACT_MON_TOGGLE_VOLUMES,
    ACT_MON_TOGGLE_PROCS,
    ACT_SNAP_TAKE,
    ACT_SNAP_SET_BEFORE,
    ACT_SNAP_SET_AFTER,
    ACT_SNAP_DELETE,
    ACT_CTX_OPEN,
    ACT_CTX_QUICKLOOK,
    ACT_CTX_REVEAL,
    ACT_CTX_COPY,
    ACT_CTX_STAGE,
    ACT_TOGGLE_SIDEBAR_DRAWER,
    ACT_DUP_KEEP_ALL_NEWEST,
    ACT_LIST_SORT,
    ACT_RESCAN_CURRENT,
    ACT_APP_STAGE_LEFTOVER,
    ACT_DUP_CANCEL,
    ACT_TOGGLE_SHORTCUTS_HELP
} DDActionCode;

typedef struct {
    DDSettings settings;
    DDTheme theme;
    DDDiskScanner *scanner;
    DDScanProgress scan_progress;
    DDFileTree *tree;

    DDTab active_tab;
    DDVizMode viz_mode;
    DDColorMode color_mode;
    int depth_limit;
    bool abbreviate_labels;
    bool show_inspector;
    bool show_sidebar_drawer;
    bool denied_banner_dismissed;

    int32_t current_folder;
    int32_t selected_node;
    int32_t hovered_node;

    char search_query[128];
    bool search_focused;

    double copy_confirm_until;
    double inspector_copy_until;

    DDVolumeInfo primary_vol;
    DDQuickWins quick_wins;
    DDTopSizesCache top_sizes_cache;
    DDAgeReport age_report;
    DDTopScope top_scope;

    int32_t expanded_folders[512];
    int expanded_count;
    int list_sort_col;
    bool list_sort_asc;

    DDCleanupQueue cleanup_queue;
    bool show_queue_popover;
    bool show_trash_confirm;
    char trash_error_msg[512];
    bool show_trash_error;
    bool show_shortcuts_help;

    bool show_quicklook;
    int32_t quicklook_node;

    bool show_ctx_menu;
    double ctx_x;
    double ctx_y;
    int32_t ctx_node_idx;
    int ctx_qw_group;

    DDDuplicateStore duplicates;
    bool prev_dup_scanning;
    DDAppStore app_store;
    bool prev_apps_loading;
    int uninstall_app_idx;
    bool show_uninstall_modal;

    DDSystemMonitor monitor;
    bool mon_sort_by_mem;
    bool mon_show_all_vols;
    bool mon_show_all_procs;

    DDSnapshotStore snapshots;

    int scroll_y_main;
    int scroll_y_sidebar;
    int scroll_y_inspector;
    int scroll_y_popover;
    int max_scroll_main;
    int max_scroll_sidebar;
    int max_scroll_inspector;
    int max_scroll_popover;
    int last_win_w;
    int last_win_h;

    int focused_hit_idx;
    double mouse_x;
    double mouse_y;
} DDAppState;

void dd_app_state_init(DDAppState *st);
void dd_app_state_destroy(DDAppState *st);

void dd_app_start_scan(DDAppState *st, const char *path);
bool dd_app_tick(DDAppState *st);

void dd_views_render(DDAppState *st, DDCanvas *cv);
bool dd_views_handle_action(DDAppState *st, const DDHitRegion *hit);
bool dd_views_handle_click(DDAppState *st, DDCanvas *cv, double mx, double my, int button, bool is_double);
bool dd_views_handle_motion(DDAppState *st, DDCanvas *cv, double mx, double my);
bool dd_views_handle_scroll(DDAppState *st, double mx, double my, int delta_y);
bool dd_views_handle_key(DDAppState *st, DDCanvas *cv, int key_code, uint32_t ch, bool ctrl);

#endif
