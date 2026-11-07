# Mario Texture's VIRGO 0.5 – onde gravitazionali per l'M-VAVE FM-1

Firmware synth per l'FM-1: ogni nota è una coalescenza di buchi neri (inspiral → fusione → ringdown), 4 eventi insieme.
Fratello di [GRANA](https://github.com/mongio/grana-fm1), costruito sullo stesso impianto (Dinghy / Felucca), GPL-3.0.

*[English: README.md](README.md)*

> Firmware non ufficiale: non è fatto né approvato da M-VAVE, non è affiliato con la collaborazione Virgo. Si installa e
> si disinstalla via USB; per tornare al firmware originale reinstallalo con l'updater ufficiale o con
> l'[FM-1 Firmware Hub](https://fm1.designburgapps.com). Uso a tuo rischio.

## Installare
1. Dalla pagina [Releases](../../releases) scarica `virgo-editor-X.Y.html` e aprilo in **Chrome o Edge** (servono Web MIDI e WebUSB).
2. Prova VIRGO nel simulatore della pagina: è lo stesso codice che gira sull'FM-1. Nella pagina c'è anche la guida completa, con le foto di tutte le pagine.
3. Collega l'FM-1 via USB e premi **Installa VIRGO**; resta sulla scheda fino alla fine. Con la casella Bluetooth installi VIRGO + Bluetooth (SMC-Mixer).
4. I preset, i campioni e le impostazioni di GRANA restano in memoria e tornano quando reinstalli GRANA.

Soccorso: accendi l'FM-1 tenendo premuto **OCT−** (parte solo l'aggiornamento via USB) e reinstalla. Nel caso peggiore usa
il kit di pronto soccorso in [rescue/ di GRANA](https://github.com/mongio/grana-fm1/tree/main/rescue): rimette GRANA, poi
reinstalli VIRGO dal suo editor.

## File
- firmware/src/virgo.c          parametri, eventi, preset (48, a 0x93000), impostazioni (0x96000), catalogo, RUN
- firmware/src/virgo_dsp.c      il suono: chirp con i modi (2,2) (3,3) (4,4) (2,1), due polarizzazioni, ringdown a
                                risonatori complessi (modi quasi-normali di Kerr), rumore del rivelatore, cielo,
                                interferometro (bracci + Fabry-Perot), riverbero Dattorro a 22 kHz
- firmware/src/virgo_ui.c       lo schermo 240x240 antialiasato (vgfx.c, caratteri Inter da tools/gen_aafont.py)
- firmware/src/virgo_panel.c    pulsanti, MIDI, LEARN, clock, SysEx (banca utente come GRANA 1.6, preset 21..26)
- firmware/src/virgo_bt.c, virgo_smc.c   Bluetooth e SMC-Mixer (da GRANA)
- tools/gen_virgo_tables.py     tabelle (seno, log2/exp2, modi quasi-normali di Berti-Cardoso-Will, catalogo)
- web/virgo.template.html, web/make_virgo_page.py   la pagina: installazione, banca utente, guida

## Compilare
    ./build.sh --release 0.5          # build/virgo-0.5.fwsc (FM-1_975)
    ./build.sh --release 0.5 --bt     # build/virgo-0.5-bluetooth.fwsc (FM-1_985)
    python3 web/make_virgo_page.py 0.5
Prova sul computer: cc -O1 -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen -o build/host/virgo firmware/src/virgo.c,
poi build/host/virgo build/host/out script.txt (schermi PPM e WAV; build/host/ubank.bin come banca utente).

## Memoria
Preset e impostazioni in 0x93000..0x97000 (libera su FM-1): GRANA (preset a 0x97000, impostazioni a 0xFC000) resta intatto.
Banca utente condivisa con GRANA a 0x9B000. Codice 154 KB (206 KB con BT), RAM 28,6 KB (39,3 KB), pool 278 KB.
Carico audio misurato in fm1-emu: 2,1–2,7 ms su 5,8 con 4 eventi e tutti gli effetti.

## Novità 0.2
- Pagine nuove: ARMONIA e FUSIONI (HOME ×2, ×3), MOTORE (SEL ×2), FILTRO (FX, poi INTERFEROMETRO), MOD A e MOD B (LFO, poi CIELO). 15 pagine.
- Armonia dalla coalescenza: INTERVALLO dalle masse (secondo modo sull'intervallo del rapporto), SCALA (13, compresa DA ROTAZIONE), TONICA, CHIRP GLISSANDO / SCALA / ARMONICI.
- Fusioni gerarchiche: OFF / RUN / TASTI / RUN+TASTI. Ogni evento parte dal resto del precedente; l'accordo si muove nel Tonnetz (P, L, R secondo il rapporto delle masse), GRAVITÀ riporta alla tonica; oltre 150 M☉ ricomincia. TONFO (colpo della fusione), SUB.
- Motore FM: i quattro modi come operatori; algoritmi AUTO, BINARIA, SPIRALE, ANELLO, CAMPANA; indice ∝ v², massimo alla fusione; SATURAZIONE.
- Filtro RIVELATORE / BASSO / BANDA / ALTO che insegue il chirp; risonanza dal Q del ringdown; DISTANZA lo chiude.
- Due modulazioni: 9 sorgenti (strain, frequenza, inviluppo, fusione, precessione, orbita, LFO, caso, velocity) su 10 destinazioni.
- RUN: TEMPO ORBITE 2:3 (poliritmo da risonanza orbitale); con GERARCHIA RUN le note seguono l'accordo.
- Preset: formato 2 (64 parametri); quelli della 0.1 si convertono da soli. 10 di fabbrica (nuovi: TONNETZ, ORBITE 2:3).
- Editor: l'editor dei campioni di GRANA (forma d'onda, tagli, zoom, guadagno, dissolvenze, normalizza, HIFI/LOFI, 22/11 kHz, ascolto); apri e salva banche .grnb; la banca resta nel browser.
- SMC-Mixer: fader INDICE e FREQUENZA filtro al posto di MIX interferometro e CIELO; manopole 7–8 SATURAZIONE e RISONANZA; SELECT 1–8 sulle pagine nuove.

## Novità 0.3 – il motore sonoro
- MOTORE ha quattro motori nuovi oltre a ONDA e FM; il terzo knob cambia nome con il motore:
  - NEBULOSA (APERTURA): cinque seghe band-limited (polyBLEP); lo spread si chiude in unisono alla fusione e si riapre nella coda.
  - PIEGA (PIEGHE): wavefolder West Coast (sin(pi/2 G x)); le pieghe crescono come v^2 e si richiudono con il ringdown (come un low pass gate).
  - CORDA (BRILLANZA): corda Karplus-Strong accordata sulla nota del ringdown; il chirp a dente di sega la fa risuonare, la fusione la pizzica, i modi del resto la tengono viva.
  - VOCE (VOCALE): due seghe in tre formanti (u o a e i) con vibrato e un filo di respiro; la vocale si apre verso la fusione e si richiude.
  - Con i motori l'inspiral sale come f^(1/3) (presente da subito); dopo la fusione il motore scivola sulla nota e segue l'inviluppo del modo (2,2,0); i modi restano sotto, più piano.
- Pagina nuova SPAZIO (GLO ×2): CORO (ensemble a due voci incrociate), ECO (ping-pong a 11025 Hz, 1/16 … 1/4., a tempo, anche col clock MIDI), TEMPO ECO, RITORNO.
- Più corpo per tutti: CODA ora è il livello di sostegno (fino al 77 %); filtro passa-banda con Q al massimo ~9 e +3,5 dB; preset di fabbrica con il filtro vicino alla nota.
- Corretti: coseno sfasato della 0.2 (ringdown e FM leggermente stonati), salto di livello del filtro alla fusione, click della voce INTERVALLO alla fusione, click delle note ribattute in CHIRP SCALA, click delle voci rubate (coda di 3 ms), overflow del volume con somme forti.
- Preset di fabbrica nuovi: NEBULOSA, MAREA (PIEGA), CORDA NERA, CORO VIRGO (posti 11–14, se liberi). I preset della 0.2 si caricano con SPAZIO a CORO 30.
- CPU (fm1-emu, 4 voci tenute): FM 3,4 ms, NEBULOSA 3,9 ms, VOCE 3,9 ms su 5,8. Il cielo ora gira a 22 kHz; se un blocco supera 4,4 ms il successivo salta il cielo.

## Novità 0.4
- RIVERBERO denso e lungo: sei diffusori in ingresso, quattro allpass in più nella vasca, vasca modulata (niente metallo nelle code lunghe); CODA RIV. fino a 0,998 (code di minuti). Pagina RIVERBERO (GLO ×2): DENSITÀ, BRILLANZA, MOVIMENTO, PRE-DELAY (0–100 ms). Attenzione: CODA RIV. ora allunga molto di più a parità di valore.
- FINALE (SEL ×2): FINALE (il tempo del chirp piegato: IMPROVVISO … FISICO … DISTESO), PICCO (×0,5 … ×2 la frequenza della fusione), SOSPENSIONE (la cima tenuta fino a 8 s), APPRODO (scivolo dalla fusione alla nota, fino a 1,5 s, anche per ONDA e FM: i risonatori glissano). DURATA fino a 3 minuti (125).
- DÉJÀ VU (ARP ×2), alla Marbles: DÉJÀ VU (0 caso, 50 bloccato, oltre 50 salti nel giro), LUNGHEZZA 1–16, DISPERSIONE, SU (RUN / RUN+TASTI). Vale per note, masse, distanza, rotazione e ritmo di RUN. ARP ora alterna RUN e DÉJÀ VU; RUN si accende con PLAY.
- RADIO (ENV ×2): ogni evento ha una stazione (massa di chirp 1–100 M☉). SINTONIA lontana: statica (fruscio AM, crepitii, evanescenza); vicino: fischio di battimento e onda stonata; sulla stazione: onda trovata. RADIO, SINTONIA, SELETTIVITÀ, STATICA.
- Preset formato 3 (80 parametri in 96); quelli di 0.1, 0.2 e 0.3 si convertono da soli. Carattere É aggiunto.
- CPU (fm1-emu, 4 voci tenute): FM 3,8 ms, NEBULOSA 4,2 ms su 5,8. Sopra 4,4 ms il blocco dopo salta il cielo; sopra 4,9 anche rumore e coro.

## Novità 0.4.1 – processore e volume
- Il codice audio e le sue tabelle (seno, log2, exp2) girano dalla RAM interna (sezione .fast_text in RAMTEXT, che ora usa anche 0x01C06000–0x01C08000), non più dalla flash attraverso la cache. Le funzioni piccole sono always_inline: tra RAM e flash non ci sono chiamate dirette (troppo lontane per un call).
- Governatore: se un blocco audio supera il 75 % del suo tempo (90 %: subito), i blocchi dopo lasciano il CIELO e il CORO; poi il RUMORE e i modi sotto i motori; poi una voce, poi due (sfumano in 12 ms). Torna indietro un passo ogni mezzo secondo sotto il 55 %. Sotto le quattro lucine dell'header: linea ambra = il processore sta riducendo.
- Limitatore in uscita a −3 dB (attacco istantaneo, rilascio 120 ms) e 3 dB in meno di volume generale; la CORDA 4 dB più bassa. Linea rossa nell'header = il limitatore sta lavorando. Pagina MIDI: AUDIO, RIDUZIONE, LIMITATORE.
- Corretto: con NEBULOSA, PIEGA e VOCE i modi sotto il motore facevano un'inutile modulazione FM (CAMPANA) con valori non inizializzati: più CPU e un po' di sporco.
- I modi spenti non si calcolano più; il microsismo calcola il suo seno una volta per blocco.
- CAMPIONE: mentre giri SUONO o INIZIO il campione si ricarica solo quando la manopola si ferma (200 ms): prima si decodificava tutto a ogni scatto.

## Novità 0.5 – effetti riordinati, shimmer, audio
- FX ora contiene tutti gli effetti: FILTRO → INTERFEROMETRO → RIVERBERO (RIVERBERO, CODA RIV., DENSITÀ, BRILLANZA) → SHIMMER → SPAZIO.
- SHIMMER (FX ×4): due testine dentro la vasca leggono il suono trasposto e lo rimettono nel giro. TIPO: OTTAVA, QUINTA, KERR (l'intervallo fra i primi due modi del buco nero, cambia con la ROTAZIONE), REDSHIFT (scende di ottava), SPIRALE (semitoni in una scala che gira). Nella stessa pagina MOVIMENTO e PRE-DELAY del riverbero. Il guadagno dello shimmer e la coda del riverbero sono legati (dec² + g² ≤ 0,995): la coda non esplode neanche al massimo.
- GLO ora è AUDIO → MIDI (→ BLUETOOTH). AUDIO: VOLUME, BPM, COMP/LIMIT (compressore morbido: la manopola abbassa la soglia e alza il rapporto, con guadagno di compensazione; OFF = solo limitatore), STEREO (MONO, STRETTO, NORMALE, LARGO fino al 160 %; oltre, AUTOPAN da 0,05 a ~8 Hz, a potenza costante). La pagina mostra livelli, riduzione del compressore, campo stereo con il punto dell'autopan, BPM.
- Il BPM è il tempo di RUN (se il clock MIDI non arriva) e dell'ECO di SPAZIO.
- HOLD: HOME tenuto 0,6 s gira HOLD (le code restano) → HOLD INFINITO (le code si fermano e non calano più) → spento. Il pedale di sustain (CC 64) fa HOLD.
- SMC-Mixer: MUTE 1 HOLD, MUTE 2 HOLD INFINITO, MUTE 3–8 aprono FINALE, RADIO, DÉJÀ VU, RIVERBERO, SHIMMER, SPAZIO.
- Schermata iniziale: "Mario Texture's" sopra VIRGO.
- Riverbero: la vasca lavora 12 dB più in basso (ingresso ÷4, uscita ×4). Prima con code lunghe e accordi forti la vasca poteva saturare internamente: è la causa probabile dei rari click di saturazione della 0.4.1. Limitatore finale a −1 dB (era −3; il volume generale resta 3 dB sotto la 0.4).
- Preset: 84 parametri; quelli di 0.4 prendono shimmer spento, COMP/LIMIT al 20 % (leggero), stereo NORMALE. Alcuni preset di fabbrica (CIELO LISA, CORO VIRGO, NEBULOSA, CORDA NERA, TONNETZ) hanno lo shimmer acceso.
