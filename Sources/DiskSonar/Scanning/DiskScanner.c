#define _GNU_SOURCE
#include "DiskScanner.h"
#include "../Utils/Utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#endif

typedef struct {
    int32_t folder_node_idx;
    int depth;
    bool inside_app;
    char path[512];
} ScanJob;

struct DDDiskScanner {
#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cond;
    HANDLE coordinator_thread;
#else
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t coordinator_thread;
#endif
    bool has_coordinator;
    volatile int cancel_flag;

    ScanJob *job_stack;
    int job_count;
    int job_cap;
    int active_workers;
    bool workers_done;

    DDScanProgress progress;
    DDFileTree *building_tree;
    DDFileTree *finished_tree;
    char target_root[512];
};

static void lock_scanner(DDDiskScanner *s) {
#ifdef _WIN32
    EnterCriticalSection(&s->lock);
#else
    pthread_mutex_lock(&s->lock);
#endif
}

static void unlock_scanner(DDDiskScanner *s) {
#ifdef _WIN32
    LeaveCriticalSection(&s->lock);
#else
    pthread_mutex_unlock(&s->lock);
#endif
}

static void broadcast_scanner(DDDiskScanner *s) {
#ifdef _WIN32
    WakeAllConditionVariable(&s->cond);
#else
    pthread_cond_broadcast(&s->cond);
#endif
}

static void wait_scanner(DDDiskScanner *s) {
#ifdef _WIN32
    SleepConditionVariableCS(&s->cond, &s->lock, INFINITE);
#else
    pthread_cond_wait(&s->cond, &s->lock);
#endif
}

static bool should_skip_path(const char *path) {
    static const char * const skip_paths[] = {
        "/proc", "/sys", "/dev", "/run", "/System", "/Volumes",
        "/private/var/vm", "/.Spotlight-V100", "/.fseventsd",
        "/cores", "/net", "/lost+found", NULL
    };
    for (int i = 0; skip_paths[i]; i++) {
        if (strcmp(path, skip_paths[i]) == 0) return true;
    }
    if (strstr(path, "/.gvfs") != NULL || strstr(path, "/run/user/") != NULL) {
        return true;
    }
#ifdef _WIN32
    if (strstr(path, "$Recycle.Bin") != NULL || strstr(path, "System Volume Information") != NULL) {
        return true;
    }
#endif
    return false;
}

static bool is_app_dir_name(const char *name, size_t len) {
    if (len > 4 && strcmp(name + len - 4, ".app") == 0) return true;
    if (len > 7 && strcmp(name + len - 7, ".AppDir") == 0) return true;
    return false;
}

static void push_job_locked(DDDiskScanner *s, int32_t folder_node_idx, int depth, bool inside_app, const char *path) {
    if (depth > 120) return;
    if (s->job_count >= s->job_cap) {
        int new_cap = s->job_cap == 0 ? 512 : s->job_cap * 2;
        ScanJob *grown = (ScanJob *)realloc(s->job_stack, (size_t)new_cap * sizeof(ScanJob));
        if (!grown) return;
        s->job_stack = grown;
        s->job_cap = new_cap;
    }
    ScanJob *j = &s->job_stack[s->job_count++];
    j->folder_node_idx = folder_node_idx;
    j->depth = depth;
    j->inside_app = inside_app;
    snprintf(j->path, sizeof(j->path), "%s", path);
}

typedef struct {
    char name[256];
    int64_t logical_size;
    int64_t disk_size;
    double mtime;
    double ctime;
    bool is_dir;
    bool inside_app;
    DDFileCategory cat;
} LocalEntry;

static void process_directory_job(DDDiskScanner *s, const ScanJob *job) {
    size_t cap = 64, count = 0;
    LocalEntry *entries = (LocalEntry *)malloc(cap * sizeof(LocalEntry));
    if (!entries) return;

    bool denied = false;
    int64_t local_files = 0;
    int64_t local_bytes = 0;

#ifdef _WIN32
    char pattern[540];
    size_t plen = strlen(job->path);
    if (plen > 0 && (job->path[plen - 1] == '\\' || job->path[plen - 1] == '/')) {
        snprintf(pattern, sizeof(pattern), "%s*", job->path);
    } else {
        snprintf(pattern, sizeof(pattern), "%s\\*", job->path);
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileExA(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) denied = true;
    } else {
        do {
            if (s->cancel_flag) break;
            const char *name = fd.cFileName;
            if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

            if (count >= cap) {
                cap *= 2;
                LocalEntry *grown = (LocalEntry *)realloc(entries, cap * sizeof(LocalEntry));
                if (!grown) break;
                entries = grown;
            }
            LocalEntry *e = &entries[count];
            memset(e, 0, sizeof(*e));
            size_t nlen = strlen(name);
            if (nlen >= sizeof(e->name)) nlen = sizeof(e->name) - 1;
            memcpy(e->name, name, nlen);
            e->name[nlen] = '\0';

            e->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            e->inside_app = job->inside_app || (e->is_dir && is_app_dir_name(e->name, nlen));
            uint64_t fsz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            e->logical_size = e->is_dir ? 0 : (int64_t)fsz;
            e->disk_size = e->is_dir ? 0 : (int64_t)((fsz + 4095ULL) & ~4095ULL);
            uint64_t wt = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            uint64_t ct = ((uint64_t)fd.ftCreationTime.dwHighDateTime << 32) | fd.ftCreationTime.dwLowDateTime;
            e->mtime = wt > 116444736000000000ULL ? (double)(wt - 116444736000000000ULL) / 10000000.0 : 0.0;
            e->ctime = ct > 116444736000000000ULL ? (double)(ct - 116444736000000000ULL) / 10000000.0 : e->mtime;
            e->cat = e->is_dir ? CAT_OTHER : dd_classify_filename(e->name, nlen, e->inside_app);
            if (!e->is_dir) {
                local_files++;
                local_bytes += e->disk_size;
            }
            count++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *dir = opendir(job->path);
    if (!dir) {
        if (errno == EACCES || errno == EPERM) denied = true;
    } else {
        int dfd = dirfd(dir);
        struct dirent *de;
        while (!s->cancel_flag && (de = readdir(dir)) != NULL) {
            const char *name = de->d_name;
            if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) continue;

            struct stat st;
            if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
                if (errno == EACCES || errno == EPERM) denied = true;
                continue;
            }
            if (S_ISLNK(st.st_mode)) continue;
            bool is_dir = S_ISDIR(st.st_mode);
            bool is_reg = S_ISREG(st.st_mode);
            if (!is_dir && !is_reg) continue;

            if (count >= cap) {
                cap *= 2;
                LocalEntry *grown = (LocalEntry *)realloc(entries, cap * sizeof(LocalEntry));
                if (!grown) break;
                entries = grown;
            }
            LocalEntry *e = &entries[count];
            memset(e, 0, sizeof(*e));
            size_t nlen = strlen(name);
            if (nlen >= sizeof(e->name)) nlen = sizeof(e->name) - 1;
            memcpy(e->name, name, nlen);
            e->name[nlen] = '\0';

            e->is_dir = is_dir;
            e->inside_app = job->inside_app || (is_dir && is_app_dir_name(e->name, nlen));
            e->logical_size = is_dir ? 0 : (int64_t)st.st_size;
            int64_t alloc = is_dir ? 0 : (int64_t)st.st_blocks * 512;
            if (!is_dir && alloc == 0 && st.st_size > 0) alloc = (int64_t)st.st_size;
            e->disk_size = alloc;
            e->mtime = (double)st.st_mtim.tv_sec + (double)st.st_mtim.tv_nsec / 1e9;
            e->ctime = (double)st.st_ctim.tv_sec + (double)st.st_ctim.tv_nsec / 1e9;
            e->cat = is_dir ? CAT_OTHER : dd_classify_filename(e->name, nlen, e->inside_app);
            if (!is_dir) {
                local_files++;
                local_bytes += e->disk_size;
            }
            count++;
        }
        closedir(dir);
    }
#endif

    if (s->cancel_flag) {
        free(entries);
        return;
    }

    lock_scanner(s);
    DDFileTree *tree = s->building_tree;
    if (denied) {
        tree->denied_count++;
        if (tree->denied_sample_count < 8) {
            snprintf(tree->denied_samples[tree->denied_sample_count++], 512, "%s", job->path);
        }
    }
    s->progress.files_scanned += local_files;
    s->progress.bytes_scanned += local_bytes;
    snprintf(s->progress.current_path, sizeof(s->progress.current_path), "%s", job->path);

    for (size_t i = 0; i < count; i++) {
        LocalEntry *e = &entries[i];
        int32_t cidx = dd_tree_add_node(tree, job->folder_node_idx, e->name, strlen(e->name), e->is_dir, e->cat, e->inside_app);
        if (cidx < 0) continue;
        DDFileNode *cn = &tree->nodes[cidx];
        cn->logical_size = e->logical_size;
        cn->size_on_disk = e->disk_size;
        cn->modified_stamp = e->mtime;
        cn->created_stamp = e->ctime;
        dd_folder_add_child(tree, job->folder_node_idx, cidx);

        if (e->is_dir) {
            char child_path[512];
            size_t jlen = strlen(job->path);
#ifdef _WIN32
            const char sep = '\\';
#else
            const char sep = '/';
#endif
            if (jlen > 0 && (job->path[jlen - 1] == '/' || job->path[jlen - 1] == '\\')) {
                snprintf(child_path, sizeof(child_path), "%s%s", job->path, e->name);
            } else {
                snprintf(child_path, sizeof(child_path), "%s%c%s", job->path, sep, e->name);
            }
            if (!should_skip_path(child_path)) {
                push_job_locked(s, cidx, job->depth + 1, e->inside_app, child_path);
            }
        }
    }
    broadcast_scanner(s);
    unlock_scanner(s);

    free(entries);
}

#ifdef _WIN32
static unsigned __stdcall worker_thread_fn(void *arg)
#else
static void *worker_thread_fn(void *arg)
#endif
{
    DDDiskScanner *s = (DDDiskScanner *)arg;
    for (;;) {
        ScanJob job;
        lock_scanner(s);
        while (!s->cancel_flag && s->job_count == 0 && !s->workers_done) {
            if (s->active_workers == 0) {
                s->workers_done = true;
                broadcast_scanner(s);
                break;
            }
            wait_scanner(s);
        }
        if (s->cancel_flag || s->workers_done) {
            unlock_scanner(s);
            break;
        }
        job = s->job_stack[--s->job_count];
        s->active_workers++;
        unlock_scanner(s);

        process_directory_job(s, &job);

        lock_scanner(s);
        s->active_workers--;
        if (s->job_count == 0 && s->active_workers == 0) {
            s->workers_done = true;
        }
        broadcast_scanner(s);
        unlock_scanner(s);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void rollup_node_recursive(DDFileTree *tree, int32_t idx) {
    DDFileNode *n = &tree->nodes[idx];
    if (!n->is_directory || n->folder_idx < 0) return;

    int32_t fidx = n->folder_idx;
    int32_t child_count = tree->folders[fidx].child_count;
    for (int32_t i = 0; i < child_count; i++) {
        int32_t cidx = tree->folders[fidx].children[i];
        if (tree->nodes[cidx].is_directory) {
            rollup_node_recursive(tree, cidx);
        }
    }

    DDFolderExtra *fe = &tree->folders[fidx];
    int64_t log_sum = 0, disk_sum = 0;
    int32_t files = 0, folders = 0;
    double max_mtime = n->modified_stamp;
    memset(fe->type_sizes, 0, sizeof(fe->type_sizes));

    for (int32_t i = 0; i < fe->child_count; i++) {
        int32_t cidx = fe->children[i];
        const DDFileNode *cn = &tree->nodes[cidx];
        log_sum += cn->logical_size;
        disk_sum += cn->size_on_disk;
        if (cn->modified_stamp > max_mtime) max_mtime = cn->modified_stamp;

        if (cn->is_directory && cn->folder_idx >= 0) {
            const DDFolderExtra *cfe = &tree->folders[cn->folder_idx];
            files += cfe->file_count;
            folders += cfe->folder_count + 1;
            for (int c = 0; c < CAT_COUNT; c++) {
                fe->type_sizes[c] += cfe->type_sizes[c];
            }
        } else {
            files += 1;
            fe->type_sizes[cn->category] += cn->size_on_disk;
        }
    }

    n->logical_size = log_sum;
    n->size_on_disk = disk_sum;
    n->modified_stamp = max_mtime;
    fe->file_count = files;
    fe->folder_count = folders;
    dd_tree_sort_folder_children(tree, idx);
}

#ifdef _WIN32
static unsigned __stdcall coordinator_thread_fn(void *arg)
#else
static void *coordinator_thread_fn(void *arg)
#endif
{
    DDDiskScanner *s = (DDDiskScanner *)arg;
    double t0 = dd_now_seconds();

    int num_workers = 4;
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    num_workers = (int)sysinfo.dwNumberOfProcessors;
#else
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu > 0) num_workers = (int)ncpu;
#endif
    if (num_workers < 2) num_workers = 2;
    if (num_workers > 12) num_workers = 12;

#ifdef _WIN32
    HANDLE threads[16];
    for (int i = 0; i < num_workers; i++) {
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, worker_thread_fn, s, 0, NULL);
    }
    for (int i = 0; i < num_workers; i++) {
        if (threads[i]) {
            WaitForSingleObject(threads[i], INFINITE);
            CloseHandle(threads[i]);
        }
    }
#else
    pthread_t threads[16];
    for (int i = 0; i < num_workers; i++) {
        pthread_create(&threads[i], NULL, worker_thread_fn, s);
    }
    for (int i = 0; i < num_workers; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    lock_scanner(s);
    bool cancelled = (s->cancel_flag != 0);
    DDFileTree *tree = s->building_tree;
    s->building_tree = NULL;
    unlock_scanner(s);

    if (cancelled && tree) {
        dd_tree_free_async(tree);
        tree = NULL;
    } else if (tree) {
        rollup_node_recursive(tree, 0);
        tree->scan_duration = dd_now_seconds() - t0;
    }

    lock_scanner(s);
    if (s->finished_tree) {
        dd_tree_free_async(s->finished_tree);
    }
    s->finished_tree = tree;
    s->progress.is_running = false;
    s->progress.is_finished = (tree != NULL);
    s->progress.was_cancelled = cancelled;
    unlock_scanner(s);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

DDDiskScanner *dd_scanner_create(void) {
    DDDiskScanner *s = (DDDiskScanner *)calloc(1, sizeof(DDDiskScanner));
    if (!s) return NULL;
#ifdef _WIN32
    InitializeCriticalSection(&s->lock);
    InitializeConditionVariable(&s->cond);
#else
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init(&s->cond, NULL);
#endif
    return s;
}

void dd_scanner_cancel(DDDiskScanner *s) {
    if (!s) return;
    lock_scanner(s);
    s->cancel_flag = 1;
    broadcast_scanner(s);
    unlock_scanner(s);

    if (s->has_coordinator) {
#ifdef _WIN32
        WaitForSingleObject(s->coordinator_thread, INFINITE);
        CloseHandle(s->coordinator_thread);
#else
        pthread_join(s->coordinator_thread, NULL);
#endif
        s->has_coordinator = false;
    }
}

void dd_scanner_destroy(DDDiskScanner *s) {
    if (!s) return;
    dd_scanner_cancel(s);
    if (s->building_tree) dd_tree_free(s->building_tree);
    if (s->finished_tree) dd_tree_free(s->finished_tree);
    free(s->job_stack);
#ifdef _WIN32
    DeleteCriticalSection(&s->lock);
#else
    pthread_mutex_destroy(&s->lock);
    pthread_cond_destroy(&s->cond);
#endif
    free(s);
}

void dd_scanner_start(DDDiskScanner *s, const char *root_path) {
    if (!s || !root_path || !*root_path) return;
    dd_scanner_cancel(s);

    lock_scanner(s);
    if (s->finished_tree) {
        dd_tree_free_async(s->finished_tree);
        s->finished_tree = NULL;
    }
    s->cancel_flag = 0;
    s->job_count = 0;
    s->active_workers = 0;
    s->workers_done = false;
    memset(&s->progress, 0, sizeof(s->progress));
    s->progress.is_running = true;
    snprintf(s->progress.current_path, sizeof(s->progress.current_path), "%s", root_path);
    snprintf(s->target_root, sizeof(s->target_root), "%s", root_path);

    s->building_tree = dd_tree_create(root_path);
    push_job_locked(s, 0, 0, false, s->building_tree->root_path);
    unlock_scanner(s);

#ifdef _WIN32
    s->coordinator_thread = (HANDLE)_beginthreadex(NULL, 0, coordinator_thread_fn, s, 0, NULL);
    s->has_coordinator = (s->coordinator_thread != NULL);
#else
    s->has_coordinator = (pthread_create(&s->coordinator_thread, NULL, coordinator_thread_fn, s) == 0);
#endif
}

void dd_scanner_get_progress(DDDiskScanner *s, DDScanProgress *out_progress) {
    if (!s || !out_progress) return;
    lock_scanner(s);
    *out_progress = s->progress;
    unlock_scanner(s);
}

DDFileTree *dd_scanner_take_result(DDDiskScanner *s) {
    if (!s) return NULL;
    lock_scanner(s);
    DDFileTree *res = s->finished_tree;
    s->finished_tree = NULL;
    s->progress.is_finished = false;
    unlock_scanner(s);
    return res;
}

static int64_t measure_path_depth(const char *path, int depth, int *io_budget) {
    if (!path || depth > 16 || !io_budget || *io_budget <= 0) return 0;
    (*io_budget)--;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) return 0;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return 0;
    if (!(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        uint64_t fsz = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        return (int64_t)((fsz + 4095ULL) & ~4095ULL);
    }
    char pattern[540];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int64_t sum = 0;
    do {
        if (*io_budget <= 0) break;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char sub[540];
            snprintf(sub, sizeof(sub), "%s\\%s", path, fd.cFileName);
            sum += measure_path_depth(sub, depth + 1, io_budget);
        } else {
            (*io_budget)--;
            uint64_t fsz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            sum += (int64_t)((fsz + 4095ULL) & ~4095ULL);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return sum;
#else
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISLNK(st.st_mode)) return 0;
    if (S_ISREG(st.st_mode)) {
        int64_t alloc = (int64_t)st.st_blocks * 512;
        return (alloc > 0) ? alloc : (int64_t)st.st_size;
    }
    if (!S_ISDIR(st.st_mode)) return 0;

    DIR *dir = opendir(path);
    if (!dir) return 0;
    int64_t sum = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && *io_budget > 0) {
        if (de->d_name[0] == '.' && (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0'))) continue;
        char sub[540];
        snprintf(sub, sizeof(sub), "%s/%s", path, de->d_name);
        sum += measure_path_depth(sub, depth + 1, io_budget);
    }
    closedir(dir);
    return sum;
#endif
}

int64_t dd_scanner_allocated_size(const char *path) {
    int budget = 3500;
    return measure_path_depth(path, 0, &budget);
}
