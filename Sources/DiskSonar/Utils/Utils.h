#ifndef DISKSONAR_UTILS_H
#define DISKSONAR_UTILS_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

double dd_now_seconds(void);

void dd_format_bytes(int64_t bytes, char *buf, size_t cap);
void dd_format_percent(double ratio, char *buf, size_t cap);
void dd_format_relative_age(double timestamp, char *buf, size_t cap);
void dd_format_date(double timestamp, char *buf, size_t cap);
void dd_format_duration(double seconds, char *buf, size_t cap);
void dd_abbreviate_label(const char *name, char *buf, size_t cap);
void dd_truncate_middle(const char *str, size_t max_chars, char *buf, size_t cap);

bool dd_sha256_file(const char *path, int64_t max_bytes, bool nocache, uint8_t out_hash[32]);
void dd_sha256_hex(const uint8_t hash[32], char out_hex[65]);

void dd_get_data_dir(const char *subdir, char *buf, size_t cap);
bool dd_write_file_atomic(const char *path, const char *content);
bool dd_exec_sync(const char *prog, const char *arg1, const char *arg2, const char *arg3);

typedef struct {
    char name[64];
    char mount_path[256];
    int64_t total_bytes;
    int64_t free_bytes;
    int64_t used_bytes;
} DDVolumeInfo;

typedef struct {
    int pid;
    char name[64];
    double cpu_percent;
    int64_t rss_bytes;
    uint64_t raw_ticks;
} DDProcessInfo;

typedef struct {
    double cpu_user_pct;
    double cpu_sys_pct;
    double cpu_idle_pct;
    double cpu_total_pct;
    double load_avg[3];

    int64_t mem_total;
    int64_t mem_wired;
    int64_t mem_compressed;
    int64_t mem_other_used;
    int64_t mem_free;
    int64_t mem_used;

    int64_t net_down_per_sec;
    int64_t net_up_per_sec;
    int64_t net_session_in;
    int64_t net_session_out;

    DDVolumeInfo volumes[16];
    int volume_count;

    DDProcessInfo procs_by_cpu[60];
    int procs_cpu_count;
    DDProcessInfo procs_by_mem[60];
    int procs_mem_count;
    int total_proc_count;

    double cpu_history[120];
    double mem_history[120];
    double free_history[120];
    int history_count;

    uint64_t prev_cpu_user;
    uint64_t prev_cpu_sys;
    uint64_t prev_cpu_idle;
    uint64_t prev_cpu_total;
    uint64_t prev_net_rx;
    uint64_t prev_net_tx;
    uint64_t base_net_rx;
    uint64_t base_net_tx;
    double prev_sample_time;
    bool initialized;

    struct {
        int pid;
        uint64_t ticks;
    } prev_procs[2048];
    int prev_proc_count;
} DDSystemMonitor;

void dd_monitor_init(DDSystemMonitor *mon);
void dd_monitor_sample(DDSystemMonitor *mon);
void dd_get_primary_volume(const char *path, DDVolumeInfo *out_vol);

void dd_reveal_in_file_manager(const char *path);
void dd_copy_to_clipboard(const char *text);
const char *dd_get_clipboard_cache(void);
void dd_open_permissions_help(void);
bool dd_pick_folder_dialog(char *out_path, size_t cap);
void dd_get_home_dir(char *out_path, size_t cap);
void dd_get_root_dir(char *out_path, size_t cap);

#include "../Models/Models.h"
void dd_write_html_escaped(FILE *f, const char *s);
void dd_write_csv_field_safe(FILE *f, const char *s);
bool dd_export_tree_json(const DDFileTree *tree, const char *out_path);
bool dd_export_tree_csv(const DDFileTree *tree, const char *out_path);
bool dd_export_tree_html(const DDFileTree *tree, const char *out_path);

#endif
