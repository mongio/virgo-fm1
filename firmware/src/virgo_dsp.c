/* SPDX-License-Identifier: GPL-3.0-only */
/* VIRGO's sound: four events at a time, each a binary's last moments. Included by virgo.c.
 *
 *   INSPIRAL  the orbital phase phi runs at half the wave's frequency; the wave is the sum of the modes m = 1..4 at
 *             m phi (the (2,2) mode the loudest, (3,3) (4,4) (2,1) stronger as the masses differ and near the merger,
 *             as the post-Newtonian amplitudes go). The frequency climbs as f = fM (1 + s K)^(-3/8), s the time left
 *             (1 .. 0), K = (fM / f0)^(8/3) - 1; the amplitude as f^(2/3). Two polarisations: h+ to both sides, hx (a
 *             quarter turn on) between them, the inclination swinging with PRECESSIONE.
 *   MERGER    at s = 0 the ringdown takes over, phase for phase.
 *   RINGDOWN  the remnant's quasi-normal modes (2,2,0), (3,3,0), (2,2,1), (4,4,0): complex one-pole resonators, their
 *             frequencies and Q from the spin (Berti, Cardoso, Will 2006; tables in virgo_tables.h), Q stretched by
 *             SMORZAMENTO, the ratios pulled to a chord by ACCORDO; then a quiet CODA while the key is held.
 *   CAMPIONE  a sound of the user bank: MATERIA (it strikes the resonators at the merger), PORTANTE (it is the (2,2)
 *             wave, its pitch following the chirp), CIELO (a slow loop under the sky).
 *   MOTORE    0.2: ONDA (the modes as they are), FM (the modes as operators). 0.3, the engines of a synthesizer that the
 *             coalescence plays: NEBULOSA (five band-limited saws, their spread closing into one at the merger and opening
 *             again as the ring dies), PIEGA (a sine folded as a West Coast folder folds it, the folds growing as v^2 and
 *             dying with the ring like a low pass gate), CORDA (a Karplus-Strong string tuned to the key: the chirp's
 *             saw sings in it, the merger plucks it, the remnant's modes bow it), VOCE (two saws through three formants:
 *             the vowel opens u-o-a toward the merger and closes after). After the merger the engines glide to the key
 *             and follow the ringdown's envelope; the remnant's modes stay under them, softer.
 *   then the detector's noise (RUMORE), the sky of far binaries (CIELO), the interferometer (two arms whose lengths
 *   the waves stretch, Fabry-Perot cavities, the dark port), SPAZIO (an ensemble chorus; a ping-pong echo at 11025 Hz
 *   in tempo), a plate reverb (Dattorro, at 22050 Hz), a soft clip.
 * Integers only: phases 2^32 a turn, levels Q15, the resonators' coefficients Q30. */

/* ---- fixed point */
VAI static inline int32_t log2_q16(uint32_t x)                 /* log2 x, Q16; x > 0 */
{
    int32_t n = 31;
    uint32_t m, i, f;
    if (!x) return -(32 << 16);
    while (!(x & 0x80000000u)) { x <<= 1; n--; }
    m = x;
    i = (m >> 23) & 255u;
    f = (m >> 15) & 255u;
    return (n << 16) + (int32_t)(LOG2_T[i] + (((LOG2_T[i + 1u] - LOG2_T[i]) * f) >> 8));
}
VAI static inline uint32_t exp2_q16(int32_t y)                 /* 2^(y / 65536), Q16; y in -16 .. +15 octaves */
{
    int32_t i = y >> 16;
    uint32_t f = (uint32_t)y & 0xFFFFu, k = f >> 8, r = f & 255u, m;
    m = EXP2_T[k] + (((EXP2_T[k + 1u] - EXP2_T[k]) * r) >> 8);
    if (i >= 15) return 0xFFFFFFFFu;
    if (i >= 0) return m << i;
    if (i <= -31) return 0;
    return m >> (-i);
}
VAI static inline int32_t sin_q15(uint32_t ph)
{
    uint32_t i = ph >> 22, f = (ph >> 6) & 0xFFFFu;
    int32_t a = SIN_T[i], b = SIN_T[i + 1u];
    return a + (((b - a) * (int32_t)f) >> 16);
}
VAI static inline int32_t cos_q15(uint32_t ph) { return sin_q15(ph + 0x40000000u); }
VAI static inline int32_t sat15(int32_t x) { return x > 32767 ? 32767 : x < -32767 ? -32767 : x; }
VAI static inline int32_t mul15(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 15); }
#define INC_A4 42852281u                            /* 440 Hz: 440 * 2^32 / 44100 */
VAI static inline uint32_t inc_of(int32_t note_q16)            /* a note (MIDI, Q16 = fractions of a semitone) -> phase step */
{
    int32_t oct = (note_q16 - (69 << 16)) / 12;     /* octaves from A4, Q16 */
    return (uint32_t)(((uint64_t)INC_A4 * exp2_q16(oct)) >> 16);
}
VAI static inline uint32_t hz16_of_inc(uint32_t inc) { return (uint32_t)(((uint64_t)inc * 44100u * 16u) >> 32); }   /* sixteenths of a Hz */

static uint32_t rnd = 0x12345678u, rnd_main = 0x9E3779B9u;
VAI static inline uint32_t xrand(void) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return rnd; }
static uint32_t xrand_main(void) { rnd_main ^= rnd_main << 13; rnd_main ^= rnd_main >> 17; rnd_main ^= rnd_main << 5; return rnd_main; }

/* ---- the sound in RAM (CAMPIONE): 22050 Hz, 16 bit */
#define SMP_MAX 72000u                              /* 3.3 s */
static int16_t smp[SMP_MAX + 2u] POOL;
static volatile uint32_t smp_len;                   /* frames, 0 = none */

/* ---- an event */
#define NV 4u
#define NM 4u                                       /* ringdown modes */
#define BLK 32u                                     /* the control block, frames */
enum { ST_OFF, ST_INSP, ST_RING, ST_CODA, ST_REL, ST_SWELL };   /* 0.6 ST_SWELL: a reversed event's ring, growing */
#define NDEG 13u                                    /* 0.6: the degrees of the physics' scales at most */
enum { MS_OFF, MS_STRAIN, MS_FREQ, MS_ENV, MS_MERGE, MS_PREC, MS_ORBIT, MS_LFO, MS_RAND, MS_VEL };
enum { MD_OFF, MD_IDX, MD_FILT, MD_RES, MD_SAT, MD_PAN, MD_PITCH, MD_AMP, MD_ONDA, MD_IMIX, MD_SKY, MD_N };
typedef struct {                                    /* what the main loop works out for an event (ev_start) */
    uint32_t inc_m;                                 /* the (2,2) wave's step at the merger */
    uint32_t k_q8;                                  /* K = (fM / f0)^(8/3) - 1, Q8 */
    uint32_t tc;                                    /* the inspiral, frames */
    uint32_t rtc;                                   /* 2^32 / tc */
    int32_t delta, eta;                             /* (m1 - m2) / M, m1 m2 / M^2, Q15 */
    int32_t amp;                                    /* Q15 */
    int32_t harm;                                   /* ARMONICHE, Q15 of 2 */
    int32_t prec;                                   /* PRECESSIONE, Q15 */
    uint32_t iota0;                                 /* the inclination, a phase */
    int32_t pan_l, pan_r;                           /* the place in the sky, Q15 */
    int32_t cr[NM][5], sr[NM][5];                   /* the resonators: [ring, coda, release, frozen (0.6 CONGELA), growing (INVERSO)], Q30 */
    int32_t mamp[NM];                               /* each mode's share at the merger, Q15 */
    int32_t ing[NM];                                /* MATERIA into each mode: its bandwidth (1 - r), Q15, so a long one rings no louder */
    int32_t coda_lv;                                /* the level where the coda takes over, Q15 of the peak */
    uint32_t nm;                                    /* MODI */
    uint32_t ring_inc[NM];                          /* the modes' steps (the screen) */
    uint8_t use, smp_on;                            /* CAMPIONE: 0 MATERIA, 1 PORTANTE, 2 CIELO; a sound there */
    int32_t smp_lv;                                 /* its level, Q15 */
    uint32_t smp_start;                             /* where it starts, frames */
    /* 0.2 */
    uint8_t eng, alg_i, alg_r, cq;                  /* ONDA / FM; the algorithm in the inspiral and in the ring; the chirp's steps */
    int32_t idx;                                    /* FM index: phase per unit of the modulator (Q15 -> 2^32 a turn), at the merger */
    int32_t drive;                                  /* SATURAZIONE: Q8 gain, 256 = clean */
    uint32_t r2_q16;                                /* INTERVALLO: the second voice's ratio (Q16), 0 = none */
    int32_t lv2;                                    /* its level, Q15 */
    uint16_t smask;                                 /* the scale (CHIRP SCALA), bit i = i semitones over the tonic */
    uint8_t tonic;
    int32_t note_m16;                               /* the merger's note, Q16 semitones (CHIRP SCALA) */
    uint32_t inc_h;                                 /* the harmonic grid (CHIRP ARMONICI): fM / 16 */
    uint8_t ftype;                                  /* FILTRO */
    int32_t fmul_l2;                                /* its frequency: log2 of the multiple of the wave's (Q16), the distance in it */
    int32_t fdamp;                                  /* the damping (Q15, 2 = none .. 0.05) */
    int32_t fdamp_ring;                             /* the ringdown's own Q as damping */
    int32_t tonfo, sub;                             /* the thump at the merger, the sub octave: Q15 */
    int32_t rnd, vel;                               /* MOD's CASO and VELOCITY, Q15 bipolar / unipolar */
    int32_t oct16;                                  /* PARTENZA (MOD's FREQUENZA: how far along the chirp) */
    /* 0.3 */
    int32_t e_amt;                                  /* the engine's knob (INDICE, APERTURA, PIEGHE, BRILLANZA, VOCALE), Q15 */
    uint32_t ks_d8;                                 /* CORDA: the loop's delay, Q8 frames (the period less the filter's own) */
    int32_t ks_g[4];                                /* its loss a pass: ringing, held, let go, frozen (Q15) */
    int32_t ks_b;                                   /* its brightness: the loop's one-pole, Q15 */
    /* 0.4 */
    int32_t station;                                /* RADIO: its place on the dial (0 .. 100, Q8) */
    int32_t fin_p;                                  /* FINALE: the exponent of the time left, Q12 (1 the physical law) */
    uint32_t hold;                                  /* SOSPENSIONE: frames at the top before the merger */
    int32_t glk;                                    /* APPRODO: the glide's step a block, Q15 */
    int32_t rad[NM][5];                             /* the resonators' radii (ring, coda, release, frozen, growing), Q30 */
    /* 0.6 */
    uint8_t fus, rev, frz, nd;                      /* FUSIONE 0 by itself, 1 at the key's release, 2 on command; reversed; CONGELA on; degrees */
    uint32_t rush;                                  /* TUFFO: the plunge's frames */
    uint32_t swell;                                 /* INVERSO: the growing ring's frames */
    int32_t deg[NDEG];                              /* the physics' scale (CHIRP SCALA), Q16 semitones over the tonic */
    int32_t sw0[NM];                                /* INVERSO: where each mode starts (Q15 of its share at the peak) */
} ev_t;
typedef struct {
    ev_t e;
    uint8_t stage;
    uint32_t left;                                  /* inspiral frames left */
    uint32_t phi, pph;                              /* the orbital phase, the precession's */
    int32_t a_now, f_l2;                            /* the (2,2) amplitude (Q15), log2 f/fM (Q16) at the last block */
    int32_t re[NM], im[NM];                         /* the resonators (Q23 or so) */
    int32_t peak;                                   /* the ringdown's start, |re| + |im| of mode 0 */
    uint32_t spos;                                  /* the sample's place, Q16 */
    int32_t ci;                                     /* cos(inclination), Q15, at the last block */
    int32_t h33, h44, h21;                          /* the higher modes' amplitudes at the last block */
    uint32_t ostep, xf;                             /* the orbit's step at the merger; the crossfade's frames left */
    uint32_t age;
    /* 0.2 */
    uint32_t phi2;                                  /* the second voice (INTERVALLO), its orbit */
    int32_t ya, yb;                                 /* the FM operators' last outputs (BINARIA's coupling, ANELLO's feedback) */
    int32_t f_lp[2], f_bp[2];                       /* the filter, for h+ and hx */
    uint32_t kph, kinc; int32_t kenv;               /* the thump */
    uint32_t sph;                                   /* the sub, in the ring */
    uint32_t q_last; int32_t art;                   /* CHIRP quantised: the step held, the attack after each step */
    int32_t menv;                                   /* the merger's envelope (MOD FUSIONE) */
    uint32_t orb;                                   /* MOD ORBITA: a slow image of the orbit */
    int32_t strain;                                 /* MOD STRAIN: the last block's h+ (Q15 of the amplitude) */
    int32_t ipk;                                    /* 1 / peak for the ring's FM (Q15 of the modes' level) */
    int32_t md[MD_N];                               /* the modulations now (Q15) */
    /* 0.3: the engines */
    uint32_t einc;                                  /* the engine's step now: the chirp, then a glide to the key */
    uint32_t sw[5];                                 /* the saws (NEBULOSA five, VOCE two), PIEGA's phase */
    int32_t fz[6];                                  /* VOCE's formants (lp, bp) x 3; PIEGA's DC */
    uint32_t vph;                                   /* VOCE's vibrato */
    int32_t eenv;                                   /* the ring's envelope at the last block (absolute, Q15) */
    int16_t *ks;                                    /* CORDA: the string */
    uint32_t ksw, ksx;                              /* its write place; the pluck's frames left */
    int32_t kslp, kexc, ksabs;                      /* the loop's filter; the pluck's colour; the string's level (block) */
    int32_t ol, orr, dcl, dcr;                      /* the last frame out; a stolen voice's tail, fading (no click) */
    int32_t fqp, fdp;                               /* the filter's frequency and damping as they glide (no steps at the merger) */
    uint32_t hl, lastinc;                           /* SOSPENSIONE: frames left at the top; the chirp's last step */
    int32_t gll;                                    /* APPRODO: log2 (pitch now / the ring's), Q16, gliding to 0 */
    int32_t rgain, rgain0;                          /* RADIO: how much of it comes through (Q15), this block and the last */
    int32_t rdet;                                   /* RADIO: the mistuning, octaves Q16 */
    int32_t rsig;                                   /* RADIO: how well tuned (Q15, 1 on the station) */
    int32_t kill;                                   /* 0.4.1: the governor lets it go: its level falling (Q15, 0 = it sounds) */
    uint32_t spd;                                   /* 0.6 TUFFO: the inspiral's speed (Q8, 0 its own) */
    uint32_t sleft;                                 /* INVERSO: the growing ring's frames left */
    int32_t fin;                                    /* INVERSO: the reversed chirp's fade in (Q15) */
} vstate_t;
static vstate_t vs[NV];                             /* the ISR's */
static ev_t ev_pend[NV];                            /* the main loop's: complete when ev_trig[v] is set */
static volatile uint8_t ev_trig[NV], ev_gate[NV];
static volatile uint8_t ev_go[NV];                  /* 0.6 FUSIONE A COMANDO: the main loop says merge now */
static volatile uint8_t hold_on;                    /* HOLD: the codas go on */
/* for the screen (written by the ISR once a block) */
typedef struct { volatile uint8_t stage; volatile uint32_t hz16, phi, prog; volatile int32_t amp, amode[NM]; volatile uint32_t mhz16[NM]; volatile int32_t idx_now, fc_hz; } vis_t;
static vis_t vis[NV];
static volatile int32_t vis_strain;                 /* the waves together, Q15 (the interferometer's arms; the screen) */
static volatile int32_t vis_lvl[2];                 /* the output's level, left and right */
static volatile uint32_t audio_us;
static volatile uint32_t vis_glitch;                /* the last blip: its frequency (low 16 bits), a count above */
static volatile int32_t vis_msrc[2];                /* MOD A and B: the source's value of the last event (the screen) */
static int32_t vis_strain_acc;
static int32_t gmod[3];                             /* the global destinations (ONDA, MIX INTERF., CIELO), Q15, this call */

/* ---- global settings the ISR reads (engine_update writes them) */
static struct {
    volatile int32_t n_seis, n_term, n_quant, n_glitch;   /* RUMORE, Q15 */
    volatile int32_t s_lv, s_dens, s_drift;               /* CIELO: level Q15, oscillators, drift */
    volatile uint32_t s_fc_inc;                           /* the band's centre, a step */
    volatile int32_t i_fb, i_onda, i_mix, i_len8;         /* INTERFEROMETRO: feedback Q15, the waves' reach (Q8 frames), mix Q15, the arm (Q8 frames) */
    volatile int32_t r_mix, r_dec;                        /* the reverb */
    volatile int32_t vol;                                 /* Q15 */
    volatile int32_t cs_lv;                               /* CAMPIONE in the sky, Q15 (USO CIELO) */
    volatile uint32_t cs_step;                            /* its step, Q16 */
    volatile uint8_t ms[2], md[2];                        /* MOD A and B: source, destination */
    volatile int32_t ma[2];                               /* their amounts, Q15 bipolar */
    volatile uint32_t lfo_inc;                            /* the LFO: a step a block */
    volatile uint8_t lfo_shape;
    volatile int32_t c_mix, e_mix, e_fb, e_d8;            /* SPAZIO: CORO, ECO, RITORNO (Q15), the echo's time (11025 Hz frames, Q8) */
    volatile int32_t radio, dial, selw, stat;             /* 0.4 RADIO: how much (Q15), the dial (Q8, 0 .. 100), the station's width (Q8), the static (Q15) */
    volatile int32_t r_id1, r_id2, r_dd, r_damp, r_mov, r_pre;   /* RIVERBERO: input diffusion, the tank's, damping (Q15), movement (Q8 frames), pre-delay (frames) */
    volatile int32_t sh_amt, sh_dq;                       /* 0.5 SHIMMER: the feed (Q15), the shift's step a frame (Q16; 0x7FFFFFFF SPIRALE) */
    volatile int32_t c_thr, c_k, c_mk;                    /* COMP/LIMIT: threshold (log2 Q16), 1 - 1/ratio (Q15), make-up (log2 Q16) */
    volatile int32_t st_w;                                /* STEREO: the sides (Q15, 32768 as they are) */
    volatile uint32_t ap_inc;                             /* the autopan's step (0: none) */
} eng;
static uint32_t lfo_ph;
static int32_t lfo_val, lfo_sh;

/* ---- the detector's noise */
static struct { int32_t s1, s2, p1, q1, vre[2], vim[2], gre, gim, gcr, gsr, gt; uint32_t mph; } nz;
static const int32_t VIOL_CR[2] = {1071194605, 1064528539}, VIOL_SR[2] = {69079478, 137872566};   /* 452 and 904 Hz, r 0.9997 (Q30) */

/* ---- the sky: far binaries, slowly rising */
#define NSKY 24u
static struct { uint32_t ph, inc; int32_t oct, amp, tw, pan; } sky[NSKY];
static int32_t sky_bp1, sky_bp2;

/* ---- the interferometer: two arms */
#define ARM_N 2048u
static int16_t arm_x[ARM_N] POOL, arm_y[ARM_N] POOL;
static uint32_t arm_w;
static int32_t arm_lx, arm_ly;

/* ---- the strings (CORDA), the ensemble, the echo */
#define KS_N 2048u                                  /* 21.5 Hz the lowest */
static int16_t ks_buf[NV][KS_N] POOL;
#define CH_N 1024u
static int16_t ch_l[CH_N] POOL, ch_r[CH_N] POOL;
static uint32_t ch_w, ch_ph;
#define EC_N 16384u                                 /* 1.49 s at 11025 Hz: two taps, D and 2D */
static int16_t ec_buf[EC_N] POOL;
static uint32_t ec_w, ec_q;
static int32_t ec_acc, ec_d8 = 256 * 2000, ec_lp, ec_l0, ec_l1, ec_r0, ec_r1;

/* ---- the plate (Dattorro), at 22050 Hz */
#define RV_MEM 21950u                               /* 0.4: a longer pre-delay, two more diffusers in, four more allpasses in the tank */
static int16_t rv_mem[RV_MEM] POOL;
typedef struct { int16_t *b; int32_t n, w; } rline_t;
static rline_t rv_pre, rv_ap[6], rv_lap1, rv_ld1, rv_lap2, rv_ld2, rv_rap1, rv_rd1, rv_rap2, rv_rd2, rv_lap3, rv_lap4, rv_rap3, rv_rap4;
static int32_t rv_lp_l, rv_lp_r, rv_bw, rv_in_prev, rv_out_l, rv_out_r, rv_ol_prev, rv_or_prev;
static uint32_t rv_half, rv_mph;
static void rline(rline_t *l, int16_t **m, int32_t n) { l->b = *m; l->n = n; l->w = 0; *m += n; }
static void reverb_init(void)
{
    int16_t *m = rv_mem;
    static const int16_t AP[6] = {105, 79, 281, 205, 163, 127};
    uint32_t k;
    rline(&rv_pre, &m, 2206);
    for (k = 0; k < 6u; k++) rline(&rv_ap[k], &m, AP[k]);
    rline(&rv_lap1, &m, 522); rline(&rv_ld1, &m, 3300); rline(&rv_lap2, &m, 1334); rline(&rv_ld2, &m, 2757);
    rline(&rv_rap1, &m, 697); rline(&rv_rd1, &m, 3125); rline(&rv_rap2, &m, 1968); rline(&rv_rd2, &m, 2344);
    rline(&rv_lap3, &m, 887); rline(&rv_lap4, &m, 463); rline(&rv_rap3, &m, 1013); rline(&rv_rap4, &m, 347);
}
VFAST static inline int32_t rtap(const rline_t *l, int32_t k) { int32_t i = l->w - 1 - k; while (i < 0) i += l->n; return l->b[i]; }
VFAST static inline void rput(rline_t *l, int32_t x) { l->b[l->w] = (int16_t)sat15(x); if (++l->w >= l->n) l->w = 0; }
VFAST static inline int32_t rdelay(rline_t *l, int32_t x) { int32_t y = l->b[l->w]; rput(l, x); return y; }
VFAST static inline int32_t rallpass(rline_t *l, int32_t x, int32_t g)   /* g Q15 */
{
    int32_t d = l->b[l->w], v = x - ((d * g) >> 15);
    rput(l, v);
    return d + ((v * g) >> 15);
}
VFAST static inline int32_t rallpass_mod(rline_t *l, int32_t x, int32_t g, int32_t d8)   /* the delay moving (Q8 frames): no metal */
{
    int32_t k = d8 >> 8, f = d8 & 255, a = rtap(l, k - 1), c = rtap(l, k), d = a + (((c - a) * f) >> 8), v = x - ((d * g) >> 15);
    rput(l, v);
    return d + ((v * g) >> 15);
}
/* 0.5 SHIMMER: a pitch shift read out of the tank's own left delay (no memory of its own): two heads sweep a 2048-frame
 * window at the shift's rate, half a window apart, each faded in a triangle; what they read goes back into the tank's
 * input, so the tail climbs (or, REDSHIFT, sinks) turn after turn. SPIRALE: a semitone higher each turn of the window,
 * up to the octave, then again from one. */
#define SH_MIN 96
#define SH_W 2048
static int32_t sh_pos = (SH_MIN + 300) << 16, sh_lp, sh_hp;
static uint32_t sh_k = 1;
static const int32_t SH_SPI[13] = {0, 3901, 8034, 12413, 17052, 21967, 27174, 32691, 38537, 44729, 51289, 58240, 65536};   /* 2^(k/12) - 1, Q16 */
VFAST static int32_t shimmer_frame(void)
{
    int32_t dq = eng.sh_dq, pa, pb, ya, yb, wa, wb, x;
    if (dq == 0x7FFFFFFF) dq = SH_SPI[sh_k];
    sh_pos -= dq;                                    /* up: the delay shortens (the heads catch up) */
    if (sh_pos < SH_MIN << 16) { sh_pos += SH_W << 16; if (++sh_k > 12u) sh_k = 1; }
    if (sh_pos >= (SH_MIN + SH_W) << 16) sh_pos -= SH_W << 16;
    pa = sh_pos; pb = pa + (SH_W << 15);
    if (pb >= (SH_MIN + SH_W) << 16) pb -= SH_W << 16;
    {
        int32_t ka = pa >> 16, fa = (pa >> 8) & 255, kb = pb >> 16, fb = (pb >> 8) & 255, a0, a1, b0, b1;
        a0 = rtap(&rv_ld1, ka - 1); a1 = rtap(&rv_ld1, ka); ya = a0 + (((a1 - a0) * fa) >> 8);
        b0 = rtap(&rv_ld1, kb - 1); b1 = rtap(&rv_ld1, kb); yb = b0 + (((b1 - b0) * fb) >> 8);
    }
    wa = (pa - (SH_MIN << 16)) >> 12;                /* where in the window, Q15 (2048 frames = 2^27) */
    if (wa > 32767) wa = 32767;
    wa = wa < 16384 ? wa * 2 : (32767 - wa) * 2;     /* the triangle: 0 at the edges */
    wb = 32767 - wa;
    x = (ya * wa + yb * wb) >> 15;
    sh_lp += ((x - sh_lp) * 18000) >> 15;            /* softer on top, */
    sh_hp += ((sh_lp - sh_hp) * 300) >> 15;          /* no rumble below */
    return sh_lp - sh_hp;
}
VFAST static void reverb_frame(int32_t in, int32_t dec)   /* one frame at 22050 Hz: rv_out_l, rv_out_r */
{
    int32_t x, l, r, k, id1 = eng.r_id1, id2 = eng.r_id2, dd = eng.r_dd, dd2 = dd - 6000, dmp = eng.r_damp, mv = eng.r_mov, pre = eng.r_pre;
    in >>= 2;                                        /* 0.5: 12 dB of room in the tank (a dense, long tail filled it: clipping, clicks) */
    rput(&rv_pre, in);
    x = pre > 0 ? rtap(&rv_pre, pre - 1) : in;
    rv_bw += ((x - rv_bw) * 29491) >> 15;            /* bandwidth 0.9 */
    x = rallpass(&rv_ap[0], rv_bw, id1);
    x = rallpass(&rv_ap[1], x, id1);
    x = rallpass(&rv_ap[4], x, id1);
    x = rallpass(&rv_ap[2], x, id2);
    x = rallpass(&rv_ap[3], x, id2);
    x = rallpass(&rv_ap[5], x, id2);
    if (eng.sh_amt) x += mul15(shimmer_frame(), eng.sh_amt);   /* SHIMMER back into the tank */
    rv_mph += 103000u;                                /* the tank's sway: 0.53 Hz */
    l = x + ((rtap(&rv_rd2, rv_rd2.n - 1) * dec) >> 15);   /* the figure eight */
    r = x + ((rtap(&rv_ld2, rv_ld2.n - 1) * dec) >> 15);
    l = rallpass_mod(&rv_lap1, l, -dd, (498 << 8) + mul15(sin_q15(rv_mph), mv));
    l = rallpass(&rv_lap4, l, dd2);
    l = rdelay(&rv_ld1, l);
    rv_lp_l += ((l - rv_lp_l) * dmp) >> 15;          /* damping */
    l = rallpass(&rv_lap2, (rv_lp_l * dec) >> 15, dd2);
    l = rallpass(&rv_lap3, l, dd2);
    rput(&rv_ld2, l);
    r = rallpass_mod(&rv_rap1, r, -dd, (673 << 8) + mul15(cos_q15(rv_mph), mv));
    r = rallpass(&rv_rap4, r, dd2);
    r = rdelay(&rv_rd1, r);
    rv_lp_r += ((r - rv_lp_r) * dmp) >> 15;
    r = rallpass(&rv_rap2, (rv_lp_r * dec) >> 15, dd2);
    r = rallpass(&rv_rap3, r, dd2);
    rput(&rv_rd2, r);
    k = rtap(&rv_rd1, 197) + rtap(&rv_rd1, 2204) - rtap(&rv_rap2, 1418) + rtap(&rv_rd2, 1479) - rtap(&rv_ld1, 1475) - rtap(&rv_lap2, 139) - rtap(&rv_ld2, 790);
    rv_out_l = (k * 19661) >> 13;                    /* (and the 12 dB back) */
    k = rtap(&rv_ld1, 262) + rtap(&rv_ld1, 2688) - rtap(&rv_lap2, 910) + rtap(&rv_ld2, 1981) - rtap(&rv_rd1, 1564) - rtap(&rv_rap2, 248) - rtap(&rv_rd2, 90);
    rv_out_r = (k * 19661) >> 13;
}

/* ---- the events, a block of BLK frames each (the ISR) */
VAI static inline int32_t snap_note(int32_t k, uint32_t mask, uint32_t tonic)   /* a note (semitones) to the nearest of the scale, a tie: down */
{
    int32_t d, j;
    if ((mask & 0xFFFu) == 0xFFFu || !mask) return k;
    d = ((k - (int32_t)tonic) % 12 + 12) % 12;
    for (j = 0; j <= 6; j++) {
        if (mask >> ((d - j + 12) % 12) & 1u) return k - j;
        if (j && (mask >> ((d + j) % 12) & 1u)) return k + j;
    }
    return k;
}
VFAST static inline uint32_t fm_off(int32_t m, int32_t I) { return (uint32_t)(int32_t)(((int64_t)m * I) >> 13); }   /* a modulator (Q15) x the index: a phase */
VFAST static inline int32_t softclip(int32_t x, int32_t drive)   /* SATURAZIONE: x - 4x^3/27, flat from 1.5 */
{
    int32_t t, t3;
    if (drive <= 256) return x;
    t = (int32_t)(((int64_t)x * drive) >> 8);
    if (t > 49152) return 32767;
    if (t < -49152) return -32767;
    t3 = mul15(mul15(t, t), t);
    return t - t3 * 4 / 27;
}
VFAST static inline int32_t svf(int32_t x, int32_t f, int32_t d, int32_t *lp, int32_t *bp, uint32_t type)
{
    int32_t h;
    *lp += mul15(f, *bp);
    h = x - *lp - mul15(d, *bp);
    *bp += mul15(f, h);
    if (*bp > 4500000) *bp = 4500000;
    if (*bp < -4500000) *bp = -4500000;
    if (*lp > 4500000) *lp = 4500000;
    if (*lp < -4500000) *lp = -4500000;
    switch (type) {
    case 2: return *lp;                             /* BASSO */
    case 4: return h;                               /* ALTO */
    default: return mul15(*bp, d) * 3 >> 1;         /* RIVELATORE, BANDA: the band, its peak at 1.5 (what it leaves out, back) */
    }
}

VAI static inline int32_t deg_snap16(int32_t x16, uint32_t tonic, const int32_t *deg, uint32_t n)   /* 0.6: to the nearest degree */
{
    int32_t rel = x16 - ((int32_t)tonic << 16), oct = rel >= 0 ? rel / (12 << 16) : -((-rel + (12 << 16) - 1) / (12 << 16));
    int32_t r = rel - oct * (12 << 16), best = deg[0], bd = 0x7FFFFFFF, d;
    uint32_t i;
    for (i = 0; i <= n; i++) {
        int32_t g = i < n ? deg[i] : deg[0] + (12 << 16);
        d = r - g; if (d < 0) d = -d;
        if (d < bd) { bd = d; best = g; }
    }
    return ((int32_t)tonic << 16) + oct * (12 << 16) + best;
}
VAI static inline void rot_q30(uint32_t inc, int32_t r, int32_t *c, int32_t *s)   /* r (cos, sin) of a step, Q30, the length made exact */
{
    int64_t c30 = (int64_t)cos_q15(inc) << 15, s30 = (int64_t)sin_q15(inc) << 15, n, k;
    n = (c30 * c30 + s30 * s30) >> 30;
    k = ((int64_t)3 << 30) - n;                    /* Newton: 1 / sqrt(n) ~ (3 - n) / 2 */
    c30 = (c30 * k) >> 31; s30 = (s30 * k) >> 31;
    *c = (int32_t)((c30 * r) >> 30); *s = (int32_t)((s30 * r) >> 30);
}

static int32_t lim_env;                             /* 0.4.1: the limiter's envelope */
static int32_t cp_env, cp_g = 32767, cp_g1 = 32767, cp_d, ap_gl = 32767, ap_gr = 32767;   /* 0.5: the compressor's level and gain; the autopan's gains */
static volatile int32_t cp_red = 32767;             /* (the screen: the compressor's gain) */
static uint32_t ap_ph;
static volatile uint32_t lim_hits;                  /* frames it held down (the screen: SATURA) */
/* ---- 0.4.1: the governor. When a block takes too long, the next ones do less: first the sky and the ensemble, then the
 * detector's noise and the modes under the engines, then one voice, then two (they fade out in 12 ms). It comes back
 * down step by step when there is room again. So the audio never runs over its time, and the panel never freezes. */
static volatile uint32_t gov;                       /* 0 .. 4 */
static uint32_t gov_calm;

/* ---- 0.3: the engines */
VFAST static inline int32_t saw_blep(uint32_t *ph, uint32_t inc)   /* a saw, its step smoothed (polyBLEP): no aliasing to speak of */
{
    uint32_t p = *ph += inc, dv = (inc >> 15) + 1u;
    int32_t y = (int32_t)(p >> 16) - 32768, t;
    if (p < inc) { t = (int32_t)(p / dv); if (t > 32767) t = 32767; y -= 2 * t - ((t * t) >> 15) - 32768; }
    else if (p > ~inc) { t = (int32_t)((0u - p) / dv); if (t > 32767) t = 32767; y -= ((t * t) >> 15) - 2 * t + 32768; }
    return y;
}
static const int16_t NEB_D[5] = {-32767, -14090, 0, 15400, 31785};   /* the saws' places in the spread (not quite even) */
static const int16_t VF[5][3] = {{300, 870, 2240}, {570, 840, 2410}, {730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}};   /* u o a e i */
static const int16_t VD[3] = {4681, 3277, 2731};    /* the formants' 1 / Q: 7, 10, 12 */
/* a block of the engine at step inc, unit level: bp the middle, bx the sides. shape: how far along (1 at the merger) */
VFAST static void eng_render(vstate_t *s, uint32_t eng_k, uint32_t inc, int32_t shape, int32_t amt, uint32_t n, int32_t *bp, int32_t *bx)
{
    uint32_t i, k;
    if (amt < 0) amt = 0;
    if (amt > 32767) amt = 32767;
    if (shape < 0) shape = 0;
    if (shape > 32767) shape = 32767;
    if (eng_k == 2u) {                              /* NEBULOSA: the spread closes into one at the merger */
        int32_t sp = mul15(amt, 7000 + mul15(32767 - shape, 25767));
        uint32_t ik[5];
        for (k = 0; k < 5u; k++) ik[k] = inc + (uint32_t)(int32_t)(((int64_t)(int32_t)(inc >> 1) * mul15(NEB_D[k], mul15(sp, 3277))) >> 15);
        for (i = 0; i < n; i++) {
            int32_t y0 = saw_blep(&s->sw[0], ik[0]), y1 = saw_blep(&s->sw[1], ik[1]), y2 = saw_blep(&s->sw[2], ik[2]);
            int32_t y3 = saw_blep(&s->sw[3], ik[3]), y4 = saw_blep(&s->sw[4], ik[4]);
            bp[i] = ((y0 + y1 + y3 + y4) * 9 + y2 * 12) >> 5;
            bx[i] = ((y0 - y4) * 11 + (y1 - y3) * 7) >> 5;
        }
    } else if (eng_k == 3u) {                       /* PIEGA: a sine folded, the folds as v^2, the ring closing them */
        int32_t sa = mul15(amt, shape), G1 = 4096 + mul15(sa, 28672), b1 = mul15(sa, 6000), G0 = s->fz[2], b0 = s->fz[3];
        uint32_t ph = s->sw[0];
        if (!G0) { G0 = G1; b0 = b1; }              /* the folds glide across the block: no steps */
        for (i = 0; i < n; i++) {
            int32_t G = G0 + (((G1 - G0) * (int32_t)i) >> 5), bias = b0 + (((b1 - b0) * (int32_t)i) >> 5);
            int32_t u = ((sin_q15(ph) + bias) * G) >> 12, w = ((cos_q15(ph) + bias) * G) >> 12, y, yx;
            y = sin_q15((uint32_t)u << 15); yx = sin_q15((uint32_t)w << 15);   /* sin (pi/2 u): the folder */
            s->fz[0] += (y - s->fz[0]) >> 10; s->fz[1] += (yx - s->fz[1]) >> 10;   /* no DC */
            bp[i] = y - s->fz[0];
            bx[i] = ((yx - s->fz[1]) * 3) >> 2;
            ph += inc;
        }
        s->sw[0] = ph;
        s->fz[2] = G1; s->fz[3] = b1;
    } else {                                        /* VOCE: u o a e i, two saws, three formants */
        int32_t pos = mul15(amt, 512) + mul15(shape, 512), fr, fc[3], vib;
        uint32_t iv, i2;
        if (pos > 1023) pos = 1023;
        k = (uint32_t)pos >> 8; fr = pos & 255;
        for (i = 0; i < 3u; i++) fc[i] = ((VF[k][i] + (((VF[k + 1u][i] - VF[k][i]) * fr) >> 8)) * 4781) >> 10;   /* 2 pi f / fs, Q15 */
        s->vph += 516172u * n;                      /* 5.3 Hz */
        vib = mul15(sin_q15(s->vph), 150);
        iv = inc + (uint32_t)(int32_t)(((int64_t)(int32_t)(inc >> 1) * vib) >> 14);
        i2 = iv + (iv >> 9);                        /* the second singer: 3 cents up */
        for (i = 0; i < n; i++) {
            int32_t x = (saw_blep(&s->sw[0], iv) + saw_blep(&s->sw[1], i2)) >> 1, o = 0, j;
            x += ((int32_t)(xrand() >> 16) - 32768) >> 6;   /* a breath */
            for (j = 0; j < 3; j++) {
                int32_t *lp = &s->fz[2 * j], *b = &s->fz[2 * j + 1], h;
                *lp += mul15(fc[j], *b);
                h = x - *lp - mul15(VD[j], *b);
                *b += mul15(fc[j], h);                  /* (f < 0.43, Q < 13: it stays put, no clamps) */
            }
            o = mul15(s->fz[1], VD[0]) + ((mul15(s->fz[3], VD[1]) * 23) >> 5) + ((mul15(s->fz[5], VD[2]) * 13) >> 5);
            bp[i] = o * 11 >> 1;
            bx[i] = mul15(s->fz[3], VD[1]) * 3 >> 1;
        }
    }
}
VFAST static inline int32_t ks_tick(vstate_t *s, int32_t in, int32_t g, int32_t b, uint32_t d8)   /* CORDA: a frame of the string */
{
    int16_t *bf = s->ks;
    uint32_t w = s->ksw, p = w - (d8 >> 8);
    int32_t f = (int32_t)(d8 & 255u), a = bf[p & (KS_N - 1u)], c = bf[(p - 1u) & (KS_N - 1u)], y;
    y = a + (((c - a) * f) >> 8);
    s->kslp += mul15(y - s->kslp, b);
    y = sat15(mul15(s->kslp, g) + in);
    bf[w] = (int16_t)y;
    s->ksw = (w + 1u) & (KS_N - 1u);
    return y;
}
VFAST static inline int32_t ks_tap(const vstate_t *s, uint32_t d) { return s->ks[(s->ksw - d) & (KS_N - 1u)]; }   /* a second pickup */
/* the inspiral's wave at orbital phase p, without its amplitude: h+ and hx (the carrier a quarter turn on) */
VFAST static inline void insp_wave(vstate_t *s, uint32_t p, int32_t I, int32_t *hp, int32_t *hx)
{
    ev_t *e = &s->e;
    int32_t a33 = s->h33, a44 = s->h44, a21 = s->h21, y;
    uint32_t ph;
    if (e->eng != 1u) {                             /* ONDA (and CORDA's, and the engines' crossfade): the modes as they are */
        *hp = cos_q15(p * 2u) + mul15(cos_q15(p * 3u), a33) + mul15(cos_q15(p * 4u), a44) + mul15(cos_q15(p), a21);
        *hx = sin_q15(p * 2u) + mul15(sin_q15(p * 3u), a33) + mul15(sin_q15(p * 4u), a44) + mul15(sin_q15(p), a21);
        return;
    }
    switch (e->alg_i) {
    case 1: {                                       /* BINARIA: the two masses modulate each other, closer and closer */
        int32_t m21 = sin_q15(p + fm_off(s->yb, I >> 2)), yb;
        uint32_t pb;
        ph = p * 2u + fm_off(m21, I);
        y = sin_q15(ph);
        pb = p * 3u + fm_off(sin_q15(p * 4u), (I * 7) >> 3) + fm_off(s->ya, I >> 2);
        yb = sin_q15(pb);
        *hp = cos_q15(ph) + mul15(cos_q15(pb), a33 + 6000);
        *hx = y + mul15(yb, a33 + 6000);
        s->ya = y; s->yb = yb;
        return; }
    case 2: {                                       /* SPIRALE: (4,4) > (3,3) > (2,1) > (2,2) */
        int32_t x = sin_q15(p * 4u);
        x = sin_q15(p * 3u + fm_off(x, I));
        x = sin_q15(p + fm_off(x, I));
        ph = p * 2u + fm_off(x, I);
        *hp = cos_q15(ph); *hx = sin_q15(ph);
        return; }
    case 3:                                         /* ANELLO: the (2,2) on itself, rung against the (3,3) */
        ph = p * 2u + fm_off(s->ya, I);
        y = sin_q15(ph);
        s->ya = y;
        *hp = cos_q15(ph) + mul15(mul15(y, sin_q15(p * 3u)), 2 * a33 + 8000);
        *hx = y;
        return;
    default:                                        /* CAMPANA: the (2,2) lit by the (3,3), the others beside it */
        ph = p * 2u + fm_off(sin_q15(p * 3u), I >> 1);
        *hp = cos_q15(ph) + mul15(cos_q15(p * 4u), a44) + mul15(cos_q15(p), a21);
        *hx = sin_q15(ph) + mul15(sin_q15(p * 4u), a44) + mul15(sin_q15(p), a21);
        return;
    }
}
VFAST static void ev_begin(uint32_t v)
{
    vstate_t *s = &vs[v];
    uint32_t k;
    s->e = ev_pend[v];
    if (s->stage != ST_OFF) { s->dcl += s->ol; s->dcr += s->orr; }   /* stolen while it sounded: its last value fades out */
    s->ol = s->orr = 0;
    s->fqp = s->fdp = 0;
    s->hl = s->e.hold; s->lastinc = 0; s->gll = 0;
    s->stage = ST_INSP;
    s->left = s->e.tc;
    s->phi = xrand() & 0x3FFFFFFFu;
    s->phi2 = s->phi;
    s->pph = xrand();
    s->a_now = 0;
    for (k = 0; k < NM; k++) s->re[k] = s->im[k] = 0;
    s->spos = s->e.smp_start << 16;
    s->ci = 32767;
    s->ya = s->yb = 0;
    s->f_lp[0] = s->f_lp[1] = s->f_bp[0] = s->f_bp[1] = 0;
    s->kenv = 0; s->menv = 0; s->art = 32767; s->q_last = 0; s->strain = 0; s->orb = 0;
    s->h33 = s->h44 = s->h21 = 0;
    for (k = 0; k < MD_N; k++) s->md[k] = 0;
    s->einc = 0; s->eenv = 0; s->vph = xrand();
    for (k = 0; k < 5u; k++) s->sw[k] = xrand();    /* the saws start apart: a cloud from the start */
    for (k = 0; k < 6u; k++) s->fz[k] = 0;
    s->ks = ks_buf[v]; s->ksx = 0; s->kslp = 0; s->kexc = 0; s->ksabs = 0;
    if (s->e.eng == 4u) { for (k = 0; k < KS_N; k++) s->ks[k] = 0; s->ksw = 0; }   /* a new string, quiet */
    for (k = 0; k < NM; k++) vis[v].mhz16[k] = k < s->e.nm ? hz16_of_inc(s->e.ring_inc[k]) : 0u;
    vis[v].amp = 0;
    vis[v].prog = 0;
    s->spd = 0; s->fin = 32767; ev_go[v] = 0;
    if (s->e.rev) {                                 /* 0.6 INVERSO: the ring first, growing from far after the merger to it */
        static const uint8_t MM[NM] = {2, 3, 2, 4};
        int32_t a = s->e.amp;
        for (k = 0; k < s->e.nm; k++) {
            int32_t am = (int32_t)(((int64_t)((a * s->e.mamp[k]) >> 15) * s->e.sw0[k]) >> 15);
            uint32_t ph = s->phi * MM[k];
            s->re[k] = (cos_q15(ph) * am) >> 7;
            s->im[k] = (sin_q15(ph) * am) >> 7;
        }
        s->peak = (a << 8) + 1;
        s->ipk = (int32_t)(0x3FFFFFFFu / (uint32_t)((s->peak >> 8) + 1));
        s->ostep = s->e.inc_m >> 1;
        s->sph = s->phi;
        s->eenv = (int32_t)(((int64_t)a * s->e.sw0[0]) >> 15);
        s->sleft = s->e.swell;
        s->xf = 0;
        s->stage = ST_SWELL;
    }
}
VFAST static void ev_unmerge(vstate_t *s)            /* 0.6 INVERSO: at the merger, the chirp backwards (it falls, it fades) */
{
    s->dcl += s->ol; s->dcr += s->orr;              /* the ring's last value fades: no step */
    s->stage = ST_INSP;
    s->left = 0;                                    /* (reversed: the time since the merger) */
    s->a_now = 0;
    s->fin = 0;
    s->fqp = s->fdp = 0;
    s->menv = 32767;
    s->f_lp[0] = s->f_lp[1] = s->f_bp[0] = s->f_bp[1] = 0;
}
VFAST static void ev_merge(vstate_t *s)                   /* the ringdown takes over, phase for phase */
{
    uint32_t k, m;
    static const uint8_t MM[NM] = {2, 3, 2, 4};     /* the modes' m: (2,2,0) (3,3,0) (2,2,1) (4,4,0) */
    int32_t a = s->a_now;
    for (k = 0; k < s->e.nm; k++) {
        uint32_t ph;
        int32_t am = (a * s->e.mamp[k]) >> 15;
        m = MM[k];
        ph = (k == 2u && s->e.r2_q16 ? s->phi2 * 2u : s->phi * m) + (k == 2u && !s->e.r2_q16 ? 0x40000000u : 0u);
        s->re[k] = (cos_q15(ph) * am) >> 7;         /* Q23-ish: Q15 x Q15 >> 7 */
        s->im[k] = (sin_q15(ph) * am) >> 7;
    }
    s->peak = (a << 8) + 1;
    s->ipk = (int32_t)(0x3FFFFFFFu / (uint32_t)((s->peak >> 8) + 1));   /* Q15 of the peak's level, Q15 */
    s->xf = 132u;                                   /* 3 ms: the inspiral fades into the ring, no step */
    s->stage = ST_RING;
    s->spos = s->e.smp_start << 16;
    s->menv = 32767;
    if (s->e.tonfo) {                               /* the thump: 150 Hz falling to 45, about 90 ms */
        s->kph = 0;                                 /* from zero: no step */
        s->kinc = 150u * 97391u;
        s->kenv = mul15(s->e.tonfo, s->e.amp) * 2;
    }
    s->sph = s->phi;
    if (s->e.eng == 4u) s->ksx = s->e.ks_d8 >> 8;    /* CORDA: the merger plucks the string, a period of noise */
    s->gll = s->lastinc ? log2_q16(s->lastinc) - log2_q16(s->e.ring_inc[0]) - s->rdet : 0;   /* APPRODO starts where the chirp was */
    s->eenv = a;
}
VAI static inline int32_t ratio15(int32_t a, int32_t b)         /* a / b in Q15, 32-bit only (no __divdi3) */
{
    uint32_t ua = (uint32_t)(a < 0 ? -a : a), ub = (uint32_t)(b < 0 ? -b : b), r;
    int neg = (a < 0) != (b < 0);
    if (!ub) return 0;
    while (ua > 65535u) { ua >>= 1; ub >>= 1; }
    r = ub ? ua * 32767u / ub : 0x7FFFFFFFu;
    if (r > 0x7FFFFFFFu) r = 0x7FFFFFFFu;
    return neg ? -(int32_t)r : (int32_t)r;
}
VFAST static int32_t msrc(vstate_t *s, uint32_t src, int32_t env, int32_t freq)
{
    switch (src) {
    case MS_STRAIN: return s->strain;
    case MS_FREQ: return freq;
    case MS_ENV: return env;
    case MS_MERGE: return s->menv;
    case MS_PREC: return sin_q15(s->pph);
    case MS_ORBIT: return sin_q15(s->orb);
    case MS_LFO: return lfo_val;
    case MS_RAND: return s->e.rnd;
    case MS_VEL: return s->e.vel;
    default: return 0;
    }
}
VFAST static void ev_block(uint32_t v, int32_t *out, uint32_t n)
{
    vstate_t *s = &vs[v];
    ev_t *e = &s->e;
    uint32_t i, k, base_inc;
    int32_t ci, hp_k, hx_k, env, freq, fq, fd, drive, pl, pr, amul, sacc = 0, fq0, fq1, fd0, fd1;
    if (s->stage == ST_OFF) return;
    /* the precession: the inclination swings (block rate) */
    s->pph += (uint32_t)(((uint64_t)e->inc_m * (uint32_t)(e->prec + 64)) >> 22) * n;
    {
        uint32_t io = e->iota0 + (uint32_t)(int32_t)(((int64_t)sin_q15(s->pph) * e->prec) >> 1);   /* up to 45 degrees */
        ci = cos_q15(io);
    }
    hp_k = (32767 + ((ci * ci) >> 15)) >> 1;        /* (1 + cos^2 i) / 2 */
    hx_k = ci;                                      /* cos i */
    s->ci = ci;
    /* MOD: the sources of this event, the two slots */
    if (s->stage == ST_INSP) {
        env = e->amp ? ratio15(s->a_now, e->amp) : 0;
        freq = e->oct16 ? ratio15(s->f_l2 + e->oct16, e->oct16) : 32767;
    } else {
        int32_t e0 = (s->re[0] < 0 ? -s->re[0] : s->re[0]) + (s->im[0] < 0 ? -s->im[0] : s->im[0]);
        env = (int32_t)((int64_t)(e0 >> 8) * s->ipk >> 15);
        freq = 32767;
    }
    if (env > 32767) env = 32767;
    if (freq < 0) freq = 0;
    if (freq > 32767) freq = 32767;
    for (k = 0; k < MD_N; k++) s->md[k] = 0;
    for (k = 0; k < 2u; k++) {
        uint32_t src = eng.ms[k], dst = eng.md[k];
        int32_t val;
        if (!src || !dst || dst >= MD_N) continue;
        val = msrc(s, src, env, freq);
        if (v == 0u || vis_msrc[k] == 0) vis_msrc[k] = val;
        s->md[dst] += mul15(val, eng.ma[k]);
    }
    for (k = MD_ONDA; k < MD_N; k++) gmod[k - MD_ONDA] += s->md[k];
    {   /* RADIO: off the station the wave fades into the static and slips out of tune, as a voice on short waves */
        int32_t r = eng.radio;
        s->rgain0 = s->rgain;
        if (r) {
            int32_t d = e->station - eng.dial, q = (d << 8) / (eng.selw > 64 ? eng.selw : 64), g, dt;
            uint32_t q2;
            if (q > 40000) q = 40000;
            if (q < -40000) q = -40000;
            q2 = (uint32_t)(q < 0 ? -q : q) * (uint32_t)(q < 0 ? -q : q);   /* (d / width)^2, Q16 */
            g = (int32_t)(0x7FFFFFFFu / (65536u + q2));                   /* a station's shape: 1 / (1 + x^2) */
            if (g > 32767) g = 32767;
            s->rsig = g;
            s->rgain = 32767 - r + mul15(r, g);
            dt = d * 41 / 4;                        /* an octave every 25 on the dial */
            if (dt > 65536) dt = 65536;
            if (dt < -65536) dt = -65536;
            s->rdet = mul15(r, mul15(dt, 32767 - g));   /* in tune on the station */
        } else { s->rgain = 32767; s->rdet = 0; s->rsig = 32767; }
        if (s->stage == ST_INSP && s->left == e->tc) s->rgain0 = s->rgain;   /* the first block: no fade in from nothing */
    }
    if (s->menv) s->menv -= (s->menv >> 8) + 1;
    drive = e->drive + mul15(e->drive, s->md[MD_SAT] * 3);
    amul = 32767 + s->md[MD_AMP];
    if (amul < 0) amul = 0;
    if (amul > 65535) amul = 65535;
    pl = e->pan_l - mul15(s->md[MD_PAN], 16000); pr = e->pan_r + mul15(s->md[MD_PAN], 16000);
    if (pl < 0) pl = 0;
    if (pr < 0) pr = 0;
    fd = e->fdamp - mul15(e->fdamp, s->md[MD_RES]);
    if (s->stage != ST_INSP && e->fdamp_ring < fd) fd = e->fdamp_ring;
    if (fd < (e->ftype == 2u || e->ftype == 4u ? 3600 : 6500)) fd = e->ftype == 2u || e->ftype == 4u ? 3600 : 6500;   /* 0.3: Q at most 9 (a pass band 5): it sings, it does not swallow the note */
    if (fd > 65000) fd = 65000;
    if (s->stage == ST_INSP) {
        uint32_t sq, x, inc22, step, step2, xeng = e->eng >= 2u && e->eng != 4u;
        int32_t l2, a0, a1, a33, a44, a21, v1, v2, dm, I, sub = 0, lv2 = e->lv2, ebp[BLK], ebx[BLK], ksum = 0;
        sq = (uint32_t)(((uint64_t)s->left * e->rtc) >> 16);   /* the time left, Q16 of the inspiral (INVERSO: the time since) */
        if (sq > 65536u) sq = 65536u;
        if (e->fin_p != 4096 && sq) {              /* FINALE: the time bent: under 1 a late whoop, over 1 a long top */
            int32_t lt = log2_q16(sq) - (16 << 16);
            sq = exp2_q16((int32_t)(((int64_t)lt * e->fin_p) >> 12));
        }
        x = 65536u + (uint32_t)(((uint64_t)sq * e->k_q8) >> 8);
        l2 = -((log2_q16(x) - (16 << 16)) * 3) >> 3;    /* log2 (f / fM) */
        s->f_l2 = l2;
        inc22 = (uint32_t)(((uint64_t)e->inc_m * exp2_q16(l2 + mul15(s->md[MD_PITCH], 65536) + s->rdet)) >> 16);
        if (e->cq == 1u && e->nd) {                 /* 0.6: the physics' scales, to the fraction */
            inc22 = inc_of(deg_snap16(e->note_m16 + 12 * l2, e->tonic % 12u, e->deg, e->nd));
        } else if (e->cq == 1u) {                   /* CHIRP SCALA: the wave climbs the scale's steps */
            int32_t nn = (e->note_m16 + 12 * l2 + 32768) >> 16;
            inc22 = inc_of(snap_note(nn, e->smask, e->tonic) << 16);
        } else if (e->cq == 2u && e->inc_h) {       /* CHIRP ARMONICI: the harmonics of fM / 16, closer as it climbs */
            uint32_t j = (inc22 + e->inc_h / 2u) / e->inc_h;
            inc22 = e->inc_h * (j ? j : 1u);
        }
        if (e->cq && inc22 != s->q_last) { s->q_last = inc22; if (s->art > 0) s->art = -s->art; }   /* each step struck again: a dip, then up */
        step = inc22 >> 1;                              /* the orbit: half the wave */
        step2 = e->r2_q16 ? (uint32_t)(((uint64_t)step * e->r2_q16) >> 16) : 0u;
        a1 = (int32_t)((exp2_q16(e->eng >= 2u ? l2 / 3 : l2 * 2 / 3) * (uint32_t)e->amp) >> 16);   /* A ~ f^(2/3) (the engines: f^(1/3), present from the start) */
        if (e->rev) {                               /* 0.6 INVERSO: in from the merger (13 ms), out over the last sixth */
            uint32_t rest = s->left < e->tc ? e->tc - s->left : 0u, f6 = e->tc / 6u + 1u;
            if (s->fin < 32767) { s->fin += 1800; if (s->fin > 32767) s->fin = 32767; }   /* (13 ms) */
            a1 = mul15(a1, s->fin);
            if (rest < f6) a1 = (int32_t)(((int64_t)a1 * rest) / f6);
        }
        v1 = (int32_t)(exp2_q16(l2 / 3) * 14746u >> 16);                    /* v ~ 0.45 (f/fM)^(1/3), Q15 */
        v2 = (v1 * v1) >> 15;
        dm = e->delta;
        a33 = (int32_t)(((int64_t)((dm * v1) >> 15) * 52429 >> 15) * e->harm >> 15);   /* 1.6 delta v */
        a44 = (int32_t)(((int64_t)(((32767 - 3 * e->eta) * v2) >> 15) * 45875 >> 15) * e->harm >> 15);   /* 1.4 (1 - 3 eta) v^2 */
        a21 = (int32_t)(((int64_t)((dm * v1) >> 15) * 29491 >> 15) * e->harm >> 15);   /* 0.9 delta v */
        if (a44 < 0) a44 = 0;
        if (inc22 > 876523938u) a44 = 0;            /* (4,4) above 18 kHz: out */
        if (inc22 > 1168698584u) a33 = 0;
        s->h33 = a33; s->h44 = a44; s->h21 = a21;
        I = (int32_t)(((int64_t)e->idx * exp2_q16(l2 * 2 / 3)) >> 16);    /* the index grows as v^2, all of it at the merger */
        I += mul15(I, s->md[MD_IDX]);
        if (I < 0) I = 0;
        vis[v].idx_now = I;
        base_inc = inc22;
        {   /* the filter follows the wave: its multiple, the distance, the modulation */
            uint32_t fi = (uint32_t)(((uint64_t)base_inc * exp2_q16(e->fmul_l2 + mul15(s->md[MD_FILT], 4 << 16))) >> 16);
            fq = (int32_t)(fi / 20861u);
            vis[v].fc_hz = (int32_t)(fi / 97391u);
        }
        if (fq < 40) fq = 40;
        if (fq > 24000) fq = 24000;
        fq0 = s->fqp ? s->fqp : fq; fd0 = s->fdp ? s->fdp : fd;
        fq1 = fq0 + (fq - fq0) / 4; fd1 = fd0 + (fd - fd0) / 4;
        s->fqp = fq1; s->fdp = fd1;
        a0 = s->a_now;
        if (e->sub) sub = e->sub;
        if (xeng) {                                 /* the engine follows the chirp */
            int32_t amt = e->e_amt + mul15(e->e_amt, s->md[MD_IDX]);
            s->einc = inc22;
            eng_render(s, e->eng, inc22, (int32_t)(exp2_q16(l2 * 2 / 3) >> 1), amt, n, ebp, ebx);
        }
        for (i = 0; i < n; i++) {
            int32_t a = a0 + (((a1 - a0) * (int32_t)i) >> 5), hp, hx;
            uint32_t p = s->phi;
            if (e->cq) {
                if (s->art < 0) { s->art += 900; if (s->art > 0) s->art = 0; }   /* down in ~1 ms (no click) */
                else if (s->art < 32767) { s->art += 600; if (s->art > 32767) s->art = 32767; }
                a = mul15(a, 2000 + mul15(s->art < 0 ? -s->art : s->art, 30767));
            }
            if (xeng) { hp = ebp[i]; hx = ebx[i]; }
            else insp_wave(s, p, I, &hp, &hx);
            if (e->smp_on && e->use == 1u && smp_len) {     /* PORTANTE: the sound is (or joins) the (2,2) wave */
                uint32_t sp = s->spos >> 16;
                int32_t sv = sp < smp_len ? smp[sp] : 0;
                hp += mul15(sv - (e->eng ? 0 : cos_q15(p * 2u)), e->smp_lv);
                s->spos += (uint32_t)(((uint64_t)inc22 * 5523423u) >> 32);
                if ((s->spos >> 16) >= smp_len) s->spos = e->smp_start << 16;
            }
            if (lv2) {                              /* INTERVALLO: the lighter mass's voice */
                uint32_t q = s->phi2 * 2u + (e->eng == 1u ? fm_off(sin_q15(s->phi2 * 2u), I / 3) : 0u);
                hp += mul15(cos_q15(q), lv2); hx += mul15(sin_q15(q), lv2);
                s->phi2 += step2;
            }
            if (sub) hp += mul15(sin_q15(p), sub);
            hp = mul15(hp, a); hx = mul15(hx, a);
            if (e->eng == 4u) {                     /* CORDA: the chirp's saw sings in the string (its harmonics cross the string's) */
                int32_t y = ks_tick(s, mul15(saw_blep(&s->sw[0], inc22), a) >> 3, e->ks_g[0], e->ks_b, e->ks_d8);
                hp = (hp >> 1) + ((y * 21) >> 5); hx = (hx >> 1) + ((ks_tap(s, e->ks_d8 / 768u) * 21) >> 5);   /* (0.4.1: the string 4 dB down) */
                ksum += y < 0 ? -y : y;
            }
            hp = mul15(hp, amul); hx = mul15(hx, amul);
            hp = softclip(hp, drive); hx = softclip(hx, drive);
            if (e->ftype) {
                int32_t fqi = fq0 + (((fq1 - fq0) * (int32_t)i) >> 5), fdi = fd0 + (((fd1 - fd0) * (int32_t)i) >> 5);
                hp = svf(hp, fqi, fdi, &s->f_lp[0], &s->f_bp[0], e->ftype); hx = svf(hx, fqi, fdi, &s->f_lp[1], &s->f_bp[1], e->ftype);
            }
            {
                int32_t h = mul15(hp, hp_k), xx = mul15(mul15(hx, hx_k), 19661), l = h + xx, r = h - xx;
                int32_t yl, yr;
                if (eng.radio) { int32_t rg = s->rgain0 + (((s->rgain - s->rgain0) * (int32_t)i) >> 5); l = mul15(l, rg); r = mul15(r, rg); }
                yl = mul15(l, pl); yr = mul15(r, pr);
                if (s->kill) { yl = mul15(yl, s->kill); yr = mul15(yr, s->kill); s->kill -= 80; if (s->kill < 1) s->kill = 1; }
                if (s->dcl | s->dcr) { yl += s->dcl; yr += s->dcr; s->dcl -= (s->dcl >> 7) + (s->dcl > 0) - (s->dcl < 0); s->dcr -= (s->dcr >> 7) + (s->dcr > 0) - (s->dcr < 0); }
                out[2u * i] += yl; out[2u * i + 1u] += yr;
                s->ol = yl; s->orr = yr;
                vis_strain_acc += hp; sacc += hp;
            }
            s->phi = p + step;
        }
        s->a_now = a1;
        s->lastinc = inc22;
        s->ksabs = ksum / (int32_t)n;
        s->ostep = step;
        s->orb += (step >> 5) * n;
        if (e->rev) {                               /* INVERSO: back to the start, then silence */
            s->left += n;
            if (s->left >= e->tc) { s->stage = ST_OFF; s->dcl += s->ol; s->dcr += s->orr; }
        } else {
            uint32_t dn = s->spd ? (uint32_t)(((uint64_t)n * s->spd) >> 8) : n;
            int wait = (e->fus == 1u && ev_gate[v]) || (e->fus == 2u && !ev_go[v]);
            if (e->fus && !wait && !s->spd) {       /* 0.6 TUFFO: let go (or the command): the rest of the chirp in TUFFO's time */
                uint64_t q = s->left > e->rush ? ((uint64_t)s->left << 8) / e->rush : 256u;
                s->spd = q > 0x1000000u ? 0x1000000u : (uint32_t)q;
                dn = (uint32_t)(((uint64_t)n * s->spd) >> 8);
            }
            if (s->left > dn) s->left -= dn;
            else if (e->fus && wait) s->left = 0;  /* FUSIONE AL RILASCIO, A COMANDO: the last orbit, until then */
            else if (s->hl > n && !s->spd) { s->left = 0; s->hl -= n; }   /* SOSPENSIONE: at the top, a while */
            else ev_merge(s);
        }
        vis[v].hz16 = hz16_of_inc(inc22);
        vis[v].amp = a1;
        vis[v].prog = (uint32_t)(65536u - (sq > 65536u ? 65536u : sq));
        vis[v].phi = s->phi;
        s->strain = a1 ? ratio15(sacc / (int32_t)n, a1) : 0;
    } else {                                        /* RING, CODA, REL: the resonators */
        uint32_t set = s->stage == ST_RING ? 0u : s->stage == ST_CODA ? 1u : s->stage == ST_REL ? 2u : 4u, nm = e->nm, alg = e->eng == 1u ? e->alg_r : 0u;
        uint32_t xeng = e->eng >= 2u && e->eng != 4u;
        int32_t e0, Ir = e->idx + mul15(e->idx, s->md[MD_IDX]), subamp = 0, ipk = s->ipk, ebp[BLK], ebx[BLK], en0 = s->eenv, en1 = 0, ksum = 0;
        int32_t mixm = e->eng == 5u ? 6500 : e->eng == 4u ? 9000 : 11000;   /* the engines: the remnant's modes under them */
        uint32_t mneed = alg == 4u || alg == 2u ? 0xAu : alg == 1u ? 0x3u : 0u, rown = 0, act;
        int32_t glr, rcc[NM], rss[NM];   /* CAMPANA, SPIRALE: modes 1 and 3; BINARIA: 0 and 1 */
        if (Ir < 0) Ir = 0;
        if (set <= 1u && e->frz && (ev_gate[v] || hold_on)) set = 3u;   /* 0.6 CONGELA: held, the ring stands */
        if (s->stage == ST_SWELL && s->sleft < 220u) amul = mul15(amul, (int32_t)s->sleft * 148);   /* its last 5 ms: out, the chirp comes */
        vis[v].idx_now = Ir;
        base_inc = e->ring_inc[0];
        {
            uint32_t fi = (uint32_t)(((uint64_t)base_inc * exp2_q16(e->fmul_l2 + mul15(s->md[MD_FILT], 4 << 16))) >> 16);
            fq = (int32_t)(fi / 20861u);
            vis[v].fc_hz = (int32_t)(fi / 97391u);
        }
        if (fq < 40) fq = 40;
        if (fq > 24000) fq = 24000;
        fq0 = s->fqp ? s->fqp : fq; fd0 = s->fdp ? s->fdp : fd;
        fq1 = fq0 + (fq - fq0) / 4; fd1 = fd0 + (fd - fd0) / 4;
        s->fqp = fq1; s->fdp = fd1;
        e0 = (s->re[0] < 0 ? -s->re[0] : s->re[0]) + (s->im[0] < 0 ? -s->im[0] : s->im[0]);
        if (e->sub) subamp = mul15(e0 >> 8, e->sub);
        {   /* APPRODO: from the merger's pitch to the key's, in log; RADIO's mistuning on top */
            int32_t gl = s->gll;
            if (gl) { gl -= (int32_t)(((int64_t)gl * e->glk) >> 15) + (gl > 0) - (gl < 0); s->gll = gl; }
            glr = gl + s->rdet;
            if ((glr && e->eng != 4u) || (hold_on == 2u && set == 1u)) {   /* (0.5 HOLD INFINITO: the codas no longer fade) */
                for (k = 0; k < nm; k++) rot_q30((uint32_t)(((uint64_t)e->ring_inc[k] * exp2_q16(glr)) >> 16),
                                                 hold_on == 2u && set == 1u ? (1 << 30) - 8 : e->rad[k][set], &rcc[k], &rss[k]);
                rown = 1;
            }
        }
        if (xeng) {                                 /* the engine: glides to the key, follows the (2,2,0)'s envelope */
            int32_t ar = s->re[0] < 0 ? -s->re[0] : s->re[0], ai = s->im[0] < 0 ? -s->im[0] : s->im[0];
            int32_t amt = e->e_amt + mul15(e->e_amt, s->md[MD_IDX]), tgt = (int32_t)e->ring_inc[0];
            en1 = ((ar > ai ? ar + ai * 3 / 8 : ai + ar * 3 / 8) >> 8);   /* |z|, near enough */
            (void)tgt;
            {
                uint32_t ie = (uint32_t)(((uint64_t)e->ring_inc[0] * exp2_q16(glr + mul15(s->md[MD_PITCH], 65536))) >> 16);
                s->einc = ie;
                int32_t pk = s->peak >> 8;
                eng_render(s, e->eng, ie, pk ? ratio15(en1, pk) : 0, amt, n, ebp, ebx);
            }
            s->eenv = en1;
        }
        {   /* 0.4.1: the modes that still sound (and, under the engines, at most three; one when the governor asks) */
            uint32_t lim = e->eng >= 2u && e->eng != 4u ? (gov >= 2u ? 1u : 3u) : NM;
            act = 0;
            for (k = 0; k < nm; k++) {
                int32_t m = (s->re[k] < 0 ? -s->re[k] : s->re[k]) + (s->im[k] < 0 ? -s->im[k] : s->im[k]);
                if (k < lim && (m >= 384 || k == 0u || s->xf || s->stage == ST_SWELL || (e->smp_on && e->use == 0u))) act |= 1u << k;
                else { s->re[k] = 0; s->im[k] = 0; }
            }
        }
        for (i = 0; i < n; i++) {
            int32_t hp = 0, hx = 0, x = 0, mo[NM], kq = 0;
            if (e->smp_on && e->use == 0u && smp_len) {    /* MATERIA: the sound strikes the modes */
                uint32_t sp = s->spos >> 16;
                if (sp < smp_len) { x = (smp[sp] * e->smp_lv) >> 4; s->spos += 32768u; }   /* Q26: x 8 the level */
            }
            for (k = 0; k < nm; k++) {
                int32_t re, im;
                if (!(act >> k & 1u)) continue;
                re = s->re[k] + (int32_t)(((int64_t)x * e->ing[k]) >> 18); im = s->im[k];
                int32_t c = rown ? rcc[k] : e->cr[k][set], sn = rown ? rss[k] : e->sr[k][set];
                s->re[k] = (int32_t)(((int64_t)c * re - (int64_t)sn * im) >> 30);
                s->im[k] = (int32_t)(((int64_t)sn * re + (int64_t)c * im) >> 30);
                if (mneed >> k & 1u) mo[k] = (int32_t)(((int64_t)(s->re[k] >> 8) * ipk) >> 15);   /* the mode, normalised: a modulator (only those that modulate) */
            }
            for (k = 0; k < nm; k++) {
                int32_t mod = 0, has = 1;
                if (!(act >> k & 1u)) continue;
                if (alg == 4u) { if (k == 0u && nm > 1u) mod = mo[1]; else if (k == 2u && nm > 3u) mod = mo[3]; else has = 0; }   /* CAMPANA (0.4.1: only FM's) */
                else if (alg == 1u) { if (k == 0u && nm > 1u) mod = mo[1]; else if (k == 1u) mod = mo[0]; else has = 0; }   /* BINARIA */
                else if (alg == 2u) { if (k == 1u && nm > 3u) mod = mo[3]; else if (k == 0u && nm > 1u) mod = mo[1]; else has = 0; }   /* SPIRALE */
                else if (alg == 3u) { if (k == 0u) mod = s->ya; else has = 0; }   /* ANELLO */
                else has = 0;
                if (has && Ir) {
                    uint32_t off = fm_off(mod, Ir);
                    int32_t c = cos_q15(off), sn = sin_q15(off);
                    int32_t yr = (int32_t)(((int64_t)s->re[k] * c - (int64_t)s->im[k] * sn) >> 15);
                    hp += yr;
                    hx += (int32_t)(((int64_t)s->im[k] * c + (int64_t)s->re[k] * sn) >> 15);
                    if (alg == 3u && k == 0u) s->ya = (int32_t)(((int64_t)(yr >> 8) * ipk) >> 15);
                } else { hp += s->re[k]; hx += s->im[k]; }
            }
            hp >>= 8; hx >>= 8;
            if (e->eng >= 2u) { hp = mul15(hp, mixm); hx = mul15(hx, mixm); }
            if (s->xf) {                            /* the crossfade from the inspiral */
                uint32_t p = s->phi;
                int32_t ip, ix, g = (int32_t)s->xf * 248;
                int32_t vp = 0, vx = 0;
                if (e->lv2) {                       /* INTERVALLO's voice fades out with the inspiral */
                    uint32_t q = s->phi2 * 2u;
                    vp = mul15(cos_q15(q), e->lv2); vx = mul15(sin_q15(q), e->lv2);
                    s->phi2 += (uint32_t)(((uint64_t)s->ostep * e->r2_q16) >> 16);
                }
                if (xeng) {                         /* the engine goes on: only the modes fade in */
                    hp -= mul15(hp, g); hx -= mul15(hx, g);
                    hp += mul15(mul15(vp, s->a_now), g); hx += mul15(mul15(vx, s->a_now), g);
                } else {
                    insp_wave(s, p, e->idx, &ip, &ix);
                    ip = mul15(ip + vp, s->a_now); ix = mul15(ix + vx, s->a_now);
                    if (e->eng == 4u) { ip >>= 1; ix >>= 1; }
                    hp += mul15(ip - hp, g); hx += mul15(ix - hx, g);
                }
                s->phi = p + s->ostep;
                s->xf--;
            }
            if (xeng) {
                int32_t ea = en0 + (((en1 - en0) * (int32_t)i) >> 5);
                hp += mul15(ebp[i], ea); hx += mul15(ebx[i], ea);
            } else if (e->eng == 4u) {              /* CORDA: plucked at the merger, bowed by the remnant's modes */
                int32_t ex = hp >> 4, y;
                if (s->ksx) {
                    int32_t w = (int32_t)(xrand() >> 16) - 32768;
                    s->kexc += mul15(w - s->kexc, e->ks_b);
                    ex += mul15(s->kexc, (s->peak >> 8) * 3 / 2);
                    s->ksx--;
                }
                y = ks_tick(s, ex, hold_on == 2u && set == 1u ? 32740 : e->ks_g[set > 3u ? 0u : set], e->ks_b, e->ks_d8);
                hp += (y * 21) >> 5; hx += (ks_tap(s, e->ks_d8 / 768u) * 21) >> 5;
                ksum += y < 0 ? -y : y;
            }
            if (subamp) { s->sph += e->ring_inc[0] >> 1; hp += mul15(sin_q15(s->sph), subamp); }
            hp = mul15(hp, amul); hx = mul15(hx, amul);
            hp = softclip(hp, drive); hx = softclip(hx, drive);
            if (e->ftype) {
                int32_t fqi = fq0 + (((fq1 - fq0) * (int32_t)i) >> 5), fdi = fd0 + (((fd1 - fd0) * (int32_t)i) >> 5);
                hp = svf(hp, fqi, fdi, &s->f_lp[0], &s->f_bp[0], e->ftype); hx = svf(hx, fqi, fdi, &s->f_lp[1], &s->f_bp[1], e->ftype);
            }
            if (s->kenv) {                          /* the thump, in the middle, unfiltered */
                int32_t k2 = mul15(sin_q15(s->kph), s->kenv);
                s->kph += s->kinc;
                s->kenv -= (s->kenv >> 12) + 1;
                if (s->kenv < 0) s->kenv = 0;
                out[2u * i] += k2; out[2u * i + 1u] += k2;
                kq = k2;
            }
            {
                int32_t h = mul15(hp, hp_k), xx = mul15(mul15(hx, hx_k), 19661), l = h + xx, r = h - xx;
                int32_t yl, yr;
                if (eng.radio) { int32_t rg = s->rgain0 + (((s->rgain - s->rgain0) * (int32_t)i) >> 5); l = mul15(l, rg); r = mul15(r, rg); }
                yl = mul15(l, pl); yr = mul15(r, pr);
                if (s->kill) { yl = mul15(yl, s->kill); yr = mul15(yr, s->kill); s->kill -= 80; if (s->kill < 1) s->kill = 1; }
                if (s->dcl | s->dcr) { yl += s->dcl; yr += s->dcr; s->dcl -= (s->dcl >> 7) + (s->dcl > 0) - (s->dcl < 0); s->dcr -= (s->dcr >> 7) + (s->dcr > 0) - (s->dcr < 0); }
                out[2u * i] += yl; out[2u * i + 1u] += yr;
                s->ol = yl + kq; s->orr = yr + kq;
                vis_strain_acc += hp; sacc += hp;
            }
        }
        if (s->kenv) s->kinc = 45u * 97391u + (uint32_t)(((uint64_t)(s->kinc - 45u * 97391u) * 58982u) >> 16);   /* the pitch falls */
        s->orb += (s->ostep >> 5) * n;
        {
            int32_t pk = s->peak >> 8;
            s->strain = pk ? ratio15(sacc / (int32_t)n, pk) : 0;
        }
        e0 = (s->re[0] < 0 ? -s->re[0] : s->re[0]) + (s->im[0] < 0 ? -s->im[0] : s->im[0]);
        if (s->stage == ST_SWELL) { if (s->sleft > n) s->sleft -= n; else ev_unmerge(s); }
        else {
            if (s->stage == ST_RING && e0 < (int32_t)(((int64_t)s->peak * e->coda_lv) >> 15)) s->stage = ST_CODA;
            if (s->stage == ST_CODA && !ev_gate[v] && !hold_on) s->stage = ST_REL;
            if (s->stage == ST_REL && (ev_gate[v] || hold_on)) s->stage = ST_CODA;
        }
        {
            int32_t tot = 0;
            for (k = 0; k < nm; k++) {
                int32_t m = (s->re[k] < 0 ? -s->re[k] : s->re[k]) + (s->im[k] < 0 ? -s->im[k] : s->im[k]);
                tot += m;
                vis[v].amode[k] = m >> 8;
            }
            if (e->eng == 4u) s->ksabs = ksum / (int32_t)n;
            if (s->stage >= ST_RING && s->stage != ST_SWELL && tot < 2048 && !s->kenv && (s->stage != ST_RING || tot < 256) && (e->eng != 4u || (s->ksabs < 6 && !s->ksx))) s->stage = ST_OFF;
            vis[v].amp = tot >> 8;
        }
        vis[v].hz16 = hz16_of_inc(e->ring_inc[0]);
    }
    if (s->kill == 1) { s->stage = ST_OFF; s->kill = 0; s->kenv = 0; }   /* the governor's fade is over */
    vis[v].stage = s->stage;
}

/* ---- the noise of the detector, the sky, the arms, the plate: a frame each */
static uint32_t glitch_wait;
VFAST static void noise_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t gs = eng.n_seis, gt = eng.n_term, gq = eng.n_quant, msw;
    nz.mph += 6119u * n;                             /* the microseism, 0.14 Hz */
    if (eng.n_glitch && !glitch_wait-- ) {           /* a blip: a short tone low down */
        uint32_t f = 40u + xrand() % 260u, inc = f * 97391u, r;   /* 2^32 / 44100 = 97391 */
        int32_t c = cos_q15(inc), sn = sin_q15(inc);
        r = 1073741824u - (f * 1600u);               /* fast decay: ~ 10 cycles */
        nz.gcr = (int32_t)(((int64_t)c * r) >> 15);
        nz.gsr = (int32_t)(((int64_t)sn * r) >> 15);
        nz.gre = eng.n_glitch << 6; nz.gim = 0;
        vis_glitch = ((vis_glitch + (1u << 16)) & 0xFFFF0000u) | f;
        glitch_wait = 20u + (uint32_t)(((uint64_t)xrand() * (4000u - (uint32_t)eng.n_glitch / 9u)) >> 32);
    }
    if (!gs && !gt && !gq && !nz.gre) return;
    msw = 24 + ((sin_q15(nz.mph) + 32768) >> 11);   /* the microseism's swell: once a block (0.4.1) */
    for (i = 0; i < n; i++) {
        int32_t w = (int32_t)(xrand() >> 16) - 32768, w2 = (int32_t)(xrand() >> 16) - 32768, s, t, q, gl = 0, k;
        nz.s1 += (w - nz.s1) >> 8;
        nz.s2 += (nz.s1 - nz.s2) >> 8;
        s = (nz.s2 * msw) >> 2;
        nz.p1 += (w - nz.p1) >> 4;
        t = nz.p1 >> 1;
        for (k = 0; k < 2; k++) {                    /* the violin modes of the suspensions */
            int32_t re = nz.vre[k] + (w >> 7), im = nz.vim[k];
            nz.vre[k] = (int32_t)(((int64_t)VIOL_CR[k] * re - (int64_t)VIOL_SR[k] * im) >> 30);
            nz.vim[k] = (int32_t)(((int64_t)VIOL_SR[k] * re + (int64_t)VIOL_CR[k] * im) >> 30);
            t += nz.vre[k] >> 3;
        }
        nz.q1 += (w2 - nz.q1) >> 2;
        q = (w2 - nz.q1) >> 2;
        if (nz.gre || nz.gim) {
            int32_t re = nz.gre;
            nz.gre = (int32_t)(((int64_t)nz.gcr * re - (int64_t)nz.gsr * nz.gim) >> 30);
            nz.gim = (int32_t)(((int64_t)nz.gsr * re + (int64_t)nz.gcr * nz.gim) >> 30);
            gl = nz.gre >> 8;
            if (gl < 4 && gl > -4 && (nz.gim >> 8) < 4 && (nz.gim >> 8) > -4) nz.gre = nz.gim = 0;
        }
        s = mul15(s, gs); t = mul15(t, gt);
        out[2u * i] += s + t + mul15(q, gq) + gl;
        out[2u * i + 1u] += s + t + mul15((w >> 3) - (nz.q1 >> 3), gq) + gl;
    }
}
static void sky_init(void)
{
    uint32_t k;
    for (k = 0; k < NSKY; k++) {
        sky[k].oct = (int32_t)(xrand() >> 7) - (1 << 24);   /* -1 .. +1 octave, Q24 */
        sky[k].ph = xrand();
        sky[k].amp = (int32_t)(xrand() >> 17);
        sky[k].tw = (int32_t)(xrand() >> 8);
        sky[k].pan = (int32_t)(xrand() >> 17);
    }
}
VFAST static void sky_block(int32_t *out, uint32_t n)
{
    uint32_t i, k, cnt = (uint32_t)eng.s_dens;
    int32_t lv = eng.s_lv + mul15(eng.s_lv, gmod[2]);
    if (lv <= 0) return;
    if (cnt > NSKY) cnt = NSKY;
    for (k = 0; k < cnt; k++) {                      /* each binary: its frequency creeps up, its light twinkles */
        sky[k].oct += eng.s_drift + ((int32_t)(xrand() >> 24) - 128) * (eng.s_drift >> 6);
        if (sky[k].oct > (1 << 24)) { sky[k].oct = -(1 << 24); sky[k].pan = (int32_t)(xrand() >> 17); }   /* merged: another one */
        sky[k].tw += 1 + (k & 3);
        sky[k].inc = (uint32_t)(((uint64_t)eng.s_fc_inc * exp2_q16(sky[k].oct >> 8)) >> 16);
    }
    int32_t gl[NSKY], gr[NSKY];
    for (k = 0; k < cnt; k++) {                      /* the twinkle and the place: once a block (0.3: half the work) */
        int32_t a = (sky[k].amp * (40000 + sin_q15((uint32_t)sky[k].tw << 16) / 2)) >> 16;
        gl[k] = (a * sky[k].pan) >> 15; gr[k] = (a * (32767 - sky[k].pan)) >> 15;
    }
    {   /* 0.3: the sky at 22050 Hz (it lives low), each frame between two of its own: half the work */
        static int32_t pl, pr;
        int32_t f = (int32_t)(eng.s_fc_inc / 10430u);   /* 2 pi f / (fs / 2), Q15 */
        if (f > 24000) f = 24000;
        for (i = 0; i + 1u < n; i += 2u) {
            int32_t l = 0, r = 0, w, hp, ol, orr;
            for (k = 0; k < cnt; k++) {
                int32_t s = sin_q15(sky[k].ph += 2u * sky[k].inc);
                l += (s * gl[k]) >> 15;
                r += (s * gr[k]) >> 15;
            }
            w = (int32_t)(xrand() >> 16) - 32768;      /* the unresolved ones: noise in the band (a Chamberlin band pass) */
            sky_bp2 += (f * sky_bp1) >> 15;
            hp = (w >> 2) - sky_bp2 - sky_bp1;
            sky_bp1 += (f * hp) >> 15;
            w = (sky_bp1 * (int32_t)cnt) >> 5;
            if (eng.cs_lv && smp_len) {                /* CAMPIONE in the sky: a slow loop */
                static uint32_t cpos;
                uint32_t p = cpos >> 16;
                int32_t sv = p < smp_len ? smp[p] : 0;
                cpos += 2u * eng.cs_step;
                if ((cpos >> 16) >= smp_len) cpos = 0;
                sv = (sv * eng.cs_lv) >> 16;
                l += sv; r += sv;
            }
            ol = mul15(l + w, lv) >> 2; orr = mul15(r + w, lv) >> 2;
            out[2u * i] += (pl + ol) >> 1; out[2u * i + 1u] += (pr + orr) >> 1;
            out[2u * i + 2u] += ol; out[2u * i + 3u] += orr;
            pl = ol; pr = orr;
        }
    }
}
VFAST static inline int32_t arm_read(const int16_t *b, int32_t d8)   /* d8 frames back, Q8 */
{
    int32_t p, f, a, c;
    if (d8 > (int32_t)(ARM_N - 4u) << 8) d8 = (int32_t)(ARM_N - 4u) << 8;
    p = (int32_t)arm_w - (d8 >> 8); f = d8 & 255;
    a = b[(uint32_t)(p - 1) & (ARM_N - 1u)];
    c = b[(uint32_t)(p - 2) & (ARM_N - 1u)];
    return a + (((c - a) * f) >> 8);
}


/* ---- 0.4 RADIO: between the stations, the static; near one, its whistle; on it, the wave */
static struct { int32_t hp, lp, lp2, crk, crkd, fade; uint32_t wph, fph; } rz;
static volatile int32_t vis_found;                  /* how well the dial sits on a sounding event (the screen) */
VFAST static void radio_block(int32_t *out, uint32_t n, int32_t found, int32_t dmin)
{
    uint32_t i, winc;
    int32_t lv = mul15(eng.radio, eng.stat), sl, wl;
    if (lv <= 0) return;
    sl = mul15(lv, 32767 - mul15(found, 29000));    /* the static gives way to the wave */
    wl = mul15(lv, mul15(found, 32767 - found) * 3);   /* the whistle: loudest half tuned */
    if (dmin < 0) dmin = -dmin;
    winc = (180u + (uint32_t)dmin * 3u / 16u) * 97391u;   /* the beat's pitch: the further, the higher */
    rz.fph += 1300u * n;                            /* QSB: the static fades in and out, slowly */
    for (i = 0; i < n; i++) {
        int32_t w = (int32_t)(xrand() >> 16) - 32768, x, y;
        rz.hp += (w - rz.hp) >> 3;                  /* a band: 300 .. 3000 Hz, as an AM set */
        x = w - rz.hp;
        rz.lp += (x - rz.lp) >> 1;
        rz.lp2 += (rz.lp - rz.lp2) >> 1;
        y = rz.lp2 >> 1;
        if (!rz.crkd && (xrand() & 2047u) == 0u) { rz.crk = (int32_t)(xrand() >> 17); rz.crkd = 40; }   /* a crackle */
        if (rz.crkd) { rz.crkd--; y += (rz.crkd & 1) ? rz.crk : -rz.crk; rz.crk -= rz.crk >> 3; }
        y = mul15(y, 20000 + (sin_q15(rz.fph) >> 2));
        y = mul15(y, sl);
        if (wl > 0) { rz.wph += winc; y += mul15(sin_q15(rz.wph), wl) >> 2; }
        out[2u * i] += y; out[2u * i + 1u] += y;
    }
}
VFAST static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t t0 = VTICKS(), v, b, i, shed = gov >= 1u, shed2 = gov >= 2u;   /* the governor: what to leave out this time */
    for (v = 0; v < NV; v++)
        if (ev_trig[v]) { ev_begin(v); ev_trig[v] = 0; vs[v].kill = 0; }
    {   /* the governor's voice cap: 4, then 3, then 2; the quietest goes first */
        uint32_t cap = gov >= 4u ? 2u : gov >= 3u ? 3u : NV, cnt = 0;
        for (v = 0; v < NV; v++) if (vs[v].stage != ST_OFF && !vs[v].kill) cnt++;
        while (cnt > cap) {
            int32_t lo = 0x7FFFFFFF;
            uint32_t w = NV;
            for (v = 0; v < NV; v++) if (vs[v].stage != ST_OFF && !vs[v].kill && vis[v].amp < lo) { lo = vis[v].amp; w = v; }
            if (w == NV) break;
            vs[w].kill = 32767;
            cnt--;
        }
    }
    for (b = 0; b < n; b += BLK) {
        int32_t *o = out + 2u * b;
        uint32_t m = n - b < BLK ? n - b : BLK;
        {   /* the LFO (MOD), once a block */
            uint32_t was = lfo_ph;
            lfo_ph += eng.lfo_inc;
            switch (eng.lfo_shape) {
            case 1: lfo_val = (int32_t)(lfo_ph >> 15) - 65536; lfo_val = (lfo_val < 0 ? -lfo_val : lfo_val) - 32768; break;
            case 2: lfo_val = lfo_ph >> 31 ? 32767 : -32767; break;
            case 3: if (lfo_ph < was) lfo_sh = (int32_t)(xrand() >> 17) * 2 - 32767; lfo_val = lfo_sh; break;
            default: lfo_val = sin_q15(lfo_ph); break;
            }
        }
        vis_strain_acc = 0;
        gmod[0] = gmod[1] = gmod[2] = 0;
        for (v = 0; v < NV; v++) ev_block(v, o, m);
        vis_strain = vis_strain_acc / (int32_t)m;
        if (!shed2) noise_block(o, m);
        if (!shed) sky_block(o, m);
        if (eng.radio) {                            /* the best tuned of the sounding events */
            int32_t f = 0, dm = 25600;
            for (v = 0; v < NV; v++) if (vs[v].stage != ST_OFF) {
                int32_t d = vs[v].e.station - eng.dial;
                if (vs[v].rsig > f) f = vs[v].rsig;
                if ((d < 0 ? -d : d) < (dm < 0 ? -dm : dm)) dm = d;
            }
            vis_found = f;
            radio_block(o, m, f, dm);
        }
    }
    {   /* the arms, the plate, the volume, a soft clip */
        int32_t fb = eng.i_fb, mix = eng.i_mix + (gmod[1] >> 3), len = eng.i_len8, onda = eng.i_onda + mul15(eng.i_onda, gmod[0] >> 3), h = vis_strain;
        if (mix < 0) mix = 0;
        if (mix > 32767) mix = 32767;
        int32_t dx = len + ((h * onda) >> 15), dy = ((len * 27853) >> 15) - ((h * onda) >> 15), rmix = eng.r_mix, dec = eng.r_dec;
        int32_t vol = eng.vol, pl = 0, pr = 0, cmix = eng.c_mix, emix = eng.e_mix, efb = eng.e_fb, ed8 = eng.e_d8, stw = eng.st_w, ck = eng.c_k;
        uint32_t apinc = eng.ap_inc;
        static int32_t pdx = 512, pdy = 512;        /* the arms' lengths glide across the block: no steps */
        int32_t sdx, sdy;
        if (dx < 512) dx = 512;
        if (dy < 512) dy = 512;
        sdx = (dx - pdx) / (int32_t)n; sdy = (dy - pdy) / (int32_t)n;
        for (i = 0; i < n; i++) {
            int32_t l = out[2u * i], r = out[2u * i + 1u], x, y, mono;
            mono = (l + r) >> 1;
            pdx += sdx; pdy += sdy;
            if (mix) {
                x = arm_read(arm_x, pdx); y = arm_read(arm_y, pdy);
                arm_lx += ((x - arm_lx) * 20000) >> 15;   /* the mirrors' losses */
                arm_ly += ((y - arm_ly) * 20000) >> 15;
                arm_x[arm_w] = (int16_t)sat15(mono + ((arm_lx * fb) >> 15));
                arm_y[arm_w] = (int16_t)sat15(mono + ((arm_ly * fb) >> 15));
                arm_w = (arm_w + 1u) & (ARM_N - 1u);
                l += (((x - (y >> 1)) - l) * mix) >> 15;      /* the dark port and the bright one, between the sides */
                r += (((y - (x >> 1)) - r) * mix) >> 15;
            }
#ifndef NO_SPAZIO
            if (cmix && !shed2) {                   /* CORO: two voices a side, 8 .. 16 ms, swaying in quadrature (0.47 Hz) */
                int32_t m1, m2, d1, d2, wl, wr;
                uint32_t pa, pb;
                ch_l[ch_w] = (int16_t)sat15(l); ch_r[ch_w] = (int16_t)sat15(r);
                ch_ph += 45775u;
                m1 = sin_q15(ch_ph); m2 = cos_q15(ch_ph);
                d1 = 135475 + mul15(m1, 45158); d2 = 135475 + mul15(m2, 45158);   /* Q8 frames: 12 +- 4 ms */
                pa = ch_w - (uint32_t)(d1 >> 8); pb = ch_w - (uint32_t)(d2 >> 8);
                wl = ch_r[pa & (CH_N - 1u)]; wl += (((int32_t)ch_r[(pa - 1u) & (CH_N - 1u)] - wl) * (d1 & 255)) >> 8;
                wr = ch_l[pb & (CH_N - 1u)]; wr += (((int32_t)ch_l[(pb - 1u) & (CH_N - 1u)] - wr) * (d2 & 255)) >> 8;
                ch_w = (ch_w + 1u) & (CH_N - 1u);
                l = mul15(l, 32767 - cmix / 3) + mul15(wl, cmix);   /* crossed: wide */
                r = mul15(r, 32767 - cmix / 3) + mul15(wr, cmix);
            }
            {   /* ECO: a ping-pong at 11025 Hz, one line, taps at D (left) and 2D (right), the return from 2D, darker each time */
                ec_acc += (l + r) >> 1;
                if (++ec_q >= 4u) {
                    int32_t in = ec_acc >> 2, tl, tr;
                    uint32_t q;
                    ec_q = 0; ec_acc = 0;
                    ec_d8 += (ed8 - ec_d8) >> 9;        /* a new time glides, as tape would */
                    q = ec_w - (uint32_t)(ec_d8 >> 8);
                    tl = ec_buf[q & (EC_N - 1u)]; tl += (((int32_t)ec_buf[(q - 1u) & (EC_N - 1u)] - tl) * (ec_d8 & 255)) >> 8;
                    q = ec_w - (uint32_t)(ec_d8 >> 7);
                    tr = ec_buf[q & (EC_N - 1u)]; tr += (((int32_t)ec_buf[(q - 1u) & (EC_N - 1u)] - tr) * ((ec_d8 << 1) & 255)) >> 8;
                    ec_lp += ((tr - ec_lp) * 20000) >> 15;
                    ec_buf[ec_w] = (int16_t)sat15(emix ? in + mul15(ec_lp, efb) : 0);
                    ec_w = (ec_w + 1u) & (EC_N - 1u);
                    ec_l0 = ec_l1; ec_l1 = tl; ec_r0 = ec_r1; ec_r1 = tr;
                }
                if (emix) {
                    int32_t f = (int32_t)ec_q * 8192;
                    l += mul15(ec_l0 + mul15(ec_l1 - ec_l0, f), emix);
                    r += mul15(ec_r0 + mul15(ec_r1 - ec_r0, f), emix);
                }
            }
#endif
            if (rmix) {
                if (!(rv_half ^= 1u)) {
                    reverb_frame((((l + r) >> 1) + rv_in_prev) >> 1, dec);
                    rv_ol_prev = rv_out_l; rv_or_prev = rv_out_r;
                }
                rv_in_prev = (l + r) >> 1;
                l += (rv_ol_prev * rmix) >> 15;
                r += (rv_or_prev * rmix) >> 15;
            }
            l = mul15(l, vol); r = mul15(r, vol);       /* (64-bit: a loud sum does not wrap) */
            if (stw != 32768 || apinc) {               /* 0.5 STEREO: the sides narrowed or widened; the autopan */
                int32_t m = (l + r) >> 1, sd = (int32_t)(((int64_t)((l - r) >> 1) * stw) >> 15);
                l = m + sd; r = m - sd;
                if (apinc) {
                    if (!(i & 15u)) {                   /* the pan's gains, every 16 frames, equal power */
                        uint32_t an;
                        ap_ph += apinc * 16u;
                        an = (uint32_t)(sin_q15(ap_ph) + 32768) << 14;   /* 0 .. a quarter turn */
                        ap_gl = mul15(cos_q15(an), 46341); ap_gr = mul15(sin_q15(an), 46341);   /* x sqrt 2: the middle as it was */
                    }
                    l = mul15(l, ap_gl); r = mul15(r, ap_gr);
                }
            }
            if (ck) {                                   /* COMP/LIMIT: a compressor (1.5 ms attack, 100 ms release), the gain anew every 16 frames */
                int32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r, pk = al > ar ? al : ar;
                if (pk > cp_env) cp_env += (pk - cp_env) >> 6; else cp_env -= ((cp_env - pk) >> 12) + (cp_env > 0);
                if (!(i & 15u)) {
                    int32_t lv = cp_env > 1 ? log2_q16((uint32_t)cp_env) : 0, gl2 = eng.c_mk;
                    if (lv > eng.c_thr) gl2 -= (int32_t)(((int64_t)(lv - eng.c_thr) * ck) >> 15);
                    cp_g1 = (int32_t)(exp2_q16(gl2) >> 1);   /* Q15 */
                    cp_d = (cp_g1 - cp_g) >> 4;
                }
                cp_g += cp_d;
                l = (int32_t)(((int64_t)l * cp_g) >> 15); r = (int32_t)(((int64_t)r * cp_g) >> 15);
                cp_red = cp_g;
            }
            {   /* 0.4.1: a limiter (0.5: at -1 dB; instant attack, 120 ms release): loud presets and big chords stay clean */
                int32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r, pk = al > ar ? al : ar;
                if (pk > lim_env) lim_env = pk; else lim_env -= (lim_env >> 12) + 1;
                if (lim_env > 29205) {
                    int32_t g = (int32_t)(29205u * 32768u / (uint32_t)lim_env);
                    l = mul15(l, g); r = mul15(r, g);
                    lim_hits++;
                }
            }
            out[2u * i] = sat15(l); out[2u * i + 1u] = sat15(r);
            if ((l < 0 ? -l : l) > pl) pl = l < 0 ? -l : l;
            if ((r < 0 ? -r : r) > pr) pr = r < 0 ? -r : r;
        }
        pdx = dx; pdy = dy;
        vis_lvl[0] = pl; vis_lvl[1] = pr;
    }
    audio_us = (VTICKS() - t0) / FM1_TICKS_PER_US;
    {   /* the governor: up a step when a block takes more than 75 % of its time (at once over 90 %, else at most every
         * 50 ms), down a step after 0.5 s under 55 % */
        static uint32_t up_wait;
        uint32_t lim = n * 1000u / 44u, pc = audio_us * 100u / (lim ? lim : 1u);   /* the block's time, us (5805 for 256) */
        if (up_wait) up_wait--;
        if (pc > 90u || (pc > 75u && !up_wait)) { if (gov < 4u) gov++; gov_calm = 0; up_wait = 8u; }
        else if (pc < 55u) { if (++gov_calm > 86u) { if (gov) gov--; gov_calm = 0; } }
        else gov_calm = 0;
    }
}
