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
        /* Industrial Precision - Daylight Slate */
        t->background     = dd_color_hex(0xFAFAFA, 1.0); /* Zinc 50 canvas */
        t->panel          = dd_color_hex(0xF4F4F5, 1.0); /* Zinc 100 structural frame */
        t->card           = dd_color_hex(0xFFFFFF, 1.0); /* Pure card surface */
        t->border         = dd_color_hex(0xE4E4E7, 1.0); /* Zinc 200 hairline border */
        t->control_border = dd_color_hex(0xD4D4D8, 1.0); /* Zinc 300 interactive border */
        t->divider        = dd_color_hex(0xE4E4E7, 1.0); /* Zinc 200 divider */
        t->chip           = dd_color_hex(0xF4F4F5, 1.0); /* Zinc 100 chip */
        t->track          = dd_color_hex(0xE4E4E7, 1.0); /* Zinc 200 track */
        t->text           = dd_color_hex(0x18181B, 1.0); /* Zinc 900 high-contrast ink */
        t->text_secondary = dd_color_hex(0x52525B, 1.0); /* Zinc 600 metadata */
        t->text_tertiary  = dd_color_hex(0x71717A, 1.0); /* Zinc 500 captions */
        t->ink            = dd_color_hex(0xD97706, 1.0); /* Industrial Amber 600 accent */
        t->on_ink         = dd_color_hex(0xFFFFFF, 1.0); /* High contrast on accent */
        t->used           = dd_color_hex(0xDC2626, 1.0); /* Signal Red 600 */
        t->used_text      = dd_color_hex(0xB91C1C, 1.0);
        t->free_col       = dd_color_hex(0x059669, 1.0); /* Functional Emerald 600 */
        t->free_text      = dd_color_hex(0x047857, 1.0);
        t->focus_ring     = dd_color_hex(0xD97706, 1.0);
    } else {
        /* Industrial Precision - Dark Graphite Pro */
        t->background     = dd_color_hex(0x18181B, 1.0); /* Zinc 900 base canvas */
        t->panel          = dd_color_hex(0x121215, 1.0); /* Deep graphite frame */
        t->card           = dd_color_hex(0x202024, 1.0); /* Clean elevated surface */
        t->border         = dd_color_hex(0x2E2E33, 1.0); /* Hairline 1px border */
        t->control_border = dd_color_hex(0x3F3F46, 1.0); /* Zinc 700 interactive border */
        t->divider        = dd_color_hex(0x27272A, 1.0); /* Zinc 800 divider */
        t->chip           = dd_color_hex(0x27272A, 1.0); /* Zinc 800 chip */
        t->track          = dd_color_hex(0x27272A, 1.0); /* Zinc 800 meter track */
        t->text           = dd_color_hex(0xF4F4F5, 1.0); /* Zinc 100 crisp text */
        t->text_secondary = dd_color_hex(0xA1A1AA, 1.0); /* Zinc 400 secondary */
        t->text_tertiary  = dd_color_hex(0x71717A, 1.0); /* Zinc 500 tertiary */
        t->ink            = dd_color_hex(0xF59E0B, 1.0); /* Industrial Amber 500 accent */
        t->on_ink         = dd_color_hex(0x18181B, 1.0); /* High contrast on accent */
        t->used           = dd_color_hex(0xEF4444, 1.0); /* Signal Red 500 */
        t->used_text      = dd_color_hex(0xF87171, 1.0);
        t->free_col       = dd_color_hex(0x10B981, 1.0); /* Functional Emerald 500 */
        t->free_text      = dd_color_hex(0x34D399, 1.0);
        t->focus_ring     = dd_color_hex(0xF59E0B, 1.0);
    }

    if (!is_dark) {
        t->pastel_text_primary   = dd_color_hex(0x18181B, 1.0);
        t->pastel_text_secondary = dd_color_hex(0x52525B, 1.0);

        /* Soft architectural slate folder families */
        t->folder_families[0] = dd_color_hex(0xF1F5F9, 1.0);
        t->folder_families[1] = dd_color_hex(0xE2E8F0, 1.0);
        t->folder_families[2] = dd_color_hex(0xEDF2F7, 1.0);
        t->folder_families[3] = dd_color_hex(0xE8EEF5, 1.0);
        t->folder_families[4] = dd_color_hex(0xEEF2F6, 1.0);
        t->folder_families[5] = dd_color_hex(0xE5E7EB, 1.0);
        t->folder_families[6] = dd_color_hex(0xF3F4F6, 1.0);
        t->folder_families[7] = dd_color_hex(0xE2E8F0, 1.0);
    } else {
        t->pastel_text_primary   = dd_color_hex(0xF4F4F5, 1.0);
        t->pastel_text_secondary = dd_color_hex(0xA1A1AA, 1.0);

        /* Tonal graphite folder families */
        t->folder_families[0] = dd_color_hex(0x27272A, 1.0);
        t->folder_families[1] = dd_color_hex(0x2E2E33, 1.0);
        t->folder_families[2] = dd_color_hex(0x323238, 1.0);
        t->folder_families[3] = dd_color_hex(0x2A2A30, 1.0);
        t->folder_families[4] = dd_color_hex(0x28282E, 1.0);
        t->folder_families[5] = dd_color_hex(0x26262B, 1.0);
        t->folder_families[6] = dd_color_hex(0x242429, 1.0);
        t->folder_families[7] = dd_color_hex(0x222227, 1.0);
    }

    /* Architectural technical category spectrum (subdued, cohesive, zero candy neon) */
    t->categories[CAT_VIDEO]     = dd_color_hex(0x4B6584, 1.0); /* Video: Steel Slate */
    t->categories[CAT_AUDIO]     = dd_color_hex(0x575FCF, 1.0); /* Audio: Muted Indigo */
    t->categories[CAT_IMAGE]     = dd_color_hex(0x9E6B55, 1.0); /* Images: Warm Copper */
    t->categories[CAT_DOCUMENT]  = dd_color_hex(0x9B783E, 1.0); /* Documents: Muted Ochre */
    t->categories[CAT_DEVELOPER] = dd_color_hex(0x3B7A7A, 1.0); /* Developer / Code: Technical Teal */
    t->categories[CAT_ARCHIVE]   = dd_color_hex(0x7C6A59, 1.0); /* Archives: Raw Umber */
    t->categories[CAT_APPS]      = dd_color_hex(0xD97706, 1.0); /* Apps: Amber Industrial */
    t->categories[CAT_SYSTEM]    = dd_color_hex(0x596275, 1.0); /* System: Iron Gray */
    t->categories[CAT_OTHER]     = dd_color_hex(0x4A4E5A, 1.0); /* Other: Deep Charcoal */

    /* Monochromatic thermal age steps */
    t->age_buckets[0] = dd_color_hex(0x3B7A7A, 1.0); /* < 7d: Recent teal */
    t->age_buckets[1] = dd_color_hex(0x4B6584, 1.0); /* 7-30d: Active steel */
    t->age_buckets[2] = dd_color_hex(0x64748B, 1.0); /* 1-3m: Settled slate */
    t->age_buckets[3] = dd_color_hex(0x475569, 1.0); /* 3-12m: Cool slate */
    t->age_buckets[4] = dd_color_hex(0x334155, 1.0); /* 1-2y: Cold slate */
    t->age_buckets[5] = dd_color_hex(0x1E293B, 1.0); /* > 2y: Dormant dark slate */
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
    s->is_dark = true; /* Default to Dark Graphite Pro */
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
