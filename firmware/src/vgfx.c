/* SPDX-License-Identifier: GPL-3.0-only */
/* VIRGO's drawing: antialiased shapes and text on the platform's canvas (gfx.c's cv_px, RGB565 stored byte-swapped).
 * Colours are 0xRRGGBB; alpha 0..256. Coordinates of the smooth shapes in Q4 (1/16 of a pixel) where it says so.
 * Integers only. */
typedef struct {
    uint8_t h, asc, first, last;
    const uint8_t *adv, *bw;
    const int8_t *bx;
    const uint16_t *off;
    const uint8_t *data;
} aafont_t;
#include "virgo_font.h"

#define RGB24(r, g, b) ((uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))
static int32_t vg_x0, vg_y0, vg_x1, vg_y1;      /* the clip, in canvas pixels */

static inline uint32_t to565(uint32_t c) { return ((c >> 19) & 31u) << 11 | ((c >> 10) & 63u) << 5 | ((c >> 3) & 31u); }
static inline uint16_t sw565(uint32_t c) { return swap16(to565(c)); }
static inline uint32_t from_px(uint16_t s)
{
    uint32_t v = swap16(s), r = v >> 11, g = (v >> 5) & 63u, b = v & 31u;
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}
static void vg_begin(uint32_t w, uint32_t h, uint32_t bg)
{
    cv_begin(w, h, (uint16_t)to565(bg));
    vg_x0 = 0; vg_y0 = 0; vg_x1 = (int32_t)cv_w; vg_y1 = (int32_t)cv_h;
}
static void vg_clip(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    vg_x0 = x0 < 0 ? 0 : x0; vg_y0 = y0 < 0 ? 0 : y0;
    vg_x1 = x1 > (int32_t)cv_w ? (int32_t)cv_w : x1; vg_y1 = y1 > (int32_t)cv_h ? (int32_t)cv_h : y1;
}
static void vg_noclip(void) { vg_x0 = 0; vg_y0 = 0; vg_x1 = (int32_t)cv_w; vg_y1 = (int32_t)cv_h; }

/* one pixel: over (blend) or added (light on the dark) */
static inline void vg_px(int32_t x, int32_t y, uint32_t c, uint32_t a)
{
    uint16_t *p;
    uint32_t d, r, g, b;
    if (x < vg_x0 || y < vg_y0 || x >= vg_x1 || y >= vg_y1 || !a) return;
    p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 256u) { *p = sw565(c); return; }
    d = from_px(*p);
    r = ((d >> 16) * (256u - a) + (c >> 16) * a) >> 8;
    g = (((d >> 8) & 255u) * (256u - a) + ((c >> 8) & 255u) * a) >> 8;
    b = ((d & 255u) * (256u - a) + (c & 255u) * a) >> 8;
    *p = sw565(r << 16 | g << 8 | b);
}
static inline void vg_add(int32_t x, int32_t y, uint32_t c, uint32_t a)
{
    uint16_t *p;
    uint32_t d, r, g, b;
    if (x < vg_x0 || y < vg_y0 || x >= vg_x1 || y >= vg_y1 || !a) return;
    p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    d = from_px(*p);
    r = (d >> 16) + (((c >> 16) * a) >> 8);
    g = ((d >> 8) & 255u) + ((((c >> 8) & 255u) * a) >> 8);
    b = (d & 255u) + (((c & 255u) * a) >> 8);
    *p = sw565((r > 255u ? 255u : r) << 16 | (g > 255u ? 255u : g) << 8 | (b > 255u ? 255u : b));
}
static uint32_t vg_mix(uint32_t c0, uint32_t c1, uint32_t t)   /* t 0..256 */
{
    uint32_t r = ((c0 >> 16) * (256u - t) + (c1 >> 16) * t) >> 8;
    uint32_t g = (((c0 >> 8) & 255u) * (256u - t) + ((c1 >> 8) & 255u) * t) >> 8;
    uint32_t b = ((c0 & 255u) * (256u - t) + (c1 & 255u) * t) >> 8;
    return r << 16 | g << 8 | b;
}
static uint32_t vg_scale(uint32_t c, uint32_t a)            /* a colour dimmed, a 0..256 */
{
    return (((c >> 16) * a) >> 8) << 16 | ((((c >> 8) & 255u) * a) >> 8) << 8 | (((c & 255u) * a) >> 8);
}

static void vg_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c, uint32_t a)
{
    int32_t i, j;
    for (j = y; j < y + h; j++) for (i = x; i < x + w; i++) vg_px(i, j, c, a);
}
static void vg_vgrad(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c0, uint32_t c1)   /* top c0 to bottom c1 */
{
    int32_t j, i;
    for (j = 0; j < h; j++) {
        uint16_t s = sw565(vg_mix(c0, c1, h > 1 ? (uint32_t)(j * 256 / (h - 1)) : 0u));
        int32_t yy = y + j;
        if (yy < vg_y0 || yy >= vg_y1) continue;
        for (i = x < vg_x0 ? vg_x0 : x; i < x + w && i < vg_x1; i++) cv_px[(uint32_t)yy * cv_w + (uint32_t)i] = s;
    }
}

static uint32_t isqrt32(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* a disc, centre and radius Q4, its edge smoothed */
static void vg_disc(int32_t cx, int32_t cy, int32_t r, uint32_t c, uint32_t a)
{
    int32_t x, y, x0 = (cx - r) >> 4, x1 = (cx + r + 15) >> 4, y0 = (cy - r) >> 4, y1 = (cy + r + 15) >> 4;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int32_t dx = x * 16 + 8 - cx, dy = y * 16 + 8 - cy;
            uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
            int32_t d, cov;
            if (d2 > (uint32_t)((r + 16) * (r + 16))) continue;
            d = (int32_t)isqrt32(d2);
            cov = r - d + 8;                 /* Q4: a pixel's width of smoothing */
            if (cov <= 0) continue;
            if (cov > 16) cov = 16;
            vg_px(x, y, c, a * (uint32_t)cov >> 4);
        }
}
/* a ring: radius r, width w (Q4) */
static void vg_ring(int32_t cx, int32_t cy, int32_t r, int32_t w, uint32_t c, uint32_t a)
{
    int32_t x, y, ro = r + w / 2, ri = r - w / 2, x0 = (cx - ro) >> 4, x1 = (cx + ro + 15) >> 4, y0 = (cy - ro) >> 4, y1 = (cy + ro + 15) >> 4;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int32_t dx = x * 16 + 8 - cx, dy = y * 16 + 8 - cy, d, co, ci;
            uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
            if (d2 > (uint32_t)((ro + 16) * (ro + 16))) continue;
            d = (int32_t)isqrt32(d2);
            co = ro - d + 8; ci = d - ri + 8;
            if (co <= 0 || ci <= 0) continue;
            if (co > 16) co = 16;
            if (ci > 16) ci = 16;
            vg_px(x, y, c, a * (uint32_t)(co * ci) >> 8);
        }
}
/* light: added, falling off as (1 - d^2/r^2)^2; centre Q4, radius in pixels */
static void vg_glow(int32_t cx, int32_t cy, int32_t r, uint32_t c, uint32_t a)
{
    int32_t x, y, rr = r * 16, x0 = (cx - rr) >> 4, x1 = (cx + rr) >> 4, y0 = (cy - rr) >> 4, y1 = (cy + rr) >> 4;
    uint32_t R2 = (uint32_t)(rr * rr);
    if (!r || !a) return;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int32_t dx = x * 16 + 8 - cx, dy = y * 16 + 8 - cy;
            uint32_t d2 = (uint32_t)(dx * dx + dy * dy), f;
            if (d2 >= R2) continue;
            f = 256u - (d2 << 8) / R2;
            vg_add(x, y, c, a * (f * f >> 8) >> 8);
        }
}

/* a line, Xiaolin Wu's way: ends Q4, alpha a; add: light rather than paint */
static void vg_line_(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c, uint32_t a, int add)
{
    int32_t steep = (y1 > y0 ? y1 - y0 : y0 - y1) > (x1 > x0 ? x1 - x0 : x0 - x1), t, dx, dy, x, xs, xe;
    int32_t grad, iy;
    if (steep) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    dx = x1 - x0; dy = y1 - y0;
    grad = dx ? (dy * 65536) / dx : 0;                        /* Q16 (|dy| < 32768) */
    xs = (x0 + 8) >> 4; xe = (x1 + 8) >> 4;
    iy = (y0 << 12) + (int32_t)(((int64_t)grad * (xs * 16 + 8 - x0)) >> 4);   /* Q16 pixels */
    for (x = xs; x <= xe; x++) {
        int32_t yy = (iy - 32768) >> 16, fr = ((iy - 32768) >> 8) & 255;
        uint32_t a0 = a * (uint32_t)(256 - fr) >> 8, a1 = a * (uint32_t)fr >> 8;
        if (steep) {
            if (add) { vg_add(yy, x, c, a0); vg_add(yy + 1, x, c, a1); } else { vg_px(yy, x, c, a0); vg_px(yy + 1, x, c, a1); }
        } else {
            if (add) { vg_add(x, yy, c, a0); vg_add(x, yy + 1, c, a1); } else { vg_px(x, yy, c, a0); vg_px(x, yy + 1, c, a1); }
        }
        iy += grad;
    }
}
static void vg_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c, uint32_t a) { vg_line_(x0, y0, x1, y1, c, a, 0); }
static void vg_line_add(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c, uint32_t a) { vg_line_(x0, y0, x1, y1, c, a, 1); }
/* a beam: a line with a halo around it */
static void vg_beam(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t core, uint32_t halo, uint32_t a)
{
    int32_t k;
    int32_t steep = (y1 > y0 ? y1 - y0 : y0 - y1) > (x1 > x0 ? x1 - x0 : x0 - x1);
    for (k = -2; k <= 2; k++) {
        if (!k) continue;
        if (steep) vg_line_add(x0 + k * 12, y0, x1 + k * 12, y1, halo, a * (k == 1 || k == -1 ? 90u : 35u) >> 8);
        else vg_line_add(x0, y0 + k * 12, x1, y1 + k * 12, halo, a * (k == 1 || k == -1 ? 90u : 35u) >> 8);
    }
    vg_line(x0, y0, x1, y1, core, a);
}

/* a curve y(x), one value a column (Q4), drawn as a smooth line of width ~1.2 px; glow: a halo added */
static void vg_curve(int32_t x0, const int16_t *ys, int32_t n, uint32_t c, uint32_t a, uint32_t glow)
{
    int32_t i;
    for (i = 0; i < n; i++) {
        int32_t ya = ys[i], yb = i + 1 < n ? ys[i + 1] : ys[i], lo, hi, y, x = x0 + i;
        int32_t m = (ya + yb) / 2;
        lo = (ya < m ? ya : m) - 10; hi = (ya > m ? ya : m) + 10;   /* the column spans to the midpoint, plus the width */
        if (i) { int32_t p = (ys[i - 1] + ya) / 2; if (p - 10 < lo) lo = p - 10; if (p + 10 > hi) hi = p + 10; }
        for (y = lo >> 4; y <= (hi + 15) >> 4; y++) {
            int32_t top = y * 16, bot = top + 16, ov = (bot < hi ? bot : hi) - (top > lo ? top : lo);
            if (ov > 0) vg_px(x, y, c, a * (uint32_t)(ov > 16 ? 16 : ov) >> 4);
            if (glow) { vg_add(x, y - 1, c, glow >> 2); vg_add(x, y + 1, c, glow >> 2); }
        }
    }
}
/* the area under a curve to a base line, a vertical gradient of c from a_top (at the top of the area) to a_bot */
static void vg_fill_under(int32_t x0, const int16_t *ys, int32_t n, int32_t base, int32_t top, uint32_t c, uint32_t a_top, uint32_t a_bot)
{
    int32_t i, y;
    for (i = 0; i < n; i++) {
        int32_t yv = ys[i], y0 = yv >> 4, fr = yv & 15;
        for (y = y0; y < base; y++) {
            uint32_t t = base > top ? (uint32_t)((y - top) * 256 / (base - top)) : 0u, al;
            if (t > 256u) t = 256u;
            al = (a_top * (256u - t) + a_bot * t) >> 8;
            if (y == y0) al = al * (uint32_t)(16 - fr) >> 4;
            vg_px(x0 + i, y, c, al);
        }
    }
}

/* the angle of (dx, dy), a turn = 1024, 0 to the right, growing clockwise (y down) */
static uint32_t vg_atan2(int32_t dy, int32_t dx)
{
    int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, a;
    if (!ax && !ay) return 0;
    if (ax >= ay) a = ATAN_T[(ay * 256 + ax / 2) / ax];
    else a = 256 - ATAN_T[(ax * 256 + ay / 2) / ay];
    if (dx < 0) a = 512 - a;
    if (dy < 0) a = 1024 - a;
    return (uint32_t)(a & 1023);
}
/* an arc of a ring, from angle a0 for len (a turn = 1024), radius r and width w in Q4 */
static void vg_arc(int32_t cx, int32_t cy, int32_t r, int32_t w, uint32_t a0, uint32_t len, uint32_t c, uint32_t a)
{
    int32_t x, y, ro = r + w / 2, ri = r - w / 2, x0 = (cx - ro) >> 4, x1 = (cx + ro + 15) >> 4, y0 = (cy - ro) >> 4, y1 = (cy + ro + 15) >> 4;
    if (!len) return;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int32_t dx = x * 16 + 8 - cx, dy = y * 16 + 8 - cy, d, co, ci;
            uint32_t d2 = (uint32_t)(dx * dx + dy * dy), an;
            if (d2 > (uint32_t)((ro + 16) * (ro + 16)) || d2 + 16u * 16u < (uint32_t)(ri * ri)) continue;
            an = (vg_atan2(dy, dx) - a0) & 1023u;
            if (an > len) continue;
            d = (int32_t)isqrt32(d2);
            co = ro - d + 8; ci = d - ri + 8;
            if (co <= 0 || ci <= 0) continue;
            if (co > 16) co = 16;
            if (ci > 16) ci = 16;
            vg_px(x, y, c, a * (uint32_t)(co * ci) >> 8);
        }
}

/* ---- text: baseline at y; returns the pen after it */
static uint32_t vg_glyph(const aafont_t *f, uint32_t ch)
{
    if (ch < f->first || ch > f->last) ch = '?';
    return ch - f->first;
}
static int32_t vg_text_w(const aafont_t *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++) w += f->adv[vg_glyph(f, (uint8_t)*s)];
    return w;
}
static int32_t vg_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint32_t c, uint32_t a)
{
    for (; *s; s++) {
        uint32_t gi = vg_glyph(f, (uint8_t)*s), w = f->bw[gi], bpr = (w + 1u) / 2u, gx, gy;
        const uint8_t *gd = f->data + f->off[gi];
        int32_t ox = x + f->bx[gi], oy = y - f->asc;
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[gy * bpr + gx / 2u];
                v = (gx & 1u) ? (v & 15u) : (v >> 4);
                if (v) vg_px(ox + (int32_t)gx, oy + (int32_t)gy, c, a * (v * 17u + 1u) >> 8);
            }
        x += f->adv[gi];
    }
    return x;
}
static void vg_text_al(int32_t x, int32_t y, const aafont_t *f, const char *s, uint32_t c, uint32_t a, int al)   /* al 0 left, 1 centre, 2 right */
{
    int32_t w = vg_text_w(f, s);
    vg_text(al == 1 ? x - w / 2 : al == 2 ? x - w : x, y, f, s, c, a);
}
static const aafont_t *vg_fit(const char *s, int32_t w)   /* the biggest value font that fits */
{
    if (vg_text_w(&F_MD, s) <= w) return &F_MD;
    if (vg_text_w(&F_SM, s) <= w) return &F_SM;
    return &F_XS;
}
