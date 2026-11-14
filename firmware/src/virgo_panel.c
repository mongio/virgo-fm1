/* SPDX-License-Identifier: GPL-3.0-only */
/* VIRGO's panel, MIDI (LEARN, the clock), the SMC-Mixer and Bluetooth, the editor's SysEx, the main loop.
 * Included by virgo.c. Much of it is GRANA's, as Mario knows it. */

/* ---- the values as words */
static void value_text(uint32_t p, char *s)
{
    uint32_t v;
    char *e = s;
    *s = 0;
    if (p >= NP) return;
    v = par[p];
    switch (p) {
    case P_M1: case P_M2: e = cat_d1(s, mass10(v)); cat(e, " \x7F"); break;
    case P_SPIN: e = cat(s, "a 0,"); *e++ = (char)('0' + v / 10u); *e++ = (char)('0' + v % 10u); *e = 0; break;
    case P_DIST: case P_DMAX: { uint32_t d = p == P_DIST ? dist_mpc(v) : 100u + v * 49u; if (p == P_DMAX) e = cat(s, "\x81"); e = cat_u(e, d); cat(e, " Mpc"); break; }
    case P_DUR: { uint32_t ms = dur_ms(v); if (ms < 1000u) { e = cat_u(s, ms); cat(e, " ms"); } else if (ms < 60000u) { e = cat_d1(s, ms / 100u); cat(e, " s"); }
                  else { uint32_t sc = ms / 1000u; e = cat_u(s, sc / 60u); *e++ = ':'; *e++ = (char)('0' + sc % 60u / 10u); *e++ = (char)('0' + sc % 10u); cat(e, " min"); } break; }
    case P_PART: e = cat_d1(s, (uint32_t)part_oct16(v) * 10u >> 16); cat(e, " OTT"); break;
    case P_MODI: cat_u(s, v + 1u); break;
    case P_ACC: if (!v) cat(s, "FISICO"); else if (v >= 100u) cat(s, "ACCORDO"); else { e = cat_u(s, v); cat(e, "%"); } break;
    case P_ARM: e = cat_d1(s, arm_ms10(v)); cat(e, " ms"); break;
    case P_FIN: cat_u(s, v * 9u / 2u); break;
    case P_DENS: cat_u(s, dens_n(v)); break;
    case P_BAND: e = cat_u(s, band_hz(v)); cat(e, " Hz"); break;
    case P_RATE: if (!v) cat(s, "OFF"); else if (par[P_RITMO]) { e = cat(s, "COLPI "); e = cat_u(e, v); cat(e, "%"); }   /* 0.6: with RITMO, each hit's chance */
                 else { e = cat_u(s, v * 30u / 100u ? v * 30u / 100u : 1u); cat(e, "/MIN"); } break;
    case P_FUSM: cat(s, FUSN[v % 3u]); break;       /* 0.6 */
    case P_CONG: if (!v) cat(s, "OFF"); else if (v >= 100u) cat(s, "FERMO"); else { uint32_t m = exp2_q16((int32_t)v * 6554) >> 16; e = cat(s, "CODA \x82"); cat_u(e, m); } break;
    case P_INV: if (!v) cat(s, "OFF"); else if (v >= 100u) cat(s, "SEMPRE"); else { e = cat(s, "A CASO "); e = cat_u(e, v); cat(e, "%"); } break;
    case P_TUFFO: { uint32_t ms = (20u * exp2_q16((int32_t)v * 4738)) >> 16; if (ms < 1000u) { e = cat_u(s, ms); cat(e, " ms"); } else { e = cat_d1(s, ms / 100u); cat(e, " s"); } break; }
    case P_NOTE: cat(s, NOTEN[v % 3u]); break;
    case P_TCHG: if (!v) cat(s, "BLOCCATO"); else if (v >= 100u) cat(s, "CASO"); else { e = cat_u(s, v); cat(e, "%"); } break;
    case P_TLEN: e = cat_u(s, v + 2u); cat(e, " PASSI"); break;
    case P_TEXT: if (par[P_NOTE] == 2u) { e = cat(s, "1/1 .. 1/"); cat_u(e, v + 1u > 16u ? 16u : v + 1u); } else { e = cat_u(s, v + 1u); cat(e, v ? " GRADI" : " GRADO"); } break;
    case P_RITMO: cat(s, RITN[v % 3u]); break;
    case P_EPASSI: if (par[P_RITMO] == 2u) { e = cat(s, "OGNI "); cat_u(e, (v > 15u ? 15u : v) + 1u); } else cat_u(s, v + 2u); break;
    case P_ECOLPI: if (par[P_RITMO] == 2u) { e = cat(s, "OGNI "); cat_u(e, (v > 15u ? 15u : v) + 1u); }
                   else { uint32_t N = (uint32_t)par[P_EPASSI] + 2u; e = cat_u(s, v + 1u > N ? N : v + 1u); e = cat(e, " SU "); cat_u(e, N); } break;
    case P_EROT: cat_u(s, par[P_RITMO] == 2u ? v : v % ((uint32_t)par[P_EPASSI] + 2u)); break;
    case P_MMAX: e = cat(s, "\x81"); e = cat_u(e, 10u + v * 14u / 10u); cat(e, " \x7F"); break;
    case P_RTEMPO: cat(s, TEMPON[v % 6u]); break;
    case P_SND: if (!v) cat(s, "OFF"); else cat(s, snd_name(v - 1u)); break;
    case P_USE: cat(s, USEN[v % 3u]); break;
    case P_BPM: cat_u(s, v + 40u); break;
    case P_ENG: cat(s, ENGN[v % 6u]); break;
    case P_ALG: cat(s, par[P_ENG] == 1u ? ALGN[v % 5u] : "(SOLO FM)"); break;
    case P_ETIME: cat(s, ETN[v % 6u]); break;
    case P_SHTYPE: cat(s, SHN[v % 5u]); break;
    case P_SHIM: case P_COMP: if (!v) cat(s, "OFF"); else { e = cat_u(s, v); cat(e, "%"); } break;
    case P_STEREO: if (!v) cat(s, "MONO"); else if (v <= 40u) { e = cat(s, v < 25u ? "STRETTO " : v == 25u ? "NORMALE" : "LARGO "); if (v != 25u) { e = cat_u(e, v * 4u); cat(e, "%"); } }
                   else { uint32_t mhz = (uint32_t)(((uint64_t)50u * exp2_q16((int32_t)(v - 41u) * 8120)) >> 16); e = cat(s, "PAN ");
                          if (mhz < 1000u) { e = cat(e, "0,"); *e++ = (char)('0' + mhz / 100u); *e++ = (char)('0' + mhz / 10u % 10u); *e = 0; cat(e, " Hz"); } else { e = cat_d1(e, mhz / 100u); cat(e, " Hz"); } } break;
    case P_FINALE: if (v >= 48u && v <= 52u) cat(s, "FISICO"); else { e = cat(s, v < 50u ? "IMPROVVISO " : "DISTESO "); cat_u(e, v < 50u ? (50u - v) * 2u : (v - 50u) * 2u); } break;
    case P_PICCO: { uint32_t m = (10u * exp2_q16(((int32_t)v - 50) * 1311)) >> 16; e = cat(s, "\x82"); cat_d1(e, m); break; }
    case P_SOSP: case P_APPR: { uint32_t ms = p == P_SOSP ? v * v * 4u / 5u : v * v * 15u / 100u;
                  if (!ms) cat(s, p == P_SOSP ? "OFF" : "SALTO"); else if (ms < 1000u) { e = cat_u(s, ms); cat(e, " ms"); } else { e = cat_d1(s, ms / 100u); cat(e, " s"); } break; }
    case P_RPRE: e = cat_u(s, v * v / 100u); cat(e, " ms"); break;
    case P_DEJA: if (!v) cat(s, "OFF: CASO"); else if (v == 50u) cat(s, "BLOCCATO"); else if (v < 50u) { e = cat(s, "RITORNA "); e = cat_u(e, v * 2u); cat(e, "%"); }
                 else { e = cat(s, "SALTI "); e = cat_u(e, (v - 50u) * 2u); cat(e, "%"); } break;
    case P_DLEN: e = cat_u(s, v + 1u); cat(e, v ? " PASSI" : " PASSO"); break;
    case P_DSU: cat(s, v ? "RUN+TASTI" : "RUN"); break;
    case P_RADIO: if (!v) cat(s, "OFF"); else { e = cat_u(s, v); cat(e, "%"); } break;
    case P_DIAL: { uint32_t mc10 = (10u * exp2_q16((int32_t)v * 4354)) >> 16; e = cat(s, "Mc "); e = cat_d1(e, mc10 < 100u ? mc10 : mc10 / 10u * 10u); cat(e, " \x7F"); break; }
    case P_IVL: if (!v) cat(s, IVLN[0]); else { const ev_t *x = 0; (void)x; cat(s, IVL_N[ivl_index(mass10(par[P_M1]) > mass10(par[P_M2]) ? mass10(par[P_M1]) : mass10(par[P_M2]), mass10(par[P_M1]) > mass10(par[P_M2]) ? mass10(par[P_M2]) : mass10(par[P_M1]))]); } break;
    case P_SCALE: if (v == 1u) { e = cat(s, "ROT: "); cat(e, SCALEN[scale_shown(par[P_SPIN])]); } else cat(s, SCALEN[v % NSCALE]); break;
    case P_TONIC: cat(s, NTN[v % 12u]); break;
    case P_CQ: cat(s, CQN[v % 3u]); break;
    case P_HIER: cat(s, HIERN[v & 3u]); break;
    case P_FTYPE: cat(s, FTN[v % 5u]); break;
    case P_FREQ: { int32_t l2 = -131072 + (int32_t)v * 4588; uint32_t m10 = (10u * exp2_q16(l2)) >> 16; e = cat(s, "\x82"); cat_d1(e, m10); break; }
    case P_MS1: case P_MS2: cat(s, MSRCN[v % 10u]); break;
    case P_MD1: case P_MD2: cat(s, MDSTN[v % 11u]); break;
    case P_MA1: case P_MA2: { int32_t a = (int32_t)v - 100; if (a > 0) *e++ = '+'; if (a < 0) { *e++ = '-'; a = -a; } e = cat_u(e, (uint32_t)a); cat(e, "%"); break; }
    case P_LRATE: { uint32_t mhz = (20u * exp2_q16((int32_t)v * 6554)) >> 16; if (mhz < 1000u) { e = cat_u(s, mhz); cat(e, " mHz"); } else { e = cat_d1(s, mhz / 100u); cat(e, " Hz"); } break; }
    case P_LSHAPE: cat(s, LSHN[v & 3u]); break;
    default: e = cat_u(s, v); cat(e, "%"); break;
    }
}
static const char *pname_of(uint32_t p) { return plabel(p); }
static uint32_t par_top(uint32_t p) { return p == P_SND ? nsnd : PMAX[p]; }

static void param_change(uint32_t p, int32_t d)
{
    int32_t v = (int32_t)par[p] + d, mx = (int32_t)par_top(p);
    if (v < 0) v = 0;
    if (v > mx) v = mx;
    if ((uint32_t)v == par[p]) return;
    par[p] = (uint8_t)v;
    edited = 1;
    if (p == P_TONIC || p == P_SCALE || p == P_HIER) { tz_home(); hier_m10 = 0; hier_n = 0; tz_last = 0; }
    if (p == P_M1 || p == P_M2) { int32_t c = cell_of(page, P_IVL); if (c >= 0) dirty_cells |= 1u << c; }
    dirty_hdr = 1;
    engine_update();
    if (cell_of(page, p) >= 0) dirty_cells |= 1u << cell_of(page, p);
    if (p == P_ENG || p == P_NOTE || p == P_RITMO || p == P_EPASSI) dirty_cells = 0xF;   /* names and values that follow another knob */
}
static void param_set(uint32_t p, uint32_t pos) { param_change(p, (int32_t)pos - (int32_t)par[p]); }
static int32_t accel(int32_t d, uint32_t range)
{
    int32_t a = d < 0 ? -d : d;
    if (range > 60u && a > 1) d *= a > 4 ? 4 : a;
    return d;
}

/* ---- pages */
static void goto_page(uint32_t p) { page = p; dirty_hdr = 1; dirty_cells = 0xF; t_flash = 0; }

/* ---- SAVE and the name (as GRANA's: KNOB 1 cursor, 2 letter, 3 dice, 4 delete) */
static const char NAME_CH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-'";
static const char *const NAME_DICE[24] = {"COALESCENZA", "CHIRP", "ORIZZONTE", "SPIRALE", "MAREA", "BINARIA", "NANA BIANCA",
    "PULSAR", "KILONOVA", "MAGNETAR", "QUASAR", "ECO COSMICO", "CASCINA", "HANFORD", "LIVINGSTON", "KAGRA", "LISA",
    "STRAIN", "SPECCHIO", "FASCIO", "SILENZIO", "ABISSO", "GALASSIA", "AURORA"};
static uint32_t name_len(void) { uint32_t n = 0; while (n < 12u && save_name[n]) n++; return n; }
static void name_start(void)
{
    uint32_t k;
    const char *n = pr[save_dest].used ? pr[save_dest].name : "";
    for (k = 0; k < 11u && n[k]; k++) save_name[k] = n[k];
    save_name[k] = 0;
    if (!k) {                                       /* an empty place: the last event's name */
        const char *b = nlog ? evlog[0].name : "VIRGO";
        for (k = 0; k < 9u && b[k]; k++) save_name[k] = b[k];
        save_name[k] = 0;
    }
    name_cur = name_len();
    if (name_cur > 10u) name_cur = 10u;
    name_dirty = 1;
}
static void name_knob(uint32_t k, int32_t d)
{
    uint32_t n = name_len(), i;
    name_dirty = 1;
    t_savemode = fm1_ms;
    if (k == 0u) {
        int32_t c = (int32_t)name_cur + (d > 0 ? 1 : -1), mx = (int32_t)(n < 11u ? n : 10u);
        name_cur = (uint32_t)(c < 0 ? 0 : c > mx ? mx : c);
    } else if (k == 1u) {
        int32_t at = 0, nc = (int32_t)sizeof NAME_CH - 1;
        char ch = name_cur < n ? save_name[name_cur] : ' ';
        for (i = 0; i < (uint32_t)nc; i++) if (NAME_CH[i] == ch) at = (int32_t)i;
        at = (at + d % nc + nc) % nc;
        save_name[name_cur] = NAME_CH[at];
        if (name_cur >= n) save_name[name_cur + 1u] = 0;
        n = name_len();
        while (n && save_name[n - 1u] == ' ' && n - 1u != name_cur) save_name[--n] = 0;
    } else if (k == 2u) {
        const char *w = NAME_DICE[xrand_main() % 24u];
        for (i = 0; i < 11u && w[i]; i++) save_name[i] = w[i];
        save_name[i] = 0;
        name_cur = i < 11u ? i : 10u;
    } else if (name_cur < n) {
        for (i = name_cur; i < n; i++) save_name[i] = save_name[i + 1u];
    }
}
static void save_to(uint32_t dst)
{
    uint32_t k;
    dst %= NPRESET;
    for (k = 0; k < 12u; k++) pr[dst].name[k] = save_name[k];
    pr[dst].name[11] = 0;
    { uint32_t n = 11u; while (n && (pr[dst].name[n - 1u] == ' ' || !pr[dst].name[n - 1u])) pr[dst].name[--n] = 0; }
    for (k = 0; k < NP; k++) pr[dst].p[k] = par[k];
    pr[dst].used = 1;
    save_sectors |= 1u << (dst / 16u);
    cur_preset = dst;
    t_cur = fm1_ms;
    edited = 0;
    save_pending = 1;
    t_save = fm1_ms;
    learn_say("SALVATO: ", pr[dst].name);
}

/* ---- MIDI: USB and the TRS jack (and Bluetooth) together. The clock in sets RUN's tempo; LEARN as GRANA's */
static uint32_t learn_have, learn_st, learn_num, t_learn, del_all;
static int32_t learn_par = -1;
static char *msg_text(char *e, uint32_t st, uint32_t num)
{
    uint32_t hi = st & 0xF0u;
    if (hi == 0xB0u) { e = cat(e, "CC "); e = cat_u(e, num); }
    else if (hi == 0x90u) { e = cat(e, "NOTA "); note_name(e, num); while (*e) e++; }
    else e = cat(e, "BEND");
    e = cat(e, " CH"); e = cat_u(e, (st & 15u) + 1u);
    return e;
}
static void learn_status(void)
{
    char *e = lmsg;
    if (!learn_on) return;
    e = cat(e, "LEARN: ");
    if (learn_have) e = msg_text(e, learn_st, learn_num); else e = cat(e, "?");
    e = cat(e, " > ");
    cat(e, learn_par >= 0 ? pname_of((uint32_t)learn_par) : "?");
    t_lmsg = 0;
    dirty_hdr = 1;
}
static void learn_start(int on)
{
    learn_on = on ? 1u : 0u;
    learn_have = 0;
    learn_par = -1;
    t_learn = fm1_ms;
    if (on) learn_status(); else { lmsg[0] = 0; dirty_hdr = 1; }
}
static void learn_try(void)
{
    uint32_t i;
    char *e;
    if (!learn_have || learn_par < 0) { learn_status(); return; }
    for (i = 0; i < store.nlearn; i++) if (store.lr[i].st == learn_st && store.lr[i].num == learn_num) break;
    if (i == store.nlearn) {
        if (store.nlearn >= NLEARN) { learn_on = 0; learn_say("LEARN: PIENO (32)", 0); return; }
        store.nlearn++;
    }
    store.lr[i].st = (uint8_t)learn_st; store.lr[i].num = (uint8_t)learn_num; store.lr[i].par = (uint8_t)learn_par; store.lr[i].pad = 0;
    settings_touch();
    learn_on = 0;
    e = msg_text(cat(lmsg, "OK: "), learn_st, learn_num);
    e = cat(e, " > ");
    cat(e, pname_of((uint32_t)learn_par));
    t_lmsg = fm1_ms ? fm1_ms : 1u;
    dirty_hdr = 1;
    if (page == PG_MIDI) dirty_cells = 0xF;
}
static void note_button(uint32_t p)
{
    uint32_t top = par_top(p), pos = par[p];
    if (top <= 7u) param_set(p, pos >= top ? 0u : pos + 1u);
    else param_set(p, pos < top / 2u ? top : 0u);
}
static int learnt(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t i, hi = st & 0xF0u, key = hi == 0x80u ? 0x90u | (st & 15u) : st;
    for (i = 0; i < store.nlearn; i++) {
        const learn_t *l = &store.lr[i];
        if (l->st != key || l->par >= NP) continue;
        if (hi == 0xE0u) { param_set(l->par, (d1 | d2 << 7) * par_top(l->par) / 16383u); return 1; }
        if (l->num != d1) continue;
        if (hi == 0xB0u) param_set(l->par, d2 * par_top(l->par) / 127u);
        else if (hi == 0x90u && d2) note_button(l->par);
        return 1;
    }
    return 0;
}
static void run_toggle(void)
{
    run_on = !run_on;
    store.run_on = (uint8_t)run_on;
    run_next = fm1_ms + 200u; run_grid_t = fm1_ms;
    if (run_on && !par[P_RATE]) { param_set(P_RATE, 40u); }
    learn_say(run_on ? "RUN: IN CORSO" : "RUN: FERMO", 0);
    if (page == PG_RUN) dirty_cells = 0xF;
}
static void midi_in(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t hi = st & 0xF0u;
    if (st >= 0xF8u) {
        if (!clk_ext) return;
        if (st == 0xF8u) {
            uint32_t dt = fm1_ms - ext_last_ms;
            ext_last_ms = fm1_ms;
            ext_ticks++;
            if (dt > 250u) { ext_cnt = 0; ext_t0 = fm1_ms; }
            else if (++ext_cnt >= 24u) {
                uint32_t el = fm1_ms - ext_t0, b = el ? 600000u / el : ext_bpm10;
                if (b < 200u) b = 200u;
                if (b > 3000u) b = 3000u;
                ext_bpm10 = (ext_bpm10 * 3u + b) / 4u;
                if (b > ext_bpm10 + 50u || b + 50u < ext_bpm10) ext_bpm10 = b;
                { static uint32_t t_e; if (par[P_ECO] && fm1_ms - t_e > 500u) { t_e = fm1_ms; engine_update(); } }   /* ECO in the clock's tempo */
                ext_cnt = 0; ext_t0 = fm1_ms;
            }
        } else if (st == 0xFAu) { ext_ticks = 0; run_seen_ticks = 0; if (!run_on) run_toggle(); }
        else if (st == 0xFCu) { if (run_on) run_toggle(); }
        return;
    }
    if (learn_on && ((hi == 0xB0u && d1 < 120u) || (hi == 0x90u && d2) || hi == 0xE0u)) {
        learn_st = st; learn_num = hi == 0xE0u ? 0u : d1; learn_have = 1;
        learn_try();
        return;
    }
    if (learn_on && (hi == 0x80u || hi == 0x90u)) return;
    if (learnt(st, d1, d2)) return;
    if (store.rx == 1u) return;
    if (store.rx >= 2u && (st & 15u) != store.rx - 2u) return;
    if (hi == 0x90u && d2) note_on(d1, d2);
    else if (hi == 0x80u || hi == 0x90u) note_off(d1);
    else if (hi == 0xB0u && d1 == 64u) { uint32_t h = d2 >= 64u; if ((hold_on != 0u) != h) { hold_on = (uint8_t)h; learn_say(h ? "PEDALE: HOLD" : "PEDALE: SPENTO", 0); } }   /* 0.5: the sustain pedal */
    else if (hi == 0xB0u && d1 >= 20u && d1 < 20u + NP) param_set(d1 - 20u, d2 * par_top(d1 - 20u) / 127u);
    else if (hi == 0xC0u && d1 < NPRESET) { pend_preset = -1; preset_recall(d1); }
    else if (hi == 0xB0u && d1 == 123u) stop_all();
}
static void midi_value(uint32_t c, char *s)
{
    char *e;
    *s = 0;
    switch (c) {
    case 0:
        if (!clk_ext) e = cat(s, "INTERNO");
        else if (fm1_ms - ext_last_ms > 500u) e = cat(s, "ESTERNO");
        else { e = cat_u(s, ext_bpm10 / 10u); e = cat(e, " BPM"); }
        break;
    case 1:
        if (!store.rx) cat(s, "TUTTO");
        else if (store.rx == 1u) cat(s, "SOLO CLOCK");
        else { e = cat(s, "CANALE "); cat_u(e, store.rx - 1u); }
        break;
    case 2: cat(s, learn_on ? "ATTIVO" : "SPENTO"); break;
    default: cat(s, del_all ? "TUTTE (OCT+)" : "ULTIMA (OCT+)"); break;
    }
}
static void midi_knob(uint32_t c, int32_t d)
{
    if (c == 0u) { clk_ext = d > 0; store.clk_in = (uint8_t)clk_ext; settings_touch(); }
    else if (c == 1u) { int32_t r = (int32_t)store.rx + d; store.rx = (uint8_t)(r < 0 ? 0 : r > 17 ? 17 : r); settings_touch(); }
    else if (c == 2u) { if ((d > 0) != (learn_on != 0u)) learn_start(d > 0); }
    else del_all = d > 0;
    dirty_cells |= 1u << c;
}
static void midi_delete(void)
{
    if (!store.nlearn) { learn_say("LEARN: NIENTE DA CANCELLARE", 0); return; }
    if (del_all) store.nlearn = 0; else store.nlearn--;
    settings_touch();
    learn_say(del_all ? "LEARN: CANCELLATE TUTTE" : "LEARN: CANCELLATA L'ULTIMA", 0);
}

static uint32_t smc_btn;                            /* the SMC-Mixer's transport as the FM-1's buttons */
#if GRANA_BT
#include "virgo_bt.c"
#elif defined(GRANA_WEB)
static uint32_t smc_sent[64], smc_nsent;
static int bt_send(uint32_t st, uint32_t d1, uint32_t d2) { smc_sent[smc_nsent++ & 63u] = st << 16 | d1 << 8 | d2; return 1; }
#include "virgo_smc.c"
__attribute__((export_name("smc_in"))) void web_smc_in(uint32_t st, uint32_t d1, uint32_t d2) { smc_in((st >> 4) | st << 8 | d1 << 16 | d2 << 24); }
__attribute__((export_name("smc_frame"))) void web_smc_frame(void) { smc_frame(); }
#endif

/* ---- the user bank over SysEx (as GRANA 1.6: the same editor sends it) and the presets */
static void ub_reply(uint32_t cmd, const uint8_t *a, uint32_t x, uint32_t y)
{
    uint8_t m[10];
    uint32_t n = 5;
    m[0] = 0xF0; m[1] = 0x7D; m[2] = 0x47; m[3] = 0x52; m[4] = (uint8_t)cmd;
    if (a) { m[n++] = a[0] & 0x7Fu; m[n++] = a[1] & 0x7Fu; m[n++] = a[2] & 0x7Fu; }
    else { m[n++] = (uint8_t)(x & 0x7Fu); m[n++] = (uint8_t)(y & 0x7Fu); }
    m[n++] = 0xF7;
    sysex_send(m, n);
}
static void bank_reload(void)
{
    smp_len = 0;
    smp_loaded = 0xFFFFu;
    bank_open();
    if (par[P_SND] > nsnd) par[P_SND] = 0;
    samp_sync();
    dirty_hdr = 1; dirty_cells = 0xF;
}
#if !defined(DINGHY_HOST)
#define VUB_LEN UB_LEN
#else
static int ub_erase(uint32_t off) { (void)off; return 0; }
static int ub_write(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return 0; }
static int flash_peek(uint32_t at, void *p, uint32_t n) { (void)at; (void)p; (void)n; return -1; }
#define VUB_LEN 0x45000u
#endif
static int ub_sysex(const uint8_t *p, uint32_t n)
{
    static uint8_t blk[256], m[9 + 512 + 1];
    uint32_t k, at;
    switch (p[0]) {
    case 0x10u:
        stop_all();
        ub_busy = 1;
        ub_total = n >= 4u ? ((uint32_t)(p[1] & 0x7Fu) << 14 | (uint32_t)(p[2] & 0x7Fu) << 7 | (p[3] & 0x7Fu)) : 0u;
        ub_got = 0;
        learn_say("BANCA UTENTE: RICEVO...", 0);
        bank_reload();
        if (ub_erase(0)) { ub_reply(0x1Fu, 0, 1, 0); return 1; }
        ub_reply(0x11u, 0, 0, 0);
        return 1;
    case 0x12u:
        if (n < 4u + 512u) { ub_reply(0x1Fu, 0, 2, 0); return 1; }
        at = ((uint32_t)(p[1] & 0x7Fu) << 14 | (uint32_t)(p[2] & 0x7Fu) << 7 | (p[3] & 0x7Fu)) * 256u;
        if (!ub_busy || at + 256u > VUB_LEN) { ub_reply(0x1Fu, 0, 3, 0); return 1; }
        for (k = 0; k < 256u; k++) blk[k] = (uint8_t)((p[4 + 2u * k] & 15u) << 4 | (p[5 + 2u * k] & 15u));
        if (!(at & 0xFFFu) && at && ub_erase(at)) { ub_reply(0x1Fu, 0, 4, 0); return 1; }
        if (ub_write(at, blk, 256u)) { ub_reply(0x1Fu, 0, 5, 0); return 1; }
        ub_reply(0x13u, p + 1, 0, 0);
        if (!(++ub_got & 7u)) {
            char t[24], *e = cat_u(t, ub_got / 4u);
            if (ub_total) { e = cat(e, " DI "); e = cat_u(e, ub_total / 4u); }
            cat(e, " KB");
            learn_say("BANCA UTENTE: ", t);
        }
        return 1;
    case 0x14u:
        ub_busy = 0;
        bank_reload();
        ub_reply(0x15u, 0, 0, nsnd);
        if (nsnd) { char t[8]; fmt_u(t, nsnd); learn_say("BANCA UTENTE: SUONI ", t); }
        else learn_say(ub_diag == 1 ? "BANCA UTENTE: NON LEGGIBILE" : "BANCA UTENTE: NON VALIDA", 0);
        return 1;
    case 0x16u:
        if (n < 4u) return 1;
        at = ((uint32_t)(p[1] & 0x7Fu) << 14 | (uint32_t)(p[2] & 0x7Fu) << 7 | (p[3] & 0x7Fu)) * 256u;
        if (flash_peek(at, blk, 256u)) { ub_reply(0x1Fu, 0, 6, 0); return 1; }
        m[0] = 0xF0; m[1] = 0x7D; m[2] = 0x47; m[3] = 0x52; m[4] = 0x17;
        m[5] = p[1] & 0x7Fu; m[6] = p[2] & 0x7Fu; m[7] = p[3] & 0x7Fu;
        for (k = 0; k < 256u; k++) { m[8 + 2u * k] = blk[k] >> 4; m[9 + 2u * k] = blk[k] & 15u; }
        m[8 + 512] = 0xF7;
        sysex_send(m, 9u + 512u);
        return 1;
    case 0x18u:
        ub_reply(0x19u, 0, 0, nsnd);
        return 1;
    default: return 0;
    }
}
/* the presets: 21 dump (48 x 22 idx <256 B as nibbles>, then 25); 23 idx <nibbles> into RAM (26 idx); 24 keep (25).
 * (GRANA's 01..06 are left alone, so GRANA's editor cannot mistake VIRGO's presets for its own) */
static uint32_t sx_got;
static void app_sysex(const uint8_t *p, uint32_t n)
{
    static uint8_t m[6 + 512 + 1];
    uint32_t i, k;
    if (!n) return;
    if (ub_sysex(p, n)) return;
    if (p[0] == 0x21u) {
        for (i = 0; i < NPRESET; i++) {
            prec_t r = pr[i];
            const uint8_t *b = (const uint8_t *)&r;
            if (r.used) { r.magic = PREC_MAGIC; r.ver = PREC_VER; r.np = NP; r.sum = prec_sum(&r); }
            m[0] = 0xF0; m[1] = 0x7D; m[2] = 0x47; m[3] = 0x52; m[4] = 0x22; m[5] = (uint8_t)i;
            for (k = 0; k < 256u; k++) { uint8_t v = k < sizeof r ? b[k] : 0xFFu; m[6 + 2u * k] = v >> 4; m[7 + 2u * k] = v & 15u; }
            m[6 + 512] = 0xF7;
            if (sysex_send(m, sizeof m)) return;
        }
        m[4] = 0x25; m[5] = 0xF7;
        sysex_send(m, 6);
    } else if (p[0] == 0x23u && n >= 2u + 2u * sizeof(prec_t) && p[1] < NPRESET) {
        prec_t r;
        uint8_t *b = (uint8_t *)&r;
        for (k = 0; k < sizeof r; k++) b[k] = (uint8_t)((p[2 + 2u * k] & 15u) << 4 | (p[3 + 2u * k] & 15u));
        i = p[1];
        if (r.magic == PREC_MAGIC && r.ver == PREC_VER && (r.np == NP || r.np == NP5 || r.np == NP4) && r.sum == prec_sum(&r) && r.used) {
            for (k = r.np; k < NP; k++) r.p[k] = DEF2[k - NP1];   /* 0.6: an older editor's preset, the new knobs as they come */
            r.np = NP;
            for (k = 0; k < NP; k++) if (r.p[k] > PMAX[k]) r.p[k] = PMAX[k];
            r.name[11] = 0;
            pr[i] = r;
        } else { for (k = 0; k < sizeof r; k++) ((uint8_t *)&pr[i])[k] = 0; }
        save_sectors |= 1u << (i / 16u);
        sx_got++;
        m[0] = 0xF0; m[1] = 0x7D; m[2] = 0x47; m[3] = 0x52; m[4] = 0x26; m[5] = (uint8_t)i; m[6] = 0xF7;
        sysex_send(m, 7);
    } else if (p[0] == 0x24u) {
        if (sx_got) { save_pending = 1; t_save = fm1_ms; sx_got = 0; }
        if (!pr[cur_preset].used) cur_preset = 0;
        if (pr[cur_preset].used) preset_recall(cur_preset);
        m[0] = 0xF0; m[1] = 0x7D; m[2] = 0x47; m[3] = 0x52; m[4] = 0x25; m[5] = 0xF7;
        sysex_send(m, 6);
    }
}

/* ---- the loop */
static uint32_t prev_keys;
static uint8_t held[27];
static void app_setup(void)
{
    uint32_t k;
    pgp_init();
    presets_load();
    reverb_init();
    sky_init();
    spec_init();
    for (k = 0; k < NP; k++) par[k] = pr[cur_preset].p[k];
    bank_open();
    if (par[P_SND] > nsnd) par[P_SND] = 0;
    clk_ext = store.clk_in & 1u;
    run_on = store.run_on & 1u;
    engine_update();
    tz_home();
    intro = 1; intro_t0 = fm1_ms;
    lcd_fill(0, 0, 240, 240, 0);
}
static void intro_finish(void)
{
    uint32_t k;
    intro = 0;
    fm1_led_bg_ns = 5000;
    for (k = 0; k < 41u; k++) fm1_led_back(k, 1);
    lcd_sync();
    lcd_fill(0, 0, 240, 240, 0);
    samp_sync();
    dirty_hdr = 1; dirty_cells = 0xF;
}
static void app_step(void)
{
    static uint32_t t_pic, t_spec, t_hold_home, home_fired;
    uint32_t keys, down, up, b, rel, k, kb, lit = 0;
    uint8_t st, d1, d2;
    int32_t d;

    if (intro) {
        static uint32_t last;
        uint32_t r2;
        if (fm1_input_edges(&r2) || (fm1_in.notes & ~prev_keys) || fm1_ms - intro_t0 >= IN_END) { prev_keys = fm1_in.notes; intro_finish(); }
        else if (fm1_ms - last >= 33u) { last = fm1_ms; intro_frame(); }
        if (intro) return;
    }

    kb = (uint32_t)(53 + 12 * octave);
    keys = fm1_in.notes;
    down = keys & ~prev_keys;
    up = prev_keys & ~keys;
    prev_keys = keys;
    for (k = 0; k < 27u; k++) {
        if (down >> k & 1u) { held[k] = (uint8_t)(kb + k); note_on(held[k], 110); midi_send(0x90, held[k], 110); }
        if (up >> k & 1u) { note_off(held[k]); midi_send(0x80, held[k], 0); }
    }

    b = fm1_input_edges(&rel) | smc_btn;
    smc_btn = 0;
    if (save_mode && ((b & ~(1u << BTN_SAVE)) || fm1_ms - t_savemode > 20000u)) { save_mode = 0; dirty_hdr = 1; dirty_cells = 0xF; }
    if (b & (1u << BTN_HOME)) { t_hold_home = fm1_ms; home_fired = 0; }
    if ((fm1_in.buttons >> BTN_HOME & 1u) && !home_fired && fm1_ms - t_hold_home >= 600u) {   /* HOME held: HOLD */
        home_fired = 1; hold_on = hold_on == 0u ? 1u : hold_on == 1u ? 2u : 0u;   /* 0.5: OFF, HOLD, HOLD INFINITO */
        learn_say(hold_on == 1u ? "HOLD: LE CODE RESTANO" : hold_on == 2u ? "HOLD INFINITO: CODE FERME" : "HOLD: SPENTO", 0);
    }
    if ((rel & (1u << BTN_HOME)) && !home_fired && !save_mode) goto_page(page == PG_SORG ? PG_ARM : page == PG_ARM ? PG_FUS : PG_SORG);
    if (b & (1u << BTN_SEL)) goto_page(page == PG_CHIRP ? PG_FINE : page == PG_FINE ? PG_MOT : page == PG_MOT ? PG_EVT : PG_CHIRP);   /* 0.6 EVENTO */
    if (b & (1u << BTN_EDIT)) goto_page(PG_RING);
    if (b & (1u << BTN_FX)) goto_page(page == PG_FILT ? PG_INT : page == PG_INT ? PG_RIV : page == PG_RIV ? PG_SHIM : page == PG_SHIM ? PG_SPAZ : PG_FILT);   /* 0.5: all the effects */
    if (b & (1u << BTN_ENV)) goto_page(page == PG_RUM ? PG_RADIO : PG_RUM);
    if (b & (1u << BTN_LFO)) goto_page(page == PG_MODA ? PG_MODB : page == PG_MODB ? PG_CIELO : PG_MODA);
    if (b & (1u << BTN_SEQ)) goto_page(PG_CAMP);
    if (b & (1u << BTN_GLO)) goto_page(page == PG_MIX ? PG_MIDI : page == PG_MIDI && GRANA_BT ? PG_BT : PG_MIX);   /* 0.5: AUDIO, MIDI (BT) */
    if (b & (1u << BTN_ARP)) goto_page(page == PG_RUN ? PG_DEJA : page == PG_DEJA ? PG_TUR : page == PG_TUR ? PG_RIT : PG_RUN);   /* 0.6 TURING, RITMO */   /* 0.4: RUN, DEJA VU (PLAY: RUN on / off) */
    if (b & (1u << BTN_PLAY)) run_toggle();
    if (b & (1u << BTN_REC)) rec_action();   /* 0.6: A COMANDO, the merger */
    if (b & (1u << BTN_OCT_DOWN) && octave_i > 0u) { octave_i--; store.oct = (uint8_t)octave_i; settings_touch(); }
    if (b & (1u << BTN_OCT_UP)) {
        if (page == PG_MIDI) midi_delete();
#if GRANA_BT
        else if (page == PG_BT) bt_confirm();
#endif
        else if (octave_i < 6u) { octave_i++; store.oct = (uint8_t)octave_i; settings_touch(); }
    }
    if (b & (1u << BTN_SAVE)) {
        if (!save_mode) { save_mode = 1; save_dest = pend_preset >= 0 ? (uint32_t)pend_preset : cur_preset; t_savemode = fm1_ms; t_savehold = 0; name_start(); }
        else if (!pr[save_dest].used) { save_mode = 0; save_to(save_dest); dirty_cells = 0xF; }
        else { t_savehold = fm1_ms ? fm1_ms : 1u; t_savemode = fm1_ms; }
        dirty_hdr = 1;
    }
    if (save_mode && t_savehold) {
        if (!(fm1_in.buttons >> BTN_SAVE & 1u)) t_savehold = 0;
        else if ((int32_t)(fm1_ms - t_savehold) >= 1000) { save_mode = 0; t_savehold = 0; save_to(save_dest); dirty_cells = 0xF; }
        dirty_hdr = 1;
    }

    for (k = 0; k < 4u; k++)
        if ((d = fm1_enc_take(ENC_KNOB1 + k)) != 0) {
            uint32_t p = pp(page, k);
            if (save_mode) { name_knob(k, d); continue; }
            if (page == PG_MIDI) { midi_knob(k, d); continue; }
#if GRANA_BT
            if (page == PG_BT) { bt_knob(k, d); continue; }
#endif
            if (learn_on) { if (p < NP) { learn_par = (int32_t)p; t_learn = fm1_ms; learn_try(); } continue; }
            param_change(p, accel(d, par_top(p)));
            t_touch[k] = fm1_ms ? fm1_ms : 1u;
            if (p == P_SND || p == P_SSTART) samp_sync();
        }
    if ((d = fm1_enc_take(ENC_SELECT)) != 0) {
        uint32_t i, at = 0;
        for (i = 0; i < NPAGE; i++) if (PG_ORDER[i] == page) at = i;
        at = (at + NPAGE + (d > 0 ? 1u : NPAGE - 1u)) % NPAGE;
        goto_page(PG_ORDER[at]);
        t_flash = fm1_ms ? fm1_ms : 1u;
    }
    if ((d = fm1_enc_take(ENC_ALGORITHM)) != 0) cat_play((cat_i + NCAT + (d > 0 ? 1u : NCAT - 1u)) % NCAT);
    if ((d = fm1_enc_take(ENC_PRESETS)) != 0) {
        int32_t pp2 = (save_mode ? (int32_t)save_dest : pend_preset >= 0 ? pend_preset : (int32_t)cur_preset) + (d > 0 ? 1 : -1);
        while (pp2 < 0) pp2 += NPRESET;
        if (save_mode) { save_dest = (uint32_t)pp2 % NPRESET; t_savemode = fm1_ms; t_savehold = 0; name_start(); }
        else { pend_preset = pp2 % (int32_t)NPRESET; t_pend = fm1_ms; }
        dirty_hdr = 1;
    }
    if (pend_preset >= 0 && fm1_ms - t_pend >= 350u) {
        preset_recall((uint32_t)pend_preset);
        pend_preset = -1;
        samp_sync();
    }

    while (midi_get(&st, &d1, &d2)) midi_in(st, d1, d2);
#if GRANA_BT
    {
        static uint8_t bt_inited;
        if (!bt_inited) { bt_inited = 1; bt_boot(); if (store.bt_auto) bt_set(1); }
    }
    bt_step();
    {
        uint8_t mk = (uint8_t)(!bt_on ? 0u : bt_state() == BTS_LINK ? 2u : 1u);
        if (mk != bt_mark) { bt_mark = mk; dirty_hdr = 1; }
    }
    if (bt_kept_new) { bt_kept_new = 0; settings_touch(); }
#endif
    if (learn_on && fm1_ms - t_learn > 30000u) { learn_start(0); learn_say("LEARN: TEMPO SCADUTO", 0); if (page == PG_MIDI) dirty_cells |= 4u; }
    if (t_lmsg && fm1_ms - t_lmsg > 2500u) { t_lmsg = 0; lmsg[0] = 0; dirty_hdr = 1; if (page == PG_MIDI) dirty_cells = 0xF; }
    run_step();
    samp_sync();                                    /* (0.4.1: a sound waits until its knobs rest) */
    {   /* the audio's worst block, decaying */
        static uint32_t t_am;
        static uint32_t t_gv;
        if (audio_us > audio_max) audio_max = audio_us;
        if (fm1_ms - t_am > 1000u) { t_am = fm1_ms; audio_max = audio_max * 7u / 8u; if (page == PG_MIDI) dirty_cells |= 0; }
        if (gov >= gov_max) { gov_max = gov; t_gv = fm1_ms; } else if (fm1_ms - t_gv > 4000u) gov_max = gov;   /* the governor's highest step, held 4 s */
        { static uint32_t lh, t_lh; if (lim_hits != lh) { lh = lim_hits; t_lh = fm1_ms; lim_seen = 1; } else if (fm1_ms - t_lh > 3000u) lim_seen = 0; }   /* the limiter at work, held 3 s */
    }

    /* LEDs */
    for (k = 0; k < NV; k++)
        if (vs[k].stage != ST_OFF && v_note[k] >= kb && v_note[k] < kb + 27u) lit |= 1u << (v_note[k] - kb);
    for (k = 0; k < 27u; k++) fm1_led_key(KEY_LED(k), lit >> k & 1u);
    fm1_led_key(BTN_HOME, page == PG_SORG || page == PG_ARM || page == PG_FUS || hold_on);
    fm1_led_key(BTN_SEL, page == PG_CHIRP || page == PG_FINE || page == PG_MOT || page == PG_EVT);
    fm1_led_key(BTN_EDIT, page == PG_RING);
    fm1_led_key(BTN_FX, page == PG_INT || page == PG_FILT || page == PG_RIV || page == PG_SHIM || page == PG_SPAZ);
    fm1_led_key(BTN_ENV, page == PG_RUM || page == PG_RADIO);
    fm1_led_key(BTN_LFO, page == PG_CIELO || page == PG_MODA || page == PG_MODB);
    fm1_led_key(BTN_SEQ, page == PG_CAMP);
    fm1_led_key(BTN_GLO, page == PG_MIX || page == PG_MIDI || page == PG_BT);
    fm1_led_key(BTN_ARP, page == PG_RUN || page == PG_DEJA || page == PG_TUR || page == PG_RIT || (run_on && par[P_RATE]));
    fm1_led_key(BTN_PLAY, run_on && par[P_RATE] && (fm1_ms >> 9 & 1u));
    fm1_led_key(BTN_REC, nlog && fm1_ms - evlog[0].t < 300u);
    fm1_led_key(BTN_OCT_DOWN, octave < 0);
    fm1_led_key(BTN_OCT_UP, octave > 0);
    fm1_led_key(BTN_SAVE, save_mode ? (fm1_ms >> 7 & 1u) : edited);

    /* the screen */
    track_merges();
    if (fm1_ms - t_spec >= 40u) { t_spec += 40u; if (fm1_ms - t_spec > 160u) t_spec = fm1_ms; spec_column(); }
    if (t_flash && fm1_ms - t_flash > 700u) t_flash = 0;
    {   /* the header follows the events (their dots) */
        static uint32_t was;
        uint32_t now = vis[0].stage | vis[1].stage << 4 | vis[2].stage << 8 | vis[3].stage << 12 | (gov_max != 0) << 16 | lim_seen << 17;
        if (now != was) { was = now; dirty_hdr = 1; }
        {   /* 0.6: the battery */
            static uint32_t bwas, low_said;
            uint32_t b2 = batt_state();
            if (b2 != bwas) {
                bwas = b2; dirty_hdr = 1;
                if ((b2 & 15u) == 1u && !low_said) { low_said = 1; learn_say("BATTERIA SCARICA", 0); }
                if ((b2 & 7u) >= 2u || (b2 & 8u)) low_said = 0;
                if (page == PG_MIDI) dirty_cells |= 0;
            }
        }
        if (save_mode && t_savehold) dirty_hdr = 1;
    }
    if (dirty_hdr) { dirty_hdr = 0; draw_header(); }
    {
        static uint32_t was_saving;
        if (save_mode && (name_dirty || !was_saving)) { name_dirty = 0; draw_name(); }
        if (!save_mode && was_saving) dirty_cells = 0xF;
        was_saving = save_mode;
    }
    if (!save_mode) {
        for (k = 0; k < 4u; k++) if (t_touch[k] && fm1_ms - t_touch[k] > 2500u && fm1_ms - t_touch[k] < 2600u) dirty_cells |= 1u << k;
        if (page == PG_MIDI && clk_ext) { static uint32_t t_mc; if (fm1_ms - t_mc > 500u) { t_mc = fm1_ms; dirty_cells |= 1u; } }
        for (k = 0; k < 4u; k++) if (dirty_cells >> k & 1u) { draw_cell(k); dirty_cells &= ~(1u << k); }
    }
    if (fm1_ms - t_pic >= 50u) { t_pic = fm1_ms; draw_picture(); }

    if (save_pending && quiet() && fm1_ms - t_save > 300u) { presets_flush(); save_pending = 0; }
    if (((t_cur && pr[cur_preset].used && store.cur != cur_preset && fm1_ms - t_cur > 3000u) ||
         (set_dirty && fm1_ms - t_set > 2000u)) && quiet()) {
        store.ver = SET_VER;
        if (pr[cur_preset].used) store.cur = (uint8_t)cur_preset;
        vset_save();
        t_cur = 0;
        set_dirty = 0;
    }
}

#ifdef GRANA_WEB
static void web_presets_out(void) { save_sectors = 7u; presets_flush(); }
static void web_presets_in(void) { presets_load(); if (!pr[cur_preset].used) cur_preset = 0; preset_recall(cur_preset); }
static void web_bank_changed(void) { bank_reload(); }
static uint8_t web_fbank[16];                       /* (VIRGO has no factory bank) */
static uint8_t *web_bank_buf(void) { return web_fbank; }
static uint32_t web_bank_size(void) { return sizeof web_fbank; }
static uint32_t web_state(void) { return page | hold_on << 5 | run_on << 6 | (uint32_t)(octave + 3) << 9 | cur_preset << 12 | audio_us << 18; }
__attribute__((export_name("par"))) uint8_t *web_par(void) { return par; }
#else
static void app_main(void)
{
    app_setup();
    for (;;) {
        platform_poll();
        app_step();
    }
}
#endif
