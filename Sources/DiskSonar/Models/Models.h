#ifndef DISKSONAR_MODELS_H
#define DISKSONAR_MODELS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    CAT_VIDEO = 0,
    CAT_AUDIO = 1,
    CAT_IMAGE = 2,
    CAT_DOCUMENT = 3,
    CAT_DEVELOPER = 4,
    CAT_ARCHIVE = 5,
    CAT_APPS = 6,
    CAT_SYSTEM = 7,
    CAT_OTHER = 8,
    CAT_COUNT = 9
} DDFileCategory;

const char *dd_category_name(DDFileCategory cat);
DDFileCategory dd_classify_filename(const char *name, size_t len, bool inside_app_bundle);

typedef struct {
    uint32_t name_offset;
    int32_t parent_idx;
    int32_t folder_idx;
    int64_t logical_size;
    int64_t size_on_disk;
    double modified_stamp;
    double created_stamp;
    uint8_t category;
    uint8_t is_directory;
    uint8_t is_deleted;
    uint8_t inside_app;
} DDFileNode;

typedef struct {
    int32_t *children;
    int32_t child_count;
    int32_t child_cap;
    int32_t file_count;
    int32_t folder_count;
    int64_t type_sizes[CAT_COUNT];
} DDFolderExtra;

typedef struct {
    char root_path[512];
    DDFileNode *nodes;
    int32_t node_count;
    int32_t node_cap;
    DDFolderExtra *folders;
    int32_t folder_count;
    int32_t folder_cap;
    char *string_pool;
    uint32_t string_used;
    uint32_t string_cap;

    double scan_duration;
    int32_t denied_count;
    char denied_samples[8][512];
    int32_t denied_sample_count;
    uint32_t generation;
} DDFileTree;

DDFileTree *dd_tree_create(const char *root_path);
void dd_tree_free(DDFileTree *tree);
void dd_tree_free_async(DDFileTree *tree);

uint32_t dd_tree_intern_string(DDFileTree *tree, const char *str, size_t len);
int32_t dd_tree_add_node(DDFileTree *tree, int32_t parent_idx, const char *name, size_t name_len, bool is_dir, DDFileCategory cat, bool inside_app);
void dd_folder_add_child(DDFileTree *tree, int32_t folder_node_idx, int32_t child_node_idx);
void dd_tree_sort_folder_children(DDFileTree *tree, int32_t folder_node_idx);

const char *dd_node_name(const DDFileTree *tree, int32_t node_idx);
void dd_node_build_path(const DDFileTree *tree, int32_t node_idx, char *buf, size_t cap);
double dd_node_share_of_parent(const DDFileTree *tree, int32_t node_idx);
int64_t dd_node_compression_savings(const DDFileTree *tree, int32_t node_idx);
DDFileCategory dd_node_dominant_category(const DDFileTree *tree, int32_t node_idx);
void dd_node_top_categories(const DDFileTree *tree, int32_t node_idx, int max_n, DDFileCategory *out_cats, int *out_count);
bool dd_node_is_descendant_of(const DDFileTree *tree, int32_t node_idx, int32_t ancestor_idx);

typedef struct {
    char title[64];
    int icon_kind;
    int32_t items[400];
    int32_t item_count;
    int64_t total_size;
    int32_t biggest_node_idx;
} DDQuickWinGroup;

typedef struct {
    DDQuickWinGroup groups[7];
    int group_count;
    int64_t grand_total;
} DDQuickWins;

void dd_quick_wins_compute(const DDFileTree *tree, DDQuickWins *out_qw);
void dd_quick_wins_compute_for(const DDFileTree *tree, int32_t root_idx, DDQuickWins *out_qw);

typedef struct {
    int32_t node_idx;
    int64_t size;
} DDRankedItem;

typedef struct {
    uint32_t generation;
    int32_t folder_idx;
    DDRankedItem top_files[200];
    int top_files_count;
    DDRankedItem top_folders[200];
    int top_folders_count;
    bool valid;
} DDTopSizesCache;

void dd_top_sizes_compute(const DDFileTree *tree, int32_t folder_idx, DDTopSizesCache *cache);

typedef struct {
    uint32_t generation;
    int32_t folder_idx;
    int64_t bucket_bytes[6];
    int32_t bucket_counts[6];
    int years[6];
    int year_count;
    int64_t heatmap[6][12];
    int busiest_year;
    int busiest_month;
    int64_t busiest_bytes;
    DDRankedItem big_untouched[150];
    int big_untouched_count;
    bool valid;
} DDAgeReport;

void dd_age_report_compute(const DDFileTree *tree, int32_t folder_idx, DDAgeReport *report);

typedef struct {
    int id;
    char path[512];
    char name[128];
    char reason[128];
    int64_t size;
    int32_t node_idx;
} DDCleanupItem;

typedef struct {
    DDCleanupItem items[2048];
    int count;
    int next_id;
    int64_t total_size;
    uint32_t version;
} DDCleanupQueue;

void dd_queue_init(DDCleanupQueue *q);
bool dd_queue_contains_path(const DDCleanupQueue *q, const char *path);
void dd_queue_stage(DDCleanupQueue *q, const char *path, const char *name, int64_t size, const char *reason, int32_t node_idx);
void dd_queue_unstage_path(DDCleanupQueue *q, const char *path);
void dd_queue_remove_at(DDCleanupQueue *q, int index);
void dd_queue_clear(DDCleanupQueue *q);

typedef struct {
    int trashed_count;
    int failed_count;
    char failed_paths[8][512];
    int64_t freed_bytes;
} DDTrashResult;

void dd_trash_move_paths(char paths[][512], int count, DDTrashResult *out_res);
void dd_tree_prune_paths(DDFileTree *tree, char paths[][512], int path_count, int32_t *io_current_folder, int32_t *io_selected_node);

typedef struct {
    int64_t file_size;
    int64_t wasted_bytes;
    char hash_hex[65];
    int32_t node_indices[32];
    char paths[32][512];
    int count;
} DDDuplicateGroup;

typedef struct {
    DDDuplicateGroup *groups;
    int group_count;
    int group_cap;
    int64_t total_reclaimable;
    volatile int scanning;
    volatile int cancel_requested;
    volatile int progress_done;
    volatile int progress_total;
    volatile int pass_num;
    bool has_run;
    uint32_t tree_generation;
    const DDFileTree *active_tree;
    int32_t root_folder_idx;
} DDDuplicateStore;

void dd_duplicates_scan(const DDFileTree *tree, DDDuplicateStore *store);
void dd_duplicates_start(const DDFileTree *tree, int32_t folder_idx, DDDuplicateStore *store);
void dd_duplicates_cancel(DDDuplicateStore *store);
void dd_duplicates_prune(DDDuplicateStore *store, char trashed_paths[][512], int trashed_count);
void dd_duplicates_free(DDDuplicateStore *store);

typedef struct {
    char category[48];
    char path[512];
    int64_t size;
} DDAppLeftover;

typedef struct {
    char name[128];
    char bundle_id[128];
    char version[48];
    char bundle_path[512];
    double last_used_stamp;
    int64_t bundle_size;
    int64_t leftovers_size;
    int64_t total_size;
    DDAppLeftover leftovers[16];
    int leftover_count;
    bool expanded;
} DDInstalledApp;

typedef struct {
    DDInstalledApp apps[256];
    int count;
    int64_t grand_total;
    volatile int loaded;
    volatile int loading;
    volatile int cancel_requested;
} DDAppStore;

void dd_apps_load(DDAppStore *store);
void dd_apps_start_load(DDAppStore *store);
void dd_apps_cancel(DDAppStore *store);
void dd_apps_prune(DDAppStore *store, char trashed_paths[][512], int trashed_count);
bool dd_app_terminate_and_trash(const DDInstalledApp *app, DDTrashResult *out_res);

typedef struct {
    char id[64];
    char root_path[512];
    double timestamp;
    int64_t total_bytes;
    int32_t folder_count;
} DDSnapshotSummary;

typedef struct {
    char path[512];
    int64_t before_size;
    int64_t after_size;
    int64_t delta;
} DDSnapshotDiffRow;

typedef struct {
    DDSnapshotSummary summaries[128];
    int count;
    int before_idx;
    int after_idx;
    bool saving;
    bool diff_valid;
    bool roots_differ;
    DDSnapshotDiffRow diff_rows[200];
    int diff_count;
} DDSnapshotStore;

void dd_snapshots_load_index(DDSnapshotStore *store);
bool dd_snapshots_take(DDSnapshotStore *store, const DDFileTree *tree);
void dd_snapshots_delete(DDSnapshotStore *store, int index);
void dd_snapshots_compute_diff(DDSnapshotStore *store);

#endif
