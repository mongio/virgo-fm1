/* SPDX-License-Identifier: GPL-3.0-only */
/* VIRGO: a synthesizer of gravitational waves for the M-VAVE FM-1. Mario Texture's, after GRANA.
 * Built on Dinghy (zvenson), on Felucca's platform (Leo Kuroshita, Hugelton Instruments).
 *
 *   keys           each note is an event: two masses spiral in (the chirp), merge, and the remnant rings
 *                  (the key: the ringdown's pitch); 4 at a time
 *   HOME           SORGENTE (masses, spin, distance); held: HOLD (the codas go on)
 *   SEL            CHIRP          EDIT  RINGDOWN       FX   INTERFEROMETRO
 *   ENV            RUMORE         LFO   CIELO          SEQ  CAMPIONE
 *   ARP            RUN (pressed again there: on / off)        GLO  MIX, SPAZIO, MIDI (BT)
 *   (0.2: HOME also ARMONIA, FUSIONI; SEL MOTORE; FX FILTRO first; LFO MOD A, MOD B first. 0.3: MOTORE has the engines
 *    NEBULOSA, PIEGA, CORDA, VOCE; SPAZIO the ensemble and the echo)
 *   PLAY           RUN on / off   REC   an event at random now
 *   ALGORITHM      the catalogue: real detections (their masses, spin, distance), heard at once
 *   PRESETS, SAVE  48 presets, as GRANA's
 *   SELECT         the pages     OCT- OCT+  the octave
 *   USB-MIDI, TRS  notes, CC 20..55 the parameters, MIDI LEARN, clock in (RUN in time); Bluetooth in VIRGO + Bluetooth
 * Integers only (no FPU, no libgcc). */
#define APP_NAME "VIRGO"
#define CV_MAX (240u * 128u)                            /* the canvas: the picture, 240 x 128 */
#define USER_AT 0x00093000u                             /* the presets and settings: the 16 KiB free before GRANA's presets */
#define USER_LEN 0x4000u
#include "platform.c"
#if !defined(DINGHY_HOST) && !defined(GRANA_WEB)
#define VFAST __attribute__((section(".fast_text")))   /* 0.4.1: the audio's code runs from RAM, out of the flash cache's way */
#define VTAB __attribute__((section(".fast_rodata")))  /* and its tables */
#define VTICKS() fm1_ticks_ai()                      /* (RAM code may not call into the flash: too far for a call) */
#define VAI __attribute__((always_inline))          /* the small helpers: inlined everywhere, no calls between flash and RAM */
#else
#define VTICKS() fm1_ticks()
#define VAI
#define VFAST
#define VTAB
#endif
#include "virgo_tables.h"
#include "vgfx.c"

#if defined(GRANA_WEB) || defined(DINGHY_HOST)
#define POOL
#else
#define POOL __attribute__((section(".pool")))
#endif
#ifndef GRANA_BT
#define GRANA_BT 0
#endif

#include "virgo_dsp.c"

/* ---- the parameters */
enum { P_M1, P_M2, P_SPIN, P_DIST,                  /* SORGENTE (HOME) */
       P_DUR, P_PART, P_HARM, P_PREC,               /* CHIRP (SEL) */
       P_MODI, P_SMORZ, P_ACC, P_CODA,              /* RINGDOWN (EDIT) */
       P_ARM, P_FIN, P_ONDA, P_IMIX,                /* INTERFEROMETRO (FX, again) */
       P_SEIS, P_THERM, P_QUANT, P_GLITCH,          /* RUMORE (ENV) */
       P_DENS, P_DRIFT, P_BAND, P_SKY,              /* CIELO (LFO, third) */
       P_RATE, P_MMAX, P_DMAX, P_RTEMPO,            /* RUN (ARP) */
       P_SND, P_USE, P_SLV, P_SSTART,               /* CAMPIONE (SEQ) */
       P_RMIX, P_RDEC, P_BPM, P_VOL,                /* MIX (GLO) */
       P_ENG, P_ALG, P_IDX, P_DRIVE,                /* 0.2, MOTORE (SEL, again): ONDA / FM, the algorithm, the index, saturation */
       P_IVL, P_SCALE, P_TONIC, P_CQ,               /* ARMONIA (HOME, again): the masses' interval, the scale, its tonic, the chirp */
       P_HIER, P_GRAV, P_TONFO, P_SUB,              /* FUSIONI (HOME, third): the hierarchy (Tonnetz), gravity, the thump, the sub */
       P_FTYPE, P_FREQ, P_RES, P_FDIST,             /* FILTRO (FX) */
       P_MS1, P_MD1, P_MA1, P_LRATE,                /* MOD A (LFO) */
       P_MS2, P_MD2, P_MA2, P_LSHAPE,               /* MOD B (LFO, again) */
       P_CORO, P_ECO, P_ETIME, P_EFB,               /* 0.3, SPAZIO (GLO, third): the ensemble, the echo, its time, its return */
       P_FINALE, P_PICCO, P_SOSP, P_APPR,           /* 0.4, FINALE (SEL, again): the chirp's last part, its top, a hold there, the glide to the key */
       P_RDENS, P_RBRIL, P_RMOV, P_RPRE,            /* RIVERBERO (GLO, again): density, brightness, movement, pre-delay */
       P_DEJA, P_DLEN, P_DSPR, P_DSU,               /* DEJA VU (ARP, again): the loop of chances, its length, the spread, on what */
       P_RADIO, P_DIAL, P_SELET, P_STAT,            /* RADIO (ENV, again): how much, the dial, selectivity, static */
       P_SHIM, P_SHTYPE, P_COMP, P_STEREO,          /* 0.5: SHIMMER and its kind; AUDIO's compressor-limiter and stereo (autopan) */
       NP };
#define NP1 36u                                     /* the parameters of 0.1's presets */
#define NPAGE 21u
static const uint8_t PMAX[NP] = {100, 100, 99, 100,  125, 100, 100, 100,  3, 100, 100, 100,  100, 100, 100, 100,
                                 100, 100, 100, 100,  100, 100, 100, 100,  100, 100, 100, 5,  48, 2, 100, 100,
                                 100, 100, 200, 100,
                                 5, 4, 100, 100,  1, 12, 11, 2,  3, 100, 100, 100,  4, 100, 100, 100,
                                 9, 10, 200, 100,  9, 10, 200, 3,  100, 100, 5, 100,
                                 100, 100, 100, 100,  100, 100, 100, 100,  100, 15, 100, 1,  100, 100, 100, 100,
                                 100, 4, 100, 100};
static const char *const PNAME[NP] = {"MASSA 1", "MASSA 2", "ROTAZIONE", "DISTANZA",
                                      "DURATA", "PARTENZA", "ARMONICHE", "PRECESSIONE",
                                      "MODI", "SMORZAMENTO", "ACCORDO", "CODA",
                                      "BRACCIO", "FINESSE", "ONDA", "MIX",
                                      "SISMICO", "TERMICO", "QUANTISTICO", "GLITCH",
                                      "DENSIT\x83", "DERIVA", "BANDA", "LIVELLO",
                                      "TASSO", "MASSE", "DISTANZE", "TEMPO",
                                      "SUONO", "USO", "LIVELLO", "INIZIO",
                                      "RIVERBERO", "CODA RIV.", "BPM", "VOLUME",
                                      "MOTORE", "ALGORITMO", "INDICE", "SATURAZIONE",
                                      "INTERVALLO", "SCALA", "TONICA", "CHIRP",
                                      "GERARCHIA", "GRAVIT\x83", "TONFO", "SUB",
                                      "TIPO", "FREQUENZA", "RISONANZA", "DISTANZA",
                                      "SORGENTE", "DESTINAZIONE", "QUANTIT\x83", "VEL. LFO",
                                      "SORGENTE", "DESTINAZIONE", "QUANTIT\x83", "FORMA LFO",
                                      "CORO", "ECO", "TEMPO ECO", "RITORNO",
                                      "FINALE", "PICCO", "SOSPENSIONE", "APPRODO",
                                      "DENSIT\x83", "BRILLANZA", "MOVIMENTO", "PRE-DELAY",
                                      "D\x86J\x83 VU", "LUNGHEZZA", "DISPERSIONE", "SU",
                                      "RADIO", "SINTONIA", "SELETTIVIT\x83", "STATICA",
                                      "SHIMMER", "TIPO", "COMP/LIMIT", "STEREO"};
static const char *const PAGE[NPAGE + 2u] = {"SORGENTE", "CHIRP", "RINGDOWN", "INTERFEROMETRO", "RUMORE", "CIELO", "RUN",
                                             "CAMPIONE", "AUDIO", "MOTORE", "ARMONIA", "FUSIONI", "FILTRO", "MOD A", "MOD B",
                                             "SPAZIO", "FINALE", "RIVERBERO", "D\x86J\x83 VU", "RADIO", "SHIMMER", "MIDI", "BLUETOOTH"};
enum { PG_SORG, PG_CHIRP, PG_RING, PG_INT, PG_RUM, PG_CIELO, PG_RUN, PG_CAMP, PG_MIX, PG_MOT, PG_ARM, PG_FUS, PG_FILT,
       PG_MODA, PG_MODB, PG_SPAZ, PG_FINE, PG_RIV, PG_DEJA, PG_RADIO, PG_SHIM, PG_MIDI, PG_BT };
static const uint32_t PG_ACC[NPAGE + 2u] = {0x6FE3FF, 0xFFB35C, 0xB28CFF, 0xFF7A5C, 0x7DFFB0, 0x9FC4FF, 0x6FE3FF,
                                            0xFFD27A, 0xC9D4E8, 0xFF8FD8, 0xFFE07A, 0xB2F0FF, 0x8CF0C8, 0xD0A8FF, 0xD0A8FF,
                                            0x7FB8FF, 0xFFB35C, 0x9EC8FF, 0x6FE3FF, 0xFFC86B, 0xE6C8FF, 0xC9D4E8, 0x6FE3FF};
/* SELECT: the pages as the buttons open them, HOME, SEL, EDIT, FX, ENV, LFO, ARP, SEQ, GLO */
static const uint8_t PG_ORDER[NPAGE] = {PG_SORG, PG_ARM, PG_FUS, PG_CHIRP, PG_FINE, PG_MOT, PG_RING, PG_FILT, PG_INT, PG_RIV,
                                        PG_SHIM, PG_SPAZ, PG_RUM, PG_RADIO, PG_MODA, PG_MODB, PG_CIELO, PG_RUN, PG_DEJA, PG_CAMP, PG_MIX};
/* 0.5: a page's four parameters (no longer always four in a row: AUDIO, RIVERBERO, SHIMMER gather them) */
static uint8_t PGP[NPAGE][4];
static void pgp_init(void)
{
    uint32_t g, k;
    for (g = 0; g < NPAGE; g++) for (k = 0; k < 4u; k++) PGP[g][k] = (uint8_t)(g * 4u + k);
    PGP[PG_MIX][0] = P_VOL; PGP[PG_MIX][1] = P_BPM; PGP[PG_MIX][2] = P_COMP; PGP[PG_MIX][3] = P_STEREO;            /* AUDIO */
    PGP[PG_RIV][0] = P_RMIX; PGP[PG_RIV][1] = P_RDEC; PGP[PG_RIV][2] = P_RDENS; PGP[PG_RIV][3] = P_RBRIL;          /* RIVERBERO */
    PGP[PG_SHIM][0] = P_SHIM; PGP[PG_SHIM][1] = P_SHTYPE; PGP[PG_SHIM][2] = P_RMOV; PGP[PG_SHIM][3] = P_RPRE;     /* SHIMMER */
}
static uint32_t pp(uint32_t pg, uint32_t k) { return pg < NPAGE && k < 4u ? PGP[pg][k] : NP; }
static int32_t cell_of(uint32_t pg, uint32_t p)
{
    uint32_t k;
    if (pg >= NPAGE) return -1;
    for (k = 0; k < 4u; k++) if (PGP[pg][k] == p) return (int32_t)k;
    return -1;
}
static const char *const TEMPON[6] = {"LIBERO", "1 BAR", "1/2", "1/4", "1/8", "ORBITE 2:3"};
static const char *const USEN[3] = {"MATERIA", "PORTANTE", "CIELO"};
static const char *const ENGN[6] = {"ONDA", "FM", "NEBULOSA", "PIEGA", "CORDA", "VOCE"};
static const char *const IDXN[6] = {"INDICE", "INDICE", "APERTURA", "PIEGHE", "BRILLANZA", "VOCALE"};   /* INDICE, as each engine uses it */
static const char *const ETN[6] = {"1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4."};
static const uint16_t ETQ[6] = {64, 85, 128, 192, 256, 384};                                          /* the echo's time, Q8 of a quarter */
static const char *const SHN[5] = {"OTTAVA", "QUINTA", "KERR", "REDSHIFT", "SPIRALE"};
static const char *const ALGN[5] = {"AUTO", "BINARIA", "SPIRALE", "ANELLO", "CAMPANA"};
static const char *const IVLN[2] = {"LIBERO", "DALLE MASSE"};
#define NSCALE 13u
static const char *const SCALEN[NSCALE] = {"CROMATICA", "DA ROTAZIONE", "MAGGIORE", "MINORE", "DORICA", "FRIGIA", "LIDIA",
                                          "MISOLIDIA", "PENTA MAGG.", "PENTA MIN.", "TONI INTERI", "HIRAJOSHI", "ARMONICI"};
static const uint16_t SCALEM[NSCALE] = {0xFFF, 0, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x295, 0x4A9, 0x555, 0x18D, 0xCD5};
static const char *const CQN[3] = {"GLISSANDO", "SCALA", "ARMONICI"};
static const char *const HIERN[4] = {"OFF", "RUN", "TASTI", "RUN+TASTI"};
static const char *const FTN[5] = {"OFF", "RIVELATORE", "BASSO", "BANDA", "ALTO"};
static const char *const MSRCN[10] = {"OFF", "STRAIN h", "FREQUENZA", "INVILUPPO", "FUSIONE", "PRECESSIONE", "ORBITA", "LFO",
                                      "CASO", "VELOCITY"};
static const char *const MDSTN[11] = {"OFF", "INDICE FM", "FILTRO", "RISONANZA", "SATURAZIONE", "PAN", "PITCH", "AMPIEZZA",
                                      "ONDA", "MIX INTERF.", "CIELO"};
static const char *const LSHN[4] = {"SINUSOIDE", "TRIANGOLO", "QUADRA", "CASO"};
static const char *const NTN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static uint8_t par[NP];
static uint32_t gov_max, lim_seen;                  /* (the screen: the governor and the limiter, see virgo_dsp.c) */
static const char *plabel(uint32_t p) { return p == P_IDX ? IDXN[par[P_ENG] % 6u] : p < NP ? PNAME[p] : "?"; }
static uint32_t page, octave_i = 3;                 /* octave 0 = index 3 */
#define octave ((int32_t)octave_i - 3)
static uint32_t dirty_hdr = 1, dirty_cells = 0xF;
static uint32_t learn_on;                           /* MIDI LEARN waits (the MIDI page) */
static char lmsg[48];                                /* a message in the header for a while (LEARN, SMC, saves) */
static uint32_t t_lmsg;

/* ---- strings */
static char *cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static void fmt_u(char *s, uint32_t v)
{
    char t[12];
    uint32_t n = 0, i;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 11u);
    for (i = 0; i < n; i++) s[i] = t[n - 1u - i];
    s[n] = 0;
}
static char *cat_u(char *e, uint32_t v) { fmt_u(e, v); while (*e) e++; return e; }
#ifdef FL_JEDEC_OK
/* which flash this FM-1 has (0.5.1): PUYA, UC25 (newer FM-1s) or FLASH ?, then L n for late operations */
static char *flash_tag(char *e)
{
    e = cat(e, flash_id == 0x856014u ? "PUYA" : flash_id == 0xB36014u ? "UC25" : "FLASH ?");
    if (fl_late_ops) { e = cat(e, " L"); e = cat_u(e, fl_late_ops); }
    return e;
}
#endif
static char *cat_d1(char *e, uint32_t tenths)      /* "12,3" */
{
    e = cat_u(e, tenths / 10u);
    if (tenths < 1000u) { *e++ = ','; *e++ = (char)('0' + tenths % 10u); *e = 0; }
    return e;
}
static void note_name(char *s, uint32_t n)
{
    char *e = cat(s, NTN[n % 12u]);
    fmt_u(e, n / 12u - 1u);
}
static void learn_say(const char *a, const char *b)
{
    char *e = cat(lmsg, a);
    if (b) { uint32_t n = 0; while (b[n] && (uint32_t)(e - lmsg) + n < sizeof lmsg - 1u) { e[n] = b[n]; n++; } e[n] = 0; }
    t_lmsg = fm1_ms ? fm1_ms : 1u;
    dirty_hdr = 1;
}

/* ---- the parameters in physical units */
static uint32_t mass10(uint32_t v) { return (10u * exp2_q16((int32_t)v * 4354)) >> 16; }        /* tenths of a solar mass, 1..100 */
static uint32_t dist_mpc(uint32_t v) { return (10u * exp2_q16((int32_t)v * 5876)) >> 16; }       /* 10 .. 5000 Mpc */
static uint32_t dur_ms(uint32_t v) { return ((exp2_q16((int32_t)v * 6914) >> 4) * 20u) >> 12; }   /* 20 ms .. 30 s at 100 .. 3 min at 125 (0.4) */
static int32_t part_oct16(uint32_t v) { return 19661 + (int32_t)v * 2425; }                        /* 0.3 .. 4 octaves, Q16 */
static uint32_t band_hz(uint32_t v) { return (50u * exp2_q16((int32_t)v * 3488)) >> 16; }         /* 50 .. 2000 Hz */
static uint32_t dens_n(uint32_t v) { return (8u * exp2_q16((int32_t)v * 3932)) >> 16; }           /* 8 .. 512 binaries */
static uint32_t arm_ms10(uint32_t v) { return (10u * exp2_q16((int32_t)v * 3488)) >> 16; }        /* 1 .. 40 ms, tenths */
static uint32_t ring_mul16(uint32_t v) { return exp2_q16((int32_t)(100u - v) * 5898); }            /* SMORZAMENTO: Q x 1 .. 512, Q16 */
static uint32_t lvl_q15(uint32_t v) { return v * v * 32767u / 10000u; }

/* ---- the sample bank (the user bank of GRANA 1.6, sent from the editor; the same format): read in place */
#define BANK_MAX 48u
typedef struct { char name[13]; uint32_t frames; const uint8_t *adpcm; uint8_t lofi, r11; } snd_t;
static snd_t snd[BANK_MAX];
static uint32_t nsnd, ub_busy, ub_diag = 2, ub_total, ub_got;
#if defined(GRANA_WEB)
#elif defined(DINGHY_HOST)
#define UB_LEN_HOST 0x45000u
static uint8_t host_ubank[UB_LEN_HOST];
#endif
static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t bank_crc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) { c ^= *p++; for (k = 0; k < 8u; k++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1; }
    return ~c;
}
static void bank_add(const uint8_t *b, uint32_t room)
{
    uint32_t i, cnt, total;
    if (b[0] != 'G' || b[1] != 'R' || b[2] != 'N' || b[3] != 'B') return;
    cnt = b[6] | (uint32_t)b[7] << 8;
    total = rd32(b + 8);
    if (cnt > BANK_MAX - nsnd || total < 16u + 24u * cnt || total > room) return;
    if (bank_crc(b + 16, total - 16u) != rd32(b + 12)) return;
    for (i = 0; i < cnt; i++) {
        const uint8_t *e = b + 16u + 24u * i;
        snd_t *d = &snd[nsnd];
        uint32_t k, off = rd32(e + 12), fr = rd32(e + 16);
        if (off + (fr + 1u) / 2u > total) break;
        for (k = 0; k < 12u; k++) d->name[k] = (char)(e[k] >= 32u && e[k] < 127u ? e[k] : 0);
        d->name[12] = 0;
        d->frames = fr;
        d->adpcm = b + off;
        d->lofi = e[20] ? 1u : 0u;
        d->r11 = e[21] == 1u ? 1u : 0u;
        nsnd++;
    }
}
static void bank_open(void)
{
    nsnd = 0;
#if defined(GRANA_WEB)
    if (!ub_busy) { web_ub_init(); bank_add(web_ubank, UB_LEN); }
#elif defined(DINGHY_HOST)
    {   /* the tests: a user bank from a file */
        FILE *f = fopen("build/host/ubank.bin", "rb");
        if (f) { size_t n = fread(host_ubank, 1, sizeof host_ubank, f); (void)n; fclose(f); }
    }
    bank_add(host_ubank, sizeof host_ubank);
#else
    {
        static uint32_t plain;
        uint8_t h[4];
        if (!plain) { plain = 1; fl_plain_window_init(); }
        ub_diag = 0;
        if (flash_ok && !st_read(UB_AT, h, 4) && h[0] == 'G' && h[1] == 'R' && h[2] == 'N' && h[3] == 'B') ub_diag = 1;
        if (!ub_busy) bank_add(fm1_xip_ptr(UB_AT), UB_LEN);
        if (ub_diag && nsnd) ub_diag = 2;
    }
#endif
}
static const char *snd_name(uint32_t s) { return s < nsnd ? snd[s].name : "---"; }
static uint8_t smp_peak[200];                      /* the loaded sound's outline (CAMPIONE's page) */
static uint32_t smp_from, smp_full, smp_loaded = 0xFFFFu;
/* decode sound s from frame `from` into smp[] (16 bit, at 22050 Hz: an 11 kHz sound doubled), up to SMP_MAX */
static void smp_load(uint32_t s, uint32_t start_pct)
{
    uint32_t i, idx = 0, full, from, n, x = 0, m = 0, d2;
    int32_t pred = 0, prev = 0;
    const uint8_t *d;
    smp_len = 0;                                   /* the ISR lets it be while it changes */
    smp_loaded = s | start_pct << 8;
    if (s >= nsnd) { smp_full = 0; return; }
    d = snd[s].adpcm;
    full = snd[s].frames;
    d2 = snd[s].r11 ? 2u : 1u;
    from = full / 100u * start_pct + full % 100u * start_pct / 100u;
    n = (full - from) * d2;
    if (n > SMP_MAX) n = SMP_MAX;
    smp_from = from; smp_full = full;
    for (i = 0; i < 200u; i++) smp_peak[i] = 0;
    for (i = 0; i < full; i++) {
        uint32_t code = (d[i >> 1] >> ((i & 1u) * 4u)) & 15u, q;
        int32_t st = ADPCM_STEP[idx], diff = st >> 3;
        if (code & 4u) diff += st;
        if (code & 2u) diff += st >> 1;
        if (code & 1u) diff += st >> 2;
        pred = (code & 8u) ? pred - diff : pred + diff;
        if (pred > 32767) pred = 32767;
        if (pred < -32768) pred = -32768;
        idx = (uint32_t)((int32_t)idx + ADPCM_IDX[code & 7u]);
        if ((int32_t)idx < 0) idx = 0;
        if (idx > 88u) idx = 88u;
        q = (uint32_t)(pred < 0 ? -pred : pred);
        if (q > m) m = q;
        if (i + 1u >= (x + 1u) * full / 200u || i + 1u == full) { if (x < 200u) smp_peak[x] = (uint8_t)(m >> 7); x++; m = 0; }
        if (i >= from) {
            uint32_t o = (i - from) * d2;
            if (o + d2 <= n) {
                if (d2 == 2u) { smp[o] = (int16_t)((prev + pred) >> 1); smp[o + 1u] = (int16_t)pred; }
                else smp[o] = (int16_t)pred;
            }
        }
        prev = pred;
#ifndef GRANA_WEB
        if (!(i & 4095u)) platform_poll();
#endif
    }
    smp[n] = smp[n + 1u] = 0;
    smp_len = n;
}

/* ---- the octave's notes, MIDI's, RUN's: events */
static uint32_t ev_count;                          /* the events since power-on: S0001 .. */
typedef struct { char name[10]; uint16_t m1, m2, mpc; uint8_t note, spin; uint32_t t; } cat_e;
#define NLOG 6u
static cat_e evlog[NLOG];
static uint32_t nlog;
static uint8_t v_note[NV];                         /* the note each voice sounds (the LEDs) */
static uint32_t v_age[NV], ages;
static uint8_t last_notes[4] = {48, 51, 55, 58}, nlast;
static uint16_t v_m1[NV], v_m2[NV], v_mpc[NV];      /* each voice's event, for the screen */
static uint8_t v_spin[NV], v_key[NV] = {255, 255, 255, 255};   /* v_key: the key (or MIDI note) that started it, 255 none */
static char v_name[NV][10];
static void ev_name(char *s, uint32_t n)           /* S0042 */
{
    s[0] = 'S';
    s[1] = (char)('0' + n / 1000u % 10u); s[2] = (char)('0' + n / 100u % 10u);
    s[3] = (char)('0' + n / 10u % 10u); s[4] = (char)('0' + n % 10u); s[5] = 0;
}
static uint32_t rand_u16(void) { return xrand_main() >> 16; }
/* ---- ARMONIA: the coalescence as harmony.
 * INTERVALLO DALLE MASSE: m1 / m2 to the nearest just ratio (unison .. two octaves): the lighter mass sings that much higher.
 * SCALA: fixed, or DA ROTAZIONE (a slow remnant dark, frigia, minore .. a fast one bright, maggiore, lidia).
 * CHIRP: GLISSANDO, or by the scale's steps, or by the harmonics of fM / 16 (the last orbits a rhythm that speeds up).
 * FUSIONI GERARCHICHE: the remnant becomes the next event's heavier mass; the chord moves on the Tonnetz (neo-Riemannian
 * P, L, R) as the masses' ratio says (nearly equal P, unequal L, very unequal R), GRAVITA' pulls it back to the tonic. */
static const uint16_t IVL_R[11] = {4096, 4915, 5120, 5461, 6144, 6554, 6827, 8192, 10240, 12288, 16384};   /* Q12 */
static const char *const IVL_N[11] = {"UNISONO", "3a MIN.", "3a MAGG.", "4a", "5a", "6a MIN.", "6a MAGG.", "OTTAVA",
                                      "10a", "12a", "2 OTTAVE"};
static uint32_t ivl_index(uint32_t m1, uint32_t m2)
{
    int32_t l = log2_q16(m1) - log2_q16(m2 ? m2 : 1u), best = 0x7FFFFFFF;
    uint32_t i, at = 0;
    for (i = 0; i < 11u; i++) {
        int32_t d = l - (log2_q16(IVL_R[i]) - (12 << 16));
        if (d < 0) d = -d;
        if (d < best) { best = d; at = i; }
    }
    return at;
}
static uint32_t ivl_ratio16(uint32_t m1, uint32_t m2)  /* Q16 */
{
    uint32_t r = (uint32_t)IVL_R[ivl_index(m1, m2)] << 4;
    return r == 65536u ? 65732u : r;                 /* the unison a little open: two masses, two voices */
}
static uint32_t scale_mask(uint32_t spin)           /* the scale now (DA ROTAZIONE by the spin) */
{
    uint32_t s = par[P_SCALE] % NSCALE;
    if (s == 1u) s = spin < 20u ? 5u : spin < 40u ? 3u : spin < 55u ? 4u : spin < 70u ? 7u : spin < 85u ? 2u : 6u;
    return SCALEM[s];
}
static uint32_t scale_shown(uint32_t spin)
{
    uint32_t s = par[P_SCALE] % NSCALE;
    if (s == 1u) s = spin < 20u ? 5u : spin < 40u ? 3u : spin < 55u ? 4u : spin < 70u ? 7u : spin < 85u ? 2u : 6u;
    return s;
}
/* the Tonnetz: the chord now (its root, 0..11, and major or minor), the remnant's mass for the next event */
static uint8_t tz_root, tz_minor, tz_last;          /* tz_last: the transformation of the last event, 0 none, 'P' 'L' 'R', 'G' gravity */
static uint32_t hier_m10, hier_n;                   /* the remnant (tenths of a solar mass, 0 none); events in the chain */
static void tz_home(void)
{
    uint32_t mk = scale_mask(par[P_SPIN]);
    tz_root = par[P_TONIC] % 12u;
    tz_minor = (uint8_t)(!(mk >> 4 & 1u) && (mk >> 3 & 1u));   /* a minor third and no major one */
}
static void tz_move(uint32_t m1, uint32_t m2)     /* a merger: the chord moves */
{
    uint32_t q = m2 * 100u / (m1 ? m1 : 1u), r = rand_u16() % 100u;
    if (r < par[P_GRAV] && (tz_root != par[P_TONIC] % 12u)) { tz_home(); tz_last = 'G'; return; }
    if (q > 80u) { tz_minor ^= 1u; tz_last = 'P'; }                        /* P: the same root, major <> minor */
    else if (q > 45u) { tz_root = (uint8_t)((tz_root + (tz_minor ? 8u : 4u)) % 12u); tz_minor ^= 1u; tz_last = 'L'; }
    else { tz_root = (uint8_t)((tz_root + (tz_minor ? 3u : 9u)) % 12u); tz_minor ^= 1u; tz_last = 'R'; }
}
static uint32_t tz_tone(uint32_t near)              /* the chord's tone nearest a note */
{
    int32_t best = 99, at = (int32_t)near, k, o;
    static const uint8_t T[3] = {0, 0, 7};
    for (o = -1; o <= 1; o++)
        for (k = 0; k < 3; k++) {
            int32_t pc = ((int32_t)tz_root + (k == 1 ? (tz_minor ? 3 : 4) : T[k])) % 12;
            int32_t n = ((int32_t)near / 12 + o) * 12 + pc, d = n - (int32_t)near;
            if (d < 0) d = -d;
            if (d < best) { best = d; at = n; }
        }
    return (uint32_t)(at < 0 ? 0 : at > 127 ? 127 : at);
}
static int32_t idx_of(uint32_t v) { return (int32_t)((v * v * 214748u) >> 1); }   /* INDICE: v^2, up to about a turn */
static void ev_harmony(ev_t *e, uint32_t note, uint32_t inc_rd, uint32_t m1, uint32_t m2, uint32_t spin, uint32_t mpc, uint32_t vel)
{
    uint32_t d;
    e->eng = par[P_ENG];
    e->alg_i = par[P_ALG] ? par[P_ALG] : 1u;        /* AUTO: BINARIA in the inspiral */
    e->alg_r = par[P_ALG] ? par[P_ALG] : 4u;        /* CAMPANA in the ring */
    e->idx = idx_of(par[P_IDX]);
    e->drive = 256 + (int32_t)par[P_DRIVE] * par[P_DRIVE] / 6;
    e->r2_q16 = par[P_IVL] ? ivl_ratio16(m1, m2) : 0u;
    e->lv2 = par[P_IVL] ? 14000 : 0;
    e->smask = (uint16_t)scale_mask(spin);
    e->tonic = par[P_TONIC];
    e->cq = par[P_CQ];
    e->inc_h = e->inc_m / 16u;
    e->note_m16 = ((int32_t)note << 16) + 12 * (log2_q16(e->inc_m) - log2_q16(inc_rd));
    e->ftype = par[P_FTYPE];
    {   /* the filter: x 0.25 .. x 32 the wave; DISTANZA closes it as the event is far (up to 4 octaves at 5000 Mpc) */
        int32_t l2 = -131072 + (int32_t)par[P_FREQ] * 4588;
        l2 -= (((log2_q16(mpc) - log2_q16(10)) >> 8) * (int32_t)par[P_FDIST] * 4 / 897) << 8;   /* log2(5000/10) = 8.97 */
        e->fmul_l2 = l2;
    }
    e->fdamp = 65000 - (int32_t)par[P_RES] * 620;  /* 2 .. 0.05 */
    d = (uint32_t)QNM_Q[spin * 4u];                 /* the ringdown's Q (Q8), stretched as SMORZAMENTO stretches it */
    d = (uint32_t)(((uint64_t)d * ring_mul16(par[P_SMORZ])) >> 16);
    e->fdamp_ring = (int32_t)(32768u * 256u / (d < 256u ? 256u : d));
    if (e->fdamp_ring < 1600) e->fdamp_ring = 1600;
    if (!par[P_RES]) e->fdamp_ring = 65000;        /* RISONANZA 0: the filter does not ring */
    e->tonfo = (int32_t)lvl_q15(par[P_TONFO]);
    e->sub = (int32_t)lvl_q15(par[P_SUB]);
    e->rnd = (int32_t)(rand_u16() >> 0) - 32768;
    e->vel = (int32_t)(vel * 258u);
    e->oct16 = part_oct16(par[P_PART]);
    e->e_amt = (int32_t)par[P_IDX] * 327;
    e->fin_p = (int32_t)(exp2_q16(((int32_t)par[P_FINALE] - 50) * 2097) >> 4);   /* FINALE: the time's exponent, Q12, 0.33 .. 3 */
    {
        uint32_t ms = (uint32_t)par[P_SOSP] * par[P_SOSP] * 4u / 5u, gl = (uint32_t)par[P_APPR] * par[P_APPR] * 15u / 100u;   /* SOSPENSIONE up to 8 s; APPRODO up to 1.5 s */
        e->hold = ms * 44u + ms / 10u;
        e->glk = (int32_t)(32768u / (6u + gl * 1378u / 1000u));
    }
    if (e->eng == 4u) {                             /* CORDA: the string, tuned to the key; its losses by the stage */
        uint32_t ik = e->ring_inc[0], P8 = 0xFFFFFFFFu / ((ik >> 8) ? (ik >> 8) : 1u), P = P8 >> 8, k;   /* the (2,2,0)'s note (ACCORDO may move it) */
        int32_t b = 9000 + (int32_t)par[P_IDX] * 237, comp8;   /* the loop's one-pole: 0.27 (dark) .. 1 */
        uint32_t T[3];
        if (b > 32767) b = 32767;
        comp8 = (int32_t)(((uint32_t)(32768 - b) << 8) / (uint32_t)b);   /* its delay, Q8 frames */
        e->ks_b = b;
        e->ks_d8 = P8 > (uint32_t)comp8 + 512u ? P8 - (uint32_t)comp8 : 512u;
        if (e->ks_d8 > ((KS_N - 4u) << 8)) e->ks_d8 = (KS_N - 4u) << 8;
        T[0] = 700u + (uint32_t)par[P_SMORZ] * 75u;             /* T60: 0.7 .. 8.2 s while it rings */
        T[1] = 1500u + (uint32_t)par[P_CODA] * 60u;             /* held: the CODA's time */
        if (T[1] < T[0]) T[1] = T[0];
        T[2] = 900u;                                            /* let go */
        if (P > KS_N) P = KS_N;
        for (k = 0; k < 3u; k++) {
            int32_t l2 = -(int32_t)(653140u * P / (T[k] * 441u / 10u));   /* log2 of the loss a pass: -9.97 P / (T fs) */
            uint32_t g = exp2_q16(l2) >> 1;
            e->ks_g[k] = (int32_t)(g > 32700u ? 32700u : g);
        }
    }
}
static uint32_t pick_voice(void)
{
    uint32_t v, best = 0, ba = 0xFFFFFFFFu;
    for (v = 0; v < NV; v++) if (vs[v].stage == ST_OFF && !ev_trig[v]) return v;
    for (v = 0; v < NV; v++) if (v_age[v] < ba) { ba = v_age[v]; best = v; }
    return best;
}
static int32_t mode_q8(uint32_t i, uint32_t k) { return QNM_Q[i * 4u + k]; }
/* exp(-x), x Q30 (0 .. 1), Q30 */
static int32_t expneg_q30(uint32_t x)
{
    int64_t a = x > 0x40000000u ? 0x40000000 : (int64_t)x, a2 = (a * a) >> 30, a3 = (a2 * a) >> 30, a4 = (a3 * a) >> 30;
    return (int32_t)(0x40000000 - a + (a2 >> 1) - ((a3 * 178956971) >> 30) + ((a4 * 44739243) >> 30));   /* 1/6, 1/24 in Q30 */
}
static void ev_start(uint32_t note, uint32_t vel, uint32_t m1, uint32_t m2, uint32_t spin, uint32_t mpc, const char *name)
{
    uint32_t v = pick_voice(), k, i, t;
    ev_t *e = &ev_pend[v];
    int32_t lm1, lm2, lmc, ltc, oct;
    uint32_t inc_rd, tc_ms, M, mul, xr, nm;
    static const uint16_t SNAP[NM] = {4096, 6144, 5120, 8192};   /* ACCORDO: root, fifth, third, octave (Q12) */
    if (ev_trig[v]) return;
    if (m2 > m1) { t = m1; m1 = m2; m2 = t; }
    if (m1 > 1500u) m1 = 1500u;
    if (m2 < 10u) m2 = 10u;
    if (spin > 99u) spin = 99u;
    /* the inspiral's length: DURATA at a chirp mass of 28, as (28 / Mc)^(5/3) */
    lm1 = log2_q16(m1); lm2 = log2_q16(m2);
    lmc = (lm1 + lm2) * 3 / 5 - log2_q16(m1 + m2) / 5;   /* log2 Mc (tenths) */
    ltc = log2_q16(dur_ms(par[P_DUR])) + (log2_q16(280) - lmc) * 5 / 3;
    if (ltc < log2_q16(20)) ltc = log2_q16(20);
    if (ltc > log2_q16(600000)) ltc = log2_q16(600000);   /* 0.4: up to 10 minutes */
    tc_ms = exp2_q16(ltc) >> 16;
    e->tc = tc_ms * 44u + tc_ms / 10u;             /* 44.1 frames a ms */
    e->rtc = 0xFFFFFFFFu / e->tc;
    /* frequencies: the key is the ringdown; the merger a little under it; the start PARTENZA octaves under that */
    inc_rd = inc_of((int32_t)note << 16);
    e->inc_m = (uint32_t)(((uint64_t)inc_rd * (47186u + spin * 66u)) >> 16);   /* 0.72 .. 0.82 */
    e->inc_m = (uint32_t)(((uint64_t)e->inc_m * exp2_q16(((int32_t)par[P_PICCO] - 50) * 1311)) >> 16);   /* PICCO: x 0.5 .. x 2 */
    {   /* RADIO: the event's station on the dial, by its chirp mass (1 .. 100 solar masses, log) */
        int32_t st = ((lmc - log2_q16(10)) >> 4) * 25600 / 27212;          /* log2(100) = 6.64 octaves, Q12 */
        e->station = st < 0 ? 0 : st > 25600 ? 25600 : st;
    }
    oct = part_oct16(par[P_PART]);
    e->k_q8 = (exp2_q16(oct * 8 / 3) >> 8) - 256u;
    M = m1 + m2;
    e->delta = (int32_t)((m1 - m2) * 32767u / M);
    e->eta = (int32_t)(((m1 * m2) << 8) / ((M * M >> 7) ? (M * M >> 7) : 1u));
    {   /* the distance: amplitude */
        uint32_t g = 400u * 32767u / (mpc + 300u);
        if (g > 39000u) g = 39000u;
        if (g < 4000u) g = 4000u;
        e->amp = (int32_t)(g * vel / 208u);
    }
    e->harm = (int32_t)par[P_HARM] * 655;          /* 0 .. 2, Q15 */
    e->prec = (int32_t)par[P_PREC] * 327;
    e->iota0 = (xrand_main() % 1000u) * 715828u;   /* 0 .. 60 degrees */
    {
        int32_t p = (int32_t)(xrand_main() % 1001u) - 500;   /* the sky: -0.5 .. +0.5 */
        e->pan_l = 23170 - p * 18; e->pan_r = 23170 + p * 18;
    }
    /* the ringdown: the modes of the remnant by its spin; Q stretched by SMORZAMENTO; ACCORDO pulls the ratios to a chord */
    nm = (uint32_t)par[P_MODI] + 1u;
    e->nm = nm;
    mul = ring_mul16(par[P_SMORZ]);
    for (k = 0; k < NM; k++) {
        uint32_t r12 = QNM_R[spin * 4u + k], inc, q8, w30, x30;
        int32_t r, rc, rr;
        {   /* ACCORDO: toward the scale's nearest note (CROMATICA: a chord, root third fifth octave) */
            int32_t tgt = SNAP[k];
            uint32_t mk = scale_mask(spin);
            if (mk != 0xFFFu) {
                int32_t st = (log2_q16(r12) - (12 << 16)) * 12, nn = (int32_t)note + ((st + 32768) >> 16);
                nn = snap_note(nn, mk, par[P_TONIC]);
                tgt = (int32_t)(exp2_q16((nn - (int32_t)note) * 65536 / 12) >> 4);
            }
            if (k == 2u && par[P_IVL]) {            /* INTERVALLO: the second mode is the masses' interval */
                tgt = (int32_t)(ivl_ratio16(m1, m2) >> 4);
                r12 = (uint32_t)tgt;
            }
            r12 = r12 + (uint32_t)((tgt - (int32_t)r12) * (int32_t)par[P_ACC] / 100);
        }
        inc = (uint32_t)(((uint64_t)inc_rd * r12) >> 12);
        e->ring_inc[k] = inc;
        q8 = (uint32_t)(((uint64_t)(uint32_t)mode_q8(spin, k == 2u && par[P_IVL] ? 0u : k) * mul) >> 16);
        if (q8 < 64u) q8 = 64u;
        w30 = (uint32_t)(((uint64_t)inc * 1686629713u) >> 30);   /* the step in radians, Q30 */
        x30 = (w30 / q8) * 128u + ((w30 % q8) * 128u) / q8;      /* omega / 2Q */
        r = expneg_q30(x30);
        rot_q30(inc, r, &e->cr[k][0], &e->sr[k][0]);
        {   /* CODA: 1.5 .. 7.5 s; the release: 0.3 s; never faster than the ring itself where that is slower */
            uint32_t cms = 1500u + (uint32_t)par[P_CODA] * 60u;
            rc = 0x40000000 - (int32_t)(0x40000000u / (cms * 44u));
            rr = 0x40000000 - (int32_t)(0x40000000u / (300u * 44u));
            if (rc < r) rc = r;
            if (rr > rc) rr = rc;
            rot_q30(inc, rc, &e->cr[k][1], &e->sr[k][1]);
            rot_q30(inc, rr, &e->cr[k][2], &e->sr[k][2]);
            e->rad[k][0] = r; e->rad[k][1] = rc; e->rad[k][2] = rr;   /* (APPRODO: the radii, to turn the steps as the pitch glides) */
        }
        xr = (uint32_t)((0x40000000 - r) >> 15) * 8u;
        e->ing[k] = (int32_t)(xr > 32767u ? 32767u : xr);
    }
    {
        int32_t h = e->harm;
        e->mamp[0] = 32767;
        e->mamp[1] = (int32_t)(((int64_t)(1638 + ((e->delta * 11469) >> 15)) * h) >> 15);   /* 0.05 + 0.35 delta */
        e->mamp[2] = par[P_IVL] ? 22000 : 26214;
        e->mamp[3] = (int32_t)(((int64_t)(1638 + (((32767 - 3 * e->eta) * 6554) >> 15)) * h) >> 15);
        if (nm < 2u) e->mamp[1] = 0;
    }
    e->coda_lv = par[P_CODA] ? (int32_t)par[P_CODA] * 250 + 400 : 0;   /* Q15 of the peak (0.3: up to 77 %, a body) */
    ev_harmony(e, note, inc_rd, m1, m2, spin, mpc, vel);
    /* CAMPIONE */
    e->smp_on = (uint8_t)(par[P_SND] && smp_len && par[P_USE] != 2u);
    e->use = par[P_USE];
    e->smp_lv = (int32_t)lvl_q15(par[P_SLV]);
    e->smp_start = 0;
    ev_gate[v] = 1;
    v_note[v] = (uint8_t)note;
    v_key[v] = 255;
    v_age[v] = ++ages;
    ev_trig[v] = 1;
    /* the log */
    ev_count++;
    for (i = NLOG - 1u; i > 0; i--) evlog[i] = evlog[i - 1u];
    if (name) { for (k = 0; k < 9u && name[k]; k++) evlog[0].name[k] = name[k]; evlog[0].name[k] = 0; }
    else ev_name(evlog[0].name, ev_count);
    evlog[0].m1 = (uint16_t)m1; evlog[0].m2 = (uint16_t)m2; evlog[0].mpc = (uint16_t)mpc;
    evlog[0].note = (uint8_t)note; evlog[0].spin = (uint8_t)spin; evlog[0].t = fm1_ms;
    if (nlog < NLOG) nlog++;
    v_m1[v] = (uint16_t)m1; v_m2[v] = (uint16_t)m2; v_mpc[v] = (uint16_t)mpc; v_spin[v] = (uint8_t)spin;
    for (k = 0; k < 10u; k++) v_name[v][k] = evlog[0].name[k];
}
static void hier_merged(uint32_t m1, uint32_t m2)   /* the remnant stays for the next event; the chord moves */
{
    tz_move(m1 > m2 ? m1 : m2, m1 > m2 ? m2 : m1);
    hier_m10 = (m1 + m2) * 95u / 100u;
    hier_n++;
    if (hier_m10 > 1500u) { hier_m10 = 0; hier_n = 0; learn_say("FUSIONI: L'AMMASSO RICOMINCIA", 0); }
}
/* ---- DEJA VU (0.4): a loop of chances, as Mutable Instruments' Marbles keeps one. Left of the middle, the chance that a
 * value comes back instead of a new one grows; at the middle the loop is locked; right of it, the loop's values come in
 * jumps, out of order. LUNGHEZZA 1 .. 16 steps; DISPERSIONE how far a value may go; SU: RUN, or RUN and the keys. */
#define DV_N 16u
enum { DV_NOTE, DV_M1, DV_M2, DV_DIST, DV_SPIN, DV_TIME, DV_K };
static uint16_t dv_buf[DV_N][DV_K], dv_tb[DV_N];
static uint32_t dv_pos, dv_tpos, dv_fresh_last, dv_ok;
static uint32_t dv_step(uint32_t *pos)              /* the next place in the loop; 1 if it takes a new value there */
{
    uint32_t dv = par[P_DEJA], L = (uint32_t)par[P_DLEN] + 1u, r = rand_u16() % 1000u;
    if (!dv_ok) { uint32_t i, k; for (i = 0; i < DV_N; i++) { for (k = 0; k < DV_K; k++) dv_buf[i][k] = rand_u16(); dv_tb[i] = rand_u16(); } dv_ok = 1; }
    if (dv <= 50u) { *pos = (*pos + 1u) % L; return r >= dv * 20u; }          /* 0: all new .. 50: all again */
    if (r < (dv - 50u) * 20u) *pos = rand_u16() % L; else *pos = (*pos + 1u) % L;   /* 50 .. 100: jumps in the loop */
    return 0;
}
static const uint16_t *dv_next(void)                /* six chances for an event */
{
    uint32_t k;
    if (!par[P_DEJA]) { static uint16_t t[DV_K]; for (k = 0; k < DV_K; k++) t[k] = rand_u16(); dv_fresh_last = 1; return t; }
    dv_fresh_last = dv_step(&dv_pos);
    if (dv_fresh_last) for (k = 0; k < DV_K; k++) dv_buf[dv_pos][k] = rand_u16();
    return dv_buf[dv_pos];
}
static uint32_t dv_hit(void)                        /* a chance for RUN's grid (its own loop, the same knob) */
{
    if (!par[P_DEJA]) return rand_u16();
    if (dv_step(&dv_tpos)) dv_tb[dv_tpos] = rand_u16();
    return dv_tb[dv_tpos];
}
static int32_t dv_spread(uint16_t r, int32_t span)  /* -span .. +span by DISPERSIONE */
{
    int32_t x = span < 4096 ? ((int32_t)r - 32768) * span / 32768 : ((((int32_t)r - 32768) >> 3) * (span >> 6)) >> 6;   /* (no overflow) */
    return x * (int32_t)par[P_DSPR] / 100;
}

static void note_on(uint32_t note, uint32_t vel)
{
    uint32_t i, m1 = mass10(par[P_M1]), m2 = mass10(par[P_M2]), h = par[P_HIER] >= 2u, key = note;
    for (i = 3; i > 0; i--) last_notes[i] = last_notes[i - 1u];
    last_notes[0] = (uint8_t)note;
    if (nlast < 4u) nlast++;
    uint32_t spin = par[P_SPIN], mpc = dist_mpc(par[P_DIST]);
    if (par[P_DSU] && par[P_DEJA]) {                 /* DEJA VU on the keys: the sources come back as the loop says */
        const uint16_t *c = dv_next();
        int32_t sp = (int32_t)spin + dv_spread(c[DV_SPIN], 40);
        m1 = (uint32_t)(((uint64_t)m1 * exp2_q16(dv_spread(c[DV_M1], 65536))) >> 16);
        m2 = (uint32_t)(((uint64_t)m2 * exp2_q16(dv_spread(c[DV_M2], 65536))) >> 16);
        mpc = (uint32_t)(((uint64_t)mpc * exp2_q16(dv_spread(c[DV_DIST], 2 * 65536))) >> 16);
        spin = (uint32_t)(sp < 0 ? 0 : sp > 99 ? 99 : sp);
        if (m1 < 10u) m1 = 10u;
        if (m2 < 10u) m2 = 10u;
    }
    if (h) { note = tz_tone(note); if (hier_m10) m1 = hier_m10; }   /* FUSIONI TASTI: the key's nearest chord tone */
    ev_start(note, vel, m1, m2, spin, mpc, 0);
    { uint32_t v; for (v = 0; v < NV; v++) if (v_age[v] == ages) v_key[v] = (uint8_t)key; }
    if (h) hier_merged(m1, m2);
}
static void note_off(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < NV; v++) if (v_key[v] == note) ev_gate[v] = 0;
}
static void stop_all(void) { uint32_t v; for (v = 0; v < NV; v++) ev_gate[v] = 0; }
static int quiet(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++) if (vs[v].stage != ST_OFF || ev_trig[v]) return 0;
    return 1;
}

/* ---- from the parameters to the engine */
static uint32_t ext_last_ms, ext_cnt, ext_t0, ext_bpm10 = 1200;
static uint32_t clk_ext;
static void engine_update(void)
{
    uint32_t hz;
    eng.n_seis = (int32_t)lvl_q15(par[P_SEIS]);
    eng.n_term = (int32_t)lvl_q15(par[P_THERM]);
    eng.n_quant = (int32_t)lvl_q15(par[P_QUANT]) >> 1;
    eng.n_glitch = (int32_t)lvl_q15(par[P_GLITCH]);
    eng.s_lv = (int32_t)lvl_q15(par[P_SKY]);
    eng.s_dens = (int32_t)(4u + par[P_DENS] * 20u / 100u);
    eng.s_drift = (int32_t)(12u + (uint32_t)par[P_DRIFT] * par[P_DRIFT] * 12u / 100u);
    hz = band_hz(par[P_BAND]);
    eng.s_fc_inc = hz * 97391u;
    eng.i_len8 = (int32_t)(arm_ms10(par[P_ARM]) * 441u * 256u / 100u);
    eng.i_fb = (int32_t)par[P_FIN] * 311;          /* 0 .. 0.95 */
    eng.i_onda = (int32_t)par[P_ONDA] * 170;       /* up to 1.5 ms of stretch, Q8 frames per unit of strain */
    eng.i_mix = (int32_t)par[P_IMIX] * 327;
    eng.r_mix = (int32_t)lvl_q15(par[P_RMIX]);
    {   /* CODA RIV.: 0.3 .. 0.998 (0.4: very long at the top, minutes) */
        int32_t q = 100 - (int32_t)par[P_RDEC];
        eng.r_dec = 32700 - q * q * 229 / 100;
    }
    eng.r_id1 = 19661 + (int32_t)par[P_RDENS] * 72;  /* DENSITA: input diffusion 0.6 .. 0.82 */
    eng.r_id2 = 16384 + (int32_t)par[P_RDENS] * 72;  /*                         0.5 .. 0.72 */
    eng.r_dd = 19661 + (int32_t)par[P_RDENS] * 66;   /* the tank's 0.6 .. 0.8 */
    eng.r_damp = 5000 + (int32_t)par[P_RBRIL] * 270; /* BRILLANZA: the tank's low pass, dark .. open */
    eng.r_mov = (int32_t)par[P_RMOV] * 4096 / 100;   /* MOVIMENTO: up to 16 frames of sway */
    eng.r_pre = (int32_t)par[P_RPRE] * par[P_RPRE] * 2205 / 10000;   /* PRE-DELAY: up to 100 ms */
    eng.radio = (int32_t)par[P_RADIO] * 327;
    eng.dial = (int32_t)par[P_DIAL] * 256;
    eng.selw = 256 + (int32_t)(100u - par[P_SELET]) * 51;   /* SELETTIVITA: a station 1 .. 21 wide */
    eng.stat = (int32_t)lvl_q15(par[P_STAT]);
    {   /* 0.5 SHIMMER: the feed (and the tank's own decay held under it), the step of the pitch shift a frame */
        int32_t g = (int32_t)par[P_SHIM] * 130, d2;    /* up to 0.4 */
        uint32_t sp = par[P_SPIN] > 99u ? 99u : par[P_SPIN];
        eng.sh_amt = g;
        d2 = 32604 - (int32_t)(((int64_t)g * g) >> 15);   /* dec^2 + g^2 <= 0.995: a shimmer that climbs, never runs away */
        if (g && eng.r_dec > 0 && (int32_t)(((int64_t)eng.r_dec * eng.r_dec) >> 15) > d2) {
            int32_t lo = 0, hi = 32767;                 /* sqrt by halving */
            while (hi - lo > 1) { int32_t m = (lo + hi) >> 1; if (((m * m) >> 15) > d2) hi = m; else lo = m; }
            eng.r_dec = lo;
        }
        switch (par[P_SHTYPE] % 5u) {
        case 0: eng.sh_dq = 65536; break;                 /* OTTAVA */
        case 1: eng.sh_dq = 32768; break;                 /* QUINTA */
        case 2: eng.sh_dq = (int32_t)(((uint32_t)QNM_R[sp * 4u + 1u] << 16) / QNM_R[sp * 4u]) - 65536; break;   /* KERR: (3,3) over (2,2) */
        case 3: eng.sh_dq = -32768; break;                /* REDSHIFT: an octave down */
        default: eng.sh_dq = 0x7FFFFFFF; break;           /* SPIRALE: a semitone more each turn (the ISR steps it) */
        }
    }
    {   /* AUDIO: COMP/LIMIT, a compressor before the limiter (threshold down to -30 dB, ratio up to 8, half the loss back) */
        int32_t a = par[P_COMP], k = a * 2294 / (100 + a * 7);   /* (1 - 1/ratio), Q15: ratio 1 + 0.07 a */
        eng.c_thr = (15 << 16) - a * 3266;                /* log2 of the threshold (32768 = 15) */
        eng.c_k = k;
        eng.c_mk = (int32_t)(((int64_t)(a * 3266) * k) >> 15) * 5 / 10;   /* make-up, log2 Q16 */
    }
    {   /* STEREO: 0 mono .. 25 as it is .. 40 wide; past 40 the autopan, 0.05 .. 8 Hz */
        uint32_t v = par[P_STEREO];
        eng.st_w = v <= 40u ? (int32_t)(v * 32768u / 25u) : 32768;
        eng.ap_inc = v > 40u ? (uint32_t)(((uint64_t)4870u * exp2_q16((int32_t)(v - 41u) * 8120)) >> 16) : 0u;   /* 0.05 Hz x 2^(0..7.3) */
    }
    eng.vol = (int32_t)((lvl_q15(par[P_VOL]) + 3000u) * 23u / 32u);   /* 0.4.1: 3 dB less at the output: room for chords */
    eng.cs_lv = par[P_SND] && par[P_USE] == 2u ? (int32_t)lvl_q15(par[P_SLV]) : 0;
    eng.cs_step = hz * 65536u / 523u;   /* the sound's C4 at the band's centre, an octave down */
    eng.ms[0] = par[P_MS1]; eng.md[0] = par[P_MD1]; eng.ma[0] = ((int32_t)par[P_MA1] - 100) * 327;
    eng.ms[1] = par[P_MS2]; eng.md[1] = par[P_MD2]; eng.ma[1] = ((int32_t)par[P_MA2] - 100) * 327;
    eng.lfo_inc = (uint32_t)(((uint64_t)62344u * exp2_q16((int32_t)par[P_LRATE] * 6554)) >> 16);   /* 0.02 .. 20 Hz */
    eng.lfo_shape = par[P_LSHAPE];
    eng.c_mix = (int32_t)par[P_CORO] * 230;        /* CORO: 0 .. 0.7 of the ensemble */
    eng.e_mix = (int32_t)(lvl_q15(par[P_ECO]) * 7u / 8u);
    eng.e_fb = (int32_t)par[P_EFB] * 280;          /* RITORNO: 0 .. 0.85 */
    {   /* the echo's time: the division of the tempo, at 11025 Hz, Q8 (at most 0.74 s) */
        uint32_t bpm = clk_ext && ext_bpm10 >= 300u ? ext_bpm10 / 10u : (uint32_t)par[P_BPM] + 40u;
        uint32_t d8 = 661500u * ETQ[par[P_ETIME] % 6u] / bpm;
        eng.e_d8 = (int32_t)(d8 > ((EC_N / 2u - 4u) << 8) ? (EC_N / 2u - 4u) << 8 : d8);
    }
}

/* ---- presets: 48 records of 256 B at USER_AT (3 sectors), the settings in the fourth (own CRC) */
#define NPRESET 48u
#define NFACT 14u
#define PREC_MAGIC 0x56u                            /* 'V' (never 'A': the update loader looks for "AT" records) */
#define PREC_VER 3u                                 /* 0.4: 80 parameters in 96 (0.2 and 0.3's, 60 or 64 in 64, and 0.1's 36 in 48, converted) */
#define NP2 60u                                     /* the parameters of 0.2's presets */
#define PREC_NP 96u
#define NP3 64u                                     /* 0.3's */
#define NP4 80u                                     /* 0.4's (same record, fewer parameters) */
#define PREC_SIZE 256u
typedef struct { uint8_t magic, ver, np, used; char name[12]; uint8_t p[PREC_NP]; uint32_t sum; } prec_t;
static prec_t pr[NPRESET];
static uint32_t cur_preset, edited, save_pending, save_sectors, t_save, t_cur, t_pend, set_dirty, t_set;
static int32_t pend_preset = -1;
static const uint8_t DEF2[NP - NP1] = {1, 0, 45, 25,  1, 1, 0, 0,  0, 40, 45, 25,  1, 62, 35, 40,  2, 2, 150, 30,  4, 1, 170, 0,
                                       30, 0, 3, 35,  50, 50, 0, 0,  75, 50, 35, 10,  0, 7, 40, 0,  0, 50, 50, 50,
                                       0, 0, 20, 25};
static const struct { char name[12]; uint8_t p[NP]; } FACT[NFACT] = {
    /* M1 M2 SPIN DIST  DUR PART HARM PREC  MODI SMORZ ACC CODA  ARM FIN ONDA IMIX  SEIS TERM QUANT GLITCH
     * DENS DRIFT BAND SKY  RATE MMAX DMAX TEMPO  SND USE SLV SSTART  RMIX RDEC BPM VOL
     * ENG ALG IDX DRIVE  IVL SCALE TONIC CQ  HIER GRAV TONFO SUB  FTYPE FREQ RES FDIST  MS1 MD1 MA1 LRATE  MS2 MD2 MA2 LSHAPE  CORO ECO ETIME EFB
     * FINALE PICCO SOSP APPR  RDENS RBRIL RMOV RPRE  DEJA DLEN DSPR DSU  RADIO DIAL SELET STAT  SHIM SHTYPE COMP STEREO */
    {"GW150914", {78, 74, 69, 42,  50, 55, 45, 20,  3, 55, 0, 45,  45, 40, 50, 25,  15, 10, 12, 8,  50, 30, 45, 15,  0, 60, 50, 0,  0, 0, 60, 0,  35, 60, 80, 80,
                  1, 0, 45, 25,  1, 1, 0, 0,  0, 40, 45, 25,  1, 40, 35, 40,  2, 2, 150, 30,  4, 1, 170, 0,  30, 0, 3, 35,
                           50, 50, 0, 0,  70, 50, 35, 10,  0, 7, 40, 0,  0, 50, 50, 50,  0, 0, 20, 25}},
    {"BUCHI NERI", {90, 88, 95, 30,  25, 40, 30, 10,  3, 10, 100, 70,  30, 30, 30, 15,  0, 0, 6, 0,  30, 20, 40, 0,  0, 60, 50, 0,  0, 0, 60, 0,  50, 80, 80, 75,
                    1, 4, 60, 10,  1, 1, 0, 0,  0, 40, 70, 40,  2, 70, 50, 30,  4, 1, 180, 30,  5, 2, 130, 0,  35, 15, 4, 40,
                             60, 60, 15, 30,  90, 45, 45, 20,  30, 7, 40, 0,  0, 50, 50, 50,  0, 0, 20, 25}},
    {"NEUTRONI", {8, 6, 20, 25,  85, 85, 30, 30,  0, 80, 0, 10,  40, 50, 60, 30,  10, 10, 20, 5,  40, 30, 50, 20,  0, 30, 40, 0,  0, 0, 60, 0,  30, 55, 80, 80,
                  1, 1, 35, 30,  1, 1, 0, 2,  0, 40, 20, 10,  1, 40, 40, 30,  2, 2, 160, 30,  6, 1, 140, 0,  25, 0, 3, 35,
                           40, 50, 0, 0,  60, 60, 30, 5,  0, 7, 40, 0,  0, 50, 50, 50,  0, 0, 20, 25}},
    {"CIELO LISA", {60, 50, 60, 70,  70, 60, 40, 50,  2, 40, 50, 60,  60, 70, 70, 40,  5, 15, 10, 0,  90, 60, 35, 70,  15, 70, 80, 0,  0, 0, 60, 0,  60, 85, 80, 75,
                    0, 0, 30, 10,  1, 9, 9, 0,  1, 50, 20, 20,  1, 40, 30, 50,  7, 2, 140, 20,  5, 5, 140, 1,  45, 25, 5, 50,
                             70, 45, 30, 50,  95, 40, 60, 30,  60, 11, 50, 0,  0, 50, 50, 50,  40, 2, 25, 70}},
    {"RIVELATORE", {70, 60, 70, 55,  45, 55, 50, 25,  3, 60, 0, 45,  50, 60, 80, 60,  45, 35, 40, 35,  30, 30, 50, 10,  0, 60, 50, 0,  0, 0, 60, 0,  25, 50, 80, 80,
                    0, 0, 40, 20,  0, 1, 0, 0,  0, 40, 35, 15,  1, 40, 60, 80,  1, 8, 170, 30,  4, 3, 160, 0,  20, 0, 3, 35,
                             50, 50, 0, 0,  70, 50, 35, 10,  0, 7, 40, 0,  70, 40, 55, 60,  0, 0, 20, 25}},
    {"CATALOGO O4", {75, 70, 70, 50,  40, 50, 50, 25,  3, 50, 20, 45,  45, 45, 50, 25,  15, 10, 12, 10,  50, 30, 45, 20,  45, 70, 70, 0,  0, 0, 60, 0,  40, 65, 90, 80,
                     1, 0, 45, 25,  1, 1, 0, 1,  3, 35, 45, 25,  1, 40, 35, 40,  2, 2, 150, 30,  4, 1, 170, 0,  30, 0, 3, 35,
                              50, 50, 0, 0,  75, 50, 35, 10,  50, 7, 40, 0,  0, 50, 50, 50,  0, 0, 20, 25}},
    {"ASIMMETRICA", {85, 30, 40, 40,  55, 60, 100, 70,  3, 45, 0, 45,  35, 35, 40, 20,  10, 8, 10, 6,  40, 30, 45, 10,  0, 60, 50, 0,  0, 0, 60, 0,  35, 60, 80, 80,
                     1, 2, 55, 40,  1, 1, 0, 1,  0, 40, 50, 30,  2, 65, 45, 30,  2, 2, 160, 30,  4, 4, 160, 0,  30, 0, 3, 35,
                              50, 60, 0, 10,  75, 50, 35, 10,  0, 7, 40, 0,  0, 50, 50, 50,  0, 0, 20, 25}},
    {"BRACCI 40MS", {72, 68, 75, 45,  50, 55, 45, 30,  3, 35, 30, 40,  85, 85, 90, 75,  10, 10, 10, 8,  45, 30, 45, 15,  0, 60, 50, 0,  0, 0, 60, 0,  30, 70, 80, 75,
                     1, 3, 40, 20,  1, 1, 0, 0,  0, 40, 40, 20,  1, 40, 35, 40,  1, 8, 180, 30,  6, 2, 140, 0,  25, 0, 3, 35,
                              50, 50, 0, 0,  70, 55, 40, 10,  0, 7, 40, 0,  0, 50, 50, 50,  0, 0, 20, 25}},
    {"TONNETZ", {70, 55, 60, 35,  35, 45, 45, 20,  3, 45, 60, 45,  45, 40, 50, 20,  8, 8, 8, 4,  40, 30, 45, 10,  35, 70, 60, 3,  0, 0, 60, 0,  40, 70, 90, 80,
                 1, 0, 40, 20,  1, 2, 0, 1,  3, 45, 40, 25,  2, 70, 40, 30,  2, 2, 150, 30,  4, 1, 170, 0,  35, 20, 3, 40,
                          50, 50, 0, 0,  75, 50, 35, 10,  65, 7, 45, 0,  0, 50, 50, 50,  15, 2, 20, 25}},
    {"ORBITE 2:3", {40, 25, 55, 30,  65, 70, 60, 30,  3, 55, 40, 30,  40, 45, 50, 25,  10, 8, 8, 6,  40, 30, 45, 10,  60, 50, 50, 5,  0, 0, 60, 0,  35, 60, 100, 80,
                    1, 2, 50, 30,  1, 9, 4, 2,  1, 50, 55, 35,  1, 40, 40, 30,  6, 2, 150, 40,  8, 6, 115, 3,  30, 25, 2, 45,
                             50, 50, 0, 0,  70, 55, 35, 5,  75, 5, 35, 0,  0, 50, 50, 50,  0, 0, 20, 25}},
    /* 0.3: the engines */
    {"NEBULOSA", {70, 60, 70, 35,  30, 70, 40, 30,  3, 70, 80, 60,  45, 40, 40, 15,  0, 0, 4, 0,  40, 30, 45, 10,  0, 60, 50, 0,  0, 0, 60, 0,  40, 75, 80, 80,
                  2, 0, 70, 25,  1, 2, 0, 0,  0, 40, 30, 20,  2, 62, 30, 35,  3, 2, 140, 25,  9, 2, 140, 0,  55, 20, 3, 40,
                           55, 50, 10, 25,  90, 50, 50, 15,  0, 7, 40, 1,  0, 50, 50, 50,  20, 1, 25, 40}},
    {"MAREA", {60, 45, 55, 30,  25, 55, 50, 25,  3, 45, 60, 35,  45, 40, 40, 15,  6, 6, 6, 3,  40, 30, 45, 10,  0, 60, 50, 0,  0, 0, 60, 0,  30, 60, 80, 80,
               3, 0, 65, 20,  1, 3, 9, 1,  0, 40, 55, 30,  0, 60, 30, 30,  4, 1, 170, 20,  7, 1, 130, 0,  25, 25, 4, 40,
                        45, 60, 0, 15,  80, 55, 40, 10,  0, 7, 40, 0,  0, 50, 50, 50,  0, 0, 20, 25}},
    {"CORDA NERA", {50, 40, 60, 25,  20, 60, 40, 20,  3, 60, 80, 55,  45, 40, 40, 15,  0, 0, 4, 0,  40, 30, 45, 10,  25, 60, 50, 3,  0, 0, 60, 0,  40, 70, 90, 80,
                    4, 0, 60, 15,  1, 9, 4, 0,  3, 45, 35, 20,  0, 60, 30, 30,  7, 5, 140, 30,  0, 0, 100, 0,  30, 30, 3, 45,
                             50, 50, 0, 0,  85, 45, 45, 15,  55, 7, 50, 0,  0, 50, 50, 50,  15, 3, 30, 30}},
    {"CORO VIRGO", {65, 55, 50, 30,  30, 60, 40, 30,  3, 65, 80, 65,  45, 40, 40, 15,  0, 0, 4, 0,  40, 30, 45, 10,  0, 60, 50, 0,  0, 0, 60, 0,  50, 80, 75, 80,
                    5, 0, 30, 15,  1, 3, 2, 1,  1, 45, 20, 15,  0, 60, 30, 30,  6, 5, 130, 35,  0, 0, 100, 0,  45, 15, 4, 35,
                             65, 50, 20, 45,  95, 40, 55, 25,  0, 7, 40, 0,  0, 50, 50, 50,  30, 0, 25, 60}},
};
typedef struct { uint8_t st, num, par, pad; } learn_t;
#define NLEARN 32u
#define SET_MAGIC 0x56495247u                       /* "VIRG" */
#define SET_VER 1u
static struct {
    uint8_t ver, cur, clk_in, clk_out, nlearn, rx, run_on, oct;
    learn_t lr[NLEARN];
    uint8_t bt_auto, bt_mode, bt_dev_ok, bt_dev_rand;
    uint8_t bt_dev_addr[6], bt_pad[2];
    char bt_dev_name[20];
    uint8_t bt_kept[112];
    uint8_t bt_bonded, bt_ediv[2], bt_pad2;
    uint8_t bt_ltk[16], bt_rand[8];
} store;
static uint32_t prec_sum(const prec_t *r)
{
    const uint8_t *b = (const uint8_t *)r;
    uint32_t s = 0x9E37u, k;
    for (k = 0; k < (uint32_t)((const uint8_t *)&r->sum - b); k++) s = s * 31u + b[k];
    return s;
}
static void prec_factory(uint32_t i)
{
    prec_t *r = &pr[i];
    uint32_t k;
    for (k = 0; k < sizeof *r; k++) ((uint8_t *)r)[k] = 0;
    if (i >= NFACT) return;
    for (k = 0; k < 12u; k++) r->name[k] = FACT[i].name[k];
    r->name[11] = 0;
    for (k = 0; k < NP; k++) r->p[k] = FACT[i].p[k];
    r->used = 1;
}
static int prec_from_01(uint32_t i)                  /* a record of 0.1 (36 parameters in 48, its sum at 64): to this one */
{
    uint8_t b[68];
    uint32_t k, s0 = 0x9E37u, sum;
    prec_t *r = &pr[i];
    if (user_read(i * PREC_SIZE, b, sizeof b) || b[0] != PREC_MAGIC || b[1] != 1u || b[2] != NP1 || !b[3]) return 0;
    for (k = 0; k < 64u; k++) s0 = s0 * 31u + b[k];
    sum = b[64] | (uint32_t)b[65] << 8 | (uint32_t)b[66] << 16 | (uint32_t)b[67] << 24;
    if (sum != s0) return 0;
    for (k = 0; k < sizeof *r; k++) ((uint8_t *)r)[k] = 0;
    for (k = 0; k < 12u; k++) r->name[k] = (char)b[4 + k];
    r->name[11] = 0;
    for (k = 0; k < NP1; k++) r->p[k] = b[16 + k] > PMAX[k] ? PMAX[k] : b[16 + k];
    for (k = NP1; k < NP; k++) r->p[k] = DEF2[k - NP1];
    r->used = 1;
    return 1;
}
static int prec_from_02(uint32_t i)                  /* a record of 0.2 or 0.3 (60 or 64 parameters in 64, its sum at 80): to this one */
{
    uint8_t b[84];
    uint32_t k, s0 = 0x9E37u, sum, np;
    prec_t *r = &pr[i];
    if (user_read(i * PREC_SIZE, b, sizeof b) || b[0] != PREC_MAGIC || b[1] != 2u || (b[2] != NP2 && b[2] != NP3) || !b[3]) return 0;
    for (k = 0; k < 80u; k++) s0 = s0 * 31u + b[k];
    sum = b[80] | (uint32_t)b[81] << 8 | (uint32_t)b[82] << 16 | (uint32_t)b[83] << 24;
    if (sum != s0) return 0;
    np = b[2];
    for (k = 0; k < sizeof *r; k++) ((uint8_t *)r)[k] = 0;
    for (k = 0; k < 12u; k++) r->name[k] = (char)b[4 + k];
    r->name[11] = 0;
    for (k = 0; k < np; k++) r->p[k] = b[16 + k] > PMAX[k] ? PMAX[k] : b[16 + k];
    for (k = np; k < NP; k++) r->p[k] = DEF2[k - NP1];
    r->used = 1;
    return 1;
}
#define SET_OFF 0x3000u
static int vset_load(void)                          /* the settings, from the fourth sector: [magic][len][crc] then the struct */
{
    uint32_t h[3];
    if (user_read(SET_OFF, h, sizeof h) || h[0] != SET_MAGIC || h[1] != sizeof store) return -1;
    if (user_read(SET_OFF + 12u, &store, sizeof store)) return -1;
    if (bank_crc((const uint8_t *)&store, sizeof store) != h[2]) return -1;
    return 0;
}
static void vset_save(void)
{
    uint32_t h[3];
    h[0] = SET_MAGIC; h[1] = sizeof store; h[2] = bank_crc((const uint8_t *)&store, sizeof store);
    if (user_erase(SET_OFF)) return;
    user_write(SET_OFF, h, sizeof h);
    user_write(SET_OFF + 12u, &store, sizeof store);
}
static void settings_touch(void) { set_dirty = 1; t_set = fm1_ms; }
static void presets_load(void)
{
    uint32_t i, k;
    for (i = 0; i < NPRESET; i++) {
        prec_t *r = &pr[i];
        if (user_read(i * PREC_SIZE, r, sizeof *r) || r->magic != PREC_MAGIC || r->ver != PREC_VER || (r->np != NP && r->np != NP4) ||
            r->sum != prec_sum(r) || !r->used) {
            if (prec_from_02(i) || prec_from_01(i)) { save_sectors |= 1u << (i / 16u); continue; }
            prec_factory(i); continue;
        }
        r->name[11] = 0;
        if (r->np == NP4) { for (k = NP4; k < NP; k++) r->p[k] = DEF2[k - NP1]; r->np = NP; }   /* 0.4's: SHIMMER off, AUDIO as it comes */
        for (k = 0; k < NP; k++) if (r->p[k] > PMAX[k]) r->p[k] = PMAX[k];
    }
    if (vset_load() || store.ver != SET_VER) {
        for (k = 0; k < sizeof store; k++) ((uint8_t *)&store)[k] = 0;
        store.ver = SET_VER;
        store.oct = 3;
    }
    if (store.nlearn > NLEARN) store.nlearn = 0;
    if (store.rx > 17u) store.rx = 0;
    if (store.oct > 6u) store.oct = 3;
    octave_i = store.oct;
    cur_preset = store.cur % NPRESET;
    if (!pr[cur_preset].used) cur_preset = 0;
}
static void presets_flush(void)
{
    uint32_t sct, j;
    for (sct = 0; sct < NPRESET / 16u; sct++) {
        if (!(save_sectors >> sct & 1u)) continue;
        if (user_erase(sct * 4096u)) continue;
        for (j = sct * 16u; j < sct * 16u + 16u; j++) {
            prec_t *r = &pr[j];
            if (!r->used) continue;
            r->magic = PREC_MAGIC; r->ver = PREC_VER; r->np = NP;
            r->sum = prec_sum(r);
            user_write(j * PREC_SIZE, r, sizeof *r);
        }
    }
    save_sectors = 0;
}
static const char *preset_name(uint32_t i) { return pr[i].used ? (pr[i].name[0] ? pr[i].name : "SENZA NOME") : "VUOTO"; }
static void samp_sync(void)
{
    static uint32_t want_was, t_want;
    uint32_t want = par[P_SND] ? (uint32_t)(par[P_SND] - 1u) | (uint32_t)par[P_SSTART] << 8 : 0xFFFFu;
    if (want == smp_loaded) return;
    if (want != want_was) { want_was = want; t_want = fm1_ms; }
    if (fm1_ms - t_want < 200u && smp_loaded != 0xFFFFu) return;   /* 0.4.1: while SUONO or INIZIO turn, wait for them to rest (a sound is decoded whole) */
    if (!par[P_SND] || par[P_SND] > nsnd) { smp_len = 0; smp_loaded = 0xFFFFu; smp_full = 0; return; }
    smp_load(par[P_SND] - 1u, par[P_SSTART]);
}
static void preset_recall(uint32_t i)
{
    uint32_t k;
    cur_preset = i % NPRESET;
    t_cur = fm1_ms;
    if (!pr[cur_preset].used) { edited = 1; return; }
    for (k = 0; k < NP; k++) par[k] = pr[cur_preset].p[k];
    if (par[P_SND] > nsnd) par[P_SND] = 0;
    edited = 0;
    engine_update();
    dirty_hdr = 1; dirty_cells = 0xF;
}

/* ---- RUN: events at random, like a catalogue being written; in time (TEMPO) with BPM or the MIDI clock */
static uint32_t run_on, run_next, run_grid_t, run_seen_ticks;
static volatile uint32_t ext_ticks;                 /* the MIDI clock's ticks (24 a quarter) */
static void run_event(void)
{
    uint32_t mmax = 100u + (uint32_t)par[P_MMAX] * 14u, dmax = 1000u + (uint32_t)par[P_DMAX] * 490u;   /* tenths; Mpc x 10 */
    const uint16_t *c = dv_next();
    uint32_t m1 = 30u + ((uint32_t)c[DV_M1] * (mmax - 30u) >> 16), m2 = 12u + ((uint32_t)c[DV_M2] * (m1 - 12u) >> 16);
    uint32_t u = (uint32_t)c[DV_DIST] + 1u, d, n;
    d = (uint32_t)(((uint64_t)dmax * exp2_q16(log2_q16(u) / 3 - (16 << 16) / 3)) >> 16);
    d /= 10u;   /* in the volume: u^(1/3) */
    if (d < 10u) d = 10u;
    if (par[P_HIER] & 1u) {                         /* FUSIONI RUN: the remnant goes on, a tone of the chord */
        if (hier_m10) m1 = hier_m10;
        if (m2 > m1) m2 = m1;
        n = tz_tone(43u + c[DV_NOTE] % 18u);
    } else {
        n = last_notes[c[DV_NOTE] % (nlast ? nlast : 4u)];
        if (par[P_DEJA]) n = (uint32_t)((int32_t)n + dv_spread((uint16_t)(c[DV_NOTE] * 40503u), 12));   /* DISPERSIONE: up to an octave around */
        n = (uint32_t)snap_note((int32_t)n, scale_mask(par[P_SPIN]), par[P_TONIC]);
    }
    ev_start(n, 100u, m1, m2, 30u + c[DV_SPIN] % 61u, d, 0);
    if (par[P_HIER] & 1u) hier_merged(m1, m2);
}
static uint32_t run_tick, run_tacc, run_last_ms;
static void run_step(void)
{
    uint32_t rate = par[P_RATE], tm = par[P_RTEMPO], now = fm1_ms, dt = now - run_last_ms, nt = 0, k;
    static const uint8_t TK[6] = {0, 96, 48, 24, 12, 0};   /* ticks of the grid, 24 a quarter */
    run_last_ms = now;
    if (!run_on || !rate) return;
    if (!tm) {                                       /* LIBERO: a Poisson process, rate a minute */
        if ((int32_t)(now - run_next) >= 0) {
            uint32_t mean = 6000000u / (rate * 30u), u = dv_hit() + 1u;
            int32_t l = log2_q16(65536u) - log2_q16(u);   /* -log2 u, Q16 */
            run_event();
            run_next = now + 50u + (uint32_t)(((uint64_t)mean * (uint32_t)l * 45426u) >> 32);   /* x ln 2 */
        }
        return;
    }
    if (clk_ext) { nt = ext_ticks - run_seen_ticks; run_seen_ticks = ext_ticks; }
    else {                                           /* the internal clock: ticks from BPM */
        if (dt > 200u) dt = 200u;
        run_tacc += dt * ((uint32_t)par[P_BPM] + 40u) * 24u;
        while (run_tacc >= 60000u) { run_tacc -= 60000u; nt++; }
    }
    if (nt > 8u) nt = 8u;
    for (k = 0; k < nt; k++) {
        uint32_t t = ++run_tick, hit, g;
        if (tm == 5u) { hit = !(t % 24u) || !(t % 16u); g = 12u; }   /* ORBITE 2:3: two orbits in resonance, two against three */
        else { g = TK[tm]; hit = !(t % g); }
        if (hit) {                                   /* at each step: an event with the chance that keeps the rate */
            uint32_t bpm = clk_ext ? (ext_bpm10 / 10u ? ext_bpm10 / 10u : 120u) : (uint32_t)par[P_BPM] + 40u;
            uint32_t step_ms = 60000u * g / 24u / bpm, p = rate * 30u * step_ms / 6000u;   /* per mille */
            if (p > 1000u || (dv_hit() % 1000u) < p) run_event();
        }
    }
}

/* ---- the catalogue (ALGORITHM) */
static uint32_t cat_i;
static void cat_play(uint32_t i)
{
    uint32_t n = last_notes[0];
    cat_i = i % NCAT;
    par[P_M1] = 0; par[P_M2] = 0;
    {   /* the knobs nearest the catalogue's masses and distance */
        uint32_t v;
        for (v = 0; v < 100u && mass10(v) < CAT[cat_i].m1; v++) continue;
        par[P_M1] = (uint8_t)v;
        for (v = 0; v < 100u && mass10(v) < CAT[cat_i].m2; v++) continue;
        par[P_M2] = (uint8_t)v;
        for (v = 0; v < 100u && dist_mpc(v) < CAT[cat_i].mpc; v++) continue;
        par[P_DIST] = (uint8_t)v;
        par[P_SPIN] = CAT[cat_i].spin > 99u ? 99u : CAT[cat_i].spin;
    }
    edited = 1;
    engine_update();
    ev_start(n, 110u, CAT[cat_i].m1, CAT[cat_i].m2, CAT[cat_i].spin, CAT[cat_i].mpc, CAT[cat_i].name);
    learn_say("CATALOGO: ", CAT[cat_i].name);
    if (page == PG_SORG) dirty_cells = 0xF;
}

#include "virgo_ui.c"
#include "virgo_panel.c"
