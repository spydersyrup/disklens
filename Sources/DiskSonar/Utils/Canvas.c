#define _GNU_SOURCE
#include "Canvas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifndef _WIN32
typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;
typedef struct {
    double x_bearing;
    double y_bearing;
    double width;
    double height;
    double x_advance;
    double y_advance;
} cairo_text_extents_t;

typedef cairo_surface_t *(*fn_cairo_image_surface_create_for_data)(unsigned char *, int, int, int, int);
typedef void (*fn_cairo_surface_destroy)(cairo_surface_t *);
typedef void (*fn_cairo_surface_flush)(cairo_surface_t *);
typedef int (*fn_cairo_surface_write_to_png)(cairo_surface_t *, const char *);
typedef cairo_t *(*fn_cairo_create)(cairo_surface_t *);
typedef void (*fn_cairo_destroy)(cairo_t *);
typedef void (*fn_cairo_save)(cairo_t *);
typedef void (*fn_cairo_restore)(cairo_t *);
typedef void (*fn_cairo_new_path)(cairo_t *);
typedef void (*fn_cairo_close_path)(cairo_t *);
typedef void (*fn_cairo_new_sub_path)(cairo_t *);
typedef void (*fn_cairo_move_to)(cairo_t *, double, double);
typedef void (*fn_cairo_line_to)(cairo_t *, double, double);
typedef void (*fn_cairo_curve_to)(cairo_t *, double, double, double, double, double, double);
typedef void (*fn_cairo_arc)(cairo_t *, double, double, double, double, double);
typedef void (*fn_cairo_arc_negative)(cairo_t *, double, double, double, double, double);
typedef void (*fn_cairo_rectangle)(cairo_t *, double, double, double, double);
typedef void (*fn_cairo_clip)(cairo_t *);
typedef void (*fn_cairo_fill)(cairo_t *);
typedef void (*fn_cairo_fill_preserve)(cairo_t *);
typedef void (*fn_cairo_stroke)(cairo_t *);
typedef void (*fn_cairo_set_line_width)(cairo_t *, double);
typedef void (*fn_cairo_set_source_rgba)(cairo_t *, double, double, double, double);
typedef void (*fn_cairo_select_font_face)(cairo_t *, const char *, int, int);
typedef void (*fn_cairo_set_font_size)(cairo_t *, double);
typedef void (*fn_cairo_show_text)(cairo_t *, const char *);
typedef void (*fn_cairo_text_extents)(cairo_t *, const char *, cairo_text_extents_t *);

static struct {
    void *handle;
    fn_cairo_image_surface_create_for_data image_surface_create_for_data;
    fn_cairo_surface_destroy surface_destroy;
    fn_cairo_surface_flush surface_flush;
    fn_cairo_surface_write_to_png surface_write_to_png;
    fn_cairo_create create;
    fn_cairo_destroy destroy;
    fn_cairo_save save;
    fn_cairo_restore restore;
    fn_cairo_new_path new_path;
    fn_cairo_close_path close_path;
    fn_cairo_new_sub_path new_sub_path;
    fn_cairo_move_to move_to;
    fn_cairo_line_to line_to;
    fn_cairo_curve_to curve_to;
    fn_cairo_arc arc;
    fn_cairo_arc_negative arc_negative;
    fn_cairo_rectangle rectangle;
    fn_cairo_clip clip;
    fn_cairo_fill fill;
    fn_cairo_fill_preserve fill_preserve;
    fn_cairo_stroke stroke;
    fn_cairo_set_line_width set_line_width;
    fn_cairo_set_source_rgba set_source_rgba;
    fn_cairo_select_font_face select_font_face;
    fn_cairo_set_font_size set_font_size;
    fn_cairo_show_text show_text;
    fn_cairo_text_extents text_extents;
} C;

static bool init_cairo_lib(void) {
    if (C.handle) return true;
    C.handle = dlopen("libcairo.so.2", RTLD_LAZY);
    if (!C.handle) return false;
    *(void **)(&C.image_surface_create_for_data) = dlsym(C.handle, "cairo_image_surface_create_for_data");
    *(void **)(&C.surface_destroy) = dlsym(C.handle, "cairo_surface_destroy");
    *(void **)(&C.surface_flush) = dlsym(C.handle, "cairo_surface_flush");
    *(void **)(&C.surface_write_to_png) = dlsym(C.handle, "cairo_surface_write_to_png");
    *(void **)(&C.create) = dlsym(C.handle, "cairo_create");
    *(void **)(&C.destroy) = dlsym(C.handle, "cairo_destroy");
    *(void **)(&C.save) = dlsym(C.handle, "cairo_save");
    *(void **)(&C.restore) = dlsym(C.handle, "cairo_restore");
    *(void **)(&C.new_path) = dlsym(C.handle, "cairo_new_path");
    *(void **)(&C.close_path) = dlsym(C.handle, "cairo_close_path");
    *(void **)(&C.new_sub_path) = dlsym(C.handle, "cairo_new_sub_path");
    *(void **)(&C.move_to) = dlsym(C.handle, "cairo_move_to");
    *(void **)(&C.line_to) = dlsym(C.handle, "cairo_line_to");
    *(void **)(&C.curve_to) = dlsym(C.handle, "cairo_curve_to");
    *(void **)(&C.arc) = dlsym(C.handle, "cairo_arc");
    *(void **)(&C.arc_negative) = dlsym(C.handle, "cairo_arc_negative");
    *(void **)(&C.rectangle) = dlsym(C.handle, "cairo_rectangle");
    *(void **)(&C.clip) = dlsym(C.handle, "cairo_clip");
    *(void **)(&C.fill) = dlsym(C.handle, "cairo_fill");
    *(void **)(&C.fill_preserve) = dlsym(C.handle, "cairo_fill_preserve");
    *(void **)(&C.stroke) = dlsym(C.handle, "cairo_stroke");
    *(void **)(&C.set_line_width) = dlsym(C.handle, "cairo_set_line_width");
    *(void **)(&C.set_source_rgba) = dlsym(C.handle, "cairo_set_source_rgba");
    *(void **)(&C.select_font_face) = dlsym(C.handle, "cairo_select_font_face");
    *(void **)(&C.set_font_size) = dlsym(C.handle, "cairo_set_font_size");
    *(void **)(&C.show_text) = dlsym(C.handle, "cairo_show_text");
    *(void **)(&C.text_extents) = dlsym(C.handle, "cairo_text_extents");
    return C.create != NULL;
}
#endif

struct DDCanvas {
    int width;
    int height;
    uint32_t *pixels;
    DDRect clip_stack[16];
    int clip_depth;

#ifdef _WIN32
    HDC hdc;
    HBITMAP hbm;
#else
    cairo_surface_t *surface;
    cairo_t *cr;
#endif

    DDHitRegion hits[1024];
    int hit_count;

    DDVizHitItem *viz_hits;
    int viz_hit_count;
    int viz_hit_cap;
};

static void alloc_surface(DDCanvas *cv, int w, int h) {
    if (w < 64) w = 64;
    if (h < 64) h = 64;
    cv->width = w;
    cv->height = h;
    cv->clip_depth = 0;
    cv->clip_stack[0] = (DDRect){0, 0, (double)w, (double)h};

#ifdef _WIN32
    if (cv->hbm) DeleteObject(cv->hbm);
    if (!cv->hdc) cv->hdc = CreateCompatibleDC(NULL);
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    cv->hbm = CreateDIBSection(cv->hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(cv->hdc, cv->hbm);
    SetBkMode(cv->hdc, TRANSPARENT);
    cv->pixels = (uint32_t *)bits;
#else
    if (cv->cr) C.destroy(cv->cr);
    if (cv->surface) C.surface_destroy(cv->surface);
    free(cv->pixels);
    cv->pixels = (uint32_t *)calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (init_cairo_lib() && cv->pixels) {
        cv->surface = C.image_surface_create_for_data((unsigned char *)cv->pixels, 0, w, h, w * 4);
        cv->cr = C.create(cv->surface);
    }
#endif
}

DDCanvas *dd_canvas_create(int width, int height) {
    DDCanvas *cv = (DDCanvas *)calloc(1, sizeof(DDCanvas));
    if (!cv) return NULL;
    cv->viz_hit_cap = 2048;
    cv->viz_hits = (DDVizHitItem *)calloc((size_t)cv->viz_hit_cap, sizeof(DDVizHitItem));
    alloc_surface(cv, width, height);
    return cv;
}

void dd_canvas_resize(DDCanvas *cv, int width, int height) {
    if (!cv) return;
    if (cv->width == width && cv->height == height) return;
    alloc_surface(cv, width, height);
}

void dd_canvas_destroy(DDCanvas *cv) {
    if (!cv) return;
#ifdef _WIN32
    if (cv->hbm) DeleteObject(cv->hbm);
    if (cv->hdc) DeleteDC(cv->hdc);
#else
    if (cv->cr) C.destroy(cv->cr);
    if (cv->surface) C.surface_destroy(cv->surface);
    free(cv->pixels);
#endif
    free(cv->viz_hits);
    free(cv);
}

int dd_canvas_width(const DDCanvas *cv)  { return cv ? cv->width : 0; }
int dd_canvas_height(const DDCanvas *cv) { return cv ? cv->height : 0; }
uint32_t *dd_canvas_pixels(DDCanvas *cv) { return cv ? cv->pixels : NULL; }

bool dd_canvas_save_png(DDCanvas *cv, const char *filename) {
    if (!cv || !filename) return false;
#ifndef _WIN32
    if (cv->surface && C.surface_write_to_png) {
        C.surface_flush(cv->surface);
        return C.surface_write_to_png(cv->surface, filename) == 0;
    }
#endif
    return false;
}

#ifdef _WIN32
static void blend_pixel(DDCanvas *cv, int x, int y, DDColor c) {
    DDRect cl = cv->clip_stack[cv->clip_depth];
    if (x < (int)cl.x || y < (int)cl.y || x >= (int)(cl.x + cl.w) || y >= (int)(cl.y + cl.h)) return;
    if (x < 0 || y < 0 || x >= cv->width || y >= cv->height) return;
    uint32_t *dst = &cv->pixels[y * cv->width + x];
    uint32_t sr = (uint32_t)(c.r * 255.0), sg = (uint32_t)(c.g * 255.0), sb = (uint32_t)(c.b * 255.0);
    if (c.a >= 0.995) {
        *dst = (0xFFu << 24) | (sr << 16) | (sg << 8) | sb;
        return;
    }
    uint32_t d = *dst;
    double a = c.a;
    uint32_t dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
    uint32_t r = (uint32_t)(sr * a + dr * (1.0 - a));
    uint32_t g = (uint32_t)(sg * a + dg * (1.0 - a));
    uint32_t b = (uint32_t)(sb * a + db * (1.0 - a));
    *dst = (0xFFu << 24) | (r << 16) | (g << 8) | b;
}
#endif

void dd_canvas_begin_frame(DDCanvas *cv, DDColor bg) {
    if (!cv) return;
    cv->clip_depth = 0;
    cv->clip_stack[0] = (DDRect){0, 0, (double)cv->width, (double)cv->height};
    dd_canvas_clear_hits(cv);
    dd_canvas_fill_rect(cv, cv->clip_stack[0], bg);
}

void dd_canvas_end_frame(DDCanvas *cv) {
    if (!cv) return;
#ifndef _WIN32
    if (cv->surface && C.surface_flush) C.surface_flush(cv->surface);
#endif
}

void dd_canvas_push_clip(DDCanvas *cv, DDRect r) {
    if (!cv) return;
    if (cv->clip_depth < 15) {
        DDRect prev = cv->clip_stack[cv->clip_depth];
        double x0 = r.x > prev.x ? r.x : prev.x;
        double y0 = r.y > prev.y ? r.y : prev.y;
        double x1 = (r.x + r.w) < (prev.x + prev.w) ? (r.x + r.w) : (prev.x + prev.w);
        double y1 = (r.y + r.h) < (prev.y + prev.h) ? (r.y + r.h) : (prev.y + prev.h);
        cv->clip_depth++;
        cv->clip_stack[cv->clip_depth] = (DDRect){x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
    }
#ifndef _WIN32
    if (cv->cr) {
        C.save(cv->cr);
        C.new_path(cv->cr);
        C.rectangle(cv->cr, r.x, r.y, r.w, r.h);
        C.clip(cv->cr);
    }
#endif
}

void dd_canvas_pop_clip(DDCanvas *cv) {
    if (!cv) return;
    if (cv->clip_depth > 0) cv->clip_depth--;
#ifndef _WIN32
    if (cv->cr) C.restore(cv->cr);
#endif
}

void dd_canvas_fill_rect(DDCanvas *cv, DDRect r, DDColor col) {
    if (!cv || r.w <= 0 || r.h <= 0 || col.a <= 0.0) return;
#ifdef _WIN32
    int x0 = (int)r.x, y0 = (int)r.y, x1 = (int)(r.x + r.w), y1 = (int)(r.y + r.h);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            blend_pixel(cv, x, y, col);
        }
    }
#else
    if (!cv->cr) return;
    C.new_path(cv->cr);
    C.rectangle(cv->cr, r.x, r.y, r.w, r.h);
    C.set_source_rgba(cv->cr, col.r, col.g, col.b, col.a);
    C.fill(cv->cr);
#endif
}

#ifndef _WIN32
static void cairo_rounded_path(cairo_t *cr, DDRect r, double rad) {
    if (rad <= 0.5) {
        C.new_path(cr);
        C.rectangle(cr, r.x, r.y, r.w, r.h);
        return;
    }
    if (rad > r.w * 0.5) rad = r.w * 0.5;
    if (rad > r.h * 0.5) rad = r.h * 0.5;
    C.new_sub_path(cr);
    C.arc(cr, r.x + r.w - rad, r.y + rad, rad, -M_PI * 0.5, 0);
    C.arc(cr, r.x + r.w - rad, r.y + r.h - rad, rad, 0, M_PI * 0.5);
    C.arc(cr, r.x + rad, r.y + r.h - rad, rad, M_PI * 0.5, M_PI);
    C.arc(cr, r.x + rad, r.y + rad, rad, M_PI, M_PI * 1.5);
    C.close_path(cr);
}
#endif

void dd_canvas_round_rect(DDCanvas *cv, DDRect r, double radius, DDColor fill, DDColor stroke, double stroke_w) {
    if (!cv || r.w <= 0 || r.h <= 0) return;
#ifdef _WIN32
    if (fill.a > 0.0) dd_canvas_fill_rect(cv, r, fill);
    if (stroke.a > 0.0 && stroke_w > 0.0) {
        dd_canvas_fill_rect(cv, (DDRect){r.x, r.y, r.w, stroke_w}, stroke);
        dd_canvas_fill_rect(cv, (DDRect){r.x, r.y + r.h - stroke_w, r.w, stroke_w}, stroke);
        dd_canvas_fill_rect(cv, (DDRect){r.x, r.y, stroke_w, r.h}, stroke);
        dd_canvas_fill_rect(cv, (DDRect){r.x + r.w - stroke_w, r.y, stroke_w, r.h}, stroke);
    }
#else
    if (!cv->cr) return;
    C.new_path(cv->cr);
    cairo_rounded_path(cv->cr, r, radius);
    if (fill.a > 0.0) {
        C.set_source_rgba(cv->cr, fill.r, fill.g, fill.b, fill.a);
        if (stroke.a > 0.0 && stroke_w > 0.0) C.fill_preserve(cv->cr);
        else C.fill(cv->cr);
    }
    if (stroke.a > 0.0 && stroke_w > 0.0) {
        C.set_source_rgba(cv->cr, stroke.r, stroke.g, stroke.b, stroke.a);
        C.set_line_width(cv->cr, stroke_w);
        C.stroke(cv->cr);
    }
#endif
}

void dd_canvas_folder_card(DDCanvas *cv, DDRect r, DDColor fill, DDColor stroke, bool hovered) {
    if (!cv || r.w <= 10 || r.h <= 10) return;
    DDRect dr = r;
    if (hovered) {
        dr.y -= 2.0;
        DDRect sh = {r.x + 2, r.y + 4, r.w, r.h - 2};
        dd_canvas_round_rect(cv, sh, 4.0, dd_color_hex(0x000000, 0.18), dd_color_hex(0, 0), 0);
    }
#ifdef _WIN32
    DDRect tab = {dr.x, dr.y, dr.w * 0.38, 12.0};
    DDRect body = {dr.x, dr.y + 8.0, dr.w, dr.h - 8.0};
    dd_canvas_round_rect(cv, tab, 4.0, fill, stroke, 1.0);
    dd_canvas_round_rect(cv, body, 4.0, fill, stroke, 1.0);
#else
    if (!cv->cr) return;
    double tab_w = dr.w * 0.36;
    if (tab_w > 96.0) tab_w = 96.0;
    double tab_h = 9.0;
    double rad = 4.0;

    C.new_path(cv->cr);
    C.move_to(cv->cr, dr.x + rad, dr.y);
    C.line_to(cv->cr, dr.x + tab_w - 4.0, dr.y);
    C.line_to(cv->cr, dr.x + tab_w + 4.0, dr.y + tab_h);
    C.line_to(cv->cr, dr.x + dr.w - rad, dr.y + tab_h);
    C.arc(cv->cr, dr.x + dr.w - rad, dr.y + tab_h + rad, rad, -M_PI * 0.5, 0);
    C.arc(cv->cr, dr.x + dr.w - rad, dr.y + dr.h - rad, rad, 0, M_PI * 0.5);
    C.arc(cv->cr, dr.x + rad, dr.y + dr.h - rad, rad, M_PI * 0.5, M_PI);
    C.arc(cv->cr, dr.x + rad, dr.y + rad, rad, M_PI, M_PI * 1.5);
    C.close_path(cv->cr);

    C.set_source_rgba(cv->cr, fill.r, fill.g, fill.b, fill.a);
    C.fill_preserve(cv->cr);
    C.set_source_rgba(cv->cr, stroke.r, stroke.g, stroke.b, stroke.a);
    C.set_line_width(cv->cr, hovered ? 1.5 : 1.0);
    C.stroke(cv->cr);
#endif
}

void dd_canvas_line(DDCanvas *cv, double x1, double y1, double x2, double y2, DDColor col, double width) {
    if (!cv || col.a <= 0.0 || width <= 0.0) return;
#ifdef _WIN32
    int steps = (int)fmax(fabs(x2 - x1), fabs(y2 - y1));
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        double t = (double)i / (double)steps;
        blend_pixel(cv, (int)(x1 + (x2 - x1) * t), (int)(y1 + (y2 - y1) * t), col);
    }
#else
    if (!cv->cr) return;
    C.new_path(cv->cr);
    C.move_to(cv->cr, x1, y1);
    C.line_to(cv->cr, x2, y2);
    C.set_source_rgba(cv->cr, col.r, col.g, col.b, col.a);
    C.set_line_width(cv->cr, width);
    C.stroke(cv->cr);
#endif
}

void dd_canvas_bezier(DDCanvas *cv, double x1, double y1, double cx1, double cy1, double cx2, double cy2, double x2, double y2, DDColor col, double width) {
    if (!cv || col.a <= 0.0) return;
#ifdef _WIN32
    double px = x1, py = y1;
    for (int i = 1; i <= 24; i++) {
        double t = (double)i / 24.0;
        double u = 1.0 - t;
        double nx = u*u*u*x1 + 3*u*u*t*cx1 + 3*u*t*t*cx2 + t*t*t*x2;
        double ny = u*u*u*y1 + 3*u*u*t*cy1 + 3*u*t*t*cy2 + t*t*t*y2;
        dd_canvas_line(cv, px, py, nx, ny, col, width);
        px = nx; py = ny;
    }
#else
    if (!cv->cr) return;
    C.new_path(cv->cr);
    C.move_to(cv->cr, x1, y1);
    C.curve_to(cv->cr, cx1, cy1, cx2, cy2, x2, y2);
    C.set_source_rgba(cv->cr, col.r, col.g, col.b, col.a);
    C.set_line_width(cv->cr, width);
    C.stroke(cv->cr);
#endif
}

void dd_canvas_circle(DDCanvas *cv, double cx, double cy, double radius, DDColor fill, DDColor stroke, double stroke_w) {
    if (!cv || radius <= 0.0) return;
#ifdef _WIN32
    int x0 = (int)(cx - radius - 1), x1 = (int)(cx + radius + 1);
    int y0 = (int)(cy - radius - 1), y1 = (int)(cy + radius + 1);
    double r2 = radius * radius;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            double dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= r2 && fill.a > 0) blend_pixel(cv, x, y, fill);
        }
    }
#else
    if (!cv->cr) return;
    C.new_path(cv->cr);
    C.arc(cv->cr, cx, cy, radius, 0, 2.0 * M_PI);
    if (fill.a > 0.0) {
        C.set_source_rgba(cv->cr, fill.r, fill.g, fill.b, fill.a);
        if (stroke.a > 0.0 && stroke_w > 0.0) C.fill_preserve(cv->cr);
        else C.fill(cv->cr);
    }
    if (stroke.a > 0.0 && stroke_w > 0.0) {
        C.set_source_rgba(cv->cr, stroke.r, stroke.g, stroke.b, stroke.a);
        C.set_line_width(cv->cr, stroke_w);
        C.stroke(cv->cr);
    }
#endif
}

void dd_canvas_arc_ring(DDCanvas *cv, double cx, double cy, double r_inner, double r_outer, double a_start, double a_end, DDColor fill, DDColor stroke) {
    if (!cv || r_outer <= r_inner || a_end <= a_start) return;
#ifdef _WIN32
    int x0 = (int)(cx - r_outer - 1), x1 = (int)(cx + r_outer + 1);
    int y0 = (int)(cy - r_outer - 1), y1 = (int)(cy + r_outer + 1);
    double ri2 = r_inner * r_inner, ro2 = r_outer * r_outer;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            double dx = x - cx, dy = y - cy;
            double d2 = dx * dx + dy * dy;
            if (d2 >= ri2 && d2 <= ro2) {
                double a = atan2(dy, dx);
                while (a < a_start) a += 2.0 * M_PI;
                if (a <= a_end) blend_pixel(cv, x, y, fill);
            }
        }
    }
#else
    if (!cv->cr) return;
    C.new_path(cv->cr);
    C.arc(cv->cr, cx, cy, r_outer, a_start, a_end);
    C.arc_negative(cv->cr, cx, cy, r_inner, a_end, a_start);
    C.close_path(cv->cr);
    if (fill.a > 0.0) {
        C.set_source_rgba(cv->cr, fill.r, fill.g, fill.b, fill.a);
        if (stroke.a > 0.0) C.fill_preserve(cv->cr);
        else C.fill(cv->cr);
    }
    if (stroke.a > 0.0) {
        C.set_source_rgba(cv->cr, stroke.r, stroke.g, stroke.b, stroke.a);
        C.set_line_width(cv->cr, 1.0);
        C.stroke(cv->cr);
    }
#endif
}

double dd_canvas_measure_text(DDCanvas *cv, const char *text, double size, bool bold, bool mono) {
    if (!text || !*text) return 0.0;
#ifndef _WIN32
    if (cv && cv->cr) {
        C.select_font_face(cv->cr, mono ? "monospace" : "sans-serif", 0, bold ? 1 : 0);
        C.set_font_size(cv->cr, size);
        cairo_text_extents_t ext;
        memset(&ext, 0, sizeof(ext));
        C.text_extents(cv->cr, text, &ext);
        return ext.x_advance > ext.width ? ext.x_advance : ext.width;
    }
#endif
    return (double)strlen(text) * size * (mono ? 0.60 : 0.54);
}

void dd_canvas_text(DDCanvas *cv, double x, double y, const char *text, double size, bool bold, bool mono, DDColor col) {
    if (!cv || !text || !*text || col.a <= 0.0) return;
#ifdef _WIN32
    HFONT hf = CreateFontA(-(int)size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, mono ? "Consolas" : "Segoe UI");
    HFONT old = (HFONT)SelectObject(cv->hdc, hf);
    SetTextColor(cv->hdc, RGB((int)(col.r * 255), (int)(col.g * 255), (int)(col.b * 255)));
    TextOutA(cv->hdc, (int)x, (int)(y - size), text, (int)strlen(text));
    SelectObject(cv->hdc, old);
    DeleteObject(hf);
#else
    if (!cv->cr) return;
    C.select_font_face(cv->cr, mono ? "monospace" : "sans-serif", 0, bold ? 1 : 0);
    C.set_font_size(cv->cr, size);
    C.set_source_rgba(cv->cr, col.r, col.g, col.b, col.a);
    C.move_to(cv->cr, x, y);
    C.show_text(cv->cr, text);
#endif
}

void dd_canvas_text_Right(DDCanvas *cv, double right_x, double y, const char *text, double size, bool bold, bool mono, DDColor col) {
    double w = dd_canvas_measure_text(cv, text, size, bold, mono);
    dd_canvas_text(cv, right_x - w, y, text, size, bold, mono, col);
}

void dd_canvas_text_center(DDCanvas *cv, DDRect box, const char *text, double size, bool bold, bool mono, DDColor col) {
    if (!text || !*text) return;
    double w = dd_canvas_measure_text(cv, text, size, bold, mono);
    double x = box.x + (box.w - w) * 0.5;
    double y = box.y + (box.h + size * 0.72) * 0.5;
    dd_canvas_text(cv, x, y, text, size, bold, mono, col);
}

void dd_canvas_text_clipped(DDCanvas *cv, DDRect box, const char *text, double size, bool bold, bool mono, DDColor col) {
    if (!cv || !text || !*text || box.w <= 4.0) return;
    double w = dd_canvas_measure_text(cv, text, size, bold, mono);
    if (w <= box.w) {
        dd_canvas_text(cv, box.x, box.y + (box.h + size * 0.72) * 0.5, text, size, bold, mono, col);
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", text);
    size_t len = strlen(buf);
    while (len > 1) {
        buf[--len] = '\0';
        char ell[264];
        snprintf(ell, sizeof(ell), "%s..", buf);
        if (dd_canvas_measure_text(cv, ell, size, bold, mono) <= box.w) {
            dd_canvas_text(cv, box.x, box.y + (box.h + size * 0.72) * 0.5, ell, size, bold, mono, col);
            return;
        }
    }
}

void dd_canvas_draw_icon(DDCanvas *cv, DDIconType icon, double x, double y, double s, DDColor col) {
    if (!cv) return;
    double cx = x + s * 0.5, cy = y + s * 0.5;
    switch (icon) {
        case ICON_FOLDER: {
            DDRect tab = {x + 1, y + 2, s * 0.42, 3.5};
            DDRect body = {x + 1, y + 4.5, s - 2, s - 6.5};
            dd_canvas_round_rect(cv, tab, 1.5, col, dd_color_hex(0,0), 0);
            dd_canvas_round_rect(cv, body, 2.5, col, dd_color_hex(0,0), 0);
            break;
        }
        case ICON_SEARCH: {
            dd_canvas_circle(cv, x + s * 0.42, y + s * 0.42, s * 0.28, dd_color_hex(0,0), col, 1.8);
            dd_canvas_line(cv, x + s * 0.62, y + s * 0.62, x + s * 0.86, y + s * 0.86, col, 2.0);
            break;
        }
        case ICON_TRASH: {
            dd_canvas_line(cv, x + 3, y + 4, x + s - 3, y + 4, col, 1.8);
            dd_canvas_round_rect(cv, (DDRect){x + 4, y + 5, s - 8, s - 7}, 2.0, dd_color_hex(0,0), col, 1.6);
            dd_canvas_line(cv, cx - 2, y + 2, cx + 2, y + 2, col, 1.8);
            break;
        }
        case ICON_SUN: {
            dd_canvas_circle(cv, cx, cy, s * 0.24, col, dd_color_hex(0,0), 0);
            for (int i = 0; i < 8; i++) {
                double a = i * M_PI * 0.25;
                dd_canvas_line(cv, cx + cos(a)*s*0.32, cy + sin(a)*s*0.32, cx + cos(a)*s*0.46, cy + sin(a)*s*0.46, col, 1.6);
            }
            break;
        }
        case ICON_MOON: {
            dd_canvas_circle(cv, cx, cy, s * 0.34, dd_color_hex(0,0), col, 1.8);
            dd_canvas_line(cv, cx - 2, cy - s * 0.22, cx - 2, cy + s * 0.22, col, 1.8);
            break;
        }
        case ICON_SIDEBAR_RIGHT: {
            dd_canvas_round_rect(cv, (DDRect){x + 2, y + 2.5, s - 4, s - 5}, 2.5, dd_color_hex(0,0), col, 1.6);
            dd_canvas_line(cv, x + s * 0.66, y + 3, x + s * 0.66, y + s - 3, col, 1.6);
            break;
        }
        case ICON_LOCK: {
            dd_canvas_round_rect(cv, (DDRect){x + 3, y + s * 0.45, s - 6, s * 0.45}, 2.0, col, dd_color_hex(0,0), 0);
            dd_canvas_circle(cv, cx, y + s * 0.40, s * 0.22, dd_color_hex(0,0), col, 1.8);
            break;
        }
        case ICON_CHEVRON_LEFT: {
            dd_canvas_line(cv, x + s * 0.65, y + s * 0.22, x + s * 0.32, cy, col, 2.0);
            dd_canvas_line(cv, x + s * 0.32, cy, x + s * 0.65, y + s * 0.78, col, 2.0);
            break;
        }
        case ICON_CHEVRON_RIGHT: {
            dd_canvas_line(cv, x + s * 0.35, y + s * 0.22, x + s * 0.68, cy, col, 2.0);
            dd_canvas_line(cv, x + s * 0.68, cy, x + s * 0.35, y + s * 0.78, col, 2.0);
            break;
        }
        case ICON_CHEVRON_DOWN: {
            dd_canvas_line(cv, x + s * 0.22, y + s * 0.36, cx, y + s * 0.68, col, 2.0);
            dd_canvas_line(cv, cx, y + s * 0.68, x + s * 0.78, y + s * 0.36, col, 2.0);
            break;
        }
        case ICON_CHECK: {
            dd_canvas_line(cv, x + s * 0.20, cy, x + s * 0.42, y + s * 0.74, col, 2.2);
            dd_canvas_line(cv, x + s * 0.42, y + s * 0.74, x + s * 0.82, y + s * 0.26, col, 2.2);
            break;
        }
        case ICON_CLOSE: {
            dd_canvas_line(cv, x + s * 0.25, y + s * 0.25, x + s * 0.75, y + s * 0.75, col, 1.8);
            dd_canvas_line(cv, x + s * 0.75, y + s * 0.25, x + s * 0.25, y + s * 0.75, col, 1.8);
            break;
        }
        case ICON_PLUS: {
            dd_canvas_line(cv, cx, y + s * 0.22, cx, y + s * 0.78, col, 2.0);
            dd_canvas_line(cv, x + s * 0.22, cy, x + s * 0.78, cy, col, 2.0);
            break;
        }
        case ICON_REFRESH: {
            dd_canvas_circle(cv, cx, cy, s * 0.32, dd_color_hex(0,0), col, 1.8);
            dd_canvas_line(cv, cx + s * 0.15, y + s * 0.16, cx + s * 0.36, y + s * 0.24, col, 1.8);
            break;
        }
        case ICON_REVEAL: {
            dd_canvas_round_rect(cv, (DDRect){x + 2, y + 3, s - 4, s - 6}, 2.0, dd_color_hex(0,0), col, 1.6);
            dd_canvas_line(cv, cx - 2, cy + 2, cx + 3, cy - 3, col, 1.8);
            break;
        }
        default: {
            dd_canvas_round_rect(cv, (DDRect){x + 2, y + 1.5, s - 4, s - 3}, 2.5, col, dd_color_hex(0,0), 0);
            break;
        }
    }
}

void dd_canvas_clear_hits(DDCanvas *cv) {
    if (!cv) return;
    cv->hit_count = 0;
}

static bool clip_rect_to_current(const DDCanvas *cv, DDRect in_r, DDRect *out_r) {
    if (!cv) return false;
    DDRect cl = cv->clip_stack[cv->clip_depth];
    double x0 = in_r.x > cl.x ? in_r.x : cl.x;
    double y0 = in_r.y > cl.y ? in_r.y : cl.y;
    double x1 = (in_r.x + in_r.w) < (cl.x + cl.w) ? (in_r.x + in_r.w) : (cl.x + cl.w);
    double y1 = (in_r.y + in_r.h) < (cl.y + cl.h) ? (in_r.y + in_r.h) : (cl.y + cl.h);
    if (x1 <= x0 || y1 <= y0) return false;
    *out_r = (DDRect){x0, y0, x1 - x0, y1 - y0};
    return true;
}

int dd_canvas_register_hit(DDCanvas *cv, int action, int param_int, int param_int2, const char *param_str, const char *label, DDRect rect, bool focusable) {
    if (!cv || cv->hit_count >= 1024) return -1;
    DDRect clipped;
    if (!clip_rect_to_current(cv, rect, &clipped)) return -1;
    int idx = cv->hit_count++;
    DDHitRegion *h = &cv->hits[idx];
    h->id = idx;
    h->action = action;
    h->param_int = param_int;
    h->param_int2 = param_int2;
    if (param_str) snprintf(h->param_str, sizeof(h->param_str), "%s", param_str);
    else h->param_str[0] = '\0';
    if (label) snprintf(h->label, sizeof(h->label), "%s", label);
    else h->label[0] = '\0';
    h->rect = clipped;
    h->focusable = focusable;
    return idx;
}

int dd_canvas_hit_test(const DDCanvas *cv, double mx, double my) {
    if (!cv) return -1;
    for (int i = cv->hit_count - 1; i >= 0; i--) {
        DDRect r = cv->hits[i].rect;
        if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) {
            return i;
        }
    }
    return -1;
}

const DDHitRegion *dd_canvas_get_hit(const DDCanvas *cv, int idx) {
    if (!cv || idx < 0 || idx >= cv->hit_count) return NULL;
    return &cv->hits[idx];
}

int dd_canvas_hit_count(const DDCanvas *cv) {
    return cv ? cv->hit_count : 0;
}

static void ensure_viz_cap(DDCanvas *cv) {
    if (cv->viz_hit_count >= cv->viz_hit_cap) {
        int new_cap = cv->viz_hit_cap * 2;
        DDVizHitItem *grown = (DDVizHitItem *)realloc(cv->viz_hits, (size_t)new_cap * sizeof(DDVizHitItem));
        if (!grown) return;
        cv->viz_hits = grown;
        cv->viz_hit_cap = new_cap;
    }
}

void dd_canvas_clear_viz_hits(DDCanvas *cv) {
    if (cv) cv->viz_hit_count = 0;
}

void dd_canvas_add_viz_rect(DDCanvas *cv, int32_t node_idx, DDRect r) {
    if (!cv) return;
    DDRect clipped;
    if (!clip_rect_to_current(cv, r, &clipped)) return;
    ensure_viz_cap(cv);
    if (cv->viz_hit_count >= cv->viz_hit_cap) return;
    DDVizHitItem *it = &cv->viz_hits[cv->viz_hit_count++];
    memset(it, 0, sizeof(*it));
    it->cell_node_idx = node_idx;
    it->rect = clipped;
}

void dd_canvas_add_viz_circle(DDCanvas *cv, int32_t node_idx, double cx, double cy, double r) {
    if (!cv) return;
    DDRect bbox = {cx - r, cy - r, r * 2, r * 2};
    DDRect clipped;
    if (!clip_rect_to_current(cv, bbox, &clipped)) return;
    ensure_viz_cap(cv);
    if (cv->viz_hit_count >= cv->viz_hit_cap) return;
    DDVizHitItem *it = &cv->viz_hits[cv->viz_hit_count++];
    memset(it, 0, sizeof(*it));
    it->cell_node_idx = node_idx;
    it->cx = cx; it->cy = cy; it->r_out = r;
    it->is_circle = true;
    it->rect = clipped;
}

void dd_canvas_add_viz_arc(DDCanvas *cv, int32_t node_idx, double cx, double cy, double r_in, double r_out, double a0, double a1) {
    if (!cv) return;
    ensure_viz_cap(cv);
    if (cv->viz_hit_count >= cv->viz_hit_cap) return;
    DDVizHitItem *it = &cv->viz_hits[cv->viz_hit_count++];
    memset(it, 0, sizeof(*it));
    it->cell_node_idx = node_idx;
    it->cx = cx; it->cy = cy;
    it->r_in = r_in; it->r_out = r_out;
    it->a0 = a0; it->a1 = a1;
    it->is_arc = true;
}

int dd_canvas_viz_hit_test(const DDCanvas *cv, double mx, double my) {
    if (!cv) return -1;
    for (int i = cv->viz_hit_count - 1; i >= 0; i--) {
        const DDVizHitItem *it = &cv->viz_hits[i];
        if (it->is_circle) {
            double dx = mx - it->cx, dy = my - it->cy;
            if (dx * dx + dy * dy <= it->r_out * it->r_out) return i;
        } else if (it->is_arc) {
            double dx = mx - it->cx, dy = my - it->cy;
            double d = sqrt(dx * dx + dy * dy);
            if (d >= it->r_in && d <= it->r_out) {
                double a = atan2(dy, dx);
                while (a < it->a0) a += 2.0 * M_PI;
                if (a <= it->a1) return i;
            }
        } else {
            if (mx >= it->rect.x && mx <= it->rect.x + it->rect.w &&
                my >= it->rect.y && my <= it->rect.y + it->rect.h) {
                return i;
            }
        }
    }
    return -1;
}

const DDVizHitItem *dd_canvas_get_viz_hit(const DDCanvas *cv, int idx) {
    if (!cv || idx < 0 || idx >= cv->viz_hit_count) return NULL;
    return &cv->viz_hits[idx];
}
