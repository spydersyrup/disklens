#ifndef DISKSONAR_CANVAS_H
#define DISKSONAR_CANVAS_H

#include "../Theme/Theme.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    double x;
    double y;
    double w;
    double h;
} DDRect;

typedef enum {
    ICON_FOLDER = 0,
    ICON_VIDEO,
    ICON_AUDIO,
    ICON_IMAGE,
    ICON_DOCUMENT,
    ICON_DEVELOPER,
    ICON_ARCHIVE,
    ICON_APPS,
    ICON_SYSTEM,
    ICON_OTHER,
    ICON_SEARCH,
    ICON_TRASH,
    ICON_SUN,
    ICON_MOON,
    ICON_SIDEBAR_RIGHT,
    ICON_LOCK,
    ICON_REVEAL,
    ICON_COPY,
    ICON_CHECK,
    ICON_CLOSE,
    ICON_CHEVRON_LEFT,
    ICON_CHEVRON_RIGHT,
    ICON_CHEVRON_DOWN,
    ICON_REFRESH,
    ICON_TRAY,
    ICON_DOWNLOADS,
    ICON_CACHE,
    ICON_VM,
    ICON_PLUS,
    ICON_EYE
} DDIconType;

typedef struct {
    int id;
    int action;
    int param_int;
    int param_int2;
    char param_str[512];
    char label[128];
    DDRect rect;
    bool focusable;
} DDHitRegion;

typedef struct {
    int cell_node_idx;
    DDRect rect;
    double cx, cy, r_in, r_out, a0, a1;
    bool is_arc;
    bool is_circle;
} DDVizHitItem;

typedef struct DDCanvas DDCanvas;

DDCanvas *dd_canvas_create(int width, int height);
void dd_canvas_resize(DDCanvas *cv, int width, int height);
void dd_canvas_destroy(DDCanvas *cv);

int dd_canvas_width(const DDCanvas *cv);
int dd_canvas_height(const DDCanvas *cv);
uint32_t *dd_canvas_pixels(DDCanvas *cv);
bool dd_canvas_save_png(DDCanvas *cv, const char *filename);

void dd_canvas_begin_frame(DDCanvas *cv, DDColor bg);
void dd_canvas_end_frame(DDCanvas *cv);

void dd_canvas_push_clip(DDCanvas *cv, DDRect r);
void dd_canvas_pop_clip(DDCanvas *cv);

void dd_canvas_fill_rect(DDCanvas *cv, DDRect r, DDColor col);
void dd_canvas_round_rect(DDCanvas *cv, DDRect r, double radius, DDColor fill, DDColor stroke, double stroke_w);
void dd_canvas_folder_card(DDCanvas *cv, DDRect r, DDColor fill, DDColor stroke, bool hovered);
void dd_canvas_line(DDCanvas *cv, double x1, double y1, double x2, double y2, DDColor col, double width);
void dd_canvas_bezier(DDCanvas *cv, double x1, double y1, double cx1, double cy1, double cx2, double cy2, double x2, double y2, DDColor col, double width);
void dd_canvas_circle(DDCanvas *cv, double cx, double cy, double radius, DDColor fill, DDColor stroke, double stroke_w);
void dd_canvas_arc_ring(DDCanvas *cv, double cx, double cy, double r_inner, double r_outer, double a_start, double a_end, DDColor fill, DDColor stroke);

void dd_canvas_text(DDCanvas *cv, double x, double y, const char *text, double size, bool bold, bool mono, DDColor col);
void dd_canvas_text_Right(DDCanvas *cv, double right_x, double y, const char *text, double size, bool bold, bool mono, DDColor col);
void dd_canvas_text_center(DDCanvas *cv, DDRect box, const char *text, double size, bool bold, bool mono, DDColor col);
void dd_canvas_text_clipped(DDCanvas *cv, DDRect box, const char *text, double size, bool bold, bool mono, DDColor col);
double dd_canvas_measure_text(DDCanvas *cv, const char *text, double size, bool bold, bool mono);

void dd_canvas_draw_icon(DDCanvas *cv, DDIconType icon, double x, double y, double size, DDColor col);

void dd_canvas_clear_hits(DDCanvas *cv);
int dd_canvas_register_hit(DDCanvas *cv, int action, int param_int, int param_int2, const char *param_str, const char *label, DDRect rect, bool focusable);
int dd_canvas_hit_test(const DDCanvas *cv, double mx, double my);
const DDHitRegion *dd_canvas_get_hit(const DDCanvas *cv, int idx);
int dd_canvas_hit_count(const DDCanvas *cv);

void dd_canvas_clear_viz_hits(DDCanvas *cv);
void dd_canvas_add_viz_rect(DDCanvas *cv, int32_t node_idx, DDRect r);
void dd_canvas_add_viz_circle(DDCanvas *cv, int32_t node_idx, double cx, double cy, double r);
void dd_canvas_add_viz_arc(DDCanvas *cv, int32_t node_idx, double cx, double cy, double r_in, double r_out, double a0, double a1);
int dd_canvas_viz_hit_test(const DDCanvas *cv, double mx, double my);
const DDVizHitItem *dd_canvas_get_viz_hit(const DDCanvas *cv, int idx);

#endif
