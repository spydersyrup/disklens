#define _GNU_SOURCE
#include "Utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <io.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/wait.h>
#endif

double dd_now_seconds(void) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (double)(t - 116444736000000000ULL) / 10000000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

void dd_format_date(double timestamp, char *buf, size_t cap) {
    if (timestamp <= 0.0) {
        snprintf(buf, cap, "Unknown");
        return;
    }
    time_t t = (time_t)timestamp;
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(buf, cap, "%Y-%m-%d %H:%M", &tmv);
}

#ifndef _WIN32
static int cmp_proc_cpu(const void *a, const void *b) {
    const DDProcessInfo *pa = (const DDProcessInfo *)a;
    const DDProcessInfo *pb = (const DDProcessInfo *)b;
    if (pb->cpu_percent > pa->cpu_percent) return 1;
    if (pb->cpu_percent < pa->cpu_percent) return -1;
    return (pb->rss_bytes > pa->rss_bytes) - (pb->rss_bytes < pa->rss_bytes);
}
#endif

static int cmp_proc_mem(const void *a, const void *b) {
    const DDProcessInfo *pa = (const DDProcessInfo *)a;
    const DDProcessInfo *pb = (const DDProcessInfo *)b;
    if (pb->rss_bytes > pa->rss_bytes) return 1;
    if (pb->rss_bytes < pa->rss_bytes) return -1;
    return (pb->cpu_percent > pa->cpu_percent) - (pb->cpu_percent < pa->cpu_percent);
}

void dd_monitor_init(DDSystemMonitor *mon) {
    memset(mon, 0, sizeof(*mon));
    dd_monitor_sample(mon);
}

void dd_get_primary_volume(const char *path, DDVolumeInfo *out_vol) {
    memset(out_vol, 0, sizeof(*out_vol));
#ifdef _WIN32
    char root[16] = "C:\\";
    if (path && strlen(path) >= 3 && path[1] == ':') {
        root[0] = path[0];
        root[1] = ':';
        root[2] = '\\';
        root[3] = '\0';
    }
    ULARGE_INTEGER free_avail, total, total_free;
    if (GetDiskFreeSpaceExA(root, &free_avail, &total, &total_free)) {
        out_vol->total_bytes = (int64_t)total.QuadPart;
        out_vol->free_bytes = (int64_t)free_avail.QuadPart;
        out_vol->used_bytes = out_vol->total_bytes - out_vol->free_bytes;
    }
    char vol_name[64] = "";
    GetVolumeInformationA(root, vol_name, sizeof(vol_name), NULL, NULL, NULL, NULL, 0);
    snprintf(out_vol->name, sizeof(out_vol->name), "%s (%s)", vol_name[0] ? vol_name : "Local Disk", root);
    snprintf(out_vol->mount_path, sizeof(out_vol->mount_path), "%s", root);
#else
    const char *target = (path && *path) ? path : "/";
    struct statvfs vfs;
    if (statvfs(target, &vfs) != 0) {
        statvfs("/", &vfs);
        target = "/";
    }
    out_vol->total_bytes = (int64_t)vfs.f_blocks * (int64_t)vfs.f_frsize;
    out_vol->free_bytes  = (int64_t)vfs.f_bavail * (int64_t)vfs.f_frsize;
    out_vol->used_bytes  = out_vol->total_bytes - (int64_t)vfs.f_bfree * (int64_t)vfs.f_frsize;
    if (out_vol->used_bytes < 0) out_vol->used_bytes = out_vol->total_bytes - out_vol->free_bytes;
    snprintf(out_vol->name, sizeof(out_vol->name), "System Volume");
    snprintf(out_vol->mount_path, sizeof(out_vol->mount_path), "%s", target);
#endif
}

void dd_monitor_sample(DDSystemMonitor *mon) {
    double now = dd_now_seconds();
    double dt = mon->initialized ? (now - mon->prev_sample_time) : 0.0;
    if (dt <= 0.01) dt = 1.0;

#ifdef _WIN32
    FILETIME idle_ft, kernel_ft, user_ft;
    if (GetSystemTimes(&idle_ft, &kernel_ft, &user_ft)) {
        uint64_t idle = ((uint64_t)idle_ft.dwHighDateTime << 32) | idle_ft.dwLowDateTime;
        uint64_t kern = ((uint64_t)kernel_ft.dwHighDateTime << 32) | kernel_ft.dwLowDateTime;
        uint64_t user = ((uint64_t)user_ft.dwHighDateTime << 32) | user_ft.dwLowDateTime;
        uint64_t sys = (kern >= idle) ? (kern - idle) : 0;
        uint64_t total = user + sys + idle;
        if (mon->initialized && total > mon->prev_cpu_total) {
            uint64_t du = user - mon->prev_cpu_user;
            uint64_t ds = sys - mon->prev_cpu_sys;
            uint64_t di = idle - mon->prev_cpu_idle;
            uint64_t dtot = du + ds + di;
            if (dtot > 0) {
                mon->cpu_user_pct = (double)du * 100.0 / (double)dtot;
                mon->cpu_sys_pct  = (double)ds * 100.0 / (double)dtot;
                mon->cpu_idle_pct = (double)di * 100.0 / (double)dtot;
                mon->cpu_total_pct = mon->cpu_user_pct + mon->cpu_sys_pct;
            }
        }
        mon->prev_cpu_user = user;
        mon->prev_cpu_sys = sys;
        mon->prev_cpu_idle = idle;
        mon->prev_cpu_total = total;
    }

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        mon->mem_total = (int64_t)ms.ullTotalPhys;
        mon->mem_free  = (int64_t)ms.ullAvailPhys;
        mon->mem_used  = mon->mem_total - mon->mem_free;
        mon->mem_wired = mon->mem_used / 4;
        mon->mem_compressed = mon->mem_used / 6;
        mon->mem_other_used = mon->mem_used - mon->mem_wired - mon->mem_compressed;
    }

    mon->volume_count = 0;
    DWORD drives = GetLogicalDrives();
    for (int d = 0; d < 26 && mon->volume_count < 16; d++) {
        if (drives & (1 << d)) {
            char root[8];
            snprintf(root, sizeof(root), "%c:\\", 'A' + d);
            ULARGE_INTEGER avail, tot, totfree;
            if (GetDiskFreeSpaceExA(root, &avail, &tot, &totfree) && tot.QuadPart > 0) {
                DDVolumeInfo *v = &mon->volumes[mon->volume_count++];
                snprintf(v->name, sizeof(v->name), "Drive %c:", 'A' + d);
                snprintf(v->mount_path, sizeof(v->mount_path), "%s", root);
                v->total_bytes = (int64_t)tot.QuadPart;
                v->free_bytes = (int64_t)avail.QuadPart;
                v->used_bytes = v->total_bytes - v->free_bytes;
            }
        }
    }

    DWORD pids[1024], bytes_ret = 0;
    if (EnumProcesses(pids, sizeof(pids), &bytes_ret)) {
        int n = (int)(bytes_ret / sizeof(DWORD));
        DDProcessInfo *all = (DDProcessInfo *)calloc((size_t)n, sizeof(DDProcessInfo));
        int valid = 0;
        for (int i = 0; i < n; i++) {
            if (pids[i] == 0) continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pids[i]);
            if (h) {
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
                    all[valid].pid = (int)pids[i];
                    all[valid].rss_bytes = (int64_t)pmc.WorkingSetSize;
                    char mod[64] = "process";
                    GetModuleBaseNameA(h, NULL, mod, sizeof(mod));
                    snprintf(all[valid].name, sizeof(all[valid].name), "%s", mod);
                    valid++;
                }
                CloseHandle(h);
            }
        }
        mon->total_proc_count = valid;
        qsort(all, (size_t)valid, sizeof(DDProcessInfo), cmp_proc_mem);
        mon->procs_mem_count = valid < 60 ? valid : 60;
        memcpy(mon->procs_by_mem, all, (size_t)mon->procs_mem_count * sizeof(DDProcessInfo));
        mon->procs_cpu_count = mon->procs_mem_count;
        memcpy(mon->procs_by_cpu, all, (size_t)mon->procs_cpu_count * sizeof(DDProcessInfo));
        free(all);
    }
#else
    FILE *fstat = fopen("/proc/stat", "r");
    if (fstat) {
        uint64_t user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        if (fscanf(fstat, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   (unsigned long long *)&user, (unsigned long long *)&nice,
                   (unsigned long long *)&sys, (unsigned long long *)&idle,
                   (unsigned long long *)&iowait, (unsigned long long *)&irq,
                   (unsigned long long *)&softirq, (unsigned long long *)&steal) >= 4) {
            uint64_t u_tot = user + nice;
            uint64_t s_tot = sys + irq + softirq + steal;
            uint64_t i_tot = idle + iowait;
            uint64_t tot = u_tot + s_tot + i_tot;
            if (mon->initialized && tot > mon->prev_cpu_total) {
                uint64_t du = u_tot - mon->prev_cpu_user;
                uint64_t ds = s_tot - mon->prev_cpu_sys;
                uint64_t di = i_tot - mon->prev_cpu_idle;
                uint64_t dtot = du + ds + di;
                if (dtot > 0) {
                    mon->cpu_user_pct = (double)du * 100.0 / (double)dtot;
                    mon->cpu_sys_pct  = (double)ds * 100.0 / (double)dtot;
                    mon->cpu_idle_pct = (double)di * 100.0 / (double)dtot;
                    mon->cpu_total_pct = mon->cpu_user_pct + mon->cpu_sys_pct;
                }
            }
            mon->prev_cpu_user = u_tot;
            mon->prev_cpu_sys = s_tot;
            mon->prev_cpu_idle = i_tot;
            mon->prev_cpu_total = tot;
        }
        fclose(fstat);
    }

    FILE *fload = fopen("/proc/loadavg", "r");
    if (fload) {
        if (fscanf(fload, "%lf %lf %lf", &mon->load_avg[0], &mon->load_avg[1], &mon->load_avg[2]) != 3) {
            mon->load_avg[0] = 0.0;
        }
        fclose(fload);
    }

    FILE *fmem = fopen("/proc/meminfo", "r");
    if (fmem) {
        char line[256];
        int64_t mem_total = 0, mem_avail = 0, mem_free = 0, buffers = 0, cached = 0, slab = 0, pagetables = 0;
        while (fgets(line, sizeof(line), fmem)) {
            long long val = 0;
            if (sscanf(line, "MemTotal: %lld kB", &val) == 1) mem_total = val * 1024;
            else if (sscanf(line, "MemAvailable: %lld kB", &val) == 1) mem_avail = val * 1024;
            else if (sscanf(line, "MemFree: %lld kB", &val) == 1) mem_free = val * 1024;
            else if (sscanf(line, "Buffers: %lld kB", &val) == 1) buffers = val * 1024;
            else if (sscanf(line, "Cached: %lld kB", &val) == 1) cached = val * 1024;
            else if (sscanf(line, "Slab: %lld kB", &val) == 1) slab = val * 1024;
            else if (sscanf(line, "PageTables: %lld kB", &val) == 1) pagetables = val * 1024;
        }
        fclose(fmem);
        if (mem_avail <= 0) mem_avail = mem_free + buffers + cached;
        mon->mem_total = mem_total;
        mon->mem_free = mem_avail;
        mon->mem_used = (mem_total > mem_avail) ? (mem_total - mem_avail) : 0;
        mon->mem_wired = slab + pagetables + buffers;
        if (mon->mem_wired > mon->mem_used) mon->mem_wired = mon->mem_used / 3;
        mon->mem_compressed = cached / 3;
        if (mon->mem_wired + mon->mem_compressed > mon->mem_used) {
            mon->mem_compressed = (mon->mem_used - mon->mem_wired) / 2;
        }
        mon->mem_other_used = mon->mem_used - mon->mem_wired - mon->mem_compressed;
    }

    FILE *fnet = fopen("/proc/net/dev", "r");
    if (fnet) {
        char line[512];
        uint64_t rx_sum = 0, tx_sum = 0;
        int line_no = 0;
        while (fgets(line, sizeof(line), fnet)) {
            line_no++;
            if (line_no <= 2) continue;
            char *colon = strchr(line, ':');
            if (!colon) continue;
            *colon = '\0';
            char *ifname = line;
            while (*ifname == ' ') ifname++;
            if (strcmp(ifname, "lo") == 0) continue;
            unsigned long long r_bytes = 0, r_pkt, r_err, r_drop, r_fifo, r_frame, r_comp, r_mcast, t_bytes = 0;
            if (sscanf(colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &r_bytes, &r_pkt, &r_err, &r_drop, &r_fifo, &r_frame, &r_comp, &r_mcast, &t_bytes) == 9) {
                rx_sum += r_bytes;
                tx_sum += t_bytes;
            }
        }
        fclose(fnet);
        if (!mon->initialized) {
            mon->base_net_rx = rx_sum;
            mon->base_net_tx = tx_sum;
            mon->prev_net_rx = rx_sum;
            mon->prev_net_tx = tx_sum;
        } else {
            uint64_t drx = (rx_sum >= mon->prev_net_rx) ? (rx_sum - mon->prev_net_rx) : 0;
            uint64_t dtx = (tx_sum >= mon->prev_net_tx) ? (tx_sum - mon->prev_net_tx) : 0;
            mon->net_down_per_sec = (int64_t)((double)drx / dt);
            mon->net_up_per_sec   = (int64_t)((double)dtx / dt);
            mon->net_session_in   = (int64_t)(rx_sum >= mon->base_net_rx ? (rx_sum - mon->base_net_rx) : rx_sum);
            mon->net_session_out  = (int64_t)(tx_sum >= mon->base_net_tx ? (tx_sum - mon->base_net_tx) : tx_sum);
            mon->prev_net_rx = rx_sum;
            mon->prev_net_tx = tx_sum;
        }
    }

    mon->volume_count = 0;
    FILE *fmounts = fopen("/proc/mounts", "r");
    if (fmounts) {
        char dev[256], mnt[256], fstype[64];
        char line[1024];
        while (fgets(line, sizeof(line), fmounts) && mon->volume_count < 16) {
            if (sscanf(line, "%255s %255s %63s", dev, mnt, fstype) == 3) {
                if (strncmp(dev, "/dev/", 5) != 0 || strncmp(dev, "/dev/loop", 9) == 0 ||
                    strncmp(mnt, "/proc", 5) == 0 || strncmp(mnt, "/sys", 4) == 0 ||
                    strncmp(mnt, "/dev", 4) == 0 || strncmp(mnt, "/run", 4) == 0 ||
                    strncmp(fstype, "fuse", 4) == 0 || strcmp(fstype, "autofs") == 0 ||
                    strcmp(fstype, "tmpfs") == 0 || strcmp(fstype, "cgroup2") == 0 ||
                    strcmp(fstype, "overlay") == 0 || strcmp(fstype, "squashfs") == 0) {
                    continue;
                }
                struct statvfs vfs;
                if (statvfs(mnt, &vfs) == 0 && vfs.f_blocks > 0) {
                    int64_t tot = (int64_t)vfs.f_blocks * (int64_t)vfs.f_frsize;
                    if (tot < 64 * 1024 * 1024) continue;
                    bool dup = false;
                    for (int i = 0; i < mon->volume_count; i++) {
                        if (strcmp(mon->volumes[i].mount_path, mnt) == 0) {
                            dup = true;
                            break;
                        }
                    }
                    if (dup) continue;
                    DDVolumeInfo *v = &mon->volumes[mon->volume_count++];
                    snprintf(v->mount_path, sizeof(v->mount_path), "%s", mnt);
                    if (strcmp(mnt, "/") == 0) {
                        snprintf(v->name, sizeof(v->name), "Root Filesystem (/)");
                    } else {
                        const char *slash = strrchr(mnt, '/');
                        snprintf(v->name, sizeof(v->name), "%s (%s)", (slash && slash[1]) ? slash + 1 : mnt, mnt);
                    }
                    v->total_bytes = tot;
                    v->free_bytes  = (int64_t)vfs.f_bavail * (int64_t)vfs.f_frsize;
                    v->used_bytes  = tot - (int64_t)vfs.f_bfree * (int64_t)vfs.f_frsize;
                    if (v->used_bytes < 0) v->used_bytes = tot - v->free_bytes;
                }
            }
        }
        fclose(fmounts);
    }
    if (mon->volume_count == 0) {
        dd_get_primary_volume("/", &mon->volumes[0]);
        mon->volume_count = 1;
    }

    DIR *proc_dir = opendir("/proc");
    if (proc_dir) {
        size_t cap = 512, count = 0;
        DDProcessInfo *all = (DDProcessInfo *)malloc(cap * sizeof(DDProcessInfo));
        long clk_tck = sysconf(_SC_CLK_TCK);
        if (clk_tck <= 0) clk_tck = 100;
        long page_sz = sysconf(_SC_PAGESIZE);
        if (page_sz <= 0) page_sz = 4096;

        struct dirent *de;
        while (all && (de = readdir(proc_dir)) != NULL) {
            if (!isdigit((unsigned char)de->d_name[0])) continue;
            int pid = atoi(de->d_name);
            if (pid <= 0) continue;

            char stat_path[128];
            snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
            FILE *pf = fopen(stat_path, "r");
            if (!pf) continue;
            char sbuf[1024];
            size_t nr = fread(sbuf, 1, sizeof(sbuf) - 1, pf);
            fclose(pf);
            if (nr == 0) continue;
            sbuf[nr] = '\0';

            char *lparen = strchr(sbuf, '(');
            char *rparen = strrchr(sbuf, ')');
            if (!lparen || !rparen || rparen <= lparen) continue;

            if (count >= cap) {
                cap *= 2;
                DDProcessInfo *grown = (DDProcessInfo *)realloc(all, cap * sizeof(DDProcessInfo));
                if (!grown) break;
                all = grown;
            }

            DDProcessInfo *pi = &all[count];
            memset(pi, 0, sizeof(*pi));
            pi->pid = pid;
            size_t nlen = (size_t)(rparen - lparen - 1);
            if (nlen >= sizeof(pi->name)) nlen = sizeof(pi->name) - 1;
            memcpy(pi->name, lparen + 1, nlen);
            pi->name[nlen] = '\0';

            unsigned long utime = 0, stime = 0;
            long rss_pages = 0;
            char *rest = rparen + 2;
            int field = 3;
            char *saveptr = NULL;
            for (char *tok = strtok_r(rest, " ", &saveptr); tok; tok = strtok_r(NULL, " ", &saveptr), field++) {
                if (field == 14) utime = strtoul(tok, NULL, 10);
                else if (field == 15) stime = strtoul(tok, NULL, 10);
                else if (field == 24) {
                    rss_pages = strtol(tok, NULL, 10);
                    break;
                }
            }
            uint64_t ticks = (uint64_t)utime + (uint64_t)stime;
            pi->raw_ticks = ticks;
            pi->rss_bytes = (rss_pages > 0) ? (int64_t)rss_pages * (int64_t)page_sz : 0;

            double cpu_pct = 0.0;
            for (int i = 0; i < mon->prev_proc_count; i++) {
                if (mon->prev_procs[i].pid == pid) {
                    if (ticks >= mon->prev_procs[i].ticks) {
                        uint64_t dticks = ticks - mon->prev_procs[i].ticks;
                        cpu_pct = ((double)dticks / (double)clk_tck) / dt * 100.0;
                    }
                    break;
                }
            }
            pi->cpu_percent = cpu_pct;
            count++;
        }
        closedir(proc_dir);

        if (all) {
            mon->prev_proc_count = (int)(count < 2048 ? count : 2048);
            for (int i = 0; i < mon->prev_proc_count; i++) {
                mon->prev_procs[i].pid = all[i].pid;
                mon->prev_procs[i].ticks = all[i].raw_ticks;
            }
            mon->total_proc_count = (int)count;

            qsort(all, count, sizeof(DDProcessInfo), cmp_proc_cpu);
            mon->procs_cpu_count = (int)(count < 60 ? count : 60);
            memcpy(mon->procs_by_cpu, all, (size_t)mon->procs_cpu_count * sizeof(DDProcessInfo));

            qsort(all, count, sizeof(DDProcessInfo), cmp_proc_mem);
            mon->procs_mem_count = (int)(count < 60 ? count : 60);
            memcpy(mon->procs_by_mem, all, (size_t)mon->procs_mem_count * sizeof(DDProcessInfo));

            free(all);
        }
    }
#endif

    double mem_pct = (mon->mem_total > 0) ? ((double)mon->mem_used * 100.0 / (double)mon->mem_total) : 0.0;
    double free_gb = (mon->volume_count > 0) ? ((double)mon->volumes[0].free_bytes / 1e9) : 0.0;

    if (mon->history_count < 120) {
        int idx = mon->history_count++;
        mon->cpu_history[idx]  = mon->cpu_total_pct;
        mon->mem_history[idx]  = mem_pct;
        mon->free_history[idx] = free_gb;
    } else {
        memmove(&mon->cpu_history[0],  &mon->cpu_history[1],  119 * sizeof(double));
        memmove(&mon->mem_history[0],  &mon->mem_history[1],  119 * sizeof(double));
        memmove(&mon->free_history[0], &mon->free_history[1], 119 * sizeof(double));
        mon->cpu_history[119]  = mon->cpu_total_pct;
        mon->mem_history[119]  = mem_pct;
        mon->free_history[119] = free_gb;
    }

    mon->prev_sample_time = now;
    mon->initialized = true;
}

static char g_clipboard_cache[1024] = "";

void dd_copy_to_clipboard(const char *text) {
    if (!text) return;
    snprintf(g_clipboard_cache, sizeof(g_clipboard_cache), "%s", text);
#ifdef _WIN32
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        size_t len = strlen(text) + 1;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
        if (hMem) {
            void *ptr = GlobalLock(hMem);
            if (ptr) {
                memcpy(ptr, text, len);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
        }
        CloseClipboard();
    }
#else
    int pfd[2];
    if (pipe(pfd) != 0) return;
    pid_t pid = fork();
    if (pid == 0) {
        close(pfd[1]);
        if (fork() == 0) {
            dup2(pfd[0], STDIN_FILENO);
            close(pfd[0]);
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
            execl("/bin/sh", "sh", "-c",
                  "xclip -selection clipboard 2>/dev/null || xsel --clipboard --input 2>/dev/null || wl-copy 2>/dev/null",
                  (char *)NULL);
            _exit(0);
        }
        close(pfd[0]);
        _exit(0);
    }
    close(pfd[0]);
    if (pid > 0) {
        size_t len = strlen(text);
        if (len > 0) {
            ssize_t wr = write(pfd[1], text, len);
            (void)wr;
        }
        close(pfd[1]);
        int status = 0;
        waitpid(pid, &status, 0);
    } else {
        close(pfd[1]);
    }
#endif
}

const char *dd_get_clipboard_cache(void) {
    return g_clipboard_cache;
}

void dd_reveal_in_file_manager(const char *path) {
    if (!path || !*path) return;
#ifdef _WIN32
    char param[1024];
    snprintf(param, sizeof(param), "/select,\"%s\"", path);
    ShellExecuteA(NULL, "open", "explorer.exe", param, NULL, SW_SHOWNORMAL);
#else
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    struct stat st;
    if (stat(dir, &st) == 0 && !S_ISDIR(st.st_mode)) {
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir) *slash = '\0';
    }
    pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
            execlp("xdg-open", "xdg-open", dir, (char *)NULL);
            _exit(0);
        }
        _exit(0);
    } else if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
    }
#endif
}

void dd_open_permissions_help(void) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", "ms-settings:privacy-broadfilesystemaccess", NULL, NULL, SW_SHOWNORMAL);
#else
    dd_reveal_in_file_manager("/");
#endif
}

bool dd_pick_folder_dialog(char *out_path, size_t cap) {
    if (!out_path || cap == 0) return false;
#ifdef _WIN32
    BROWSEINFOA bi;
    memset(&bi, 0, sizeof(bi));
    bi.lpszTitle = "Select a folder to scan with DiskSonar";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            snprintf(out_path, cap, "%s", path);
            CoTaskMemFree(pidl);
            return true;
        }
        CoTaskMemFree(pidl);
    }
    return false;
#else
    const char *env_pick = getenv("DISKSONAR_PICK_FOLDER");
    if (!env_pick || !*env_pick) env_pick = getenv("DISKDUDE_PICK_FOLDER");
    if (env_pick && *env_pick) {
        snprintf(out_path, cap, "%s", env_pick);
        return true;
    }
    FILE *fp = popen("zenity --file-selection --directory --title=\"Choose Folder to Scan\" 2>/dev/null || kdialog --getexistingdirectory . 2>/dev/null", "r");
    if (fp) {
        char buf[1024];
        if (fgets(buf, sizeof(buf), fp)) {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
            pclose(fp);
            if (len > 0) {
                snprintf(out_path, cap, "%s", buf);
                return true;
            }
        } else {
            pclose(fp);
        }
    }
    return false;
#endif
}

void dd_get_home_dir(char *out_path, size_t cap) {
#ifdef _WIN32
    const char *h = getenv("USERPROFILE");
    if (!h) h = "C:\\";
    snprintf(out_path, cap, "%s", h);
#else
    const char *h = getenv("HOME");
    if (!h || !*h) h = "/";
    snprintf(out_path, cap, "%s", h);
#endif
}

void dd_get_root_dir(char *out_path, size_t cap) {
#ifdef _WIN32
    snprintf(out_path, cap, "C:\\");
#else
    snprintf(out_path, cap, "/");
#endif
}
