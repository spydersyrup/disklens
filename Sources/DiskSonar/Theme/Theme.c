#include "Theme.h"
#include "../Utils/Utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

DDColor dd_color_hex(uint32_t rgb, double alpha) {
    DDColor c;
    c.r = ((rgb >> 16) & 0xFF) / 255.0;
    c.g = ((rgb >> 8) & 0xFF) / 255.0;
    c.b = (rgb & 0xFF) / 255.0;
    c.a = alpha;
    return c;
}

DDColor dd_color_shade(DDColor base, double factor) {
    DDColor c = base;
    if (factor >= 0.0) {
        c.r = c.r + (1.0 - c.r) * factor;
        c.g = c.g + (1.0 - c.g) * factor;
        c.b = c.b + (1.0 - c.b) * factor;
    } else {
        double f = 1.0 + factor;
        if (f < 0.0) f = 0.0;
        c.r *= f;
        c.g *= f;
        c.b *= f;
    }
    return c;
}

void dd_theme_init(DDTheme *t, bool is_dark) {
    memset(t, 0, sizeof(*t));
    t->is_dark = is_dark;

    if (!is_dark) {
        t->background     = dd_color_hex(0xFFFFFF, 1.0);
        t->panel          = dd_color_hex(0xF5F5F7, 1.0);
        t->card           = dd_color_hex(0xFFFFFF, 1.0);
        t->border         = dd_color_hex(0xD8D8DD, 1.0);
        t->control_border = dd_color_hex(0x76767E, 1.0);
        t->divider        = dd_color_hex(0xE5E5EA, 1.0);
        t->chip           = dd_color_hex(0xF2F2F7, 1.0);
        t->track          = dd_color_hex(0xE5E5EA, 1.0);
        t->text           = dd_color_hex(0x1D1D1F, 1.0);
        t->text_secondary = dd_color_hex(0x525257, 1.0);
        t->text_tertiary  = dd_color_hex(0x595960, 1.0);
        t->ink            = dd_color_hex(0xFF6B4A, 1.0);
        t->on_ink         = dd_color_hex(0x0F172A, 1.0);
        t->used           = dd_color_hex(0xFF3B30, 1.0);
        t->used_text      = dd_color_hex(0xB91C1C, 1.0);
        t->free_col       = dd_color_hex(0x34C759, 1.0);
        t->free_text      = dd_color_hex(0x157330, 1.0);
        t->focus_ring     = dd_color_hex(0x0F172A, 1.0);
    } else {
        t->background     = dd_color_hex(0x1E1E20, 1.0);
        t->panel          = dd_color_hex(0x28282B, 1.0);
        t->card           = dd_color_hex(0x2C2C2E, 1.0);
        t->border         = dd_color_hex(0x3F3F43, 1.0);
        t->control_border = dd_color_hex(0x8E8E96, 1.0);
        t->divider        = dd_color_hex(0x38383A, 1.0);
        t->chip           = dd_color_hex(0x3A3A3C, 1.0);
        t->track          = dd_color_hex(0x3A3A3C, 1.0);
        t->text           = dd_color_hex(0xF5F5F7, 1.0);
        t->text_secondary = dd_color_hex(0xAEAEB2, 1.0);
        t->text_tertiary  = dd_color_hex(0x9E9EA4, 1.0);
        t->ink            = dd_color_hex(0xFF7A5C, 1.0);
        t->on_ink         = dd_color_hex(0x0F172A, 1.0);
        t->used           = dd_color_hex(0xFF453A, 1.0);
        t->used_text      = dd_color_hex(0xFF6B63, 1.0);
        t->free_col       = dd_color_hex(0x30D158, 1.0);
        t->free_text      = dd_color_hex(0x4ADE80, 1.0);
        t->focus_ring     = dd_color_hex(0xF5F5F7, 1.0);
    }

    t->pastel_text_primary   = dd_color_hex(0x0F172A, 1.0);
    t->pastel_text_secondary = dd_color_hex(0x1E293B, 1.0);

    t->categories[0] = dd_color_hex(0xFDA4AF, 1.0);
    t->categories[1] = dd_color_hex(0xC4B5FD, 1.0);
    t->categories[2] = dd_color_hex(0x7DD3FC, 1.0);
    t->categories[3] = dd_color_hex(0xFCD34D, 1.0);
    t->categories[4] = dd_color_hex(0x86EFAC, 1.0);
    t->categories[5] = dd_color_hex(0xFDBA74, 1.0);
    t->categories[6] = dd_color_hex(0x93C5FD, 1.0);
    t->categories[7] = dd_color_hex(0x5EEAD4, 1.0);
    t->categories[8] = dd_color_hex(0xCBD5E1, 1.0);

    t->age_buckets[0] = dd_color_hex(0x34D399, 1.0);
    t->age_buckets[1] = dd_color_hex(0x60A5FA, 1.0);
    t->age_buckets[2] = dd_color_hex(0x818CF8, 1.0);
    t->age_buckets[3] = dd_color_hex(0xA78BFA, 1.0);
    t->age_buckets[4] = dd_color_hex(0xF472B6, 1.0);
    t->age_buckets[5] = dd_color_hex(0xF87171, 1.0);

    t->folder_families[0] = dd_color_hex(0x93C5FD, 1.0);
    t->folder_families[1] = dd_color_hex(0x5EEAD4, 1.0);
    t->folder_families[2] = dd_color_hex(0xC4B5FD, 1.0);
    t->folder_families[3] = dd_color_hex(0xFCD34D, 1.0);
    t->folder_families[4] = dd_color_hex(0xFDA4AF, 1.0);
    t->folder_families[5] = dd_color_hex(0x86EFAC, 1.0);
    t->folder_families[6] = dd_color_hex(0x7DD3FC, 1.0);
    t->folder_families[7] = dd_color_hex(0xCBD5E1, 1.0);
}

static void get_settings_path(char *buf, size_t cap) {
    char dir[512];
    dd_get_data_dir("config", dir, sizeof(dir));
    snprintf(buf, cap, "%s/settings.conf", dir);
}

static bool detect_system_dark_mode(void) {
#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 1, sz = sizeof(val);
        if (RegQueryValueExA(hKey, "AppsUseLightTheme", NULL, NULL, (LPBYTE)&val, &sz) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return val == 0;
        }
        RegCloseKey(hKey);
    }
    return false;
#else
    const char *gtk_theme = getenv("GTK_THEME");
    if (gtk_theme && strstr(gtk_theme, "dark")) return true;
    const char *color_scheme = getenv("ADW_DEBUG_COLOR_SCHEME");
    if (color_scheme && strstr(color_scheme, "dark")) return true;
    return false;
#endif
}

void dd_settings_load(DDSettings *s) {
    memset(s, 0, sizeof(*s));
    s->is_dark = detect_system_dark_mode();
    s->has_theme_pref = false;

    char path[512];
    get_settings_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[600];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (strncmp(line, "theme=", 6) == 0) {
            s->has_theme_pref = true;
            s->is_dark = (strcmp(line + 6, "dark") == 0);
        } else if (strncmp(line, "recent=", 7) == 0 && s->recent_count < 5) {
            const char *p = line + 7;
            if (*p) {
                snprintf(s->recent_paths[s->recent_count], sizeof(s->recent_paths[0]), "%s", p);
                s->recent_count++;
            }
        }
    }
    fclose(f);
}

void dd_settings_save(const DDSettings *s) {
    char path[512], content[4096];
    get_settings_path(path, sizeof(path));
    size_t pos = 0;
    content[0] = '\0';
    if (s->has_theme_pref) {
        pos += (size_t)snprintf(content + pos, sizeof(content) - pos, "theme=%s\n", s->is_dark ? "dark" : "light");
    }
    for (int i = 0; i < s->recent_count && pos + 600 < sizeof(content); i++) {
        pos += (size_t)snprintf(content + pos, sizeof(content) - pos, "recent=%s\n", s->recent_paths[i]);
    }
    dd_write_file_atomic(path, content);
}

void dd_settings_add_recent(DDSettings *s, const char *path) {
    if (!path || !*path) return;
    int existing = -1;
    for (int i = 0; i < s->recent_count; i++) {
        if (strcmp(s->recent_paths[i], path) == 0) {
            existing = i;
            break;
        }
    }
    if (existing == 0) {
        dd_settings_save(s);
        return;
    }
    int limit = (existing > 0) ? existing : (s->recent_count < 5 ? s->recent_count : 4);
    if (existing < 0 && s->recent_count < 5) {
        s->recent_count++;
    }
    for (int i = limit; i > 0; i--) {
        snprintf(s->recent_paths[i], sizeof(s->recent_paths[0]), "%s", s->recent_paths[i - 1]);
    }
    snprintf(s->recent_paths[0], sizeof(s->recent_paths[0]), "%s", path);
    dd_settings_save(s);
}
