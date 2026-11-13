# GESTALT for Digitakt

A master effect chain for the **Elektron Digitakt mk1** (OS 1.53 and 1.54), as an
[elekloader](https://github.com/irpina/elekloader) mod (id `gestalt-dt`): GESTALT is a mash-up of PUMP
and RADS, Geiger Counter-style wave table destruction (RADS, after RADS for Max for Live, in stereo) into
an SP-303 Vinyl Sim-style levelling compressor (PUMP), on everything the Digitakt plays. It takes the
place of the Digitakt's stock compressor: its page among the master pages, its eight parameters for its
settings, and its time in the audio render. It is on every output: main outs, headphones and USB.

By Robbie. GPL-2.0-or-later.

**This is the Digitakt version.** For the **Digitone mk1**, use [GESTALT for Digitone](https://github.com/ClaidArremer/digitone-gestalt) (from 5.0 a
wavefolder in RADS's place, into the same compressor, on its own master page). The files here do not run on a Digitone, and the Digitone's do not run on a
Digitakt. It replaces [PUMP for Digitakt](https://github.com/ClaidArremer/digitakt-pump): use one or the other.

> Unofficial and unsupported. Elektron, Digitakt and Digitone are trademarks of Elektron Music
> Machines; WMD and Geiger Counter are WMD's. Nothing here is affiliated with or endorsed by them.

## What you need

- A Digitakt mk1 on OS 1.53 or 1.54, and the stock OS file for it.
- elekloader with core 4.1 for the Digitakt (`core-4.1-os1.54.elemod` or the 1.53 one).

## Install

Add `gestalt-dt-0.2-os1.54.elemod` (or the 1.53 file) in the elekloader app together with core 4.1,
build, and send the firmware to the Digitakt. Leave PUMP for Digitakt out: GESTALT takes its place.

## The page

**FUNC + LFO** opens the master pages: GESTALT is first, where the stock compressor was. The knobs sit
as on the page: A–D on the top row (RADS), E–H below (the compressor and the output). Settings are saved
with each pattern's kit, as the stock compressor's were (or globally, with SETTINGS > GLOBAL FX/MIX's
compressor set to global).

| Knob | Name | What it does |
|---|---|---|
| A | TABLE | OFF, then the wave table: 512 of GESTALT's own, in 32 families of 16 (see [The tables](#the-tables)). Each change crossfades (about 14 ms). OFF takes RADS out of the chain |
| B | DRIVE | The level into RADS's 8-bit converter, −12 to +51.5 dB (0 dB to start). RADS evens its output back to the dry level |
| C | SRR | Sample rate reduction: 200 Hz up to 48 kHz (OFF) |
| D | BLEND | RADS's dry / wet, OFF to 100 %, before the compressor |
| E | COMP | OFF, then compression: threshold, ratio (with RATIO on AUTO) and make-up together. 80 to start |
| F | RATIO | AUTO, 2:1, 3:1, 4:1, 8:1, 10:1, 20:1, LIMIT |
| G | REL | Release, more is slower: PUMP (fast, breathing) through GLUE to LEVEL (slow, smooth) |
| H | OUT | The chain's output level before the limiter: −32 to +6 dB. Works with COMP OFF too |

Fixed: attack 6, no WRM, no parallel MIX (as on the Digitone).

- **Press a knob and turn**: TABLE 16 tables at a time, the others in 8s.
- **FUNC + turn** snaps: TABLE 16 tables; DRIVE −12, −6, 0, +6, +12, +24, +36, +48 dB; SRR 1k, 2k, 4k, 8k,
  12k, 16k, 24k, OFF; BLEND OFF, 25, 50, 75, 100 %; COMP OFF, 32, 64, 80, 96, 127; REL 0, 32, 64, 96, 127;
  OUT −24, −12, −6, −3, 0, +3, +6 dB.
- **FUNC + REC / STOP / PLAY** copies, pastes or clears the page's settings (clear: the starting values),
  with a drop-down saying so. The stock CLEAR (FUNC + held LFO + PLAY) clears them too.
- The screen on the left shows the wave table's curve (OFF with TABLE OFF; nearer a straight line as BLEND
  comes down), bouncing with the output's level. The **GR** meter shows the gain reduction.
- **YES**: the curve across the page, then a graph of the last 2.8 s of gain reduction, then back.
  **FUNC + YES** is the stock TEMP SAVE.
- Every change shows the stock drop-down message, e.g. *Ratio=4:1*, *Drive=+6dB*.
- **SETTINGS > GLOBAL FX/MIX > FX ANIMATION**: untick it for a still page.
- Starting values: TABLE OFF, DRIVE 0 dB, SRR OFF, BLEND 100 %, COMP 80, RATIO AUTO, REL 64, OUT 0 dB.
  Kits saved under PUMP for Digitakt keep its CMP, REL, RAT and OUT (PUMP off: COMP OFF); RADS starts OFF.

## How the settings are kept

The stock compressor's eight parameters (THR, ATK, REL, MUP, RAT, SCS, SCF, MIX) hold GESTALT's nine
settings. They are 8.8 fixed-point words that the firmware stores as they are (its edit only clamps them to
each parameter's range), so TABLE and SRR use their fractions: TABLE in ATK (63 a table), SRR in SCS (2.0 and
up, which also marks the kit as GESTALT's; PUMP for Digitakt used 0 and 1 there). The render hands the page
a smoothed copy of the words that glides to each new value, so the page reads them only once they have
stood still, and works its edits out from its own copy of the kit.

## The tables

GESTALT's 512 wave tables are its own, worked out from simple maths in `tools/make_tables.py` (random
families use fixed seeds, so every build is the same). They come in 32 families of 16, each running from mild
to wild, so press-and-turn or FUNC + turn on TABLE (16 at a time) jumps a family. 00–FF lean musical,
00.–FF. lean broken.

| Tables | Family | |
|---|---|---|
| 00–0F | WARM | a soft drive, harder each step |
| 10–1F | SINE FOLD | more folds each step |
| 20–2F | TRI FOLD | the same with straight edges |
| 30–3F | LOPSIDED | even harmonics growing |
| 40–4F | CLIP | hard clipping, rising drive |
| 50–5F | STAIRS | fewer levels each step, 32 down to 1 |
| 60–6F | HARMONICS | the 2nd to the 17th harmonic over the line |
| 70–7F | WRAP | overdriven and wrapped round |
| 80–8F | RECTIFY | from the line to full-wave |
| 90–9F | CROSSOVER | a dead zone round the middle, widening |
| A0–AF | RIPPLE | a sine riding on the line |
| B0–BF | EXPAND | from an expander, through the line, to a compressor |
| C0–CF | DOUBLE FOLD | a sine of a sine |
| D0–DF | PULSE | a comparator with more edges each step |
| E0–EF | BELLS | stacked partials |
| F0–FF | CHIRP | folds that speed up away from the middle |
| 00.–0F. | XOR | bits flipped, more each step |
| 10.–1F. | AND | low bits dropped in uneven patterns |
| 20.–2F. | GRAY | Gray code, blended in |
| 30.–3F. | BIT REVERSE | the low bits reversed, more each step |
| 40.–4F. | DRIFT | smooth random curves round the line |
| 50.–5F. | JUMBLE | random steps, fewer and bigger |
| 60.–6F. | SHATTER | the line with random spikes |
| 70.–7F. | HOLES | bands of silence cut out of a soft drive |
| 80.–8F. | SKEW WRAP | wraps with a sliding fold point |
| 90.–9F. | ARCHES | chains of parabolas |
| A0.–AF. | INTERLEAVE | odd and even values pulled apart |
| B0.–BF. | SINC | a ringing curve, tighter each step |
| C0.–CF. | PARTIALS | a few sines with random phases |
| D0.–DF. | FRACTAL | a triangle fold folded again |
| E0.–EF. | LEAN | stairs that lean |
| F0.–FF. | NOISE | noise fading into a clean sine fold |

GESTALT was first inspired by WMD's Geiger Counter, but it uses none of WMD's tables.

## Memory and CPU

The Digitakt's mods share 256 KB of RAM, and the 512 tables take 128 KB as they are, so here they are
packed (`tools/pack_tables.py`: each table's steps in a Huffman code, 74 KB) and the table TABLE asks for is
unpacked, on the UI side, into a small buffer. GESTALT takes about 107 KB, and the usual build (DIGISEQ,
GLIDE, DH-101, Digi Mono, digihealth, LOST LVLS splash, Direct Jump) leaves about 25 KB free.

The audio code is GESTALT for Digitone's: both sample loops in assembly (`src/radsloop.s`, `src/pumploop.s`),
checked bit for bit against the C. About 4,900 instructions a 32-frame block with both halves on; each costs
nothing while off. The stock compressor no longer runs.

## Building

```
python -m elekloader.sdk.build . --stock Digitakt_OS1.54.syx
```

`test/dt_host.c` runs the chain on a computer with the sanitizers; `test/unpack_test.c` checks every
packed table against the original. `tools/make_tables.py` makes the tables (and rewrites them in
`src/rads_data.h`); `tools/pack_tables.py` then packs them into `src/rads_pack.h`.

## Credits and licence

GPL-2.0-or-later. The SETTINGS row (`src/pumpdtset.s`) is adapted from irpina's digihealth
(GPL-2.0-or-later). Everything else is new.
