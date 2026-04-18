# Stroke Classification and Data Sources

Research on Chinese character stroke types, their classification, and available
machine-readable datasets for stroke-based disambiguation.

## The Seven Stroke Classes Used by Predictable Pinyin

Chinese characters are composed of strokes that textbooks traditionally group
into 5 basic types (横、竖、撇、捺、折). Predictable Pinyin extends this to
**seven** classes by splitting 点 (diǎn) out of 捺 (nà) and adding 提 (tí):

| # | Chinese | Pinyin | Shape               | Key | Visual |
|---|---------|--------|---------------------|-----|--------|
| 1 | 横      | héng   | Horizontal          | `h` | `—`    |
| 2 | 竖      | shù    | Vertical            | `s` | `丨`   |
| 3 | 撇      | piě    | Left-falling        | `p` | `/`    |
| 4 | 捺      | nà     | Right-falling       | `n` | `\`    |
| 5 | 点      | diǎn   | Dot                 | `d` | `丶`   |
| 6 | 提      | tí     | Rising / up-hook    | `t` | `㇀`   |
| 7 | 折      | zhé    | Turning / hook      | `z` | `㇠`   |

Separating `d`/`t` from `n` gives a finer-grained disambiguation signal: many
characters that share the same 捺 layout differ only in whether a terminal
stroke is a proper 捺, a small dot, or a rising flick.

## Data Strategy

The stroke dictionary shipped in `data/raw/stroke.dict.yaml` is **generated
locally** from [cnchar](https://github.com/theajack/cnchar) (MIT) rather than
downloaded from `rime-stroke` (LGPL-3.0). This keeps the shipped artefact
compatible with Predictable Pinyin's Apache-2.0 license.

- **Upstream source**: `theajack/cnchar`, pinned to commit
  `d02588ecc61a5ca9d594288e92d1bb6553b415c2`, file
  `src/cnchar/plugin/order/dict/stroke-order-jian.json`
  (sha256 `412e3beb587aa5e8b423655226b956eaef0496e8c06e486d7584b2f3e1dbcdcc`).
- **Generator**: [scripts/bin/build_stroke_map.dart](../scripts/bin/build_stroke_map.dart)
  downloads the pinned JSON, verifies its SHA256, applies the collapse table
  below, and writes `data/raw/stroke.dict.yaml` in the same TSV format that
  `src/stroke_filter.cc` already parses. The output is git-ignored and only
  regenerated when it is missing (delete the file to force a rebuild).
- **Entrypoint**: `scripts/download-data.sh` runs the Dart generator after it
  finishes downloading the other data files.

### Coverage

cnchar's simplified table covers **~6,939 simplified characters**, which
comfortably exceeds the ~9,900-character hanziDB frequency table for the
subset that matters in practice: every character that ships in our pinyin
dictionary has stroke data. Rare CJK extensions outside cnchar's table simply
won't participate in stroke filtering, which is acceptable — they fall back to
pinyin-only ranking.

### cnchar's 27-letter alphabet → our 7-letter alphabet

cnchar internally encodes stroke shapes with a 27-letter alphabet (a..z plus
variants) that captures CJK stroke geometry. The generator collapses this to
our 7-letter alphabet:

| cnchar letter | Meaning in cnchar       | Predictable Pinyin class |
|---------------|-------------------------|--------------------------|
| `j`           | 横 (horizontal)         | `h`                      |
| `f`           | 竖 (vertical)           | `s`                      |
| `s`           | 撇 (left-falling)       | `p`                      |
| `l`           | 捺 (right-falling, dot) | `n`                      |
| `k`           | 点 (dot)                | `d`                      |
| `d`, `i`      | 提 / 挑 (rising hooks)  | `t`                      |
| everything else | 折 variants (all hooks/turns: 横折, 竖钩, 斜钩, 竖弯, 横折弯钩, …) | `z` |

The collapse rules are encoded inline in
[scripts/bin/build_stroke_map.dart](../scripts/bin/build_stroke_map.dart) —
see that file for the authoritative mapping.

### Example encodings (post-collapse)

```
一   h             (single horizontal stroke)
人   pn            (left-falling + right-falling)
大   hpn           (horizontal + left-falling + right-falling)
中   szhs          (vertical + turning + horizontal + vertical)
你   pspdznn       (per cnchar, after collapse)
```

## Alternative datasets (not used)

These were considered but not adopted:

### rime-stroke (LGPL-3.0)

- **Repo**: https://github.com/rime/rime-stroke
- **Coverage**: ~170,000 entries through CJK Ext J
- **License**: LGPL-3.0 — incompatible with Apache-2.0 shipping without
  preserving LGPL notices and offering source / swap rights. We avoid this by
  generating our own dict from cnchar instead.

### Make Me a Hanzi (LGPL-3.0)

- **Repo**: https://github.com/skishore/makemeahanzi
- Provides SVG geometry; stroke class would have to be derived from paths.
  Also LGPL-3.0.

### stroke-input/stroke-input-data

- **Repo**: https://github.com/stroke-input/stroke-input-data
- **Coverage**: ~28,000 entries. Encoded with 5 numeric stroke classes. Smaller
  than cnchar and lacks a machine-readable collapse table we can audit inline.

### feiandxs/chineseStrokeJson

- **Repo**: https://github.com/feiandxs/chineseStrokeJson
- **Coverage**: ~20,000 characters. Convenient JSON, but license/provenance is
  less clear than cnchar.

## References

- [cnchar](https://github.com/theajack/cnchar) — MIT, primary source
- [Rime Stroke Schema](https://github.com/rime/rime-stroke) — LGPL-3.0, reference
- [Wikipedia: Stroke count method](https://en.wikipedia.org/wiki/Stroke_count_method)
