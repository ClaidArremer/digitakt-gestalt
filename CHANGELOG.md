# Changelog: GESTALT for Digitakt

## 0.3 (2026-11-13)

- **GESTALT's own wave tables.** All 512 are new, made from simple maths by `tools/make_tables.py`: 32
  families of 16, each from mild to wild, 00–FF musical (drive, folds, clipping, stairs, harmonics…) and
  00.–FF. broken (bit masks, random curves, holes, noise…). None of WMD's tables are used.
- Packed, the tables take 74 KB (was 85 KB).
- Patterns saved with a table number keep it, but that number now plays the new table.

## 0.2 (2026-11-07)

- RATIO: 8:1 in place of 6:1 (AUTO, 2:1, 3:1, 4:1, 8:1, 10:1, 20:1, LIMIT). Kits saved at 6:1 come back
  at 8:1.

## 0.1 (2026-11-07)

- First version: GESTALT for Digitone 4.5's chain (RADS in stereo into PUMP) on the Digitakt mk1, OS 1.53
  and 1.54, in place of the stock compressor, as PUMP for Digitakt was.
- Settings in the stock compressor's eight parameters, TABLE and SRR in their fractions; kits saved under
  PUMP for Digitakt keep its compressor settings.
- The 512 tables packed to fit the Digitakt's 256 KB for mods, unpacked into a buffer as TABLE asks.
- RATIO in PUMP for Digitakt's eight steps; the page, snaps, drop-downs, copy / paste / clear and views as
  on the Digitone.
