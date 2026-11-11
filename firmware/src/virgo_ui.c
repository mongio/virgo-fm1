/* SPDX-License-Identifier: GPL-3.0-only */
/* VIRGO's screen: 240 x 240. A header (22 px: the preset, the page, the four events, Bluetooth), the picture (128 px),
 * the four knobs (two rows of 45). Smooth lines and type (vgfx.c), a dark palette, one accent a page. Included by virgo.c. */
#define HDR_H 22
#define VIS_Y 22
#define VIS_H 128
#define CELL_Y 150
#define C_TXT 0xEEF4FF
#undef C_DIM
#define C_DIM 0x8392AD
#define C_FAINT 0x4A5570
#define C_BG0 0x060912
#define C_BG1 RGB24(4, 6, 12)
#define C_AMBER 0xFFB35C
#define C_VIOL 0xB28CFF
#define C_CYAN 0x6FE3FF

static uint32_t t_flash, t_touch[4], intro, intro_t0, audio_max;
static uint32_t save_mode, save_dest, t_savemode, t_savehold;
static char save_name[13];
static uint32_t name_cur, name_dirty;

/* ---- the spectrogram: a column a 25 ms from what sounds (not an FFT: the events know their frequencies) */
#define SH VIS_H
static uint8_t spec[SH * 240u] POOL;
static uint32_t spec_head;                          /* the next column to write */
static uint16_t spec_lut[256];                      /* the colours, byte-swapped as the canvas */
static uint8_t gw[64];                              /* a gaussian a row wide, by distance (Q4 rows) */
static uint32_t glitch_seen, glitch_cols, glitch_row;
static void spec_init(void)
{
    static const uint8_t ST[6][4] = {{0, 5, 7, 15}, {56, 24, 18, 66}, {115, 78, 40, 142}, {166, 30, 140, 182}, {209, 125, 226, 224}, {255, 255, 246, 216}};
    uint32_t i, k;
    for (i = 0; i < 256u; i++) {
        uint32_t c;
        for (k = 0; k < 4u && i > ST[k + 1u][0]; k++) continue;
        {
            uint32_t a = ST[k][0], b = ST[k + 1u][0], t = b > a ? (i - a) * 256u / (b - a) : 0u;
            c = vg_mix(RGB24(ST[k][1], ST[k][2], ST[k][3]), RGB24(ST[k + 1u][1], ST[k + 1u][2], ST[k + 1u][3]), t);
        }
        spec_lut[i] = sw565(c);
    }
    for (i = 0; i < 64u; i++) {                     /* exp(-(d/16)^2 / 2.4) */
        uint32_t d2 = i * i, e = 256u;
        uint32_t x = d2 * 256u / (16u * 16u * 24u / 10u);   /* Q8 of the exponent */
        while (x >= 256u) { e = e * 94u >> 8; x -= 256u; }   /* e^-1 = 94/256 */
        e = e * (256u - x * 160u / 256u) >> 8;
        gw[i] = (uint8_t)(e > 255u ? 255u : e);
    }
    for (i = 0; i < SH * 240u; i++) spec[i] = 0;
}
static int32_t row_q4(uint32_t hz16)                /* the spectrogram's row of a frequency, Q4; 25 Hz .. 3 kHz */
{
    int32_t l = log2_q16(hz16) - log2_q16(400u);    /* octaves above 25 Hz */
    return (SH - 1) * 16 - l * ((SH - 1) * 16) / 452651;
}
static void spec_add(uint32_t *val, uint32_t hz16, uint32_t a)
{
    int32_t r = row_q4(hz16), y, y0, y1;
    if (!a || hz16 < 300u) return;
    y0 = (r >> 4) - 4; y1 = (r >> 4) + 4;
    for (y = y0; y <= y1; y++) {
        int32_t d = y * 16 + 8 - r;
        if (y < 0 || y >= SH) continue;
        if (d < 0) d = -d;
        if (d < 64) val[y] += a * gw[d] >> 8;
    }
}
static void spec_column(void)
{
    uint32_t val[SH], y, v, k;
    uint8_t *col;
    for (y = 0; y < SH; y++) {                      /* the detector's floor: seismic low, thermal across, quantum high */
        uint32_t lo = y > SH - 22u ? (y - (SH - 22u)) * par[P_SEIS] / 3u : 0u;
        uint32_t hi = y < 40u ? (40u - y) * par[P_QUANT] / 12u : 0u;
        uint32_t n = (lo + hi + par[P_THERM] / 2u + 22u) * (40u + (rand_u16() >> 9)) >> 7;
        val[y] = n;
    }
    if (par[P_THERM]) { spec_add(val, 452u * 16u, par[P_THERM] * 2u); spec_add(val, 904u * 16u, par[P_THERM]); }
    for (v = 0; v < NV; v++) {
        uint32_t st = vis[v].stage;
        if (st == ST_OFF) continue;
        if (st == ST_INSP) {
            uint32_t a = (uint32_t)(vis[v].amp > 0 ? vis[v].amp : 0) >> 4, h = par[P_HARM];
            spec_add(val, vis[v].hz16, a);
            spec_add(val, vis[v].hz16 * 3u / 2u, a * h / 300u);
            spec_add(val, vis[v].hz16 * 2u, a * h / 450u);
        } else
            for (k = 0; k < NM; k++) spec_add(val, vis[v].mhz16[k], (uint32_t)(vis[v].amode[k] > 0 ? vis[v].amode[k] : 0) >> 4);
    }
    if (par[P_SKY]) {
        uint32_t hz = band_hz(par[P_BAND]), cnt = (uint32_t)eng.s_dens;
        for (k = 0; k < cnt && k < NSKY; k++)
            spec_add(val, (uint32_t)(((uint64_t)hz * 16u * exp2_q16(sky[k].oct >> 8)) >> 16), (uint32_t)sky[k].amp * par[P_SKY] >> 12);
    }
    if (vis_glitch != glitch_seen) { glitch_seen = vis_glitch; glitch_cols = 4; glitch_row = (uint32_t)row_q4((vis_glitch & 0xFFFFu) * 16u); }
    if (glitch_cols) {
        glitch_cols--;
        for (y = 0; y < 14u; y++) { int32_t r = (int32_t)(glitch_row >> 4) - 7 + (int32_t)y; if (r >= 0 && r < SH) val[r] += 300u - y * 10u; }
    }
    col = spec;
    for (y = 0; y < SH; y++) {
        uint32_t x = val[y];
        col[y * 240u + spec_head] = (uint8_t)(x * 255u / (x + 120u));
    }
    spec_head = (spec_head + 1u) % 240u;
}
static void spec_blit(int32_t x0, int32_t y0, int32_t w, int32_t h, int32_t step)   /* the last w*step columns into the canvas */
{
    int32_t x, y;
    for (y = 0; y < h; y++) {
        uint32_t sy = (uint32_t)(y * SH / h);
        uint16_t *dst = &cv_px[(uint32_t)(y0 + y) * cv_w + (uint32_t)x0];
        const uint8_t *row = &spec[sy * 240u];
        uint32_t c = (spec_head + 240u - (uint32_t)(w * step)) % 240u;
        for (x = 0; x < w; x++) { dst[x] = spec_lut[row[c]]; c = (c + (uint32_t)step) % 240u; }
    }
}

/* ---- the header */
static void draw_vbg(int32_t h) { vg_vgrad(0, 0, 240, h, C_BG0, C_BG1); }
static uint32_t bt_mark;                            /* 0 off, 1 on, 2 linked (VIRGO + Bluetooth) */
static void draw_header(void)
{
    char s[40];
    uint32_t acc = page < NPAGE + 2u ? PG_ACC[page] : C_CYAN, v;
    vg_begin(240, HDR_H, 0x0A0F1D);
    vg_rect(0, HDR_H - 1, 240, 1, 0x1A2236, 256);
    if (lmsg[0]) {
        vg_clip(0, 0, 198, HDR_H);
        vg_text(6, 15, &F_SM, lmsg, C_AMBER, 256);
        vg_noclip();
    } else if (save_mode) {
        char *e = cat(s, "SALVA IN ");
        e = cat_u(e, save_dest + 1u);
        cat(e, pr[save_dest].used ? (t_savehold ? "  TIENI..." : "  SOSTITUISCI?") : "  VUOTO");
        vg_text(6, 15, &F_SM, s, pr[save_dest].used ? 0xFF6A5C : C_AMBER, 256);
        if (t_savehold) {                           /* the bar that fills while SAVE is held */
            uint32_t w = (fm1_ms - t_savehold) * 190u / 1000u;
            vg_rect(6, 18, (int32_t)(w > 190u ? 190u : w), 2, 0xFF6A5C, 256);
        }
    } else {
        uint32_t pi = pend_preset >= 0 ? (uint32_t)pend_preset : cur_preset, pw;
        int32_t x = 6;
        char *e = cat_u(s, pi + 1u);
        *e++ = ' '; *e = 0;
        x = vg_text(x, 16, &F_SM, s, C_FAINT, 256);
        pw = (uint32_t)vg_text_w(&F_SM, page < NPAGE + 2u ? PAGE[page] : "");
        vg_clip(0, 0, 196 - (int32_t)pw - 6, HDR_H);
        x = vg_text(x, 16, &F_TI, preset_name(pi), pend_preset >= 0 ? C_DIM : C_TXT, 256);
        if (edited && pend_preset < 0) vg_disc((x + 4) * 16, 9 * 16, 32, C_AMBER, 256);
        vg_noclip();
        vg_text_al(196, 15, &F_SM, page < NPAGE + 2u ? PAGE[page] : "", acc, 256, 2);
    }
    for (v = 0; v < NV; v++) {                      /* the four events */
        int32_t x = 204 + (int32_t)v * 6;
        uint32_t st = vis[v].stage;
        vg_disc(x * 16, 11 * 16, 30, 0x2A3348, 256);
        if (st != ST_OFF) {
            uint32_t c = st == ST_INSP ? C_CYAN : st == ST_RING ? C_AMBER : C_VIOL;
            vg_glow(x * 16, 11 * 16, 5, c, 200);
            vg_disc(x * 16, 11 * 16, 30, c, 256);
        }
    }
    if (gov_max || lim_seen)                        /* 0.4.1: a line under the events: amber, the CPU is cutting; red, the limiter */
        vg_rect(201, 18, 24, 2, lim_seen ? 0xFF4A3C : C_AMBER, 256);
#if GRANA_BT
    {   /* the Bluetooth rune: grey off, dim on, cyan linked */
        uint32_t c = bt_mark == 2u ? C_CYAN : bt_mark ? 0x4C6C88 : 0x2A3348;
        vg_line(229 * 16, 7 * 16, 234 * 16, 13 * 16, c, 256); vg_line(234 * 16, 13 * 16, 231 * 16 + 8, 15 * 16 + 8, c, 256);
        vg_line(231 * 16 + 8, 15 * 16 + 8, 231 * 16 + 8, 4 * 16 + 8, c, 256); vg_line(231 * 16 + 8, 4 * 16 + 8, 234 * 16, 7 * 16, c, 256);
        vg_line(234 * 16, 7 * 16, 229 * 16, 13 * 16, c, 256);
    }
#endif
    cv_blit(0, 0);
}

/* ---- the knobs */
static void value_text(uint32_t p, char *s);
static void midi_value(uint32_t c, char *s);
static const char *const MIDI_PN[4] = {"CLOCK", "RICEVI", "LEARN", "CANCELLA"};
#if GRANA_BT
static void bt_cell(uint32_t c, const char **lab, char *s, uint32_t *hi);
#endif
static void draw_gauge(int32_t cx, int32_t cy, uint32_t num, uint32_t den, uint32_t acc)
{
    uint32_t len = den ? 768u * num / den : 0u, a = (384u + len) & 1023u;
    int32_t dx = cos_q15(a << 22), dy = sin_q15(a << 22);
    vg_arc(cx * 16, cy * 16, 13 * 16, 48, 384u, 768u, 0x1E2740, 256);
    vg_arc(cx * 16, cy * 16, 13 * 16, 48, 384u, len, acc, 256);
    vg_disc(cx * 16 + ((dx * 13 * 16) >> 15), cy * 16 + ((dy * 13 * 16) >> 15), 40, 0xFFFFFF, 256);
}
static void draw_cell(uint32_t c)
{
    char s[40];
    const char *lab = "";
    uint32_t acc = page < NPAGE + 2u ? PG_ACC[page] : C_CYAN, hot = t_touch[c] && fm1_ms - t_touch[c] < 2500u, num = 0, den = 0, hi = 0;
    s[0] = 0;
    vg_begin(120, 45, 0x070A14);
    if (page < NPAGE) {
        uint32_t p = pp(page, c);
        if (p >= NP) p = NP - 1u;
        lab = plabel(p);
        value_text(p, s);
        num = par[p]; den = PMAX[p];
        if (p == P_SND) den = nsnd ? nsnd : 1u;
    } else if (page == PG_MIDI) {
        lab = MIDI_PN[c];
        midi_value(c, s);
        hi = c == 2u && learn_on;
    }
#if GRANA_BT
    else if (page == PG_BT) bt_cell(c, &lab, s, &hi);
#endif
    if (hot) vg_rect(0, 0, 120, 45, acc, 18);
    if (den) draw_gauge(21, 23, num > den ? den : num, den, acc);
    else vg_ring(21 * 16, 23 * 16, 13 * 16, 24, hi ? acc : 0x1E2740, 256);
    vg_text(42, 18, vg_text_w(&F_SM, lab) > 76 ? &F_XS : &F_SM, lab, hot ? C_TXT : C_DIM, 256);
    vg_text(42, 38, vg_fit(s, 76), s, hi ? acc : C_TXT, 256);
    if (!(c & 1u)) vg_rect(119, 4, 1, 37, 0x141B2B, 256);
    if (c < 2u) vg_rect(0, 44, 120, 1, 0x141B2B, 256);
    cv_blit((c & 1u) * 120u, CELL_Y + (c >> 1) * 45u);
}
static void draw_name(void)                         /* SAVE: the name, where the knobs were */
{
    static const char *const HINT[4] = {"CURSORE", "LETTERA", "DADO", "CANCELLA"};
    uint32_t i, n = 0;
    int32_t x0, cw = 15;
    char one[2] = {0, 0};
    while (n < 12u && save_name[n]) n++;
    vg_begin(240, 90, 0x070A14);
    vg_text(8, 16, &F_SM, "NOME DEL PRESET", C_DIM, 256);
    x0 = 120 - cw * (int32_t)(n > name_cur ? n : name_cur + 1u) / 2;
    if (x0 < 4) x0 = 4;
    for (i = 0; i < 12u; i++) {
        one[0] = i < n ? save_name[i] : ' ';
        if (i < n) vg_text_al(x0 + (int32_t)i * cw + cw / 2, 50, &F_MD, one, C_TXT, 256, 1);
        if (i == name_cur) vg_rect(x0 + (int32_t)i * cw + 1, 55, cw - 2, 2, C_AMBER, 256);
    }
    for (i = 0; i < 4u; i++) vg_text_al(30 + (int32_t)i * 60, 80, &F_XS, HINT[i], C_FAINT, 256, 1);
    cv_blit(0, CELL_Y);
}

/* ---- the pictures */
static int32_t latest_voice(void)
{
    uint32_t v, best = 0;
    int32_t at = -1;
    for (v = 0; v < NV; v++) if (vis[v].stage != ST_OFF && v_age[v] >= best) { best = v_age[v]; at = (int32_t)v; }
    return at;
}
static uint32_t t_merge[NV], st_was[NV];
static void track_merges(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++) {
        uint32_t st = vis[v].stage;
        if (st_was[v] == ST_INSP && st >= ST_RING) t_merge[v] = fm1_ms ? fm1_ms : 1u;
        st_was[v] = st;
    }
}
static void draw_orbit(int32_t ox, int32_t oy, int32_t R)   /* the two masses spiralling in, the merger, the rings */
{
    int32_t v = latest_voice(), k;
    vg_disc(ox * 16, oy * 16, R * 16, RGB24(5, 8, 15), 220);
    vg_ring(ox * 16, oy * 16, R * 16, 12, 0x2B5A70, 200);
    vg_clip(ox - R + 1, oy - R + 1, ox + R - 1, oy + R - 1);
    if (v < 0) {
        for (k = 0; k < 48; k++) {                  /* a dotted orbit, waiting */
            uint32_t a = (uint32_t)k << 26;
            if (k & 1) continue;
            vg_px(ox + ((cos_q15(a) * (R - 10)) >> 15), oy + ((sin_q15(a) * (R - 10)) >> 15) * 45 / 100, 0x5A7090, 160);
        }
        vg_text_al(ox, oy + R - 8, &F_XS, "IN ASCOLTO", 0x6A80A0, 256, 1);
    } else {
        uint32_t st = vis[v].stage, m1 = v_m1[v], m2 = v_m2[v], M = m1 + m2;
        int32_t r1 = 16 + (int32_t)isqrt32(m1 * 4u) * 3, r2 = 16 + (int32_t)isqrt32(m2 * 4u) * 3;   /* Q4 */
        if (r1 > 64) r1 = 64;
        if (r2 > 64) r2 = 64;
        if (st == ST_INSP) {
            uint32_t prog = vis[v].prog;          /* 0 .. 65536 at the merger */
            int32_t sep = (int32_t)(R - 6) * 16 * (int32_t)(65536u - prog) / 65536 + 40, d1, d2, c, sn;
            uint32_t ph = vis[v].phi;
            for (k = 1; k <= 3; k++) {              /* waves going out */
                int32_t rr = (int32_t)((fm1_ms / 20u + (uint32_t)k * 9u) % (uint32_t)(R * 3 / 2)) + sep / 16;
                vg_ring(ox * 16, oy * 16, rr * 16, 10, C_CYAN, (uint32_t)(60 - rr * 60 / (R * 2 > 1 ? R * 2 : 1)));
            }
            d1 = sep * (int32_t)m2 / (int32_t)M; d2 = sep * (int32_t)m1 / (int32_t)M;
            c = cos_q15(ph); sn = sin_q15(ph) * 45 / 100;
            {
                int32_t x1 = ox * 16 + ((c * d1) >> 15), y1 = oy * 16 + ((sn * d1) >> 15);
                int32_t x2 = ox * 16 - ((c * d2) >> 15), y2 = oy * 16 - ((sn * d2) >> 15);
                vg_glow(x1, y1, r1 / 5 + 4, 0x9FD8FF, 150); vg_glow(x2, y2, r2 / 5 + 4, 0xC7B0FF, 150);
                vg_disc(x1, y1, r1, 0x02030A, 256); vg_disc(x2, y2, r2, 0x02030A, 256);
                vg_ring(x1, y1, r1, 12, 0xDCF0FF, 220); vg_ring(x2, y2, r2, 12, 0xE6DCFF, 220);
            }
        } else {
            uint32_t since = t_merge[v] ? fm1_ms - t_merge[v] : 1000u;
            int32_t rf = (r1 > r2 ? r1 : r2) * 5 / 4, a = vis[v].amp > 0 ? vis[v].amp : 0;
            if (since < 250u) vg_glow(ox * 16, oy * 16, 22 - (int32_t)since * 14 / 250, 0xFFF2D6, 256 - since);
            for (k = 0; k < 4; k++) {
                int32_t rr = (int32_t)((since / 14u + (uint32_t)k * 7u) % (uint32_t)(R + 4)) + rf / 16;
                uint32_t al = (uint32_t)(a > 9000 ? 180 : a / 50) * (uint32_t)(R + 4 - (rr < R + 4 ? rr : R + 4)) / (uint32_t)(R + 4);
                vg_ring(ox * 16, oy * 16, rr * 16, 10, C_AMBER, al);
            }
            vg_glow(ox * 16, oy * 16, rf / 16 * 3, 0xFFCF8F, (uint32_t)(a > 9000 ? 200 : 40 + a / 60));
            vg_disc(ox * 16, oy * 16, rf, 0x02030A, 256);
            vg_ring(ox * 16, oy * 16, rf, 12, 0xFFE6BE, 220);
        }
    }
    vg_noclip();
}
static void draw_sorgente(void)
{
    int32_t v = latest_voice();
    spec_blit(0, 0, 240, VIS_H, 1);
    {   /* 100 Hz and 1 kHz */
        int32_t y1 = row_q4(1000u * 16u) >> 4, y2 = row_q4(100u * 16u) >> 4;
        vg_rect(0, y1, 240, 1, 0xFFFFFF, 18); vg_rect(0, y2, 240, 1, 0xFFFFFF, 18);
        vg_text(4, y1 - 2, &F_XS, "1 kHz", 0xC0CCE0, 140); vg_text(4, y2 - 2, &F_XS, "100 Hz", 0xC0CCE0, 140);
    }
    draw_orbit(205, 33, 30);
    if (v >= 0) {
        char s[40], *e;
        vg_rect(0, VIS_H - 30, 150, 30, RGB24(4, 7, 15), 160);
        vg_text(6, VIS_H - 17, &F_SM, v_name[v], C_TXT, 256);
        e = cat_d1(s, v_m1[v]); e = cat(e, " + "); e = cat_d1(e, v_m2[v]); e = cat(e, " \x7F  "); e = cat_u(e, v_mpc[v]); cat(e, " Mpc");
        vg_text(6, VIS_H - 5, &F_XS, s, C_DIM, 256);
        if (t_merge[v] && fm1_ms - t_merge[v] < 1200u) vg_text(6, 14, &F_SM, "FUSIONE", C_AMBER, 256 - (fm1_ms - t_merge[v]) * 256u / 1200u);
    }
}
static void draw_chirp(void)
{
    static int16_t up[224], wv[224], fr[224];
    int32_t i, n = 224, x0 = 8, mid = 72, A = 30, cf = 160;   /* the merger at column 160 */
    int32_t oct = part_oct16(par[P_PART]), kd = 3 + (100 - par[P_SMORZ]) / 12, harm = par[P_HARM], prec = par[P_PREC];
    uint32_t po = 0, m1 = mass10(par[P_M1]), m2 = mass10(par[P_M2]), q = (m1 > m2 ? m1 - m2 : m2 - m1) * 256u / (m1 + m2);
    uint32_t K = (exp2_q16(oct * 8 / 3) >> 8) - 256u;
    int32_t v = latest_voice(), kk = harm * (int32_t)(q + 30u) / 256;   /* the higher modes, percent */
    char s[40], *e;
    draw_vbg(VIS_H);
    for (i = 0; i < n; i++) {
        int32_t amp, l2, y;
        if (i < cf) {
            uint32_t tau = (uint32_t)(65536 - i * 65536 / cf), x = 65536u + (uint32_t)(((uint64_t)tau * K) >> 8);
            l2 = -((log2_q16(x) - (16 << 16)) * 3) >> 3;
            amp = (int32_t)(exp2_q16(l2 * 2 / 3) >> 1);   /* Q15, ~ f^(2/3) */
            amp += (int32_t)(((int64_t)amp * sin_q15((uint32_t)i << 25)) >> 15) * prec * 35 / 10000;
        } else {
            int32_t rr = (i - cf) * 256 / (n - cf);
            l2 = 27000;                               /* the ring a little above the merger */
            amp = (int32_t)(exp2_q16(-(rr * kd * 369)) >> 1);
        }
        po += exp2_q16(l2) << 13;                     /* the orbit: a wave's cycle in ~4 px at the merger */
        y = cos_q15(po * 2u) + cos_q15(po * 3u) * kk * 45 / 10000 + cos_q15(po * 4u) * kk * 30 / 10000;
        y = y * 100 / (100 + kk * 60 / 100);
        up[i] = (int16_t)((mid * 16) - ((A * 16 * amp) >> 15));
        wv[i] = (int16_t)((mid * 16) - (int32_t)(((int64_t)A * 16 * amp * y) >> 30));
        fr[i] = (int16_t)(116 * 16 - (l2 + oct) * (84 * 16) / (oct + 27000));
    }
    vg_fill_under(x0, up, n, mid, mid - A, C_AMBER, 10, 40);
    for (i = 0; i < n; i++) { int32_t y, lo = 2 * mid - (up[i] >> 4); for (y = mid; y < lo; y++) vg_px(x0 + i, y, C_AMBER, 40u - (uint32_t)(y - mid) * 30u / (uint32_t)A); }
    vg_rect(x0, mid, n, 1, 0xFFFFFF, 14);
    for (i = 0; i < n; i += 3) vg_px(x0 + i, fr[i] >> 4, C_AMBER, 150);   /* the frequency, dotted */
    vg_curve(x0, wv, n, 0x8FE9FF, 256, 80);
    vg_rect(x0 + cf, 18, 1, 96, C_AMBER, 90);
    vg_text_al(x0 + cf - 3, 28, &F_XS, "FUSIONE", C_AMBER, 256, 2);
    vg_text(x0 + cf + 4, 28, &F_XS, "RINGDOWN", C_VIOL, 256);
    vg_text(x0, 28, &F_XS, "INSPIRAL", C_DIM, 256);
    vg_text(x0, 14, &F_SM, "h(t)", C_TXT, 256);
    {   /* the frequencies of the last key */
        uint32_t fr_hz = hz16_of_inc(inc_of((int32_t)last_notes[0] << 16)) / 16u, fm = fr_hz * 77u / 100u;
        uint32_t f0 = (uint32_t)(((uint64_t)fm * exp2_q16(-oct)) >> 16);
        e = cat_u(s, f0); e = cat(e, " \x80 "); e = cat_u(e, fm); e = cat(e, " \x80 "); e = cat_u(e, fr_hz); cat(e, " Hz");
        vg_text_al(234, 14, &F_SM, s, C_AMBER, 256, 2);
    }
    vg_text(x0, 126, &F_XS, "frequenza", 0xB07A40, 256);
    if (v >= 0) {                                   /* where the last event is */
        uint32_t st = vis[v].stage;
        int32_t x;
        if (st == ST_INSP) x = (int32_t)(vis[v].prog * (uint32_t)cf >> 16);
        else { uint32_t since = t_merge[v] ? fm1_ms - t_merge[v] : 0u; x = cf + (int32_t)(since * (uint32_t)(n - cf) / 1500u); if (x > n - 1) x = n - 1; }
        vg_rect(x0 + x, 32, 1, 84, 0xFFFFFF, 190);
        vg_glow((x0 + x) * 16, 32 * 16, 5, 0xFFFFFF, 200);
    }
}
static void draw_ringdown(void)
{
    static int16_t ys[150];
    uint32_t spin = par[P_SPIN], nm = (uint32_t)par[P_MODI] + 1u, k, live = 90;
    int32_t i, x0 = 8, n = 142, yb = 100, ht = 70, v = latest_voice(), lo = -27200, span = 134180;
    uint32_t r12[NM], w8[NM];
    int32_t lr[NM];
    static const uint16_t SNAP[NM] = {4096, 6144, 5120, 8192};
    static const uint8_t HS[NM] = {255, 160, 120, 90};
    draw_vbg(VIS_H);
    if (v >= 0 && vis[v].stage >= ST_RING) { int32_t a = vis[v].amp; live = (uint32_t)(a > 12000 ? 256 : 90 + a * 166 / 12000); }
    for (k = 0; k < NM; k++) {
        uint32_t q8 = (uint32_t)(((uint64_t)QNM_Q[spin * 4u + k] * ring_mul16(par[P_SMORZ])) >> 16);
        r12[k] = QNM_R[spin * 4u + k] + (uint32_t)(((int32_t)SNAP[k] - (int32_t)QNM_R[spin * 4u + k]) * (int32_t)par[P_ACC] / 100);
        lr[k] = log2_q16(r12[k]) - (12 << 16);
        w8[k] = 45824u / (q8 < 64u ? 64u : q8) + 2u;    /* the peak's width ~ 1/Q, Q8 octaves */
    }
    for (i = 0; i < n; i++) {                       /* log axis: x0.83 .. x3.1 */
        int32_t l = lo + i * span / n, y = 0;
        for (k = 0; k < nm; k++) {
            int32_t d = (l - lr[k]) >> 8;
            uint32_t d2 = (uint32_t)(d * d) * 256u / (w8[k] * w8[k] + 1u);
            y += (int32_t)(HS[k] * 256u / (256u + d2));
        }
        if (y > 300) y = 300;
        ys[i] = (int16_t)((yb * 16) - (y * ht * 16 / 256) * (int32_t)live / 256);
    }
    vg_fill_under(x0, ys, n, yb, yb - ht, C_VIOL, 150, 6);
    vg_curve(x0, ys, n, 0xD5C2FF, 256, 90);
    vg_rect(x0, yb, n, 1, 0xFFFFFF, 30);
    for (k = 0; k < nm; k++) {
        char s[10];
        uint32_t r = r12[k] * 100u / 4096u;
        int32_t x = x0 + (lr[k] - lo) * n / span;
        s[0] = '\x82'; s[1] = (char)('0' + r / 100u); s[2] = ','; s[3] = (char)('0' + r / 10u % 10u); s[4] = (char)('0' + r % 10u); s[5] = 0;
        vg_rect(x, yb, 1, 3, 0xD5C2FF, 140);
        vg_text_al(x, yb + 13 + (k == 2u ? 10 : 0), &F_XS, s, 0xD5C2FF, 220, 1);
    }
    vg_text(x0, 14, &F_SM, "MODI", C_TXT, 256);
    vg_text(x0 + 40, 14, &F_XS, par[P_ACC] < 4u ? "FISICI" : par[P_ACC] > 96u ? "IN ACCORDO" : "MISTI", C_VIOL, 256);
    {   /* the remnant */
        int32_t bx = 196 * 16, by = 52 * 16, rr;
        uint32_t a = spin, rot = (fm1_ms * (4u + a / 4u)) & 1023u;
        for (k = 0; k < 64u; k++) {                 /* the ergosphere, dotted, flattened by the spin */
            uint32_t an = k << 26;
            if (k & 1u) continue;
            vg_px((bx + ((cos_q15(an) * (int32_t)(30 + a / 18) * 16) >> 15)) >> 4, (by + ((sin_q15(an) * (int32_t)(30 - a / 9) * 16) >> 15)) >> 4, C_VIOL, 120);
        }
        if (v >= 0 && vis[v].stage >= ST_RING && t_merge[v]) {
            uint32_t since = fm1_ms - t_merge[v];
            for (k = 0; k < 4u; k++) {
                rr = (int32_t)((since / 16u + k * 8u) % 34u) + 16;
                vg_ring(bx, by, rr * 16, 10, C_VIOL, live * (uint32_t)(50 - rr) / 70u);
            }
        }
        vg_glow(bx, by, 30, 0x8A63FF, 60 + live / 3);
        for (k = 0; k < 64u; k++) {                 /* the photon ring: brighter where it turns toward us */
            uint32_t an = k * 16u, d = (an - rot) & 1023u, b;
            int32_t c = cos_q15(d << 22);
            b = (uint32_t)(70 + ((c + 32768) * (60 + (int32_t)a * 2)) / 65536);
            vg_arc(bx, by, 14 * 16, 40, an, 17u, 0xFFE2BE, b > 256u ? 256u : b);
        }
        vg_disc(bx, by, 12 * 16, 0x000000, 256);
        {
            char s[40], *e;
            uint32_t mf = v >= 0 ? (uint32_t)(v_m1[v] + v_m2[v]) * 95u / 100u : (mass10(par[P_M1]) + mass10(par[P_M2])) * 95u / 100u;
            e = cat(s, "Mf "); e = cat_u(e, mf / 10u); e = cat(e, " \x7F  a 0,"); *e++ = (char)('0' + a / 10u); *e++ = (char)('0' + a % 10u); *e = 0;
            vg_text_al(196, 104, &F_XS, s, 0xDCD2FF, 256, 1);
            if (v >= 0) { e = cat_u(s, vis[v].stage >= ST_RING ? vis[v].mhz16[0] / 16u : hz16_of_inc(inc_of((int32_t)v_note[v] << 16)) / 16u); cat(e, " Hz"); }
            else cat(s, "-");
            vg_text_al(196, 118, &F_XS, s, 0x9A90C0, 256, 1);
        }
    }
}
static int16_t scope_h[96];
static uint32_t scope_w;
static void draw_interf(void)
{
    int32_t h = vis_strain, on = par[P_ONDA], fin = par[P_FIN], br = par[P_ARM], k;
    int32_t Lx = 14, Ly = 92, Bx = 48, By = 92, Dx = 48, Dy = 116;
    int32_t xe = 118 + br * 96 / 100 + h * on / 9000, ye = 58 - br * 34 / 100 - h * on / 9000;
    uint32_t I;
    draw_vbg(VIS_H);
    {   /* the output: the dark port lights as the arms differ */
        int32_t d = (h * on) >> 9;
        I = (uint32_t)((sin_q15((uint32_t)d << 18) * sin_q15((uint32_t)d << 18)) >> 15) + 300u;
        if (I > 32767u) I = 32767u;
        scope_h[scope_w++ % 96u] = (int16_t)I;
    }
    for (k = 0; k < 1 + fin / 20; k++) {            /* the cavities: the light stored in the arms */
        int32_t o = (k - fin / 40) * 6;
        vg_line_add(64 * 16, By * 16 + o, xe * 16, By * 16 + o, 0xFF7A5C, 40 + (uint32_t)fin / 2u);
        vg_line_add(Bx * 16 + o, 76 * 16, Bx * 16 + o, ye * 16, 0xFF7A5C, 40 + (uint32_t)fin / 2u);
    }
    vg_beam(Lx * 16, Ly * 16, Bx * 16, By * 16, 0xFFC0B0, 0xFF7A5C, 256);
    vg_beam(Bx * 16, By * 16, xe * 16, By * 16, 0xFFC0B0, 0xFF7A5C, 256);
    vg_beam(Bx * 16, By * 16, Bx * 16, ye * 16, 0xFFC0B0, 0xFF7A5C, 256);
    vg_beam(Bx * 16, By * 16, Dx * 16, Dy * 16, 0xFFC0B0, 0xFF7A5C, 40 + I / 150u);
    vg_rect(Lx - 10, Ly - 4, 10, 8, 0x1B2232, 256); vg_rect(Lx - 10, Ly - 4, 10, 1, 0x3A4560, 256);
    vg_glow(Lx * 16, Ly * 16, 4, 0xFF7A5C, 230);
    vg_line((Bx - 5) * 16, (By + 5) * 16, (Bx + 5) * 16, (By - 5) * 16, 0xCFE3FF, 256);
    vg_rect(64, By - 4, 2, 9, 0xDDE8F7, 256); vg_rect(Bx - 4, 76, 9, 2, 0xDDE8F7, 256);   /* the cavities' input mirrors */
    vg_rect(xe, By - 5, 2, 11, 0xDDE8F7, 256); vg_rect(Bx - 5, ye - 1, 11, 2, 0xDDE8F7, 256);
    vg_glow(xe * 16, By * 16, 6, 0xFF7A5C, 140); vg_glow(Bx * 16, ye * 16, 6, 0xFF7A5C, 140);
    vg_glow(Dx * 16, Dy * 16, 4 + (int32_t)(I / 3000u), 0xFFD3C4, 80 + I / 160u);
    vg_rect(Dx - 4, Dy, 8, 3, 0xE8F1FF, 256);
    vg_text_al((64 + xe) / 2, By + 13, &F_XS, "X  3 km", 0xFFC8B4, 220, 1);
    vg_text(Bx + 6, (76 + ye) / 2 + 4, &F_XS, "Y  3 km", 0xFFC8B4, 220);
    vg_text(Lx - 10, Ly - 8, &F_XS, "LASER", C_FAINT, 256);
    {   /* the scope of the output */
        int32_t sx = 140, sy = 8, sw = 96, shh = 44;
        static int16_t ys[92];
        vg_rect(sx, sy, sw, shh, 0x0A0E1A, 220);
        vg_rect(sx, sy, sw, 1, 0xFF7A5C, 60); vg_rect(sx, sy + shh - 1, sw, 1, 0xFF7A5C, 60);
        vg_rect(sx, sy, 1, shh, 0xFF7A5C, 60); vg_rect(sx + sw - 1, sy, 1, shh, 0xFF7A5C, 60);
        vg_text(sx + 4, sy + 11, &F_XS, "USCITA", 0xFFC8B4, 256);
        for (k = 0; k < 92; k++) ys[k] = (int16_t)((sy + shh - 4) * 16 - (int32_t)scope_h[(scope_w + 4u + (uint32_t)k) % 96u] * (shh - 16) * 16 / 32768);
        vg_curve(sx + 2, ys, 92, 0xFFB09A, 256, 70);
        {
            char s[24], *e = cat(s, "FINESSE "); e = cat_u(e, (uint32_t)fin * 9u / 2u);
            vg_text_al(sx + sw, sy + shh + 14, &F_XS, s, 0xFFC8B4, 220, 2);
        }
    }
}
static int32_t lev8(uint32_t k) { return k ? (int32_t)(((int64_t)(log2_q16(k) - log2_q16(100)) * 77) >> 16) : -2000; }   /* log10 (k / 100), Q8 */
static void draw_rumore(void)
{
    static int16_t tot[208], cs[208], ct[208], cq[208];
    static int16_t tl[208];
    int32_t i, x0 = 26, n = 208, y0 = 20, y1 = 106, v = latest_voice();
    int32_t S = lev8(par[P_SEIS]), T = lev8(par[P_THERM]), Q = lev8(par[P_QUANT]), G = glitch_cols ? (log2_q16((vis_glitch & 0xFFFFu) ? vis_glitch & 0xFFFFu : 1u) * 77) >> 16 : -9999;
    draw_vbg(VIS_H);
#define YV(x) (int16_t)((y1 * 16) - ((((x) < -614 ? -614 : (x) > 231 ? 231 : (x)) + 614) * (y1 - y0) * 16 / 845))
    for (i = 0; i < n; i++) {                       /* log f 10 Hz .. 5 kHz; levels in log10 (Q8), -2.4 .. 0.9 */
        int32_t lf = 256 + i * 691 / n, ls, lt, lq, mx, d1, d2, dg;
        ls = S + 420 - (lf - 256) * 4;
        lt = T - 60 - (lf - 256) / 4;
        d1 = lf - 678; d2 = lf - 755;
        if (d1 < 0) d1 = -d1;
        if (d2 < 0) d2 = -d2;
        if (d1 < 4) lt += (4 - d1) * 45;            /* the violin modes, 452 and 904 Hz */
        if (d2 < 4) lt += (4 - d2) * 30;
        lq = Q - 200 + (lf > 589 ? lf - 589 : 0);
        mx = ls > lt ? ls : lt;
        if (lq > mx) mx = lq;
        dg = lf - G; if (dg < 0) dg = -dg;
        if (dg < 40) mx += (40 - dg) * 9;
        tl[i] = (int16_t)(mx + 18);
        tot[i] = YV(mx + 18); cs[i] = YV(ls); ct[i] = YV(lt); cq[i] = YV(lq);
    }
    for (i = 0; i < 3; i++) {
        static const char *const L[3] = {"10 Hz", "100", "1 kHz"};
        int32_t x = x0 + i * 256 * n / 691;
        vg_rect(x, y0, 1, y1 - y0, 0xFFFFFF, 14);
        vg_text_al(x, y1 + 12, &F_XS, L[i], C_FAINT, 256, 1);
    }
    vg_fill_under(x0, tot, n, y1, y0, 0x7DFFB0, 80, 6);
    if (par[P_SEIS]) vg_curve(x0, cs, n, C_AMBER, 170, 0);
    if (par[P_THERM]) vg_curve(x0, ct, n, C_VIOL, 190, 0);
    if (par[P_QUANT]) vg_curve(x0, cq, n, C_CYAN, 190, 0);
    vg_curve(x0, tot, n, 0xB9FFD4, 256, 80);
    vg_text(x0, 14, &F_XS, "SISMICO", C_AMBER, 256);
    vg_text(x0 + 52, 14, &F_XS, "TERMICO", C_VIOL, 256);
    vg_text(x0 + 104, 14, &F_XS, "QUANTISTICO", C_CYAN, 256);
    if (glitch_cols) vg_text_al(x0 + (G - 256) * n / 691, 34, &F_SM, "GLITCH", 0xFFFFFF, 256, 1);
    if (v >= 0 && vis[v].amp > 200) {               /* the event over the noise */
        uint32_t hz = (vis[v].stage == ST_INSP ? vis[v].hz16 : vis[v].mhz16[0]) / 16u;
        int32_t lf = (log2_q16(hz ? hz : 1u) * 77) >> 16, x = x0 + (lf - 256) * n / 691;
        int32_t ly = (int32_t)(((int64_t)(log2_q16((uint32_t)vis[v].amp) - log2_q16(32768)) * 77) >> 16) + 160, y;
        char s[16], *e;
        if (x >= x0 && x < x0 + n) {
            uint32_t snr = exp2_q16((ly - tl[x - x0]) * 850) >> 16;   /* 10^(d / 256) */
            y = YV(ly) >> 4;
            vg_rect(x, y, 1, y1 - y, C_AMBER, 120);
            vg_glow(x * 16, y * 16, 6, C_AMBER, 230);
            vg_disc(x * 16, y * 16, 18, 0xFFFFFF, 256);
            e = cat(s, "SNR ");
            cat_u(e, snr > 999u ? 999u : snr);
            vg_text_al(234, 30, &F_SM, s, C_AMBER, 256, 2);
        }
    } else vg_text_al(234, 30, &F_XS, "NESSUN SEGNALE", C_FAINT, 256, 2);
#undef YV
}
static const uint16_t *star_tab(void)               /* 256 stars: x, y around the galaxy's band, size and phase (packed) */
{
    static uint16_t t[256 * 2];
    static uint32_t made;
    uint32_t i, r = 0xBEEF1234u;
    if (made) return t;
    made = 1;
    for (i = 0; i < 256u; i++) {
        uint32_t x, y, g;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5; x = r % 236u + 2u;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5; g = (r & 63u) + ((r >> 6) & 63u) + ((r >> 12) & 63u);   /* ~ normal */
        y = (uint32_t)((int32_t)(78 - (int32_t)x * 52 / 236) + ((int32_t)g - 94) * 2 / 3);
        if ((int32_t)y < 4) y = 4;
        if (y > 92u) y = 92u;
        t[2u * i] = (uint16_t)(x | y << 8);
        t[2u * i + 1u] = (uint16_t)(r >> 16);
    }
    return t;
}
static void draw_cielo(void)
{
    const uint16_t *st = star_tab();
    uint32_t n = dens_n(par[P_DENS]), lv = par[P_SKY], i, k, hz = band_hz(par[P_BAND]);
    char s[24], *e;
    vg_vgrad(0, 0, 240, VIS_H, RGB24(5, 8, 18), 0x0A1124);
    for (k = 0; k < 9u; k++)                        /* the galaxy's band: light along a slanting line */
        vg_glow((int32_t)(k * 30u) * 16, (78 - (int32_t)(k * 30u) * 52 / 236) * 16, 32, 0x8CA0E6, 34 + lv / 4u);
    for (i = 0; i < (n < 256u ? n : 256u); i++) {
        uint32_t x = st[2u * i] & 255u, y = st[2u * i] >> 8, rr = st[2u * i + 1u], b;
        b = 70u + ((uint32_t)(sin_q15((uint32_t)(fm1_ms * (3u + (rr & 7u))) << 18 ^ rr << 16) + 32768) * (60u + lv) >> 16);
        b = b * (70u + (rr >> 9 & 63u)) >> 7;
        if (rr & 0x8000u) vg_disc((int32_t)x * 16, (int32_t)y * 16, 10, 0xD2E1FF, b > 256u ? 256u : b);
        else vg_px((int32_t)x, (int32_t)y, 0xD2E1FF, b > 256u ? 256u : b);
    }
    vg_rect(6, 116, 228, 1, 0xFFFFFF, 18);
    for (i = 0; i < (n < 160u ? n : 160u); i++) {  /* the band: a line a binary; the first ones are those that sound */
        uint32_t rr = st[2u * i + 1u];
        int32_t oct, x, hh;
        if (i < NSKY) oct = sky[i].oct >> 8;
        else {
            oct = (int32_t)(((rr * 2654435761u) >> 15) & 0x1FFFFu) - 65536 + (int32_t)((fm1_ms * (uint32_t)par[P_DRIFT] / 16u) % 131072u);
            if (oct > 65536) oct -= 131072;
        }
        x = 6 + (oct + 65536) * 228 / 131072;
        if (x < 6 || x > 233) continue;
        hh = (int32_t)(4u + ((rr >> 4) & 15u)) * (int32_t)(20u + lv) / 120;
        vg_rect(x, 116 - hh, 1, hh, 0x9FC4FF, i < NSKY ? 200u : 90u);
    }
    e = cat(s, "BINARIE "); cat_u(e, n);
    vg_text(6, 14, &F_SM, s, C_TXT, 256);
    e = cat_u(s, hz); cat(e, " Hz");
    vg_text_al(120, 127, &F_XS, s, C_DIM, 256, 1);
    e = cat_u(s, hz / 2u); cat(e, " Hz");
    vg_text(6, 127, &F_XS, s, C_FAINT, 256);
    e = cat_u(s, hz * 2u); cat(e, " Hz");
    vg_text_al(234, 127, &F_XS, s, C_FAINT, 256, 2);
    e = cat(s, "DERIVA "); e = cat_u(e, par[P_DRIFT]); cat(e, "%");
    vg_text_al(234, 14, &F_XS, s, 0x9FC4FF, 256, 2);
}
static void draw_run(void)
{
    uint32_t i;
    char s[24], *e;
    draw_vbg(VIS_H);
    vg_text(6, 14, &F_SM, "CATALOGO", C_TXT, 256);
    vg_text_al(110, 126, &F_XS, "M\x7F", C_FAINT, 256, 2);
    vg_text_al(146, 126, &F_XS, "Mpc", C_FAINT, 256, 2);
    vg_text_al(146, 14, &F_XS, run_on && par[P_RATE] ? "IN CORSO" : "FERMO", run_on && par[P_RATE] ? C_CYAN : C_FAINT, 256, 2);
    for (i = 0; i < nlog; i++) {
        int32_t y = 22 + (int32_t)i * 17;
        const cat_e *c = &evlog[i];
        if (!i) { uint32_t age = fm1_ms - c->t; vg_rect(4, y, 144, 16, C_CYAN, age < 3000u ? 46u - age * 30u / 3000u : 16u); vg_rect(4, y, 2, 16, C_CYAN, 256); }
        vg_text(10, y + 12, &F_XS, c->name, i ? 0xC8D2E6 : C_TXT, 256);
        e = cat_u(s, (c->m1 + 5u) / 10u); e = cat(e, "+"); cat_u(e, (c->m2 + 5u) / 10u);
        vg_text_al(110, y + 12, &F_XS, s, C_DIM, 256, 2);
        cat_u(s, c->mpc);
        vg_text_al(146, y + 12, &F_XS, s, C_FAINT, 256, 2);
    }
    if (!nlog) vg_text(10, 40, &F_XS, "nessun evento: PLAY o REC", C_FAINT, 256);
    spec_blit(154, 6, 82, 116, 2);
    vg_rect(154, 6, 82, 1, C_CYAN, 70); vg_rect(154, 121, 82, 1, C_CYAN, 70);
    vg_rect(154, 6, 1, 116, C_CYAN, 70); vg_rect(235, 6, 1, 116, C_CYAN, 70);
    vg_disc(161 * 16, 14 * 16, 26, 0xFF5A5A, (fm1_ms >> 8) & 1u ? 256u : 70u);
    vg_text(166, 17, &F_XS, "IN DIRETTA", 0xFFFFFF, 220);
}
static void draw_campione(void)
{
    char s[32], *e;
    draw_vbg(VIS_H);
    if (!nsnd) {
        vg_text_al(120, 50, &F_SM, "NESSUN CAMPIONE", C_TXT, 256, 1);
        vg_text_al(120, 70, &F_XS, "invia la banca utente dall'editor", C_DIM, 256, 1);
        vg_text_al(120, 84, &F_XS, "(la stessa di GRANA 1.6)", C_FAINT, 256, 1);
        return;
    }
    if (!par[P_SND]) { vg_text_al(120, 60, &F_SM, "SUONO: OFF", C_DIM, 256, 1); e = cat_u(s, nsnd); cat(e, nsnd == 1u ? " suono nella banca" : " suoni nella banca"); vg_text_al(120, 80, &F_XS, s, C_FAINT, 256, 1); return; }
    {
        int32_t x0 = 20, n = 200, mid = 64, i, ls = (int32_t)(smp_from * 200u / (smp_full ? smp_full : 1u)), le;
        uint32_t d2 = smp_full && snd[par[P_SND] - 1u].r11 ? 2u : 1u;
        le = ls + (int32_t)((smp_len / d2) / ((smp_full + 199u) / 200u ? (smp_full + 199u) / 200u : 1u));
        for (i = 0; i < n; i++) {
            int32_t h = smp_peak[i] * 44 / 256, in = i >= ls && i < le;
            vg_rect(x0 + i, mid - h, 1, 2 * h + 1, in ? 0xFFD27A : 0x5A6070, in ? 230u : 160u);
        }
        vg_rect(x0 + ls, 18, 1, 92, 0xFFFFFF, 200);
        vg_text(x0 + ls + 3, 26, &F_XS, "INIZIO", 0xFFFFFF, 220);
        vg_text(6, 14, &F_SM, snd_name(par[P_SND] - 1u), C_TXT, 256);
        vg_text_al(234, 14, &F_SM, USEN[par[P_USE] % 3u], 0xFFD27A, 256, 2);
        e = cat_d1(s, smp_len / 2205u); cat(e, " s in memoria");
        vg_text_al(120, 124, &F_XS, s, C_DIM, 256, 1);
    }
}
static void draw_mix(void)                          /* 0.5: AUDIO: the levels, the compressor's work, the stereo field, the tempo */
{
    char s[24], *e;
    int32_t i, l0 = vis_lvl[0], l1 = vis_lvl[1], red;
    static int32_t pk[2];
    uint32_t c = PG_ACC[PG_MIX], v = par[P_STEREO];
    draw_vbg(VIS_H);
    for (i = 0; i < 2; i++) {                       /* the levels, with a slow peak */
        int32_t l = i ? l1 : l0, w = l * 150 / 32767;
        if (w > pk[i]) pk[i] = w; else pk[i] -= 2;
        vg_text(6, 24 + i * 16, &F_XS, i ? "R" : "L", C_DIM, 256);
        vg_rect(18, 16 + i * 16, 150, 9, 0x141B2B, 256);
        vg_rect(18, 16 + i * 16, w, 9, w > 134 ? 0xFF6A5C : 0xC9D4E8, 220);
        vg_rect(18 + (pk[i] > 0 ? pk[i] : 0), 16 + i * 16, 2, 9, 0xFFFFFF, 200);
    }
    {   /* COMP/LIMIT: how much it takes away now, in dB, a bar from the right */
        int32_t g = cp_red < 1 ? 1 : cp_red, db10 = par[P_COMP] ? -(log2_q16((uint32_t)g) - (15 << 16)) * 60 / 65536 : 0, w;
        red = db10;
        if (red < 0) red = 0;
        w = red * 150 / 200;
        if (w > 150) w = 150;
        vg_rect(18, 54, 150, 6, 0x141B2B, 256);
        vg_rect(168 - w, 54, w, 6, c, 230);
        e = cat(s, "COMP ");
        if (!par[P_COMP]) cat(e, "OFF \x84 LIMITE -1 dB"); else { e = cat(e, "-"); e = cat_d1(e, (uint32_t)red); cat(e, " dB"); }
        vg_text(18, 72, &F_XS, s, par[P_COMP] ? C_TXT : C_FAINT, 256);
        if (lim_seen) vg_text_al(168, 72, &F_XS, "LIMITE", 0xFF6A5C, 256, 2);
    }
    {   /* STEREO: the field, a dot where the autopan is */
        int32_t cx = 92, cy = 104, wdt = v <= 40u ? (int32_t)v * 60 / 40 : 60, x;
        vg_rect(cx - 62, cy, 124, 1, 0xFFFFFF, 40);
        vg_rect(cx - wdt, cy - 5, 2 * wdt + 1, 11, c, 50);
        x = v > 40u ? (int32_t)mul15(sin_q15(ap_ph), 58) : 0;
        vg_glow((cx + x) * 16, cy * 16, 5, 0xFFFFFF, 200);
        vg_disc((cx + x) * 16, cy * 16, 40, 0xFFFFFF, 256);
        vg_text(18, 124, &F_XS, "STEREO", C_DIM, 256);
        value_text(P_STEREO, s);
        vg_text_al(168, 124, &F_XS, s, C_TXT, 256, 2);
    }
    e = cat_u(s, clk_ext ? ext_bpm10 / 10u : (uint32_t)par[P_BPM] + 40u);
    vg_text_al(232, 98, &F_XL, s, C_TXT, 256, 2);
    vg_text_al(232, 114, &F_XS, clk_ext ? "BPM (MIDI)" : "BPM", C_DIM, 256, 2);
    (void)e;
}
static void draw_shimmer(void)                      /* the tail's partials climbing (or sinking), the kind's step */
{
    uint32_t c = PG_ACC[PG_SHIM], t = par[P_SHTYPE] % 5u, a = par[P_SHIM], k, j;
    int32_t dq = t == 4u ? 4096 : eng.sh_dq;         /* SPIRALE drawn as its first steps */
    int32_t base = dq < 0 ? 36 : 104;
    char s[32], *e;
    draw_vbg(VIS_H);
    vg_text(6, 14, &F_SM, "SHIMMER", C_TXT, 256);
    vg_text_al(234, 14, &F_SM, SHN[t], c, 256, 2);
    for (k = 0; k < 6u; k++) {                      /* each turn of the tank: the partial moved by the shift, fainter */
        int32_t l2 = 0, y, x0 = 14 + (int32_t)k * 36;
        uint32_t al;
        for (j = 0; j < k; j++) l2 += t == 4u ? (int32_t)((j + 1u) * 5461u) : log2_q16((uint32_t)(65536 + dq)) - (16 << 16);
        y = base - l2 * 14 / 65536;
        if (y < 24) y = 24;
        if (y > 112) y = 112;
        al = a ? 256u - k * 36u : 50u;
        vg_rect(x0, y, 26, 3, k ? c : 0xFFFFFF, al);
        if (k) vg_glow((x0 + 13) * 16, y * 16, 4, c, al);
        if (k) vg_line((x0 - 10) * 16, (y + (y < base ? 6 : -6)) * 16, (x0 - 2) * 16, (y + 1) * 16, c, al / 2u);
    }
    vg_rect(6, base, 228, 1, 0xFFFFFF, 30);
    e = cat(s, "MOVIMENTO "); e = cat_u(e, par[P_RMOV]); e = cat(e, "%  PRE-DELAY "); e = cat_u(e, (uint32_t)par[P_RPRE] * par[P_RPRE] / 100u); cat(e, " ms");
    vg_text(6, 128, &F_XS, s, C_DIM, 256);
    if (!a) vg_text_al(120, 50, &F_XS, "SHIMMER a 0: solo riverbero", C_FAINT, 256, 1);
}
static void draw_midipic(void)
{
    char s[24];
    draw_vbg(VIS_H);
    vg_text_al(120, 46, &F_TI, page == PG_MIDI ? "MIDI" : "BLUETOOTH", C_TXT, 256, 1);
    vg_text_al(120, 66, &F_XS, page == PG_MIDI ? "USB e presa TRS, sempre attive" : "un controller alla volta (OCT+ conferma)", C_DIM, 256, 1);
    if (page == PG_MIDI) {
        char *e = cat(s, "LEARN ");
        e = cat_u(e, store.nlearn); cat(e, "/32");
        vg_text_al(120, 92, &F_SM, s, 0xC9D4E8, 256, 1);
    }
    {   /* the audio's load: the worst block of the last seconds, of the 5.8 ms there are */
        char t[48], *e = cat(t, "VIRGO v" APP_VER "  ");
#ifdef FL_JEDEC_OK
        e = flash_tag(e); e = cat(e, "  ");
#endif
        e = cat(e, "\x84  AUDIO ");
        e = cat_d1(e, audio_max / 100u); e = cat(e, " ms");
        vg_text_al(120, 120, &F_XS, t, gov_max ? C_AMBER : C_FAINT, 256, 1);
        if (gov_max || lim_seen) {                  /* 0.4.1: what the governor and the limiter are doing, plainly */
            e = cat(t, "");
            if (gov_max) { e = cat(e, "CPU: RIDUZIONE "); e = cat_u(e, gov_max); }
            if (gov_max && lim_seen) e = cat(e, "  \x84  ");
            if (lim_seen) cat(e, "VOLUME: LIMITATORE");
            vg_text_al(120, 106, &F_XS, t, C_AMBER, 256, 1);
        }
    }
}

/* ---- 0.2: MOTORE, ARMONIA, FUSIONI, FILTRO, MOD */
static void arrow(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t r, uint32_t c, uint32_t a)   /* px, from circle to circle */
{
    int32_t dx = x1 - x0, dy = y1 - y0, l = 1, ux, uy;
    while (l * l < dx * dx + dy * dy) l++;
    ux = dx * 16 / l; uy = dy * 16 / l;           /* Q4 unit */
    x0 = x0 * 16 + ux * r; y0 = y0 * 16 + uy * r; x1 = x1 * 16 - ux * (r + 1); y1 = y1 * 16 - uy * (r + 1);
    vg_line(x0, y0, x1, y1, c, a);
    vg_line(x1, y1, x1 - ux * 5 - uy * 3, y1 - uy * 5 + ux * 3, c, a);
    vg_line(x1, y1, x1 - ux * 5 + uy * 3, y1 - uy * 5 - ux * 3, c, a);
}
static void op_node(int32_t x, int32_t y, const char *t, uint32_t c, uint32_t lit)
{
    vg_glow(x * 16, y * 16, 16, c, 40u + lit / 2u);
    vg_disc(x * 16, y * 16, 11 * 16, RGB24(10, 13, 26), 256);
    vg_ring(x * 16, y * 16, 11 * 16, 14, c, 150u + lit / 3u);
    vg_text_al(x, y + 4, &F_XS, t, C_TXT, 256, 1);
}
/* 0.3: the engines' pictures (MOTORE 2..5) */
static void draw_engine(void)
{
    uint32_t c = PG_ACC[PG_MOT], eg = par[P_ENG], amt = par[P_IDX], k;
    int32_t v = latest_voice(), i, live = 0;
    static int16_t ys[220];
    char s[32], *e;
    draw_vbg(VIS_H);
    if (v >= 0 && vis[v].stage != ST_OFF) live = vis[v].amp > 0 ? (vis[v].amp > 16000 ? 256 : vis[v].amp / 63) : 0;
    vg_text(6, 14, &F_SM, ENGN[eg % 6u], C_TXT, 256);
    if (eg == 2u) {                                 /* five saws: a cloud, fanning out with APERTURA */
        static const int8_t D[5] = {-100, -43, 0, 47, 97};
        vg_text(6 + vg_text_w(&F_SM, "NEBULOSA") + 6, 14, &F_XS, "5 seghe", C_DIM, 256);
        for (k = 0; k < 5u; k++) {
            uint32_t ph = (fm1_ms * (7u + k)) << 18;
            int32_t det = D[k] * (int32_t)amt;      /* the step, per mille x 100 */
            for (i = 0; i < 190; i++) {
                uint32_t q = ph + (uint32_t)i * (uint32_t)(28000000 + det * 30);
                ys[i] = (int16_t)((70 * 16) - (((int32_t)(q >> 16) - 32768) * 34 * 16 >> 15));
            }
            vg_curve(8, ys, 190, k == 2u ? 0xFFFFFF : c, k == 2u ? 230u : 150u, 40);
        }
        e = cat(s, "\x82"); e = cat_u(e, 5); e = cat(e, "  \x81 "); e = cat_u(e, amt * 50u / 100u); cat(e, " cent");
        vg_text_al(234, 14, &F_XS, s, c, 256, 2);
        vg_text(8, 124, &F_XS, "si chiudono alla fusione, si riaprono nella coda", C_FAINT, 256);
    } else if (eg == 3u) {                          /* the folder: its curve, and a sine through it */
        int32_t G16 = 16 + (int32_t)amt * 112 / 100, bx0 = 8, bw = 92, by = 26, bh = 92, cx = bx0 + bw / 2, cy = by + bh / 2;
        vg_rect(bx0, by, bw, bh, 0x0C1222, 256);
        vg_rect(bx0, cy, bw, 1, 0xFFFFFF, 30); vg_rect(cx, by, 1, bh, 0xFFFFFF, 30);
        for (i = 0; i < bw; i++) {                  /* y = sin (pi/2 G x) */
            int32_t x = (i - bw / 2) * 65536 / bw;   /* -32768 .. 32768 */
            int32_t u = (x * G16) >> 4;
            ys[i] = (int16_t)(cy * 16 - (sin_q15((uint32_t)u << 15) * (bh / 2 - 4) * 16 >> 15));
        }
        vg_curve(bx0, ys, bw, c, 256, 80);
        for (i = 0; i < 120; i++) {                 /* the wave out */
            uint32_t ph = (uint32_t)i * 71582788u + (fm1_ms << 20);
            int32_t u = (sin_q15(ph) * G16) >> 4;
            ys[i] = (int16_t)(cy * 16 - (sin_q15((uint32_t)u << 15) * (bh / 2 - 4) * 16 >> 15));
        }
        vg_curve(112, ys, 120, 0xFFFFFF, 220, 60);
        e = cat(s, "\x82"); cat_d1(e, (uint32_t)G16 * 10u / 16u);
        vg_text_al(234, 14, &F_SM, s, c, 256, 2);
        vg_text(112, 124, &F_XS, "pieghe come v\x82v", C_FAINT, 256);
    } else if (eg == 4u) {                          /* the string: its harmonics, ringing */
        uint32_t hz = v >= 0 ? hz16_of_inc(vs[v].e.ring_inc[0]) / 16u : hz16_of_inc(inc_of((int32_t)last_notes[0] << 16)) / 16u;
        int32_t x0 = 16, x1 = 224, yc = 70, n = x1 - x0, br = 30 + (int32_t)amt;
        vg_disc(x0 * 16, yc * 16, 40, 0xDDE8F7, 256); vg_disc(x1 * 16, yc * 16, 40, 0xDDE8F7, 256);
        for (i = 0; i <= n; i++) {
            int32_t y = 0, h;
            for (h = 1; h <= 6; h++) {
                int32_t a = (int32_t)(32767 / h) * br / 130 * (h == 1 ? 1 : (int32_t)amt + 10) / (h == 1 ? 1 : 110);
                uint32_t sp = (uint32_t)(h * i) * (uint32_t)(2147483648u / (uint32_t)n);
                y += mul15(mul15(sin_q15(sp), cos_q15(fm1_ms * (uint32_t)h * 5000000u)), a);
            }
            ys[i] = (int16_t)(yc * 16 - (y * (20 + live / 8) * 16 >> 15));
        }
        vg_curve(x0, ys, n + 1, c, 256, 90);
        e = cat_u(s, hz); cat(e, " Hz");
        vg_text_al(234, 14, &F_SM, s, c, 256, 2);
        vg_text(8, 124, &F_XS, "il chirp la fa cantare, la fusione la pizzica", C_FAINT, 256);
    } else {                                        /* the voice: three formants, the vowel */
        static const char *const VW[5] = {"U", "O", "A", "E", "I"};
        static const int16_t VF2[5][3] = {{300, 870, 2240}, {570, 840, 2410}, {730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}};
        int32_t pos = (int32_t)amt * 512 / 100 + (live * 512 >> 8), kk, fr, f[3], x0 = 8, n = 200;
        if (pos > 1023) pos = 1023;
        kk = pos >> 8; fr = pos & 255;
        for (k = 0; k < 3u; k++) f[k] = VF2[kk][k] + (((VF2[kk + 1][k] - VF2[kk][k]) * fr) >> 8);
        for (i = 0; i < n; i++) {                   /* 0 .. 4 kHz, linear */
            int32_t hzx = i * 4000 / n, y = 0;
            for (k = 0; k < 3u; k++) {
                int32_t d = (hzx - f[k]) * (k == 0u ? 10 : k == 1u ? 7 : 5) / 100, g = k == 0u ? 32767 : k == 1u ? 23000 : 13000;
                y += g * 64 / (64 + d * d);
            }
            ys[i] = (int16_t)(112 * 16 - (y * 76 * 16 >> 15));
        }
        vg_fill_under(x0, ys, n, 112, 36, c, 120, 8);
        vg_curve(x0, ys, n, 0xFFE0F4, 256, 80);
        for (k = 0; k < 3u; k++) { int32_t x = x0 + f[k] * n / 4000; vg_rect(x, 104, 1, 8, 0xFFFFFF, 160); }
        vg_text_al(214, 70, &F_XL, VW[(pos + 128) >> 8 > 4 ? 4 : (pos + 128) >> 8], c, 256, 1);
        vg_text(x0, 124, &F_XS, "0", C_FAINT, 256); vg_text_al(x0 + n, 124, &F_XS, "4 kHz", C_FAINT, 256, 2);
        vg_text_al(214, 94, &F_XS, "u o a e i", C_DIM, 256, 1);
    }
}
static void draw_motore(void)
{
    static const char *const OPN[4] = {"2,2", "3,3", "4,4", "2,1"};
    static const int16_t POS[5][4][2] = {
        {{30, 70}, {72, 70}, {114, 70}, {156, 70}},   /* ONDA: the modes side by side */
        {{40, 82}, {112, 82}, {112, 36}, {40, 36}},   /* BINARIA */
        {{150, 82}, {66, 36}, {24, 36}, {108, 36}},   /* SPIRALE: (4,4) > (3,3) > (2,1) > (2,2) */
        {{56, 70}, {124, 70}, {-1, 0}, {-1, 0}},      /* ANELLO */
        {{44, 82}, {44, 36}, {100, 82}, {150, 82}}};  /* CAMPANA */
    static const uint8_t OUT[5] = {15, 3, 1, 1, 13};   /* the operators to the output, bits */
    int32_t v = latest_voice(), k, lit = 0, eng = par[P_ENG], al = par[P_ALG] ? par[P_ALG] : 1, a = eng ? al : 0;
    uint32_t c = PG_ACC[PG_MOT], b10 = 0;
    char s[32], *e;
    if (par[P_ENG] >= 2u) { draw_engine(); return; }
    draw_vbg(VIS_H);
    if (v >= 0 && vis[v].stage != ST_OFF) { b10 = (uint32_t)vis[v].idx_now / 17089132u; lit = (int32_t)(b10 > 60u ? 200u : b10 * 10u / 3u); }
    vg_rect(14, 110, 160, 1, c, 120);
    vg_text(178, 113, &F_XS, "h(t)", c, 256);
    for (k = 0; k < 4; k++) {                       /* the outputs, down to the line */
        int32_t x = POS[a][k][0], y = POS[a][k][1];
        if (x < 0 || !(OUT[a] >> k & 1)) continue;
        vg_line(x * 16, (y + 12) * 16, x * 16, 110 * 16, c, 110);
    }
    if (a == 1) { arrow(40, 36, 40, 82, 11, c, 220); arrow(112, 36, 112, 82, 11, c, 220);
                  vg_line(52 * 16, 78 * 16, 100 * 16, 78 * 16, c, 200); vg_line(52 * 16, 86 * 16, 100 * 16, 86 * 16, c, 200);
                  arrow(70, 78, 101, 78, 0, c, 220); arrow(82, 86, 51, 86, 0, c, 220); }
    if (a == 2) { arrow(24, 36, 66, 36, 11, c, 220); arrow(66, 36, 108, 36, 11, c, 220); arrow(108, 36, 150, 82, 11, c, 220); }
    if (a == 3) { vg_arc(56 * 16, 52 * 16, 9 * 16, 14, 640, 768, c, 220); arrow(66, 58, 61, 61, 0, c, 220);
                  vg_text_al(90, 74, &F_SM, "\x82", c, 256, 1); vg_line(68 * 16, 70 * 16, 84 * 16, 70 * 16, c, 160); vg_line(96 * 16, 70 * 16, 112 * 16, 70 * 16, c, 160);
                  vg_line(90 * 16, 78 * 16, 90 * 16, 110 * 16, c, 110); }
    if (a == 4) arrow(44, 36, 44, 82, 11, c, 220);
    for (k = 0; k < 4; k++) {
        int32_t x = POS[a][k][0], y = POS[a][k][1];
        if (x < 0) continue;
        op_node(x, y, OPN[k], c, (OUT[a] >> k & 1) ? 80u : (uint32_t)lit + 60u);
    }
    vg_text(6, 14, &F_SM, eng ? ALGN[al] : "ONDA", C_TXT, 256);
    vg_text(6 + vg_text_w(&F_SM, eng ? ALGN[al] : "ONDA") + 6, 14, &F_XS, eng ? (par[P_ALG] ? "FM" : "FM  AUTO") : "modi additivi", C_DIM, 256);
    {   /* the index: the light of the coalescence, ~ v^2 */
        int32_t x = 206, y0 = 24, h = 80, f = (int32_t)(b10 > 120u ? 120u : b10) * h / 120;
        vg_rect(x, y0, 12, h, 0x141B2B, 256);
        vg_vgrad(x, y0 + h - f, 12, f, 0xFFD0F0, c);
        e = cat(s, "\x82"); e = cat_d1(e, b10); (void)e;
        vg_text_al(212, y0 + h + 13, &F_XS, eng ? s : "-", C_TXT, 256, 1);
        vg_text_al(212, 14, &F_XS, "INDICE", C_DIM, 256, 1);
    }
    if (eng) { e = cat(s, "FUSIONE \x80 "); cat(e, ALGN[par[P_ALG] ? par[P_ALG] : 4]); vg_text_al(196, 126, &F_XS, s, C_FAINT, 256, 2); }
}
static void triad_name(char *s, uint32_t root, uint32_t mn) { char *e = cat(s, NTN[root % 12u]); cat(e, mn ? " min" : " magg"); }
static void draw_armonia(void)
{
    int32_t cx = 66, cy = 68, R = 46, k;
    uint32_t mk = scale_mask(par[P_SPIN]), ton = par[P_TONIC] % 12u, c = PG_ACC[PG_ARM];
    int32_t pt[12][2], tr[3];
    char s[32], *e;
    draw_vbg(VIS_H);
    for (k = 0; k < 12; k++) {                      /* the clock of the notes, C on top */
        uint32_t an = (uint32_t)k << 28;
        pt[k][0] = cx * 16 + ((sin_q15(an) * R * 16) >> 15);
        pt[k][1] = cy * 16 - ((cos_q15(an) * R * 16) >> 15);
    }
    vg_ring(cx * 16, cy * 16, R * 16, 10, 0xFFFFFF, 30);
    tr[0] = tz_root; tr[1] = (tz_root + (tz_minor ? 3 : 4)) % 12; tr[2] = (tz_root + 7) % 12;
    for (k = 0; k < 3; k++) vg_line(pt[tr[k]][0], pt[tr[k]][1], pt[tr[(k + 1) % 3]][0], pt[tr[(k + 1) % 3]][1], c, 200);
    vg_glow(cx * 16, cy * 16, 20, c, 40);
    for (k = 0; k < 12; k++) {
        uint32_t in = mk >> ((12u + (uint32_t)k - ton) % 12u) & 1u, it = (uint32_t)k == tr[0] || k == tr[1] || k == tr[2];
        int32_t lx = cx * 16 + ((sin_q15((uint32_t)k << 28) * (R + 11) * 16) >> 15), ly = cy * 16 - ((cos_q15((uint32_t)k << 28) * (R + 11) * 16) >> 15);
        if (it) vg_glow(pt[k][0], pt[k][1], 7, c, 200);
        vg_disc(pt[k][0], pt[k][1], in ? 52 : 26, in ? (it ? 0xFFFFFF : c) : 0x3A4560, 256);
        if ((uint32_t)k == ton) vg_ring(pt[k][0], pt[k][1], 88, 12, 0xFFFFFF, 200);
        vg_text_al(lx >> 4, (ly >> 4) + 4, &F_XS, NTN[k], in ? C_TXT : C_FAINT, in ? 230u : 160u, 1);
    }
    triad_name(s, tz_root, tz_minor);
    vg_text(136, 30, &F_MD, s, C_TXT, 256);
    {
        const char *t = tz_last == 'P' ? "P  parallela" : tz_last == 'L' ? "L  scambio" : tz_last == 'R' ? "R  relativa" : tz_last == 'G' ? "G  GRAVIT\x83" : "accordo di casa";
        vg_text(136, 46, &F_XS, par[P_HIER] ? t : "FUSIONI: OFF", par[P_HIER] ? c : C_FAINT, 256);
    }
    e = cat(s, par[P_SCALE] == 1u ? "ROT: " : ""); cat(e, SCALEN[par[P_SCALE] == 1u ? scale_shown(par[P_SPIN]) : par[P_SCALE] % NSCALE]);
    vg_text(136, 72, &F_XS, s, C_DIM, 256);
    e = cat(s, "CHIRP "); cat(e, CQN[par[P_CQ] % 3u]);
    vg_text(136, 86, &F_XS, s, C_DIM, 256);
    {   /* the masses' interval */
        uint32_t m1 = mass10(par[P_M1]), m2 = mass10(par[P_M2]), hi = m1 > m2 ? m1 : m2, lo = m1 > m2 ? m2 : m1;
        e = cat(s, par[P_IVL] ? "MASSE: " : "MASSE (libero): "); cat(e, IVL_N[ivl_index(hi, lo)]);
        vg_text(136, 112, &F_XS, s, par[P_IVL] ? C_TXT : C_FAINT, 256);
        e = cat_u(s, (hi + 5u) / 10u); e = cat(e, " : "); cat_u(e, (lo + 5u) / 10u);
        vg_text(136, 124, &F_XS, s, C_FAINT, 256);
    }
}
static uint8_t fu_root[8], fu_min[8], fu_tr[8];
static uint16_t fu_m[8];
static uint32_t fu_n, fu_seen;
static void draw_fusioni(void)
{
    uint32_t c = PG_ACC[PG_FUS], i, n;
    char s[32], *e;
    if (hier_n != fu_seen) {                        /* a new event in the chain */
        if (hier_n < fu_seen || !hier_n) fu_n = 0;
        if (hier_n) { uint32_t j = fu_n < 8u ? fu_n++ : 7u; if (j == 7u && fu_n == 8u) for (i = 0; i < 7u; i++) { fu_root[i] = fu_root[i + 1u]; fu_min[i] = fu_min[i + 1u]; fu_tr[i] = fu_tr[i + 1u]; fu_m[i] = fu_m[i + 1u]; }
                      fu_root[j] = tz_root; fu_min[j] = tz_minor; fu_tr[j] = tz_last; fu_m[j] = (uint16_t)hier_m10; }
        fu_seen = hier_n;
    }
    draw_vbg(VIS_H);
    vg_text(6, 14, &F_SM, "FUSIONI", C_TXT, 256);
    vg_text_al(234, 14, &F_XS, HIERN[par[P_HIER] & 3u], par[P_HIER] ? c : C_FAINT, 256, 2);
    n = fu_n;
    if (!par[P_HIER]) { vg_text_al(120, 60, &F_XS, "ogni evento parte da capo", C_DIM, 256, 1); vg_text_al(120, 76, &F_XS, "scegli RUN, TASTI o RUN+TASTI", C_FAINT, 256, 1); }
    else if (!n) vg_text_al(120, 64, &F_XS, "il primo buco nero aspetta", C_DIM, 256, 1);
    for (i = 1; i < n; i++) vg_line((int32_t)(i - 1u) * 28 * 16 + 320, (58 + (int32_t)(sin_q15((i - 1u) << 29) * 10 >> 15)) * 16,
                                    (20 + (int32_t)i * 28) * 16, (58 + (int32_t)(sin_q15(i << 29) * 10 >> 15)) * 16, c, 90);
    for (i = 0; i < n; i++) {                       /* the chain: each remnant swallows the next */
        int32_t x = 20 + (int32_t)i * 28, y = 58 + (int32_t)(sin_q15(i << 29) * 10 >> 15), r = 4 + (int32_t)fu_m[i] / 70;
        if (r > 12) r = 12;
        if (i + 1u == n) vg_glow(x * 16, y * 16, r * 3, c, 200);
        vg_disc(x * 16, y * 16, r * 16, 0x000000, 256);
        vg_ring(x * 16, y * 16, r * 16, 12, i + 1u == n ? 0xFFFFFF : c, 220);
        e = cat(s, NTN[fu_root[i] % 12u]); cat(e, fu_min[i] ? "m" : "");
        vg_text_al(x, 92, &F_XS, s, i + 1u == n ? C_TXT : C_DIM, 256, 1);
        s[0] = fu_tr[i] ? (char)fu_tr[i] : '-'; s[1] = 0;
        vg_text_al(x, 104, &F_XS, s, C_FAINT, 256, 1);
    }
    {   /* the remnant against the cap (150 M sun) */
        uint32_t w = hier_m10 * 228u / 1500u;
        vg_rect(6, 114, 228, 4, 0x141B2B, 256);
        vg_rect(6, 114, (int32_t)w, 4, c, 200);
        e = cat(s, "RESTO "); e = cat_u(e, hier_m10 / 10u); e = cat(e, " M\x7F   GRAVIT\x83 "); e = cat_u(e, par[P_GRAV]); cat(e, "%");
        vg_text(6, 128, &F_XS, s, C_DIM, 256);
    }
}
static int32_t l2_64(uint64_t x) { int32_t sh = 0; while (x >> 32) { x >>= 1; sh++; } return log2_q16(x ? (uint32_t)x : 1u) + (sh << 16); }
static void draw_filtro(void)
{
    static int16_t ys[220];
    int32_t i, x0 = 8, n = 220, y0 = 20, y1 = 112, v = latest_voice(), ft = par[P_FTYPE];
    int32_t lfc, lmark = -1, lo = log2_q16(25u * 16u);   /* the axis: 25 Hz .. 8 kHz, log2 of Hz16 */
    uint32_t c = PG_ACC[PG_FILT], qq8 = 154u + par[P_RES] * 26u, fc;
    char s[32], *e;
    draw_vbg(VIS_H);
    if (v >= 0 && vis[v].stage != ST_OFF && vis[v].fc_hz > 0) fc = (uint32_t)vis[v].fc_hz;
    else fc = (uint32_t)(((uint64_t)(hz16_of_inc(inc_of((int32_t)last_notes[0] << 16)) / 16u) * exp2_q16(-131072 + (int32_t)par[P_FREQ] * 4588)) >> 16);
    if (fc < 20u) fc = 20u;
    lfc = log2_q16(fc * 16u);
    if (v >= 0 && vis[v].stage != ST_OFF) lmark = log2_q16(vis[v].stage == ST_INSP ? vis[v].hz16 : vis[v].mhz16[0]);
    for (i = 0; i < 3; i++) {
        static const char *const L[3] = {"100 Hz", "1 kHz", "5 kHz"};
        static const uint16_t F[3] = {100, 1000, 5000};
        int32_t x = x0 + (log2_q16(F[i] * 16u) - lo) * n / 545260;
        vg_rect(x, y0, 1, y1 - y0, 0xFFFFFF, 14);
        vg_text_al(x, y1 + 12, &F_XS, L[i], C_FAINT, 256, 1);
    }
    for (i = 0; i < n; i++) {                       /* |H| of the two-pole, in log2 */
        int32_t l = lo + i * 545260 / n, d = l - lfc, g;
        uint64_t R, r2, b, den;
        int64_t a;
        if (d < -393216) d = -393216;
        if (d > 6 << 16) d = 6 << 16;
        R = exp2_q16(d) >> 4;                       /* Q12 */
        r2 = R * R >> 12;
        a = 4096 - (int64_t)r2;
        b = R * 256u / qq8;
        den = (uint64_t)(a * a) + b * b;
        g = -((l2_64(den) - (24 << 16)) >> 1);
        if (ft == 4) g += l2_64(r2) - (12 << 16);
        else if (ft == 1 || ft == 3) g += l2_64(b) - (12 << 16);
        if (!ft) g = 0;
        g = (g >> 4) * 6;                           /* dB, Q12 */
        if (g > 98304) g = 98304;
        if (g < -221184) g = -221184;
        ys[i] = (int16_t)(((y0 + 18) * 16) - (g >> 8) * (y1 - y0 - 18) / 54);
    }
    vg_fill_under(x0, ys, n, y1, y0, c, 110, 6);
    vg_curve(x0, ys, n, 0xCFFFEA, 256, 80);
    vg_rect(x0, y0 + 18, n, 1, 0xFFFFFF, 26);
    if (lmark >= lo) {                              /* where the chirp is now */
        int32_t x = x0 + (lmark - lo) * n / 545260;
        if (x < x0 + n) { vg_rect(x, y0, 1, y1 - y0, C_AMBER, 200); vg_glow(x * 16, y0 * 16 + 64, 5, C_AMBER, 220); }
    }
    vg_text(6, 14, &F_SM, FTN[ft % 5], C_TXT, 256);
    e = cat_u(s, fc); cat(e, " Hz");
    vg_text_al(234, 14, &F_SM, ft ? s : "", c, 256, 2);
    vg_text_al(234, 30, &F_XS, "chirp", C_AMBER, 220, 2);
}
static int16_t mh[2][110];
static uint32_t mh_w[2];
static void draw_spazio(void)
{
    uint32_t c = PG_ACC[PG_SPAZ], co = par[P_CORO], eco = par[P_ECO], fb = par[P_EFB], k;
    int32_t i;
    static int16_t y1[100], y2[100];
    char s[40], *e;
    draw_vbg(VIS_H);
    vg_text(6, 14, &F_SM, "CORO", C_TXT, 256);
    for (i = 0; i < 100; i++) {                     /* the two voices' delays, swaying in quadrature */
        uint32_t ph = (uint32_t)i * 42949673u + ch_ph;
        y1[i] = (int16_t)((62 * 16) - (sin_q15(ph) * (int32_t)co * 22 * 16 / 100 >> 15));
        y2[i] = (int16_t)((62 * 16) - (cos_q15(ph) * (int32_t)co * 22 * 16 / 100 >> 15));
    }
    vg_rect(6, 62, 100, 1, 0xFFFFFF, 20);
    vg_curve(6, y1, 100, c, co ? 240u : 60u, 60);
    vg_curve(6, y2, 100, 0xD6E8FF, co ? 200u : 50u, 40);
    e = cat_u(s, co); cat(e, "%");
    vg_text(6, 104, &F_MD, co ? s : "OFF", co ? C_TXT : C_FAINT, 256);
    vg_text(6, 120, &F_XS, "due voci incrociate, 8-16 ms", C_FAINT, 256);
    vg_rect(116, 20, 1, 100, 0xFFFFFF, 20);
    vg_text(124, 14, &F_SM, "ECO", C_TXT, 256);
    {   /* the echoes: D left, 2D right, then darker */
        uint32_t bpm = clk_ext && ext_bpm10 >= 300u ? ext_bpm10 / 10u : (uint32_t)par[P_BPM] + 40u;
        uint32_t ms = 60000u * ETQ[par[P_ETIME] % 6u] / 256u / bpm;
        int32_t a = eco ? 60 + (int32_t)eco * 196 / 100 : 0, x;
        if (ms > 743u) ms = 743u;
        vg_rect(124, 92, 110, 1, 0xFFFFFF, 40);
        vg_text(124, 104, &F_XS, "S", C_FAINT, 256); vg_text(124, 84, &F_XS, "", C_FAINT, 256);
        for (k = 1; k <= 8u && a > 8; k++) {
            x = 124 + (int32_t)(k * ms * 104u / (8u * 743u > 0 ? 8u * 743u / 4u : 1u));
            if (x > 232) break;
            if (k & 1u) vg_rect(x, 92 - a * 50 / 256, 3, a * 50 / 256, c, 230);   /* left: up */
            else vg_rect(x, 93, 3, a * 24 / 256 + 2, 0xD6E8FF, 220);             /* right: down */
            a = a * ((int32_t)fb * 85 / 100) / 100;
        }
        vg_text(222, 40, &F_XS, "L", c, 256); vg_text(222, 118, &F_XS, "R", 0xD6E8FF, 256);
        e = cat(s, ETN[par[P_ETIME] % 6u]); e = cat(e, "  "); e = cat_u(e, ms); cat(e, " ms");
        vg_text_al(234, 14, &F_XS, eco ? s : "OFF", eco ? c : C_FAINT, 256, 2);
    }
}
static void draw_mod(void)
{
    uint32_t w = page == PG_MODB, c = PG_ACC[page], src = par[w ? P_MS2 : P_MS1], dst = par[w ? P_MD2 : P_MD1], i;
    int32_t amt = (int32_t)par[w ? P_MA2 : P_MA1] - 100, x0 = 8, n = 110, mid = 72;
    static int16_t ys[110];
    char s[32], *e;
    mh[w][mh_w[w]++ % 110u] = (int16_t)(vis_msrc[w] >> 1);
    draw_vbg(VIS_H);
    vg_text(6, 14, &F_SM, MSRCN[src % 10u], C_TXT, 256);
    vg_text_al(234, 14, &F_SM, MDSTN[dst % 11u], C_TXT, 256, 2);
    vg_text_al(120, 14, &F_SM, "\x80", c, 256, 1);
    vg_rect(x0, mid, n, 1, 0xFFFFFF, 20);
    for (i = 0; i < 110u; i++) ys[i] = (int16_t)(mid * 16 - (int32_t)mh[w][(mh_w[w] + i) % 110u] * 40 * 16 / 16384);
    if (src) { vg_fill_under(x0, ys, n, mid, mid - 40, c, 50, 50); vg_curve(x0, ys, n, c, 256, 80); }
    else vg_text_al(x0 + n / 2, mid - 6, &F_XS, "nessuna sorgente", C_FAINT, 256, 1);
    {   /* the amount: a bar from the centre, and what reaches the destination */
        int32_t bx = 178, by = 30, bh = 84, cyy = by + bh / 2, l = amt * (bh / 2) / 100, out = (int32_t)vis_msrc[w] * amt / 100;
        vg_rect(bx, by, 14, bh, 0x141B2B, 256);
        vg_rect(bx - 2, cyy, 18, 1, 0xFFFFFF, 90);
        if (l > 0) vg_rect(bx, cyy - l, 14, l, c, 200); else if (l < 0) vg_rect(bx, cyy, 14, -l, c, 200);
        e = cat(s, amt > 0 ? "+" : ""); if (amt < 0) { e = cat(e, "-"); } e = cat_u(e, (uint32_t)(amt < 0 ? -amt : amt)); cat(e, "%");
        vg_text_al(185, by + bh + 12, &F_XS, s, C_TXT, 256, 1);
        out = out * 30 / 32768;
        vg_line(124 * 16, (mid - out) * 16, 168 * 16, (mid - out) * 16, c, dst && src ? 200u : 40u);
        vg_glow(214 * 16, (mid - out) * 16, 8, c, dst && src ? 220u : 30u);
        vg_disc(214 * 16, (mid - out) * 16, 40, 0xFFFFFF, dst && src ? 256u : 60u);
    }
    if (src == 7u) {
        uint32_t mhz = (20u * exp2_q16((int32_t)par[P_LRATE] * 6554)) >> 16;
        e = cat(s, "LFO "); e = cat(e, LSHN[par[P_LSHAPE] & 3u]); e = cat(e, "  ");
        if (mhz < 1000u) { e = cat_u(e, mhz); cat(e, " mHz"); } else { e = cat_d1(e, mhz / 100u); cat(e, " Hz"); }
        vg_text(6, 126, &F_XS, s, C_DIM, 256);
    } else vg_text(6, 126, &F_XS, src >= 1u && src <= 6u ? "dall'evento che suona" : src == 8u ? "un valore nuovo a ogni evento" : src == 9u ? "la forza del tasto" : "", C_FAINT, 256);
}
/* ---- 0.4: FINALE, RIVERBERO, DEJA VU, RADIO */
static void draw_finale(void)
{
    static int16_t ys[230];
    uint32_t c = PG_ACC[PG_FINE], i, n = 230, xi = 150, xh, xg, sosp = par[P_SOSP], appr = par[P_APPR];
    int32_t oct = part_oct16(par[P_PART]), top = (int32_t)log2_q16((47186u + (uint32_t)par[P_SPIN] * 66u) * 2u) - (17 << 16) + ((int32_t)par[P_PICCO] - 50) * 1311;
    int32_t pe = (int32_t)(exp2_q16(((int32_t)par[P_FINALE] - 50) * 2097) >> 4);   /* Q12 */
    uint32_t K = (exp2_q16(oct * 8 / 3) >> 8) - 256u;
    int32_t y0 = 112, yr = 84, v = latest_voice();
    char s[32], *e;
#define FY(l) (int16_t)((y0 * 16) - (((l) + oct + 65536) * (yr * 16) / (oct + 2 * 65536)))
    xh = sosp ? 18u + sosp * 30u / 100u : 0u;
    xg = xi + xh;
    draw_vbg(VIS_H);
    for (i = 0; i < n; i++) {
        int32_t l2;
        if (i < xi) {                               /* the inspiral, its time bent */
            uint32_t sq = 65536u - i * 65536u / xi, x;
            if (pe != 4096 && sq) sq = exp2_q16((int32_t)(((int64_t)(log2_q16(sq) - (16 << 16)) * pe) >> 12));
            x = 65536u + (uint32_t)(((uint64_t)sq * K) >> 8);
            l2 = (-((log2_q16(x) - (16 << 16)) * 3) >> 3) + top;
        } else if (i < xg) l2 = top;                /* SOSPENSIONE */
        else {                                      /* APPRODO: to the key, in log */
            int32_t t = (int32_t)(i - xg), tau = 1 + (int32_t)appr * (int32_t)appr / 160;
            l2 = (int32_t)(((int64_t)top * exp2_q16(-(t * 65536 / tau) * 3 / 2)) >> 16);
        }
        ys[i] = FY(l2);
    }
    vg_rect(5, FY(0) >> 4, 230, 1, 0xFFFFFF, 40);
    vg_text_al(234, (FY(0) >> 4) - 3, &F_XS, "nota", C_FAINT, 256, 2);
    vg_fill_under(5, ys, n, y0, 20, c, 70, 6);
    vg_curve(5, ys, n, 0xFFE3C0, 256, 90);
    vg_rect(5 + (int32_t)xi, 22, 1, y0 - 22, 0xFFFFFF, 50);
    if (xh) { vg_rect(5 + (int32_t)xi, 22, (int32_t)xh, y0 - 22, c, 24); vg_text_al(5 + (int32_t)(xi + xh / 2u), 32, &F_XS, "SOSP.", c, 256, 1); }
    vg_text_al(5 + (int32_t)xg + 3, 32, &F_XS, "FUSIONE", 0xFFFFFF, 200, 0);
    vg_text(6, 14, &F_SM, "FINALE", C_TXT, 256);
    {
        uint32_t m = (10u * exp2_q16(((int32_t)par[P_PICCO] - 50) * 1311)) >> 16;
        e = cat(s, "PICCO \x82"); cat_d1(e, m);
        vg_text_al(234, 14, &F_XS, s, c, 256, 2);
    }
    if (v >= 0 && vis[v].stage == ST_INSP) {        /* where the last event is */
        int32_t x = 5 + (int32_t)(vis[v].prog * xi >> 16);
        vg_rect(x, 22, 1, y0 - 22, 0xFFFFFF, 190);
    }
#undef FY
}
static void draw_riverbero(void)
{
    uint32_t c = PG_ACC[PG_RIV], i, dens = par[P_RDENS], pre = (uint32_t)par[P_RPRE] * par[P_RPRE] / 100u, rr = 0x51ED1234u;
    int32_t dec = eng.r_dec, l, x0 = 8, w = 226, y0 = 112, hgt = 80, t60, xp;
    char s[32], *e;
    draw_vbg(VIS_H);
    l = -(log2_q16((uint32_t)dec) - log2_q16(32768u));   /* -log2 (gain a round) */
    t60 = l > 0 ? 1370000 / l : 99999;               /* tenths of s: two passes of the figure eight, 0.42 s each, lose dec^2 */
    xp = x0 + (int32_t)pre * w / 1200;               /* the axis: 12 s if long, 1.2 s a 100 px */
    vg_rect(x0, y0, w, 1, 0xFFFFFF, 40);
    vg_rect(x0, y0 - hgt, 2, hgt, 0xFFFFFF, 200);    /* the sound itself */
    {   /* the reflections: denser and denser, under the decay */
        uint32_t cnt = 60u + dens * 6u;
        int32_t span = t60 > 0 ? t60 : 1;
        for (i = 0; i < cnt; i++) {
            int32_t x, a, tt;
            rr ^= rr << 13; rr ^= rr >> 17; rr ^= rr << 5;
            x = (int32_t)((rr >> 8) % (uint32_t)(w - (xp - x0)));
            tt = x * 12 * 100 / w;                  /* hundredths of s along the axis (12 s) */
            a = (int32_t)(exp2_q16(-(tt * 43516 / (span < 1 ? 1 : span))) >> 9);   /* 2^(-6.6 t / T60): -60 dB at T60 (but a screen shows -40) */
            a = a * hgt / 128;
            if (a > hgt) a = hgt;
            if (a < 1) continue;
            vg_rect(xp + x, y0 - a, 1, a, c, 70u + (rr & 63u));
        }
    }
    vg_text(6, 14, &F_SM, "RIVERBERO", C_TXT, 256);
    if (t60 > 6000) cat(s, "T60 > 10 min"); else { e = cat(s, "T60 ~"); e = cat_u(e, (uint32_t)t60 / 10u); *e++ = ','; *e++ = (char)('0' + t60 % 10); cat(e, " s"); }
    vg_text_al(234, 14, &F_SM, s, c, 256, 2);
    e = cat(s, "DENSIT\x83 "); e = cat_u(e, dens); cat(e, "%");
    vg_text(6, 126, &F_XS, s, C_DIM, 256);
    vg_text_al(234, 126, &F_XS, "0 \x80 12 s", C_FAINT, 256, 2);
}
static void draw_deja(void)
{
    uint32_t c = PG_ACC[PG_DEJA], L = (uint32_t)par[P_DLEN] + 1u, dv = par[P_DEJA], k;
    int32_t cx = 66, cy = 70, R = 44;
    char s[32], *e;
    draw_vbg(VIS_H);
    vg_ring(cx * 16, cy * 16, R * 16, 10, 0xFFFFFF, 30);
    for (k = 0; k < L; k++) {                       /* the loop: a slot each, its note's chance as a dot's height */
        uint32_t an = k * (0xFFFFFFFFu / L), cur = k == dv_pos;
        int32_t px = cx * 16 + ((sin_q15(an) * R * 16) >> 15), py = cy * 16 - ((cos_q15(an) * R * 16) >> 15);
        int32_t rad = 24 + (int32_t)(dv_ok ? dv_buf[k][DV_NOTE] >> 12 : 0);
        if (cur) vg_glow(px, py, 8, c, 230);
        vg_disc(px, py, rad, cur ? 0xFFFFFF : c, cur ? 256u : 120u + (dv > 50u ? 0u : dv * 2u));
    }
    {   /* the arrow: next, or a jump */
        uint32_t an = dv_pos * (0xFFFFFFFFu / L);
        vg_line(cx * 16, cy * 16, cx * 16 + ((sin_q15(an) * (R - 8) * 16) >> 15), cy * 16 - ((cos_q15(an) * (R - 8) * 16) >> 15), c, 200);
    }
    e = cat_u(s, L); cat(e, L > 1u ? " passi" : " passo");
    vg_text_al(cx, cy + 4, &F_XS, s, C_DIM, 256, 1);
    vg_text(6, 14, &F_SM, "D\x86J\x83 VU", C_TXT, 256);
    vg_text(126, 36, &F_MD, !dv ? "CASO" : dv < 50u ? "RITORNA" : dv == 50u ? "BLOCCATO" : "SALTI", C_TXT, 256);
    vg_text(126, 54, &F_XS, !dv ? "ogni evento nuovo" : dv < 50u ? "a volte torna un evento" : dv == 50u ? "il giro si ripete uguale" : "lo stesso giro, a salti", C_DIM, 256);
    e = cat(s, "SU "); cat(e, par[P_DSU] ? "RUN+TASTI" : "RUN");
    vg_text(126, 80, &F_XS, s, c, 256);
    vg_text(126, 96, &F_XS, run_on && par[P_RATE] ? "RUN IN CORSO" : "RUN FERMO (PLAY)", run_on && par[P_RATE] ? C_CYAN : C_FAINT, 256);
    {   /* how new the last one was */
        vg_text(126, 118, &F_XS, dv_fresh_last ? "ultimo: NUOVO" : "ultimo: GI\x83 SENTITO", dv_fresh_last ? C_TXT : c, 256);
    }
}
static int32_t dial_x(int32_t pos8) { return 10 + pos8 * 220 / 25600; }   /* the dial: 0 .. 100 (Q8) to the screen */
static void draw_radio(void)
{
    uint32_t c = PG_ACC[PG_RADIO], i, rr = (fm1_ms * 2654435761u) | 1u;
    int32_t r = par[P_RADIO], dial = (int32_t)par[P_DIAL] * 256, f = r ? vis_found : 0, yb = 92, nx;
    static const uint16_t MCP[5] = {10, 30, 100, 300, 1000};
    static const char *const MCN[5] = {"1", "3", "10", "30", "100"};
    char s[32], *e;
    draw_vbg(VIS_H);
    if (r) for (i = 0; i < (uint32_t)(32767 - f) / 200u * (uint32_t)r / 100u; i++) {   /* the static, as snow */
        rr ^= rr << 13; rr ^= rr >> 17; rr ^= rr << 5;
        vg_px((int32_t)(rr % 236u) + 2, 20 + (int32_t)((rr >> 9) % 60u), 0xFFFFFF, 40u + (rr >> 26));
    }
    vg_rect(10, yb, 220, 1, 0xFFFFFF, 90);           /* the scale */
    for (i = 0; i <= 50u; i++) vg_rect(10 + (int32_t)i * 220 / 50, yb - (i % 5u ? 3 : 7), 1, i % 5u ? 3 : 7, 0xFFFFFF, i % 5u ? 70u : 150u);
    for (i = 0; i < 5u; i++) {
        int32_t pos = ((log2_q16(MCP[i]) - log2_q16(10)) >> 4) * 25600 / 27212;
        vg_text_al(dial_x(pos), yb + 14, &F_XS, MCN[i], C_DIM, 256, 1);
    }
    vg_text_al(234, yb + 26, &F_XS, "Mc  M\x7F", C_FAINT, 256, 2);
    for (i = 0; i < nlog && i < 8u; i++) {           /* the last events: their stations, faint */
        uint32_t m1 = evlog[i].m1, m2 = evlog[i].m2;
        int32_t lmc = (log2_q16(m1) + log2_q16(m2)) * 3 / 5 - log2_q16(m1 + m2) / 5, pos = ((lmc - log2_q16(10)) >> 4) * 25600 / 27212;
        if (pos < 0) pos = 0;
        if (pos > 25600) pos = 25600;
        vg_rect(dial_x(pos), yb - 18, 2, 18, c, i ? 70u : 140u);
    }
    for (i = 0; i < NV; i++) if (vs[i].stage != ST_OFF) {   /* those that sound: bright, tall as they are loud */
        int32_t a = vis[i].amp > 0 ? vis[i].amp : 0, h = 10 + (a > 20000 ? 40 : a / 500);
        vg_rect(dial_x(vs[i].e.station) - 1, yb - h, 3, h, 0xFFFFFF, 220);
        vg_glow(dial_x(vs[i].e.station) * 16, (yb - h) * 16, 5, c, 200);
    }
    nx = dial_x(dial);                               /* the needle */
    vg_rect(nx - 1, 22, 3, yb - 18, 0xFF5A40, 230);
    vg_glow(nx * 16, 24 * 16, 6, 0xFF5A40, 200);
    vg_text(6, 14, &F_SM, "RADIO", C_TXT, 256);
    if (!r) vg_text_al(234, 14, &F_XS, "OFF: tutto si sente", C_FAINT, 256, 2);
    else {
        int32_t w = f * 90 / 32767;
        vg_rect(140, 8, 90, 7, 0x141B2B, 256);
        vg_rect(140, 8, w, 7, f > 26000 ? 0x7DFFB0 : c, 230);
        vg_text_al(234, 26, &F_XS, f > 26000 ? "ONDA TROVATA" : f > 9000 ? "QUASI..." : "RUMORE", f > 26000 ? 0x7DFFB0 : C_DIM, 256, 2);
    }
    value_text(P_DIAL, s);
    e = s; (void)e;
    vg_text(6, 126, &F_XS, s, 0xFF8A70, 256);
}
static void draw_flash(void)                        /* the page's name, big, a moment after SELECT */
{
    uint32_t i, at = 0;
    for (i = 0; i < NPAGE; i++) if (PG_ORDER[i] == page) at = i;
    vg_rect(0, 0, 240, VIS_H, RGB24(4, 6, 12), 200);
    vg_text_al(120, 72, page == PG_INT ? &F_TI : &F_XL, PAGE[page], PG_ACC[page], 256, 1);
    vg_text(8, 112, &F_SM, PAGE[PG_ORDER[(at + NPAGE - 1u) % NPAGE]], C_FAINT, 256);
    vg_text_al(232, 112, &F_SM, PAGE[PG_ORDER[(at + 1u) % NPAGE]], C_FAINT, 256, 2);
}
static void draw_picture(void)
{
    vg_begin(240, VIS_H, C_BG0);
    switch (page) {
    case PG_SORG: draw_sorgente(); break;
    case PG_CHIRP: draw_chirp(); break;
    case PG_RING: draw_ringdown(); break;
    case PG_INT: draw_interf(); break;
    case PG_RUM: draw_rumore(); break;
    case PG_CIELO: draw_cielo(); break;
    case PG_RUN: draw_run(); break;
    case PG_CAMP: draw_campione(); break;
    case PG_MIX: draw_mix(); break;
    case PG_MOT: draw_motore(); break;
    case PG_ARM: draw_armonia(); break;
    case PG_FUS: draw_fusioni(); break;
    case PG_FILT: draw_filtro(); break;
    case PG_MODA: case PG_MODB: draw_mod(); break;
    case PG_SPAZ: draw_spazio(); break;
    case PG_FINE: draw_finale(); break;
    case PG_RIV: draw_riverbero(); break;
    case PG_DEJA: draw_deja(); break;
    case PG_RADIO: draw_radio(); break;
    case PG_SHIM: draw_shimmer(); break;
    default: draw_midipic(); break;
    }
    if (t_flash) draw_flash();
    cv_blit(0, VIS_Y);
}

/* ---- the opening: two masses spiral in, merge, ring; the name */
#define IN_END 2600u
static void intro_frame(void)
{
    uint32_t t = fm1_ms - intro_t0;
    int32_t cx = 120 * 16, cy = 64 * 16;
    vg_begin(240, VIS_H, 0x000000);
    if (t < 1500u) {                                /* the chirp: closer and faster */
        int32_t s = (int32_t)(1500u - t), sep = 40 + s * 70 / 1500, k;
        uint32_t ph = (2000000u / (uint32_t)(s + 120)) << 20;
        int32_t c = cos_q15(ph), sn = sin_q15(ph) * 4 / 10;
        for (k = 1; k <= 4; k++) { int32_t rr = (int32_t)((t / 9u + (uint32_t)k * 16u) % 70u) + sep / 16; vg_ring(cx, cy, rr * 16, 10, C_CYAN, (uint32_t)(70 - rr)); }
        vg_glow(cx + ((c * sep) >> 15), cy + ((sn * sep) >> 15), 7, 0x9FD8FF, 220);
        vg_glow(cx - ((c * sep) >> 15), cy - ((sn * sep) >> 15), 6, 0xC7B0FF, 220);
        vg_disc(cx + ((c * sep) >> 15), cy + ((sn * sep) >> 15), 40, 0x000000, 256);
        vg_disc(cx - ((c * sep) >> 15), cy - ((sn * sep) >> 15), 32, 0x000000, 256);
    } else {
        uint32_t u = t - 1500u, k;
        if (u < 300u) vg_glow(cx, cy, 60 - (int32_t)u / 6, 0xFFF2D6, 256 - u * 256u / 300u);
        for (k = 0; k < 5u; k++) { uint32_t rr = (u / 6u + k * 22u) % 130u; vg_ring(cx, cy, (int32_t)rr * 16 + 160, 12, C_AMBER, 120u - rr * 120u / 130u); }
        vg_disc(cx, cy, 120, 0x000000, 256);
        vg_ring(cx, cy, 120, 14, 0xFFE6BE, 200);
        if (u > 250u) {
            uint32_t a = u > 650u ? 256u : (u - 250u) * 256u / 400u;
            vg_text_al(120, 87, &F_SM, "Mario Texture's", 0xFFE6BE, a, 1);
            vg_text_al(120, 111, &F_XL, "VIRGO", C_TXT, a, 1);
            vg_text_al(120, 126, &F_XS, "onde gravitazionali", C_DIM, a, 1);
        }
    }
    cv_blit(0, 56);
}
