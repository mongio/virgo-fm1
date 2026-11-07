/* SPDX-License-Identifier: GPL-3.0-only */
/* VIRGO (from GRANA): the M-VAVE SMC-Mixer in its DAW (Mackie Control) mode (GRANA + Bluetooth; the simulator too, for the tests).
 * Its messages and the faders' soft takeover follow Hortator's smc.c (DeadActive, GPL-3.0), checked there on a unit.
 * The includer gives bt_send(status, d1, d2) -> 1 queued. Main loop only. */
/* ---- the SMC-Mixer for VIRGO (0.2 on): faders 1..8 MASSA 1, MASSA 2, ROTAZIONE, DURATA, INDICE, FREQUENZA (the filter's),
 * RIVERBERO, VOLUME; knobs 1..4 the page's, 5..8 ARMONICHE, SMORZAMENTO, SATURAZIONE, RISONANZA; SELECT 1..8 SORGENTE,
 * ARMONIA, FUSIONI, CHIRP, MOTORE, RINGDOWN, FILTRO, RUN;
 * PLAY / STOP RUN, REC an event; BANK the presets, CHANNEL the pages */
static const uint8_t SMC_FAD[8] = {P_M1, P_M2, P_SPIN, P_DUR, P_IDX, P_FREQ, P_RMIX, P_VOL};
static const uint8_t SMC_KN[4] = {P_HARM, P_SMORZ, P_DRIVE, P_RES};
static const uint8_t SMC_PG[8] = {PG_SORG, PG_ARM, PG_FUS, PG_CHIRP, PG_MOT, PG_RING, PG_FILT, PG_RUN};
static void smc_say(const char *who, uint32_t p)   /* 1.4.2: what the control moved, on every page: "FADER 5 NASTRO MIX 40%" */
{
    char v[24], t[40], *e = cat(t, who);
    *e++ = ' ';
    e = cat(e, pname_of(p));
    *e++ = ' ';
    *e = 0;
    value_text(p, v);
    learn_say(t, v);
}
static struct {
    uint32_t lit[2], known[2];
    uint8_t lv[8], last[8], picked, seen, pend, on, heard, daw, kv[8], kseen;
    int32_t kacc[8];
    uint8_t ccled;
} smc;
static void smc_fresh(void)
{
    uint32_t i;
    smc.known[0] = smc.known[1] = 0;
    smc.picked = smc.seen = 0;
    smc.pend = 0xFFu;
    smc.heard = 0;
    smc.daw = 0;
    smc.ccled = 0;
    smc.kseen = 0;
    for (i = 0; i < 8u; i++) smc.lv[i] = (uint8_t)(par[SMC_FAD[i]] * 127u / PMAX[SMC_FAD[i]]);
    smc.on = 1;
}
static void smc_fader(uint32_t n, uint32_t v14)
{
    uint32_t b = 1u << n, p = SMC_FAD[n], v = v14 >> 7, s, cur = par[p] * 127u / PMAX[p], crossed;
    if (cur != smc.lv[n]) { smc.lv[n] = (uint8_t)cur; smc.picked &= (uint8_t)~b; smc.pend |= (uint8_t)b; }   /* moved here */
    s = smc.lv[n];
    crossed = (smc.seen & b) && ((smc.last[n] < s) != (v < s));
    smc.last[n] = (uint8_t)v;
    smc.seen |= (uint8_t)b;
    if (!(smc.picked & b)) {                    /* soft takeover: within 2 of the value, or across it */
        if (!crossed && (v + 2u < s || v > s + 2u)) {
            char t[16], *e = cat(t, "FADER ");
            fmt_u(e, n + 1u); while (*e) e++;
            cat(e, v < s ? ": SU" : ": GIU'");
            learn_say(t, " PER AGGANCIARE");   /* where the value is: the fader's LED blinks until it gets there */
            return;
        }
        smc.picked |= (uint8_t)b;
    }
    param_set(p, v14 * PMAX[p] / 16383u);
    if (cell_of(page, p) >= 0) dirty_cells |= 1u << cell_of(page, p);   /* 1.4.4: the page shows it */
    smc.lv[n] = (uint8_t)(par[p] * 127u / PMAX[p]);
    smc.pend |= (uint8_t)b;
    {
        char t[10], *e = cat(t, "FADER ");
        fmt_u(e, n + 1u);
        smc_say(t, p);
    }
}
static void smc_button(uint32_t n)
{
    uint32_t k = n & 7u, i;
    if (n >= 0x18u && n < 0x20u) { goto_page(SMC_PG[k]); return; }
    switch (n) {
    case 0x5Eu: if (!run_on) run_toggle(); break;                                       /* PLAY */
    case 0x5Du: if (run_on) run_toggle(); break;                                        /* STOP */
    case 0x5Fu: run_event(); learn_say("EVENTO: ", evlog[0].name); break;                /* REC */
    case 0x2Eu: case 0x2Fu:                                                             /* BANK < > */
        pend_preset = -1;
        preset_recall((cur_preset + (n == 0x2Fu ? 1u : NPRESET - 1u)) % NPRESET);
        dirty_hdr = 1; dirty_cells = 0xF;
        break;
    case 0x30u: case 0x31u: {                                                           /* CHANNEL < > */
        int32_t at = 0, m = (int32_t)sizeof PG_ORDER;
        for (i = 0; i < sizeof PG_ORDER; i++) if (PG_ORDER[i] == page) at = (int32_t)i;
        at = (at + (n == 0x31u ? 1 : m - 1)) % m;
        goto_page(PG_ORDER[at]);
        break; }
    default: break;
    }
}
static void smc_unknown(uint32_t st, uint32_t d1, uint32_t d2)   /* 1.4.4: what it is not mapped, on the strip */
{
    static const char HX[] = "0123456789ABCDEF";
    char t[16], *e = t;
    uint32_t b[3], k;
    b[0] = st; b[1] = d1; b[2] = d2;
    for (k = 0; k < 3u; k++) { *e++ = ' '; *e++ = HX[(b[k] >> 4) & 15u]; *e++ = HX[b[k] & 15u]; }
    *e = 0;
    learn_say("SMC ?", t);
}
/* 1.4.4: the SMC-Mixer's DAW mode over Bluetooth, as Mario's unit sends it (28/10): CC on channel 1, 7 bit,
 *   knobs 1..8 CC 2..9 (absolute 0..127), faders 1..8 CC 10..17, MUTE 1..8 CC 20..27 (127 on, 0 off).
 *   What is not known goes on to MIDI (PLAY, the LEARNt) and shows "SMC ? B0 xx yy". The Mackie messages (pitch bend
 *   faders, notes) still understood, their LEDs sent only then. */
static void smc_cc_knob(uint32_t k, uint32_t v)   /* 1.4.5: endless encoders sending 0..127: the turn, scaled */
{
    uint32_t p = k < 4u ? pp(page, k) : SMC_KN[k - 4u];
    int32_t d, r, st;
    if (k < 4u && (page >= PG_MIDI || p >= NP)) { learn_say("KNOB 1-4: SCEGLI UNA PAGINA DI SUONO", 0); return; }
    if (!(smc.kseen >> k & 1u)) { smc.kseen |= (uint8_t)(1u << k); smc.kv[k] = (uint8_t)v; smc.kacc[k] = 0; }
    d = (int32_t)v - (int32_t)smc.kv[k];
    smc.kv[k] = (uint8_t)v;
    if (v == 0u || v == 127u) {                 /* at the mixer's end: the parameter's end too, so all of it is reached */
        param_set(p, v ? PMAX[p] : 0u);
        smc.kacc[k] = 0;
    } else {
        if (!d) return;
        r = (int32_t)(PMAX[p] * 1024u / 127u);  /* the whole turn (127 clicks) = the whole range; a choice: 4 clicks */
        if (r < 256) r = 256;
        smc.kacc[k] += d * r;
        st = smc.kacc[k] / 1024;
        smc.kacc[k] -= st * 1024;
        if (!st) return;
        param_change(p, st);
    }
    if (cell_of(page, p) >= 0) dirty_cells |= 1u << cell_of(page, p);
    {
        char t[10], *e = cat(t, "KNOB ");
        fmt_u(e, k + 1u);
        smc_say(t, p);
    }
}
static void smc_in(uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xFFu, ch = st & 15u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
    if (!smc.on) smc_fresh();
    smc.heard = 1;
    switch (st >> 4) {
    case 0xEu: smc.daw = 1; if (ch < 8u) smc_fader(ch, d2 << 7 | d1); return;
    case 0xBu:                                  /* (any channel: fader 7 comes on channel 5) */
        if (d1 >= 2u && d1 <= 9u) { smc_cc_knob(d1 - 2u, d2); return; }
        if (d1 >= 10u && d1 <= 17u) { smc_fader(d1 - 10u, d2 << 7 | d2); return; }
        if (d1 >= 0x2Cu && d1 <= 0x2Fu) return;   /* the squares: nothing */
        if (d1 >= 0x1Cu && d1 <= 0x1Fu) return;   /* SOLO: nothing in VIRGO */
        if (d1 >= 0x34u && d1 <= 0x3Cu) {         /* transport and arrows (1.4.6), on the press */
            if (!d2 && d1 != 0x34u && d1 != 0x36u && d1 != 0x3Cu) return;   /* (PLAY, REC and cursor down are toggles on the mixer: 7F, then 00 the next press) */
            switch (d1) {
            case 0x34u: smc_btn |= 1u << BTN_PLAY; break;
            case 0x35u: goto_page(PG_RUN); break;
            case 0x36u: smc_btn |= 1u << BTN_REC; break;
            case 0x37u: case 0x38u: smc_button(d1 == 0x37u ? 0x30u : 0x31u); break;       /* page before / after */
            case 0x39u: case 0x3Au: smc_button(d1 == 0x39u ? 0x2Eu : 0x2Fu); learn_say(d1 == 0x39u ? "PRESET PRECEDENTE" : "PRESET SUCCESSIVO", 0); break;
            case 0x3Bu: smc_btn |= 1u << BTN_OCT_UP; learn_say("OTTAVA SU", 0); break;
            case 0x3Cu: smc_btn |= 1u << BTN_OCT_DOWN; learn_say("OTTAVA GIU'", 0); break;
            default: break;
            }
            return;
        }
        if (d1 >= 20u && d1 <= 27u) {             /* 0.5 MUTE: 1 HOLD, 2 HOLD INFINITO (as the button's light), 3..8 FINALE, RADIO, DEJA VU, RIVERBERO, SHIMMER, SPAZIO */
            static const uint8_t MP[6] = {PG_FINE, PG_RADIO, PG_DEJA, PG_RIV, PG_SHIM, PG_SPAZ};
            uint32_t m = d1 - 20u;
            if (m == 0u) { hold_on = d2 ? 1u : 0u; learn_say(d2 ? "HOLD: LE CODE RESTANO" : "HOLD: SPENTO", 0); }
            else if (m == 1u) { hold_on = d2 ? 2u : 0u; learn_say(d2 ? "HOLD INFINITO: CODE FERME" : "HOLD: SPENTO", 0); }
            else goto_page(MP[m - 2u]);
            return;
        }
        break;
    case 0x9u: if (!ch && (d1 <= 0x31u || (d1 >= 0x5Du && d1 <= 0x5Fu))) { smc.daw = 1; if (d2) smc_button(d1); return; } break;
    case 0x8u: if (!ch && (d1 <= 0x31u || (d1 >= 0x5Du && d1 <= 0x5Fu))) return; break;
    default: break;
    }
    if ((st >> 4) != 0xFu) smc_unknown(st, d1, d2);
    midi_in(st, d1, d2);                        /* PLAY and the rest: as in MIDI mode */
}
static void smc_frame(void)                     /* the LEDs and the fader positions that changed */
{
    uint32_t w[2], i, j, b, pg = 0xFFu, budget = 4u;   /* 1.4.3: a few a frame, and only once the mixer has spoken */
    if (!smc.heard) return;
    if (!smc.daw) {                             /* CC mode (1.4.7): the square buttons' LEDs (CC 2C..2F) = the layers the
                                                 * preset has; the faders' positions (pitch bend), their LED lit until picked */
        /* (1.4.8: the squares' LEDs do not answer the mixer's CC: the layers not shown) */
        for (i = 0; i < 8u; i++) {
            uint32_t cur = par[SMC_FAD[i]] * 127u / PMAX[SMC_FAD[i]];
            if (cur != smc.lv[i]) { smc.lv[i] = (uint8_t)cur; smc.picked &= (uint8_t)~(1u << i); smc.pend |= (uint8_t)(1u << i); }
        }
        for (i = 0; smc.pend && i < 8u; i++)
            if ((smc.pend >> i) & 1u) {
                if (!budget-- || !bt_send(0xE0u + i, smc.lv[i], smc.lv[i])) return;
                smc.pend &= (uint8_t)~(1u << i);
            }
        return;
    }
    for (i = 0; i < 8u; i++) if (SMC_PG[i] == page) pg = i;
    for (i = 0; i < 8u; i++) {                  /* a fader's value moved on the FM-1: its position again, unpicked */
        uint32_t cur = par[SMC_FAD[i]] * 127u / PMAX[SMC_FAD[i]];
        if (cur != smc.lv[i]) { smc.lv[i] = (uint8_t)cur; smc.picked &= (uint8_t)~(1u << i); smc.pend |= (uint8_t)(1u << i); }
    }
    w[0] = pg < 8u ? 1u << (24u + pg) : 0u;
    w[1] = run_on ? 2u : 1u;                    /* STOP PLAY: 0x5D 0x5E */
    for (j = 0; j < 2u; j++) {
        uint32_t dirty = ((w[j] ^ smc.lit[j]) | ~smc.known[j]) & (j ? 7u : 0xFFFFFFFFu);
        for (i = 0; dirty && i < 32u; i++) {
            b = 1u << i;
            if (!(dirty & b)) continue;
            if (!budget-- || !bt_send(0x90u, j ? 0x5Du + i : i, (w[j] & b) ? 127u : 0u)) return;
            smc.lit[j] = (smc.lit[j] & ~b) | (w[j] & b);
            smc.known[j] |= b;
            dirty &= ~b;
        }
    }
    for (i = 0; smc.pend && i < 8u; i++)
        if ((smc.pend >> i) & 1u) {
            if (!budget-- || !bt_send(0xE0u + i, smc.lv[i], smc.lv[i])) return;
            smc.pend &= (uint8_t)~(1u << i);
        }
}
