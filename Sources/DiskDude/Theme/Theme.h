#ifndef DISKDUDE_THEME_H
#define DISKDUDE_THEME_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    double r;
    double g;
    double b;
    double a;
} DDColor;

typedef struct {
    bool is_dark;
    DDColor background;
    DDColor panel;
    DDColor card;
    DDColor border;
    DDColor control_border;
    DDColor divider;
    DDColor chip;
    DDColor track;
    DDColor text;
    DDColor text_secondary;
    DDColor text_tertiary;
    DDColor ink;
    DDColor on_ink;
    DDColor used;
    DDColor used_text;
    DDColor free_col;
    DDColor free_text;
    DDColor pastel_text_primary;
    DDColor pastel_text_secondary;
    DDColor focus_ring;
    DDColor categories[9];
    DDColor age_buckets[6];
    DDColor folder_families[8];
} DDTheme;

DDColor dd_color_hex(uint32_t rgb, double alpha);
DDColor dd_color_shade(DDColor base, double factor);
void dd_theme_init(DDTheme *theme, bool is_dark);

typedef struct {
    bool is_dark;
    bool has_theme_pref;
    char recent_paths[5][512];
    int recent_count;
} DDSettings;

void dd_settings_load(DDSettings *settings);
void dd_settings_save(const DDSettings *settings);
void dd_settings_add_recent(DDSettings *settings, const char *path);

#endif
