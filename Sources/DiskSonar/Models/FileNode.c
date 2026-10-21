#define _GNU_SOURCE
#include "Models.h"
#include "../Utils/Utils.h"
#include "../Scanning/DiskScanner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <direct.h>
#include <process.h>
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#endif

static const char *CAT_NAMES[CAT_COUNT] = {
    "Video", "Audio", "Image", "Document", "Developer", "Archive", "Apps", "System", "Other"
};

const char *dd_category_name(DDFileCategory cat) {
    if (cat < 0 || cat >= CAT_COUNT) return "Other";
    return CAT_NAMES[cat];
}

DDFileTree *dd_tree_create(const char *root_path) {
    DDFileTree *tree = (DDFileTree *)calloc(1, sizeof(DDFileTree));
    if (!tree) return NULL;
    snprintf(tree->root_path, sizeof(tree->root_path), "%s", root_path ? root_path : "/");
    size_t rlen = strlen(tree->root_path);
    while (rlen > 1 && (tree->root_path[rlen - 1] == '/' || tree->root_path[rlen - 1] == '\\')) {
        if (rlen == 3 && tree->root_path[1] == ':') break;
        tree->root_path[--rlen] = '\0';
    }

    tree->node_cap = 4096;
    tree->nodes = (DDFileNode *)calloc((size_t)tree->node_cap, sizeof(DDFileNode));
    tree->folder_cap = 1024;
    tree->folders = (DDFolderExtra *)calloc((size_t)tree->folder_cap, sizeof(DDFolderExtra));
    tree->string_cap = 65536;
    tree->string_pool = (char *)malloc(tree->string_cap);
    if (tree->string_pool) {
        tree->string_pool[0] = '\0';
        tree->string_used = 1;
    }
    tree->generation = 1;

    const char *base = strrchr(tree->root_path, '/');
#ifdef _WIN32
    const char *bwin = strrchr(tree->root_path, '\\');
    if (bwin && (!base || bwin > base)) base = bwin;
#endif
    const char *root_name = (base && base[1]) ? (base + 1) : tree->root_path;
    dd_tree_add_node(tree, -1, root_name, strlen(root_name), true, CAT_OTHER, false);
    return tree;
}

void dd_tree_free(DDFileTree *tree) {
    if (!tree) return;
    if (tree->folders) {
        for (int32_t i = 0; i < tree->folder_count; i++) {
            free(tree->folders[i].children);
        }
        free(tree->folders);
    }
    free(tree->nodes);
    free(tree->string_pool);
    free(tree);
}

#ifdef _WIN32
static unsigned __stdcall free_tree_thread(void *arg) {
    dd_tree_free((DDFileTree *)arg);
    return 0;
}
#else
static void *free_tree_thread(void *arg) {
    dd_tree_free((DDFileTree *)arg);
    return NULL;
}
#endif

void dd_tree_free_async(DDFileTree *tree) {
    if (!tree) return;
#ifdef _WIN32
    uintptr_t h = _beginthreadex(NULL, 0, free_tree_thread, tree, 0, NULL);
    if (h) CloseHandle((HANDLE)h);
    else dd_tree_free(tree);
#else
    pthread_t th;
    if (pthread_create(&th, NULL, free_tree_thread, tree) == 0) {
        pthread_detach(th);
    } else {
        dd_tree_free(tree);
    }
#endif
}

uint32_t dd_tree_intern_string(DDFileTree *tree, const char *str, size_t len) {
    if (!str || len == 0) return 0;
    uint32_t need = (uint32_t)len + 1;
    if (tree->string_used + need > tree->string_cap) {
        uint32_t new_cap = tree->string_cap * 2;
        while (new_cap < tree->string_used + need) new_cap *= 2;
        char *grown = (char *)realloc(tree->string_pool, new_cap);
        if (!grown) return 0;
        tree->string_pool = grown;
        tree->string_cap = new_cap;
    }
    uint32_t off = tree->string_used;
    memcpy(tree->string_pool + off, str, len);
    tree->string_pool[off + len] = '\0';
    tree->string_used += need;
    return off;
}

int32_t dd_tree_add_node(DDFileTree *tree, int32_t parent_idx, const char *name, size_t name_len, bool is_dir, DDFileCategory cat, bool inside_app) {
    if (tree->node_count >= tree->node_cap) {
        int32_t new_cap = tree->node_cap == 0 ? 512 : tree->node_cap * 2;
        DDFileNode *grown = (DDFileNode *)realloc(tree->nodes, (size_t)new_cap * sizeof(DDFileNode));
        if (!grown) return -1;
        tree->nodes = grown;
        tree->node_cap = new_cap;
    }
    if (is_dir && tree->folder_count >= tree->folder_cap) {
        int32_t new_fcap = tree->folder_cap == 0 ? 128 : tree->folder_cap * 2;
        DDFolderExtra *fgrown = (DDFolderExtra *)realloc(tree->folders, (size_t)new_fcap * sizeof(DDFolderExtra));
        if (!fgrown) return -1;
        tree->folders = fgrown;
        tree->folder_cap = new_fcap;
    }
    int32_t idx = tree->node_count++;
    DDFileNode *n = &tree->nodes[idx];
    memset(n, 0, sizeof(*n));
    n->name_offset = dd_tree_intern_string(tree, name, name_len);
    n->parent_idx = parent_idx;
    n->category = (uint8_t)cat;
    n->is_directory = is_dir ? 1 : 0;
    n->inside_app = inside_app ? 1 : 0;
    if (is_dir) {
        int32_t fidx = tree->folder_count++;
        memset(&tree->folders[fidx], 0, sizeof(DDFolderExtra));
        n->folder_idx = fidx;
    } else {
        n->folder_idx = -1;
    }
    return idx;
}

void dd_folder_add_child(DDFileTree *tree, int32_t folder_node_idx, int32_t child_node_idx) {
    if (folder_node_idx < 0 || folder_node_idx >= tree->node_count) return;
    int32_t fidx = tree->nodes[folder_node_idx].folder_idx;
    if (fidx < 0 || fidx >= tree->folder_count) return;
    DDFolderExtra *fe = &tree->folders[fidx];
    if (fe->child_count >= fe->child_cap) {
        int32_t new_cap = fe->child_cap == 0 ? 8 : fe->child_cap * 2;
        int32_t *grown = (int32_t *)realloc(fe->children, (size_t)new_cap * sizeof(int32_t));
        if (!grown) return;
        fe->children = grown;
        fe->child_cap = new_cap;
    }
    fe->children[fe->child_count++] = child_node_idx;
}

static void sort_children_range(const DDFileTree *tree, int32_t *arr, int left, int right) {
    while (left < right) {
        int i = left, j = right;
        int64_t pivot = tree->nodes[arr[left + (right - left) / 2]].size_on_disk;
        while (i <= j) {
            while (tree->nodes[arr[i]].size_on_disk > pivot) i++;
            while (tree->nodes[arr[j]].size_on_disk < pivot) j--;
            if (i <= j) {
                int32_t tmp = arr[i];
                arr[i] = arr[j];
                arr[j] = tmp;
                i++;
                j--;
            }
        }
        if (j - left < right - i) {
            if (left < j) sort_children_range(tree, arr, left, j);
            left = i;
        } else {
            if (i < right) sort_children_range(tree, arr, i, right);
            right = j;
        }
    }
}

void dd_tree_sort_folder_children(DDFileTree *tree, int32_t folder_node_idx) {
    if (!tree || folder_node_idx < 0 || folder_node_idx >= tree->node_count) return;
    int32_t fidx = tree->nodes[folder_node_idx].folder_idx;
    if (fidx < 0) return;
    DDFolderExtra *fe = &tree->folders[fidx];
    if (fe->child_count > 1) {
        sort_children_range(tree, fe->children, 0, fe->child_count - 1);
    }
}

const char *dd_node_name(const DDFileTree *tree, int32_t node_idx) {
    if (!tree || node_idx < 0 || node_idx >= tree->node_count) return "";
    return tree->string_pool + tree->nodes[node_idx].name_offset;
}

void dd_node_build_path(const DDFileTree *tree, int32_t node_idx, char *buf, size_t cap) {
    if (!tree || node_idx < 0 || node_idx >= tree->node_count || cap == 0) {
        if (cap > 0) buf[0] = '\0';
        return;
    }
    if (node_idx == 0) {
        snprintf(buf, cap, "%s", tree->root_path);
        return;
    }
    int32_t chain[128];
    int depth = 0;
    int32_t cur = node_idx;
    while (cur > 0 && depth < 128) {
        chain[depth++] = cur;
        cur = tree->nodes[cur].parent_idx;
    }
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    size_t pos = snprintf(buf, cap, "%s", tree->root_path);
    if (pos > 0 && pos < cap && (buf[pos - 1] == '/' || buf[pos - 1] == '\\')) {
        pos--;
    }
    for (int i = depth - 1; i >= 0 && pos + 2 < cap; i--) {
        const char *nm = dd_node_name(tree, chain[i]);
        pos += (size_t)snprintf(buf + pos, cap - pos, "%c%s", sep, nm);
    }
}

double dd_node_share_of_parent(const DDFileTree *tree, int32_t node_idx) {
    if (!tree || node_idx <= 0 || node_idx >= tree->node_count) return 1.0;
    int32_t pidx = tree->nodes[node_idx].parent_idx;
    if (pidx < 0 || pidx >= tree->node_count) return 1.0;
    int64_t psize = tree->nodes[pidx].size_on_disk;
    if (psize <= 0) return 0.0;
    double r = (double)tree->nodes[node_idx].size_on_disk / (double)psize;
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 1.0;
    return r;
}

int64_t dd_node_compression_savings(const DDFileTree *tree, int32_t node_idx) {
    if (!tree || node_idx < 0 || node_idx >= tree->node_count) return 0;
    int64_t diff = tree->nodes[node_idx].logical_size - tree->nodes[node_idx].size_on_disk;
    return diff > 0 ? diff : 0;
}

DDFileCategory dd_node_dominant_category(const DDFileTree *tree, int32_t node_idx) {
    if (!tree || node_idx < 0 || node_idx >= tree->node_count) return CAT_OTHER;
    const DDFileNode *n = &tree->nodes[node_idx];
    if (!n->is_directory || n->folder_idx < 0) return (DDFileCategory)n->category;
    const DDFolderExtra *fe = &tree->folders[n->folder_idx];
    DDFileCategory best = CAT_OTHER;
    int64_t best_sz = -1;
    for (int c = 0; c < CAT_COUNT; c++) {
        if (fe->type_sizes[c] > best_sz) {
            best_sz = fe->type_sizes[c];
            best = (DDFileCategory)c;
        }
    }
    return best_sz > 0 ? best : CAT_OTHER;
}

void dd_node_top_categories(const DDFileTree *tree, int32_t node_idx, int max_n, DDFileCategory *out_cats, int *out_count) {
    *out_count = 0;
    if (!tree || node_idx < 0 || node_idx >= tree->node_count || max_n <= 0) return;
    const DDFileNode *n = &tree->nodes[node_idx];
    if (!n->is_directory || n->folder_idx < 0) {
        out_cats[0] = (DDFileCategory)n->category;
        *out_count = 1;
        return;
    }
    const DDFolderExtra *fe = &tree->folders[n->folder_idx];
    bool used[CAT_COUNT] = {false};
    for (int k = 0; k < max_n && k < CAT_COUNT; k++) {
        int best = -1;
        int64_t best_sz = 0;
        for (int c = 0; c < CAT_COUNT; c++) {
            if (!used[c] && fe->type_sizes[c] > best_sz) {
                best_sz = fe->type_sizes[c];
                best = c;
            }
        }
        if (best < 0) break;
        used[best] = true;
        out_cats[(*out_count)++] = (DDFileCategory)best;
    }
}

bool dd_node_is_descendant_of(const DDFileTree *tree, int32_t node_idx, int32_t ancestor_idx) {
    if (!tree || node_idx < 0 || ancestor_idx < 0) return false;
    int32_t cur = node_idx;
    while (cur >= 0) {
        if (cur == ancestor_idx) return true;
        cur = tree->nodes[cur].parent_idx;
    }
    return false;
}

static bool str_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static void add_qw_match(DDQuickWinGroup *g, const DDFileTree *tree, int32_t node_idx) {
    int64_t sz = tree->nodes[node_idx].size_on_disk;
    if (sz <= 0) return;
    g->total_size += sz;
    if (g->biggest_node_idx < 0 || sz > tree->nodes[g->biggest_node_idx].size_on_disk) {
        g->biggest_node_idx = node_idx;
    }
    if (g->item_count < 400) {
        g->items[g->item_count++] = node_idx;
    }
}

static void walk_quick_wins(const DDFileTree *tree, int32_t idx, uint32_t active_mask, DDQuickWins *qw) {
    if (idx < 0 || idx >= tree->node_count) return;
    const DDFileNode *n = &tree->nodes[idx];
    if (n->is_deleted || n->size_on_disk <= 0) return;
    const char *name = dd_node_name(tree, idx);
    const char *pname = (n->parent_idx >= 0) ? dd_node_name(tree, n->parent_idx) : "";

    uint32_t matched_here = 0;
    if (n->is_directory) {
        if (!(active_mask & (1u << 0)) && str_eq_ci(name, "Downloads")) {
            add_qw_match(&qw->groups[0], tree, idx);
            matched_here |= (1u << 0);
        }
        if (!(active_mask & (1u << 1)) &&
            (str_eq_ci(name, ".cache") || str_eq_ci(name, "Caches") || str_eq_ci(name, "Logs") ||
             (str_eq_ci(name, "log") && str_eq_ci(pname, "var")) ||
             (str_eq_ci(name, "Temp") && str_eq_ci(pname, "Local")))) {
            add_qw_match(&qw->groups[1], tree, idx);
            matched_here |= (1u << 1);
        }
        if (!(active_mask & (1u << 2)) &&
            (str_eq_ci(name, "CoreSimulator") || str_eq_ci(name, "VirtualBox VMs") ||
             str_eq_ci(name, "containers") || (str_eq_ci(name, "docker") && str_eq_ci(pname, "lib")))) {
            add_qw_match(&qw->groups[2], tree, idx);
            matched_here |= (1u << 2);
        }
        if (!(active_mask & (1u << 4)) && str_eq_ci(name, "node_modules")) {
            add_qw_match(&qw->groups[4], tree, idx);
            matched_here |= (1u << 4);
        }
        if (!(active_mask & (1u << 5)) &&
            (strcmp(name, "build") == 0 || strcmp(name, ".build") == 0 || strcmp(name, "dist") == 0 ||
             strcmp(name, ".next") == 0 || strcmp(name, "target") == 0 || strcmp(name, ".turbo") == 0 ||
             strcmp(name, ".parcel-cache") == 0 || strcmp(name, "__pycache__") == 0)) {
            add_qw_match(&qw->groups[5], tree, idx);
            matched_here |= (1u << 5);
        }
        if (!(active_mask & (1u << 6)) &&
            (str_eq_ci(name, "DerivedData") || str_eq_ci(name, ".gradle") || str_eq_ci(name, ".cargo"))) {
            add_qw_match(&qw->groups[6], tree, idx);
            matched_here |= (1u << 6);
        }

        uint32_t next_mask = active_mask | matched_here;
        if (n->folder_idx >= 0) {
            const DDFolderExtra *fe = &tree->folders[n->folder_idx];
            for (int32_t i = 0; i < fe->child_count; i++) {
                walk_quick_wins(tree, fe->children[i], next_mask, qw);
            }
        }
    } else {
        if (!(active_mask & (1u << 3)) &&
            (n->category == CAT_VIDEO || n->category == CAT_AUDIO || n->category == CAT_IMAGE) &&
            n->size_on_disk >= 10 * 1000 * 1000) {
            add_qw_match(&qw->groups[3], tree, idx);
        }
    }
}

void dd_quick_wins_compute_for(const DDFileTree *tree, int32_t root_idx, DDQuickWins *out_qw) {
    memset(out_qw, 0, sizeof(*out_qw));
    if (!tree || tree->node_count == 0 || root_idx < 0 || root_idx >= tree->node_count) return;

    const char *titles[7] = {
        "Downloads", "Caches & logs", "Containers & VMs", "Large media",
        "node_modules", "Build artifacts", "SDK & DerivedData"
    };
    for (int i = 0; i < 7; i++) {
        snprintf(out_qw->groups[i].title, sizeof(out_qw->groups[i].title), "%s", titles[i]);
        out_qw->groups[i].icon_kind = i;
        out_qw->groups[i].biggest_node_idx = -1;
    }

    walk_quick_wins(tree, root_idx, 0, out_qw);

    int write_idx = 0;
    for (int i = 0; i < 7; i++) {
        if (out_qw->groups[i].item_count > 0 && out_qw->groups[i].total_size > 0) {
            out_qw->grand_total += out_qw->groups[i].total_size;
            if (write_idx != i) {
                out_qw->groups[write_idx] = out_qw->groups[i];
            }
            write_idx++;
        }
    }
    out_qw->group_count = write_idx;

    for (int i = 0; i < out_qw->group_count - 1; i++) {
        for (int j = i + 1; j < out_qw->group_count; j++) {
            if (out_qw->groups[j].total_size > out_qw->groups[i].total_size) {
                DDQuickWinGroup tmp = out_qw->groups[i];
                out_qw->groups[i] = out_qw->groups[j];
                out_qw->groups[j] = tmp;
            }
        }
    }
}

void dd_quick_wins_compute(const DDFileTree *tree, DDQuickWins *out_qw) {
    dd_quick_wins_compute_for(tree, 0, out_qw);
}

static void insert_ranked(DDRankedItem *arr, int *count, int max_count, int32_t node_idx, int64_t size) {
    if (size <= 0) return;
    if (*count == max_count && size <= arr[*count - 1].size) return;
    int pos = *count;
    if (*count < max_count) (*count)++;
    else pos = max_count - 1;
    while (pos > 0 && arr[pos - 1].size < size) {
        arr[pos] = arr[pos - 1];
        pos--;
    }
    arr[pos].node_idx = node_idx;
    arr[pos].size = size;
}

static void walk_top_sizes(const DDFileTree *tree, int32_t root_idx, int32_t cur_idx, DDTopSizesCache *cache) {
    if (cur_idx < 0 || cur_idx >= tree->node_count) return;
    const DDFileNode *n = &tree->nodes[cur_idx];
    if (n->is_deleted || n->size_on_disk <= 0) return;
    if (n->is_directory) {
        if (cur_idx != root_idx) {
            insert_ranked(cache->top_folders, &cache->top_folders_count, 200, cur_idx, n->size_on_disk);
        }
        if (n->folder_idx >= 0) {
            const DDFolderExtra *fe = &tree->folders[n->folder_idx];
            for (int32_t i = 0; i < fe->child_count; i++) {
                walk_top_sizes(tree, root_idx, fe->children[i], cache);
            }
        }
    } else {
        insert_ranked(cache->top_files, &cache->top_files_count, 200, cur_idx, n->size_on_disk);
    }
}

void dd_top_sizes_compute(const DDFileTree *tree, int32_t folder_idx, DDTopSizesCache *cache) {
    if (!tree || folder_idx < 0 || folder_idx >= tree->node_count) return;
    if (cache->valid && cache->generation == tree->generation && cache->folder_idx == folder_idx) return;
    memset(cache, 0, sizeof(*cache));
    cache->generation = tree->generation;
    cache->folder_idx = folder_idx;
    walk_top_sizes(tree, folder_idx, folder_idx, cache);
    cache->valid = true;
}

static void walk_age_report(const DDFileTree *tree, int32_t cur_idx, double now, int base_year, DDAgeReport *rep) {
    if (cur_idx < 0 || cur_idx >= tree->node_count) return;
    const DDFileNode *n = &tree->nodes[cur_idx];
    if (n->is_deleted) return;
    if (n->is_directory) {
        if (n->folder_idx >= 0) {
            const DDFolderExtra *fe = &tree->folders[n->folder_idx];
            for (int32_t i = 0; i < fe->child_count; i++) {
                walk_age_report(tree, fe->children[i], now, base_year, rep);
            }
        }
        return;
    }
    int64_t sz = n->size_on_disk;
    if (sz <= 0) return;
    double age_days = (n->modified_stamp > 0.0 && now > n->modified_stamp) ? ((now - n->modified_stamp) / 86400.0) : 0.0;
    int bucket = 0;
    if (age_days <= 7.0) bucket = 0;
    else if (age_days <= 30.0) bucket = 1;
    else if (age_days <= 90.0) bucket = 2;
    else if (age_days <= 365.0) bucket = 3;
    else if (age_days <= 730.0) bucket = 4;
    else bucket = 5;

    rep->bucket_bytes[bucket] += sz;
    rep->bucket_counts[bucket]++;

    if (n->modified_stamp > 0.0) {
        time_t t = (time_t)n->modified_stamp;
        struct tm tmv;
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        int yr = tmv.tm_year + 1900;
        int mo = tmv.tm_mon;
        int yidx = base_year - yr;
        if (yidx >= 0 && yidx < 6 && mo >= 0 && mo < 12) {
            rep->heatmap[yidx][mo] += sz;
        }
    }

    if (sz >= 40 * 1000 * 1000 && age_days >= 365.0) {
        insert_ranked(rep->big_untouched, &rep->big_untouched_count, 150, cur_idx, sz);
    }
}

void dd_age_report_compute(const DDFileTree *tree, int32_t folder_idx, DDAgeReport *rep) {
    if (!tree || folder_idx < 0 || folder_idx >= tree->node_count) return;
    if (rep->valid && rep->generation == tree->generation && rep->folder_idx == folder_idx) return;
    memset(rep, 0, sizeof(*rep));
    rep->generation = tree->generation;
    rep->folder_idx = folder_idx;

    double now = dd_now_seconds();
    time_t t = (time_t)now;
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    int cur_year = tmv.tm_year + 1900;
    rep->year_count = 6;
    for (int i = 0; i < 6; i++) rep->years[i] = cur_year - i;

    walk_age_report(tree, folder_idx, now, cur_year, rep);

    for (int y = 0; y < 6; y++) {
        for (int m = 0; m < 12; m++) {
            if (rep->heatmap[y][m] > rep->busiest_bytes) {
                rep->busiest_bytes = rep->heatmap[y][m];
                rep->busiest_year = rep->years[y];
                rep->busiest_month = m;
            }
        }
    }
    rep->valid = true;
}

void dd_queue_init(DDCleanupQueue *q) {
    memset(q, 0, sizeof(*q));
    q->next_id = 1;
    q->version = 1;
}

bool dd_queue_contains_path(const DDCleanupQueue *q, const char *path) {
    if (!q || !path) return false;
    for (int i = 0; i < q->count; i++) {
        if (strcmp(q->items[i].path, path) == 0) return true;
    }
    return false;
}

void dd_queue_stage(DDCleanupQueue *q, const char *path, const char *name, int64_t size, const char *reason, int32_t node_idx) {
    if (!q || !path || !*path || q->count >= 2048) return;
    if (dd_queue_contains_path(q, path)) return;
    DDCleanupItem *it = &q->items[q->count++];
    it->id = q->next_id++;
    snprintf(it->path, sizeof(it->path), "%s", path);
    if (name && *name) {
        snprintf(it->name, sizeof(it->name), "%s", name);
    } else {
        const char *slash = strrchr(path, '/');
#ifdef _WIN32
        const char *bslash = strrchr(path, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
        snprintf(it->name, sizeof(it->name), "%s", (slash && slash[1]) ? slash + 1 : path);
    }
    snprintf(it->reason, sizeof(it->reason), "%s", (reason && *reason) ? reason : "Staged for cleanup");
    it->size = size > 0 ? size : 0;
    it->node_idx = node_idx;
    q->total_size += it->size;
    q->version++;
}

void dd_queue_unstage_path(DDCleanupQueue *q, const char *path) {
    if (!q || !path) return;
    for (int i = 0; i < q->count; i++) {
        if (strcmp(q->items[i].path, path) == 0) {
            dd_queue_remove_at(q, i);
            return;
        }
    }
}

void dd_queue_remove_at(DDCleanupQueue *q, int index) {
    if (!q || index < 0 || index >= q->count) return;
    q->total_size -= q->items[index].size;
    if (q->total_size < 0) q->total_size = 0;
    for (int i = index; i < q->count - 1; i++) {
        q->items[i] = q->items[i + 1];
    }
    q->count--;
    q->version++;
}

void dd_queue_clear(DDCleanupQueue *q) {
    if (!q) return;
    q->count = 0;
    q->total_size = 0;
    q->version++;
}

static int cmp_path_len(const void *a, const void *b) {
    size_t la = strlen((const char *)a);
    size_t lb = strlen((const char *)b);
    return (la > lb) - (la < lb);
}

static bool is_subpath_of(const char *child, const char *parent) {
    size_t plen = strlen(parent);
    if (plen == 0 || strncmp(child, parent, plen) != 0) return false;
    if (parent[plen - 1] == '/' || parent[plen - 1] == '\\') return true;
    return child[plen] == '\0' || child[plen] == '/' || child[plen] == '\\';
}

#ifndef _WIN32
static void trash_percent_encode(const char *src, char *dst, size_t dst_cap) {
    if (!src || !dst || dst_cap == 0) return;
    size_t d = 0;
    for (const unsigned char *s = (const unsigned char *)src; *s && d + 1 < dst_cap; s++) {
        if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
            (*s >= '0' && *s <= '9') || *s == '-' || *s == '_' || *s == '.' || *s == '/' || *s == '~') {
            dst[d++] = (char)*s;
        } else {
            if (d + 4 >= dst_cap) break;
            snprintf(dst + d, dst_cap - d, "%%%02X", *s);
            d += 3;
        }
    }
    dst[d] = '\0';
}

static void safe_cleanup_trash_dest(const char *dest_file, const char *trash_files_dir, const char *src_path) {
    if (!dest_file || !*dest_file || !trash_files_dir || !*trash_files_dir) {
        return;
    }
    if (src_path && strcmp(dest_file, src_path) == 0) {
        return;
    }
    size_t tlen = strlen(trash_files_dir);
    if (strncmp(dest_file, trash_files_dir, tlen) != 0 || (dest_file[tlen] != '/' && dest_file[tlen] != '\0')) {
        return;
    }
    struct stat dst_st;
    if (lstat(dest_file, &dst_st) != 0) {
        return;
    }
    if (S_ISLNK(dst_st.st_mode)) {
        unlink(dest_file);
    } else if (S_ISDIR(dst_st.st_mode)) {
        dd_exec_sync("rm", "-rf", dest_file, NULL);
    } else {
        unlink(dest_file);
    }
}
#endif

static bool move_single_to_trash(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return true;
    char dbl_null[520];
    memset(dbl_null, 0, sizeof(dbl_null));
    snprintf(dbl_null, sizeof(dbl_null) - 2, "%s", path);
    SHFILEOPSTRUCTA op;
    memset(&op, 0, sizeof(op));
    op.wFunc = FO_DELETE;
    op.pFrom = dbl_null;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    return SHFileOperationA(&op) == 0;
#else
    struct stat st;
    if (lstat(path, &st) != 0) {
        return errno == ENOENT;
    }
    char files_dir[512], info_dir[512];
    dd_get_data_dir("Trash/files", files_dir, sizeof(files_dir));
    dd_get_data_dir("Trash/info", info_dir, sizeof(info_dir));

    const char *base = strrchr(path, '/');
    base = (base && base[1]) ? (base + 1) : "trashed_item";

    char dest_file[640], dest_info[640];
    int attempt = 0;
    int info_fd = -1;

    /* Find an unused slot atomically by creating the .trashinfo file with O_CREAT | O_EXCL */
    while (attempt < 1000) {
        if (attempt == 0) {
            snprintf(dest_file, sizeof(dest_file), "%s/%s", files_dir, base);
            snprintf(dest_info, sizeof(dest_info), "%s/%s.trashinfo", info_dir, base);
        } else {
            snprintf(dest_file, sizeof(dest_file), "%s/%s.%d", files_dir, base, attempt);
            snprintf(dest_info, sizeof(dest_info), "%s/%s.%d.trashinfo", info_dir, base, attempt);
        }

        /* Check destination file does not exist before attempting info reservation */
        struct stat dst;
        if (lstat(dest_file, &dst) == 0) {
            attempt++;
            continue;
        }

        info_fd = open(dest_info, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (info_fd >= 0) {
            break;
        }
        if (errno == EEXIST) {
            attempt++;
            continue;
        }
        /* Unrecoverable directory or permission failure */
        return false;
    }

    if (info_fd < 0) {
        /* Exhausted retries without overwrite - preserve original file safely */
        return false;
    }

    /* Write compliant FreeDesktop Trash Info metadata */
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char tstr[64];
    strftime(tstr, sizeof(tstr), "%Y-%m-%dT%H:%M:%S", &tmv);

    char enc_path[1024];
    trash_percent_encode(path, enc_path, sizeof(enc_path));

    char info_buf[1536];
    int info_len = snprintf(info_buf, sizeof(info_buf), "[Trash Info]\nPath=%s\nDeletionDate=%s\n", enc_path, tstr);
    if (info_len <= 0 || write(info_fd, info_buf, (size_t)info_len) != (ssize_t)info_len) {
        close(info_fd);
        unlink(dest_info);
        return false;
    }
    close(info_fd);

    /* Atomic move without replace where supported */
    bool moved = false;
#if defined(__linux__) && defined(SYS_renameat2) && defined(RENAME_NOREPLACE)
    if (syscall(SYS_renameat2, AT_FDCWD, path, AT_FDCWD, dest_file, RENAME_NOREPLACE) == 0) {
        moved = true;
    }
#endif
    if (!moved) {
        struct stat check_dst;
        if (lstat(dest_file, &check_dst) != 0) {
            if (rename(path, dest_file) == 0) {
                moved = true;
            }
        }
    }

    if (!moved) {
        int ren_err = errno;
        if (ren_err == EXDEV) {
            /* Cross-filesystem move: attempt native gio trash first */
            if (dd_exec_sync("gio", "trash", path, NULL)) {
                unlink(dest_info);
                return true;
            }
            /* Safe copy without overwrite fallback */
            if (dd_exec_sync("cp", "-an", path, dest_file)) {
                struct stat verify_dst;
                if (lstat(dest_file, &verify_dst) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        dd_exec_sync("rm", "-rf", path, NULL);
                    } else {
                        unlink(path);
                    }
                    /* Verify source was removed */
                    struct stat check_src;
                    if (lstat(path, &check_src) != 0) {
                        moved = true;
                    } else {
                        /* Source could not be removed; revert destination to prevent duplication */
                        safe_cleanup_trash_dest(dest_file, files_dir, path);
                    }
                } else {
                    safe_cleanup_trash_dest(dest_file, files_dir, path);
                }
            } else {
                /* Copy failed mid-way; cleanly remove partial destination */
                safe_cleanup_trash_dest(dest_file, files_dir, path);
            }
        }
    }

    if (!moved) {
        /* Clean up reservation on failure, preserving original file completely */
        unlink(dest_info);
        return false;
    }
    return true;
#endif
}

void dd_trash_move_paths(char paths[][512], int count, DDTrashResult *out_res) {
    memset(out_res, 0, sizeof(*out_res));
    if (count <= 0) return;

    qsort(paths, (size_t)count, 512, cmp_path_len);

    /* Heap allocate rather than 1 MiB fixed stack array to prevent stack overflows */
    typedef char PathEntry[512];
    PathEntry *trashed = (PathEntry *)calloc(2048, sizeof(PathEntry));
    if (!trashed) {
        out_res->failed_count = count;
        return;
    }
    int trashed_n = 0;

    for (int i = 0; i < count; i++) {
        bool already_inside = false;
        for (int j = 0; j < trashed_n; j++) {
            if (is_subpath_of(paths[i], trashed[j])) {
                already_inside = true;
                break;
            }
        }
        if (already_inside) {
            out_res->trashed_count++;
            continue;
        }
        if (move_single_to_trash(paths[i])) {
            if (trashed_n < 2048) {
                snprintf(trashed[trashed_n++], 512, "%s", paths[i]);
            }
            out_res->trashed_count++;
        } else {
            if (out_res->failed_count < 8) {
                snprintf(out_res->failed_paths[out_res->failed_count], 512, "%s", paths[i]);
            }
            out_res->failed_count++;
        }
    }
    free(trashed);
}

static void mark_subtree_deleted(DDFileTree *tree, int32_t idx) {
    if (idx < 0 || idx >= tree->node_count) return;
    DDFileNode *n = &tree->nodes[idx];
    n->is_deleted = 1;
    if (n->is_directory && n->folder_idx >= 0) {
        DDFolderExtra *fe = &tree->folders[n->folder_idx];
        for (int32_t i = 0; i < fe->child_count; i++) {
            mark_subtree_deleted(tree, fe->children[i]);
        }
    }
}

static void collect_prune_targets(const DDFileTree *tree, int32_t idx, const char *cur_path,
                                  char paths[][512], int path_count,
                                  int32_t *out_nodes, int *io_count, int max_nodes) {
    if (idx < 0 || idx >= tree->node_count || *io_count >= max_nodes) return;
    const DDFileNode *n = &tree->nodes[idx];
    if (n->is_deleted) return;

    if (idx > 0) {
        for (int p = 0; p < path_count; p++) {
            if (strcmp(cur_path, paths[p]) == 0) {
                out_nodes[(*io_count)++] = idx;
                return;
            }
        }
    }

    if (!n->is_directory || n->folder_idx < 0) return;

    bool any_inside = false;
    for (int p = 0; p < path_count; p++) {
        if (is_subpath_of(paths[p], cur_path)) {
            any_inside = true;
            break;
        }
    }
    if (!any_inside) return;

    const DDFolderExtra *fe = &tree->folders[n->folder_idx];
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    size_t clen = strlen(cur_path);
    bool trailing_sep = (clen > 0 && (cur_path[clen - 1] == '/' || cur_path[clen - 1] == '\\'));

    for (int32_t i = 0; i < fe->child_count; i++) {
        int32_t cidx = fe->children[i];
        char child_path[512];
        if (trailing_sep) {
            snprintf(child_path, sizeof(child_path), "%s%s", cur_path, dd_node_name(tree, cidx));
        } else {
            snprintf(child_path, sizeof(child_path), "%s%c%s", cur_path, sep, dd_node_name(tree, cidx));
        }
        collect_prune_targets(tree, cidx, child_path, paths, path_count, out_nodes, io_count, max_nodes);
    }
}

void dd_tree_prune_paths(DDFileTree *tree, char paths[][512], int path_count, int32_t *io_current_folder, int32_t *io_selected_node) {
    if (!tree || path_count <= 0 || tree->node_count <= 0) return;

    int32_t targets[2048];
    int target_count = 0;
    collect_prune_targets(tree, 0, tree->root_path, paths, path_count, targets, &target_count, 2048);

    for (int t = 0; t < target_count; t++) {
        int32_t idx = targets[t];
        DDFileNode *n = &tree->nodes[idx];
        if (n->is_deleted) continue;

        int64_t sub_logical = n->logical_size;
        int64_t sub_disk    = n->size_on_disk;
        int32_t sub_files   = 1;
        int32_t sub_folders = 0;
        int64_t sub_types[CAT_COUNT] = {0};

        if (n->is_directory && n->folder_idx >= 0) {
            DDFolderExtra *fe = &tree->folders[n->folder_idx];
            sub_files   = fe->file_count;
            sub_folders = fe->folder_count + 1;
            for (int c = 0; c < CAT_COUNT; c++) sub_types[c] = fe->type_sizes[c];
        } else {
            sub_types[n->category] = sub_disk;
        }

        mark_subtree_deleted(tree, idx);

        int32_t p = n->parent_idx;
        int32_t child_to_unlink = idx;
        while (p >= 0 && p < tree->node_count) {
            DDFileNode *pn = &tree->nodes[p];
            pn->logical_size -= sub_logical;
            if (pn->logical_size < 0) pn->logical_size = 0;
            pn->size_on_disk -= sub_disk;
            if (pn->size_on_disk < 0) pn->size_on_disk = 0;

            if (pn->folder_idx >= 0) {
                DDFolderExtra *pfe = &tree->folders[pn->folder_idx];
                pfe->file_count -= sub_files;
                if (pfe->file_count < 0) pfe->file_count = 0;
                pfe->folder_count -= sub_folders;
                if (pfe->folder_count < 0) pfe->folder_count = 0;
                for (int c = 0; c < CAT_COUNT; c++) {
                    pfe->type_sizes[c] -= sub_types[c];
                    if (pfe->type_sizes[c] < 0) pfe->type_sizes[c] = 0;
                }
                if (child_to_unlink >= 0) {
                    int32_t w = 0;
                    for (int32_t i = 0; i < pfe->child_count; i++) {
                        if (pfe->children[i] != child_to_unlink) {
                            pfe->children[w++] = pfe->children[i];
                        }
                    }
                    pfe->child_count = w;
                    child_to_unlink = -1;
                }
                dd_tree_sort_folder_children(tree, p);
            }
            p = pn->parent_idx;
        }
    }

    if (io_current_folder && *io_current_folder >= 0 && *io_current_folder < tree->node_count) {
        while (*io_current_folder > 0 && tree->nodes[*io_current_folder].is_deleted) {
            *io_current_folder = tree->nodes[*io_current_folder].parent_idx;
        }
        if (*io_current_folder < 0) *io_current_folder = 0;
    }
    if (io_selected_node && *io_selected_node >= 0 && *io_selected_node < tree->node_count) {
        if (tree->nodes[*io_selected_node].is_deleted) {
            *io_selected_node = io_current_folder ? *io_current_folder : 0;
        }
    }
    tree->generation++;
}

typedef struct {
    int32_t node_idx;
    int64_t size;
    uint8_t hash64[32];
    uint8_t hash_full[32];
    bool ok64;
    bool ok_full;
} DupCandidate;

static int cmp_cand_size(const void *a, const void *b) {
    const DupCandidate *ca = (const DupCandidate *)a;
    const DupCandidate *cb = (const DupCandidate *)b;
    return (cb->size > ca->size) - (cb->size < ca->size);
}

static int cmp_cand_hash64(const void *a, const void *b) {
    const DupCandidate *ca = (const DupCandidate *)a;
    const DupCandidate *cb = (const DupCandidate *)b;
    if (ca->size != cb->size) return (cb->size > ca->size) - (cb->size < ca->size);
    return memcmp(ca->hash64, cb->hash64, 32);
}

static int cmp_cand_hash_full(const void *a, const void *b) {
    const DupCandidate *ca = (const DupCandidate *)a;
    const DupCandidate *cb = (const DupCandidate *)b;
    if (ca->size != cb->size) return (cb->size > ca->size) - (cb->size < ca->size);
    return memcmp(ca->hash_full, cb->hash_full, 32);
}

static int cmp_dup_group(const void *a, const void *b) {
    const DDDuplicateGroup *ga = (const DDDuplicateGroup *)a;
    const DDDuplicateGroup *gb = (const DDDuplicateGroup *)b;
    return (gb->wasted_bytes > ga->wasted_bytes) - (gb->wasted_bytes < ga->wasted_bytes);
}

void dd_duplicates_cancel(DDDuplicateStore *store) {
    if (!store) return;
    store->cancel_requested = 1;
#ifdef _WIN32
    if (store->thread_handle) {
        WaitForSingleObject((HANDLE)store->thread_handle, INFINITE);
        CloseHandle((HANDLE)store->thread_handle);
        store->thread_handle = NULL;
    }
#else
    if (store->has_thread) {
        pthread_join((pthread_t)store->thread_handle, NULL);
        store->has_thread = false;
        store->thread_handle = NULL;
    }
#endif
    while (store->scanning) {
#ifdef _WIN32
        Sleep(5);
#else
        usleep(5000);
#endif
    }
    store->cancel_requested = 0;
}

void dd_duplicates_free(DDDuplicateStore *store) {
    if (!store) return;
    dd_duplicates_cancel(store);
    free(store->groups);
    memset(store, 0, sizeof(*store));
}

static void collect_dup_candidates(const DDFileTree *tree, int32_t cur_idx, int64_t min_size,
                                   DupCandidate **io_cands, size_t *io_count, size_t *io_cap,
                                   volatile int *cancel_flag) {
    if (!tree || cur_idx < 0 || cur_idx >= tree->node_count || (cancel_flag && *cancel_flag)) return;
    const DDFileNode *n = &tree->nodes[cur_idx];
    if (n->is_deleted) return;
    if (n->is_directory) {
        if (n->folder_idx >= 0) {
            const DDFolderExtra *fe = &tree->folders[n->folder_idx];
            for (int32_t i = 0; i < fe->child_count; i++) {
                collect_dup_candidates(tree, fe->children[i], min_size, io_cands, io_count, io_cap, cancel_flag);
            }
        }
        return;
    }
    if (n->logical_size < min_size || n->size_on_disk <= 0) return;
    if (*io_count >= *io_cap) {
        size_t new_cap = (*io_cap) * 2;
        DupCandidate *grown = (DupCandidate *)realloc(*io_cands, new_cap * sizeof(DupCandidate));
        if (!grown) return;
        *io_cands = grown;
        *io_cap = new_cap;
    }
    DupCandidate *c = &(*io_cands)[(*io_count)++];
    memset(c, 0, sizeof(*c));
    c->node_idx = cur_idx;
    c->size = n->logical_size;
}

static void run_duplicates_worker(DDDuplicateStore *store) {
    const DDFileTree *tree = store->active_tree;
    if (!tree || tree->node_count <= 1) {
        store->scanning = 0;
        store->has_run = true;
        return;
    }

    store->pass_num = 1;
    store->progress_done = 0;
    store->progress_total = 0;

    size_t cap = 1024, count = 0;
    DupCandidate *cands = (DupCandidate *)malloc(cap * sizeof(DupCandidate));
    if (!cands) {
        store->scanning = 0;
        store->has_run = true;
        return;
    }

    int32_t root_idx = (store->root_folder_idx >= 0 && store->root_folder_idx < tree->node_count)
                       ? store->root_folder_idx : 0;
    int64_t min_size = (tree->node_count > 5000) ? 65536 : 1;
    collect_dup_candidates(tree, root_idx, min_size, &cands, &count, &cap, &store->cancel_requested);

    if (store->cancel_requested) {
        free(cands);
        store->scanning = 0;
        return;
    }

    qsort(cands, count, sizeof(DupCandidate), cmp_cand_size);

    int total_pass2 = 0;
    size_t idx = 0;
    while (idx < count) {
        size_t end = idx + 1;
        while (end < count && cands[end].size == cands[idx].size) end++;
        if (end - idx >= 2) total_pass2 += (int)(end - idx);
        idx = end;
    }

    store->pass_num = 2;
    store->progress_done = 0;
    store->progress_total = total_pass2;

    size_t w = 0;
    idx = 0;
    while (idx < count && !store->cancel_requested) {
        size_t end = idx + 1;
        while (end < count && cands[end].size == cands[idx].size) end++;
        if (end - idx >= 2) {
            for (size_t k = idx; k < end && !store->cancel_requested; k++) {
                char path[512];
                dd_node_build_path(tree, cands[k].node_idx, path, sizeof(path));
                cands[k].ok64 = dd_sha256_file(path, 65536, false, cands[k].hash64);
                if (cands[k].ok64) {
                    cands[w++] = cands[k];
                }
                store->progress_done++;
            }
        }
        idx = end;
    }

    if (store->cancel_requested) {
        free(cands);
        store->scanning = 0;
        return;
    }

    count = w;
    qsort(cands, count, sizeof(DupCandidate), cmp_cand_hash64);

    int total_pass3 = 0;
    idx = 0;
    while (idx < count) {
        size_t end = idx + 1;
        while (end < count && cands[end].size == cands[idx].size &&
               memcmp(cands[end].hash64, cands[idx].hash64, 32) == 0) {
            end++;
        }
        if (end - idx >= 2) total_pass3 += (int)(end - idx);
        idx = end;
    }

    store->pass_num = 3;
    store->progress_done = 0;
    store->progress_total = total_pass3;

    w = 0;
    idx = 0;
    while (idx < count && !store->cancel_requested) {
        size_t end = idx + 1;
        while (end < count && cands[end].size == cands[idx].size &&
               memcmp(cands[end].hash64, cands[idx].hash64, 32) == 0) {
            end++;
        }
        if (end - idx >= 2) {
            for (size_t k = idx; k < end && !store->cancel_requested; k++) {
                if (cands[k].size <= 65536) {
                    memcpy(cands[k].hash_full, cands[k].hash64, 32);
                    cands[k].ok_full = true;
                } else {
                    char path[512];
                    dd_node_build_path(tree, cands[k].node_idx, path, sizeof(path));
                    cands[k].ok_full = dd_sha256_file(path, 0, true, cands[k].hash_full);
                }
                if (cands[k].ok_full) {
                    cands[w++] = cands[k];
                }
                store->progress_done++;
            }
        }
        idx = end;
    }

    if (store->cancel_requested) {
        free(cands);
        store->scanning = 0;
        return;
    }

    count = w;
    qsort(cands, count, sizeof(DupCandidate), cmp_cand_hash_full);

    store->group_cap = 64;
    store->groups = (DDDuplicateGroup *)calloc((size_t)store->group_cap, sizeof(DDDuplicateGroup));

    idx = 0;
    while (idx < count && store->groups) {
        size_t end = idx + 1;
        while (end < count && cands[end].size == cands[idx].size &&
               memcmp(cands[end].hash_full, cands[idx].hash_full, 32) == 0) {
            end++;
        }
        if (end - idx >= 2) {
            if (store->group_count >= store->group_cap) {
                int new_cap = store->group_cap * 2;
                DDDuplicateGroup *grown = (DDDuplicateGroup *)realloc(store->groups, (size_t)new_cap * sizeof(DDDuplicateGroup));
                if (!grown) break;
                store->groups = grown;
                store->group_cap = new_cap;
            }
            DDDuplicateGroup *g = &store->groups[store->group_count++];
            memset(g, 0, sizeof(*g));
            g->file_size = cands[idx].size;
            dd_sha256_hex(cands[idx].hash_full, g->hash_hex);
            for (size_t k = idx; k < end && g->count < 32; k++) {
                g->node_indices[g->count] = cands[k].node_idx;
                dd_node_build_path(tree, cands[k].node_idx, g->paths[g->count], sizeof(g->paths[0]));
                g->count++;
            }
            g->wasted_bytes = g->file_size * (int64_t)(g->count - 1);
            store->total_reclaimable += g->wasted_bytes;
        }
        idx = end;
    }

    if (store->group_count > 1) {
        qsort(store->groups, (size_t)store->group_count, sizeof(DDDuplicateGroup), cmp_dup_group);
    }

    free(cands);
    store->has_run = true;
    store->scanning = 0;
}

#ifdef _WIN32
static unsigned __stdcall dup_scan_thread(void *arg) {
    run_duplicates_worker((DDDuplicateStore *)arg);
    return 0;
}
#else
static void *dup_scan_thread(void *arg) {
    run_duplicates_worker((DDDuplicateStore *)arg);
    return NULL;
}
#endif

void dd_duplicates_start(const DDFileTree *tree, int32_t folder_idx, DDDuplicateStore *store) {
    if (!store) return;
    dd_duplicates_free(store);
    if (!tree || tree->node_count <= 1) {
        store->has_run = true;
        return;
    }
    store->active_tree = tree;
    store->root_folder_idx = folder_idx;
    store->tree_generation = tree->generation;
    store->scanning = 1;

#ifdef _WIN32
    uintptr_t h = _beginthreadex(NULL, 0, dup_scan_thread, store, 0, NULL);
    if (h) {
        store->thread_handle = (void *)h;
        store->has_thread = true;
    } else {
        run_duplicates_worker(store);
    }
#else
    pthread_t th;
    if (pthread_create(&th, NULL, dup_scan_thread, store) == 0) {
        store->thread_handle = (void *)th;
        store->has_thread = true;
    } else {
        run_duplicates_worker(store);
    }
#endif
}

void dd_duplicates_scan(const DDFileTree *tree, DDDuplicateStore *store) {
    if (!store) return;
    dd_duplicates_free(store);
    if (!tree || tree->node_count <= 1) {
        store->has_run = true;
        return;
    }
    store->active_tree = tree;
    store->root_folder_idx = 0;
    store->tree_generation = tree->generation;
    store->scanning = 1;
    run_duplicates_worker(store);
}

void dd_duplicates_prune(DDDuplicateStore *store, char trashed_paths[][512], int trashed_count) {
    if (!store || !store->groups || trashed_count <= 0) return;
    store->total_reclaimable = 0;
    int gw = 0;
    for (int i = 0; i < store->group_count; i++) {
        DDDuplicateGroup *g = &store->groups[i];
        int fw = 0;
        for (int j = 0; j < g->count; j++) {
            bool gone = false;
            for (int t = 0; t < trashed_count; t++) {
                if (is_subpath_of(g->paths[j], trashed_paths[t])) {
                    gone = true;
                    break;
                }
            }
            if (!gone) {
                g->node_indices[fw] = g->node_indices[j];
                if (fw != j) snprintf(g->paths[fw], 512, "%s", g->paths[j]);
                fw++;
            }
        }
        g->count = fw;
        if (g->count >= 2) {
            g->wasted_bytes = g->file_size * (int64_t)(g->count - 1);
            store->total_reclaimable += g->wasted_bytes;
            if (gw != i) store->groups[gw] = *g;
            gw++;
        }
    }
    store->group_count = gw;
}

typedef struct {
    char category[48];
    char name[128];
    char compact_lower[128];
    char full_path[512];
    int64_t cached_size;
    bool size_computed;
    bool claimed;
} LibEntry;

static void make_compact_lower(const char *src, char *dst, size_t cap) {
    size_t w = 0;
    for (const char *p = src; *p && w + 1 < cap; p++) {
        if (*p != ' ' && *p != '-' && *p != '_') {
            dst[w++] = (char)tolower((unsigned char)*p);
        }
    }
    dst[w] = '\0';
}

static int cmp_app_total(const void *a, const void *b) {
    const DDInstalledApp *aa = (const DDInstalledApp *)a;
    const DDInstalledApp *ab = (const DDInstalledApp *)b;
    return (ab->total_size > aa->total_size) - (ab->total_size < aa->total_size);
}

void dd_apps_load(DDAppStore *store) {
    if (!store) return;
    memset(store, 0, sizeof(*store));
    store->loading = true;

    char home[512];
    dd_get_home_dir(home, sizeof(home));

    LibEntry *lib_entries = (LibEntry *)calloc(2048, sizeof(LibEntry));
    int lib_count = 0;

#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    const char *localapp = getenv("LOCALAPPDATA");
    struct { const char *base; const char *sub; const char *cat; } win_dirs[] = {
        { appdata, "", "Application Support" },
        { localapp, "", "Local Storage" },
        { localapp, "\\Temp", "Caches" },
    };
    for (int d = 0; d < 3; d++) {
        if (!win_dirs[d].base) continue;
        char pattern[512];
        snprintf(pattern, sizeof(pattern), "%s%s\\*", win_dirs[d].base, win_dirs[d].sub);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
                if (lib_count < 2048) {
                    LibEntry *le = &lib_entries[lib_count++];
                    snprintf(le->category, sizeof(le->category), "%s", win_dirs[d].cat);
                    snprintf(le->name, sizeof(le->name), "%s", fd.cFileName);
                    make_compact_lower(fd.cFileName, le->compact_lower, sizeof(le->compact_lower));
                    snprintf(le->full_path, sizeof(le->full_path), "%s%s\\%s", win_dirs[d].base, win_dirs[d].sub, fd.cFileName);
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }

    const char *prog_dirs[] = { "C:\\Program Files", "C:\\Program Files (x86)" };
    for (int pd = 0; pd < 2; pd++) {
        char pattern[512];
        snprintf(pattern, sizeof(pattern), "%s\\*", prog_dirs[pd]);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
                if (store->count >= 256) break;
                DDInstalledApp *app = &store->apps[store->count++];
                memset(app, 0, sizeof(*app));
                snprintf(app->name, sizeof(app->name), "%s", fd.cFileName);
                snprintf(app->bundle_id, sizeof(app->bundle_id), "win.%s", fd.cFileName);
                snprintf(app->version, sizeof(app->version), "1.0");
                snprintf(app->bundle_path, sizeof(app->bundle_path), "%s\\%s", prog_dirs[pd], fd.cFileName);
                app->bundle_size = dd_scanner_allocated_size(app->bundle_path);
                app->total_size = app->bundle_size;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
#else
    struct { const char *rel; const char *cat; } lib_dirs[] = {
        { ".cache", "Caches" },
        { ".config", "Preferences" },
        { ".local/share", "Application Support" },
        { ".local/state", "Saved Application State" },
        { ".var/app", "Containers" },
        { "Library/Caches", "Caches" },
        { "Library/Preferences", "Preferences" },
        { "Library/Application Support", "Application Support" },
        { "Library/Logs", "Logs" },
        { "Library/HTTPStorages", "HTTP Storage" },
        { "Library/Saved Application State", "Saved Application State" },
        { "Library/Containers", "Containers" }
    };

    for (size_t d = 0; d < sizeof(lib_dirs) / sizeof(lib_dirs[0]); d++) {
        char dirpath[600];
        snprintf(dirpath, sizeof(dirpath), "%s/%s", home, lib_dirs[d].rel);
        DIR *dir = opendir(dirpath);
        if (!dir) continue;
        struct dirent *de;
        while ((de = readdir(dir)) != NULL && lib_count < 2048) {
            if (de->d_name[0] == '.') continue;
            if (strcmp(de->d_name, "applications") == 0 || strcmp(de->d_name, "icons") == 0 ||
                strcmp(de->d_name, "Trash") == 0 || strcmp(de->d_name, "mime") == 0) {
                continue;
            }
            LibEntry *le = &lib_entries[lib_count++];
            snprintf(le->category, sizeof(le->category), "%s", lib_dirs[d].cat);
            snprintf(le->name, sizeof(le->name), "%s", de->d_name);
            make_compact_lower(de->d_name, le->compact_lower, sizeof(le->compact_lower));
            snprintf(le->full_path, sizeof(le->full_path), "%s/%s", dirpath, de->d_name);
        }
        closedir(dir);
    }

    char user_apps[600];
    snprintf(user_apps, sizeof(user_apps), "%s/.local/share/applications", home);
    const char *desk_dirs[] = { "/Applications", user_apps, "/usr/share/applications" };

    for (int dd = 0; dd < 3 && store->count < 120; dd++) {
        DIR *dir = opendir(desk_dirs[dd]);
        if (!dir) continue;
        struct dirent *de;
        while ((de = readdir(dir)) != NULL && store->count < 120) {
            if (de->d_name[0] == '.') continue;
            size_t nlen = strlen(de->d_name);
            bool is_dot_app = (nlen > 4 && strcmp(de->d_name + nlen - 4, ".app") == 0);
            bool is_desktop = (nlen > 8 && strcmp(de->d_name + nlen - 8, ".desktop") == 0);
            if (!is_dot_app && !is_desktop) continue;

            char full[600];
            snprintf(full, sizeof(full), "%s/%s", desk_dirs[dd], de->d_name);
            struct stat st;
            if (stat(full, &st) != 0) continue;

            DDInstalledApp app;
            memset(&app, 0, sizeof(app));
            app.last_used_stamp = (double)st.st_atime;

            if (is_dot_app) {
                snprintf(app.name, sizeof(app.name), "%.*s", (int)(nlen - 4), de->d_name);
                snprintf(app.bundle_id, sizeof(app.bundle_id), "app.%s", app.name);
                snprintf(app.version, sizeof(app.version), "1.0");
                snprintf(app.bundle_path, sizeof(app.bundle_path), "%s", full);
                app.bundle_size = dd_scanner_allocated_size(full);
            } else {
                FILE *f = fopen(full, "r");
                if (!f) continue;
                char line[512];
                char exec_cmd[256] = "";
                bool no_display = false;
                while (fgets(line, sizeof(line), f)) {
                    size_t l = strlen(line);
                    while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
                    if (strncmp(line, "Name=", 5) == 0 && !app.name[0]) {
                        snprintf(app.name, sizeof(app.name), "%s", line + 5);
                    } else if (strncmp(line, "Exec=", 5) == 0 && !exec_cmd[0]) {
                        snprintf(exec_cmd, sizeof(exec_cmd), "%s", line + 5);
                    } else if (strncmp(line, "Version=", 8) == 0) {
                        snprintf(app.version, sizeof(app.version), "%s", line + 8);
                    } else if (strcmp(line, "NoDisplay=true") == 0) {
                        no_display = true;
                    }
                }
                fclose(f);
                if (no_display || !app.name[0]) continue;

                snprintf(app.bundle_id, sizeof(app.bundle_id), "%.*s", (int)(nlen - 8), de->d_name);
                if (!app.version[0]) snprintf(app.version, sizeof(app.version), "1.0");

                char bin_tok[256] = "";
                const char *p = exec_cmd;
                while (*p == ' ' || *p == '"') p++;
                size_t bi = 0;
                while (*p && *p != ' ' && *p != '"' && bi + 1 < sizeof(bin_tok)) {
                    bin_tok[bi++] = *p++;
                }
                bin_tok[bi] = '\0';

                char resolved_bin[512] = "";
                if (bin_tok[0] == '/') {
                    snprintf(resolved_bin, sizeof(resolved_bin), "%s", bin_tok);
                } else if (bin_tok[0]) {
                    const char *search_paths[] = { "/usr/bin", "/usr/local/bin", "/opt" };
                    for (int sp = 0; sp < 3; sp++) {
                        char cand[512];
                        snprintf(cand, sizeof(cand), "%s/%s", search_paths[sp], bin_tok);
                        struct stat bst;
                        if (stat(cand, &bst) == 0) {
                            snprintf(resolved_bin, sizeof(resolved_bin), "%s", cand);
                            break;
                        }
                    }
                }
                if (resolved_bin[0]) {
                    snprintf(app.bundle_path, sizeof(app.bundle_path), "%s", resolved_bin);
                    app.bundle_size = dd_scanner_allocated_size(resolved_bin);
                    struct stat bst;
                    if (stat(resolved_bin, &bst) == 0 && (double)bst.st_mtime > app.last_used_stamp) {
                        app.last_used_stamp = (double)bst.st_mtime;
                    }
                } else {
                    snprintf(app.bundle_path, sizeof(app.bundle_path), "%s", full);
                    app.bundle_size = (int64_t)st.st_blocks * 512;
                }
            }

            bool dup = false;
            for (int k = 0; k < store->count; k++) {
                if (str_eq_ci(store->apps[k].name, app.name)) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            store->apps[store->count++] = app;
        }
        closedir(dir);
    }
#endif

    for (int i = 0; i < store->count && !store->cancel_requested; i++) {
        DDInstalledApp *app = &store->apps[i];
        char key_name[128], key_id[128];
        make_compact_lower(app->name, key_name, sizeof(key_name));
        make_compact_lower(app->bundle_id, key_id, sizeof(key_id));
        size_t kn_len = strlen(key_name);
        size_t ki_len = strlen(key_id);

        for (int l = 0; l < lib_count && app->leftover_count < 16 && !store->cancel_requested; l++) {
            LibEntry *le = &lib_entries[l];
            if (le->claimed) continue;
            size_t lc_len = strlen(le->compact_lower);
            if (lc_len < 3) continue;
            bool match = false;
            if (kn_len >= 3 && strcmp(le->compact_lower, key_name) == 0) match = true;
            if (!match && ki_len >= 4 && strcmp(le->compact_lower, key_id) == 0) match = true;
            if (!match && kn_len >= 4) {
                size_t lc = strlen(le->compact_lower);
                if (lc > kn_len && strncmp(le->compact_lower, key_name, kn_len) == 0) {
                    char next = le->compact_lower[kn_len];
                    if (next == '-' || next == '_' || next == '.') match = true;
                }
            }
            if (match) {
                if (!le->size_computed) {
                    le->cached_size = dd_scanner_allocated_size(le->full_path);
                    le->size_computed = true;
                }
                int64_t lsz = le->cached_size;
                if (lsz > 0) {
                    le->claimed = true;
                    DDAppLeftover *lo = &app->leftovers[app->leftover_count++];
                    snprintf(lo->category, sizeof(lo->category), "%s", le->category);
                    snprintf(lo->path, sizeof(lo->path), "%s", le->full_path);
                    lo->size = lsz;
                    app->leftovers_size += lsz;
                }
            }
        }
        app->total_size = app->bundle_size + app->leftovers_size;
        store->grand_total += app->total_size;
    }

    if (store->count > 1 && !store->cancel_requested) {
        qsort(store->apps, (size_t)store->count, sizeof(DDInstalledApp), cmp_app_total);
    }

    free(lib_entries);
    store->loading = 0;
    store->loaded = 1;
}

#ifdef _WIN32
static unsigned __stdcall apps_worker_thread_fn(void *arg) {
    DDAppStore *store = (DDAppStore *)arg;
    DDAppStore *tmp = (DDAppStore *)calloc(1, sizeof(DDAppStore));
    if (tmp && !store->cancel_requested) {
        dd_apps_load(tmp);
        if (!store->cancel_requested) {
            store->pending_result = tmp;
            store->loaded = 1;
            store->loading = 0;
            return 0;
        }
    }
    free(tmp);
    store->loading = 0;
    return 0;
}
#else
static void *apps_worker_thread_fn(void *arg) {
    DDAppStore *store = (DDAppStore *)arg;
    DDAppStore *tmp = (DDAppStore *)calloc(1, sizeof(DDAppStore));
    if (tmp && !store->cancel_requested) {
        dd_apps_load(tmp);
        if (!store->cancel_requested) {
            store->pending_result = tmp;
            store->loaded = 1;
            store->loading = 0;
            return NULL;
        }
    }
    free(tmp);
    store->loading = 0;
    return NULL;
}
#endif

void dd_apps_cancel(DDAppStore *store) {
    if (!store) return;
    store->cancel_requested = 1;
#ifdef _WIN32
    if (store->thread_handle) {
        WaitForSingleObject((HANDLE)store->thread_handle, INFINITE);
        CloseHandle((HANDLE)store->thread_handle);
        store->thread_handle = NULL;
    }
#else
    if (store->has_thread) {
        pthread_join((pthread_t)store->thread_handle, NULL);
        store->has_thread = false;
        store->thread_handle = NULL;
    }
#endif
    while (store->loading) {
#ifdef _WIN32
        Sleep(5);
#else
        usleep(5000);
#endif
    }
    if (store->pending_result) {
        free(store->pending_result);
        store->pending_result = NULL;
    }
    store->cancel_requested = 0;
}

void dd_apps_start_load(DDAppStore *store) {
    if (!store || store->loading) return;
    store->cancel_requested = 0;
    store->loading = 1;
#ifdef _WIN32
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, apps_worker_thread_fn, store, 0, NULL);
    if (h) {
        store->thread_handle = (void *)h;
        store->has_thread = true;
    } else {
        dd_apps_load(store);
    }
#else
    pthread_t th;
    if (pthread_create(&th, NULL, apps_worker_thread_fn, store) == 0) {
        store->thread_handle = (void *)th;
        store->has_thread = true;
    } else {
        dd_apps_load(store);
    }
#endif
}

void dd_apps_prune(DDAppStore *store, char trashed_paths[][512], int trashed_count) {
    if (!store || !store->loaded || trashed_count <= 0) return;
    store->grand_total = 0;
    int aw = 0;
    for (int i = 0; i < store->count; i++) {
        DDInstalledApp *app = &store->apps[i];
        bool bundle_gone = false;
        for (int t = 0; t < trashed_count; t++) {
            if (is_subpath_of(app->bundle_path, trashed_paths[t])) {
                bundle_gone = true;
                break;
            }
        }
        if (bundle_gone) continue;

        int lw = 0;
        app->leftovers_size = 0;
        for (int l = 0; l < app->leftover_count; l++) {
            bool lgone = false;
            for (int t = 0; t < trashed_count; t++) {
                if (is_subpath_of(app->leftovers[l].path, trashed_paths[t])) {
                    lgone = true;
                    break;
                }
            }
            if (!lgone) {
                if (lw != l) app->leftovers[lw] = app->leftovers[l];
                app->leftovers_size += app->leftovers[lw].size;
                lw++;
            }
        }
        app->leftover_count = lw;
        app->total_size = app->bundle_size + app->leftovers_size;
        store->grand_total += app->total_size;
        if (aw != i) store->apps[aw] = *app;
        aw++;
    }
    store->count = aw;
}

bool dd_app_terminate_and_trash(const DDInstalledApp *app, DDTrashResult *out_res) {
    if (!app || !out_res) return false;
    char paths[20][512];
    int count = 0;
    if (app->bundle_path[0]) {
        snprintf(paths[count++], 512, "%s", app->bundle_path);
    }
    for (int i = 0; i < app->leftover_count && count < 20; i++) {
        snprintf(paths[count++], 512, "%s", app->leftovers[i].path);
    }
    dd_trash_move_paths(paths, count, out_res);
    return out_res->failed_count == 0;
}

static void get_snapshots_dir(char *buf, size_t cap) {
    dd_get_data_dir("Snapshots", buf, cap);
}

static void sanitize_json_str(char *s) {
    for (; *s; s++) {
        if (*s == '"' || *s == '\n' || *s == '\r') *s = '\'';
    }
}

static void save_snapshots_index(const DDSnapshotStore *store) {
    char dir[512], idx_path[600];
    get_snapshots_dir(dir, sizeof(dir));
    snprintf(idx_path, sizeof(idx_path), "%s/index.json", dir);
    size_t cap = 4096 + (size_t)store->count * 1024;
    char *buf = (char *)malloc(cap);
    if (!buf) return;
    size_t pos = (size_t)snprintf(buf, cap, "[\n");
    for (int i = 0; i < store->count && pos + 800 < cap; i++) {
        const DDSnapshotSummary *s = &store->summaries[i];
        char clean_root[512];
        snprintf(clean_root, sizeof(clean_root), "%s", s->root_path);
        sanitize_json_str(clean_root);
        pos += (size_t)snprintf(buf + pos, cap - pos,
            "  {\"id\":\"%s\",\"root\":\"%s\",\"timestamp\":%.3f,\"total\":%lld,\"folders\":%d}%s\n",
            s->id, clean_root, s->timestamp, (long long)s->total_bytes, (int)s->folder_count,
            (i + 1 < store->count) ? "," : "");
    }
    snprintf(buf + pos, cap - pos, "]\n");
    dd_write_file_atomic(idx_path, buf);
    free(buf);
}

void dd_snapshots_load_index(DDSnapshotStore *store) {
    if (!store) return;
    memset(store, 0, sizeof(*store));
    store->before_idx = -1;
    store->after_idx = -1;

    char dir[512], idx_path[600];
    get_snapshots_dir(dir, sizeof(dir));
    snprintf(idx_path, sizeof(idx_path), "%s/index.json", dir);
    FILE *f = fopen(idx_path, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof(line), f) && store->count < 128) {
        char *id_p = strstr(line, "\"id\":\"");
        char *rt_p = strstr(line, "\"root\":\"");
        char *ts_p = strstr(line, "\"timestamp\":");
        char *tot_p = strstr(line, "\"total\":");
        char *fc_p = strstr(line, "\"folders\":");
        if (id_p && rt_p && ts_p && tot_p && fc_p) {
            DDSnapshotSummary *s = &store->summaries[store->count++];
            memset(s, 0, sizeof(*s));
            sscanf(id_p + 6, "%63[^\"]", s->id);
            sscanf(rt_p + 8, "%511[^\"]", s->root_path);
            sscanf(ts_p + 12, "%lf", &s->timestamp);
            long long tot = 0;
            sscanf(tot_p + 8, "%lld", &tot);
            s->total_bytes = tot;
            sscanf(fc_p + 10, "%d", &s->folder_count);
        }
    }
    fclose(f);
}

static void write_snapshot_folders(FILE *f, const DDFileTree *tree, int32_t idx, int32_t *io_count, bool *io_first) {
    if (idx < 0 || idx >= tree->node_count) return;
    const DDFileNode *n = &tree->nodes[idx];
    if (!n->is_directory || n->is_deleted) return;
    if (idx > 0 && n->logical_size < 1000 * 1000) return;

    char path[512];
    dd_node_build_path(tree, idx, path, sizeof(path));
    sanitize_json_str(path);
    fprintf(f, "%s\n    {\"path\":\"%s\",\"size\":%lld}", *io_first ? "" : ",", path, (long long)n->logical_size);
    *io_first = false;
    (*io_count)++;

    if (n->folder_idx >= 0) {
        const DDFolderExtra *fe = &tree->folders[n->folder_idx];
        for (int32_t i = 0; i < fe->child_count; i++) {
            write_snapshot_folders(f, tree, fe->children[i], io_count, io_first);
        }
    }
}

bool dd_snapshots_take(DDSnapshotStore *store, const DDFileTree *tree) {
    if (!store || !tree || tree->node_count == 0 || store->saving) return false;
    store->saving = true;

    double now = dd_now_seconds();
    char id[64];
    snprintf(id, sizeof(id), "snap_%lld_%d", (long long)(now * 1000.0), store->count + 1);

    char dir[512], fpath[600];
    get_snapshots_dir(dir, sizeof(dir));
    snprintf(fpath, sizeof(fpath), "%s/%s.json", dir, id);

    FILE *f = fopen(fpath, "w");
    if (!f) {
        store->saving = false;
        return false;
    }

    int32_t fcount = 0;
    bool first = true;
    char clean_root[512];
    snprintf(clean_root, sizeof(clean_root), "%s", tree->root_path);
    sanitize_json_str(clean_root);
    fprintf(f, "{\n  \"root\":\"%s\",\n  \"folders\":[", clean_root);
    write_snapshot_folders(f, tree, 0, &fcount, &first);
    fprintf(f, "\n  ]\n}\n");
    fclose(f);

    if (store->count < 128) {
        for (int i = store->count; i > 0; i--) {
            store->summaries[i] = store->summaries[i - 1];
        }
        store->count++;
    } else {
        char old_fpath[600];
        snprintf(old_fpath, sizeof(old_fpath), "%s/%s.json", dir, store->summaries[127].id);
        remove(old_fpath);
        for (int i = 127; i > 0; i--) {
            store->summaries[i] = store->summaries[i - 1];
        }
    }
    DDSnapshotSummary *s = &store->summaries[0];
    snprintf(s->id, sizeof(s->id), "%s", id);
    snprintf(s->root_path, sizeof(s->root_path), "%s", tree->root_path);
    s->timestamp = now;
    s->total_bytes = tree->nodes[0].logical_size;
    s->folder_count = fcount;

    if (store->before_idx >= 0) store->before_idx++;
    if (store->after_idx >= 0) store->after_idx++;
    if (store->count >= 2 && (store->before_idx < 0 || store->after_idx < 0)) {
        store->before_idx = 1;
        store->after_idx = 0;
    }
    store->diff_valid = false;
    save_snapshots_index(store);
    store->saving = false;
    return true;
}

void dd_snapshots_delete(DDSnapshotStore *store, int index) {
    if (!store || index < 0 || index >= store->count) return;
    char dir[512], fpath[600];
    get_snapshots_dir(dir, sizeof(dir));
    snprintf(fpath, sizeof(fpath), "%s/%s.json", dir, store->summaries[index].id);
    remove(fpath);

    for (int i = index; i < store->count - 1; i++) {
        store->summaries[i] = store->summaries[i + 1];
    }
    store->count--;
    if (store->before_idx == index) store->before_idx = -1;
    else if (store->before_idx > index) store->before_idx--;
    if (store->after_idx == index) store->after_idx = -1;
    else if (store->after_idx > index) store->after_idx--;
    store->diff_valid = false;
    save_snapshots_index(store);
}

typedef struct {
    char path[512];
    int64_t size;
} SnapEntry;

static int cmp_snap_entry_path(const void *a, const void *b) {
    const SnapEntry *sa = (const SnapEntry *)a;
    const SnapEntry *sb = (const SnapEntry *)b;
    return strcmp(sa->path, sb->path);
}

static int load_snap_entries(const char *id, SnapEntry **out_entries) {
    char dir[512], fpath[600];
    get_snapshots_dir(dir, sizeof(dir));
    snprintf(fpath, sizeof(fpath), "%s/%s.json", dir, id);
    FILE *f = fopen(fpath, "r");
    if (!f) return 0;

    int cap = 512, count = 0;
    SnapEntry *arr = (SnapEntry *)malloc((size_t)cap * sizeof(SnapEntry));
    char line[1024];
    while (arr && fgets(line, sizeof(line), f)) {
        char *pp = strstr(line, "\"path\":\"");
        char *sp = strstr(line, "\"size\":");
        if (pp && sp) {
            if (count >= cap) {
                cap *= 2;
                SnapEntry *grown = (SnapEntry *)realloc(arr, (size_t)cap * sizeof(SnapEntry));
                if (!grown) break;
                arr = grown;
            }
            sscanf(pp + 8, "%511[^\"]", arr[count].path);
            long long sz = 0;
            sscanf(sp + 7, "%lld", &sz);
            arr[count].size = sz;
            count++;
        }
    }
    fclose(f);
    if (arr && count > 1) {
        qsort(arr, (size_t)count, sizeof(SnapEntry), cmp_snap_entry_path);
    }
    *out_entries = arr;
    return count;
}

static int cmp_diff_row(const void *a, const void *b) {
    const DDSnapshotDiffRow *ra = (const DDSnapshotDiffRow *)a;
    const DDSnapshotDiffRow *rb = (const DDSnapshotDiffRow *)b;
    int64_t aa = ra->delta >= 0 ? ra->delta : -ra->delta;
    int64_t ab = rb->delta >= 0 ? rb->delta : -rb->delta;
    return (ab > aa) - (ab < aa);
}

void dd_snapshots_compute_diff(DDSnapshotStore *store) {
    if (!store) return;
    store->diff_valid = false;
    store->diff_count = 0;
    if (store->before_idx < 0 || store->before_idx >= store->count ||
        store->after_idx < 0 || store->after_idx >= store->count) {
        return;
    }

    const DDSnapshotSummary *sb = &store->summaries[store->before_idx];
    const DDSnapshotSummary *sa = &store->summaries[store->after_idx];
    store->roots_differ = (strcmp(sb->root_path, sa->root_path) != 0);

    SnapEntry *eb = NULL, *ea = NULL;
    int nb = load_snap_entries(sb->id, &eb);
    int na = load_snap_entries(sa->id, &ea);

    int cap = nb + na + 16;
    DDSnapshotDiffRow *all = (DDSnapshotDiffRow *)calloc((size_t)(cap > 0 ? cap : 1), sizeof(DDSnapshotDiffRow));
    int count = 0;

    if (all) {
        int i = 0, j = 0;
        while (i < nb || j < na) {
            int cmp = 0;
            if (i < nb && j < na) cmp = strcmp(eb[i].path, ea[j].path);
            else if (i < nb) cmp = -1;
            else cmp = 1;

            const char *path = NULL;
            int64_t bsz = 0, asz = 0;
            if (cmp == 0) {
                path = eb[i].path;
                bsz = eb[i].size;
                asz = ea[j].size;
                i++; j++;
            } else if (cmp < 0) {
                path = eb[i].path;
                bsz = eb[i].size;
                asz = 0;
                i++;
            } else {
                path = ea[j].path;
                bsz = 0;
                asz = ea[j].size;
                j++;
            }
            int64_t delta = asz - bsz;
            int64_t abs_d = delta >= 0 ? delta : -delta;
            if (abs_d >= 1000 * 1000) {
                snprintf(all[count].path, sizeof(all[count].path), "%s", path);
                all[count].before_size = bsz;
                all[count].after_size = asz;
                all[count].delta = delta;
                count++;
            }
        }
        qsort(all, (size_t)count, sizeof(DDSnapshotDiffRow), cmp_diff_row);
        store->diff_count = count < 200 ? count : 200;
        memcpy(store->diff_rows, all, (size_t)store->diff_count * sizeof(DDSnapshotDiffRow));
        free(all);
    }

    free(eb);
    free(ea);
    store->diff_valid = true;
}
