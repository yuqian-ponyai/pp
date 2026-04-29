# Character Database (hanziDB)

Research on the hanziDB.csv dataset and how it is used for character metadata
in Predictable Pinyin.

## Overview

Predictable Pinyin now preserves Rime's candidate order after pinyin and stroke
filtering. hanziDB remains useful for frequency metadata and primary pinyin
readings, but it is no longer used to reorder normal Rime candidates.

## Dataset: hanziDB.csv

- **Repo**: https://github.com/ruddfawcett/hanziDB.csv
- **File**: `hanzi_db.csv` (note: underscore in actual filename)
- **Source**: Based on Jun Da's Modern Chinese Character Frequency List (simplified)
- **Size**: ~9,933 characters

### CSV Columns

| Column | Description | Example |
|--------|-------------|---------|
| `frequency_rank` | Rank from most to least common (1 = most frequent) | 1 |
| `character` | The Chinese character | 的 |
| `pinyin` | Romanized pronunciation | de |
| `definition` | English definition | "possessive, adjectival suffix" |
| `radical` | Kangxi radical | 白 |
| `radical_code` | Radical number.extra_strokes | 106.3 |
| `stroke_count` | Total number of strokes | 8 |
| `hsk_level` | HSK proficiency level (1-6) | 1 |
| `general_standard_num` | Index in General Standard Chinese Characters table | 1155 |

### Sample Rows

```csv
frequency_rank,character,pinyin,definition,radical,radical_code,stroke_count,hsk_level,general_standard_num
1,的,de,"possessive, adjectival suffix",白,106.3,8,1,1155
2,一,yī,"one; a, an; alone",一,1.0,1,1,0001
```

## How Candidate Ordering Works

The core principle of Predictable Pinyin is **deterministic ordering**: for any given
pinyin + stroke combination, the candidate list is always in the same order. This
lets users memorize which candidate position a character occupies and type without
looking at the candidate list.

### Ordering Algorithm

1. User types pinyin → Rime returns candidates in its default order
2. User types strokes → filter to candidates whose per-character stroke sequence matches
3. Preserve Rime order for the remaining candidates
4. If strokes exactly match a single character, stably promote that exact match
   above prefix-only matches

### Why Preserve Rime Order

Rime's default dictionaries already encode pinyin-specific ordering, including
multi-reading characters (多音字). A global hanziDB frequency rank can be wrong for
a specific pronunciation, such as putting 说 above 月 for `yue`. Preserving Rime
order avoids that class of error while stroke filtering keeps the list predictable.

### Multi-Reading Characters

Many characters have multiple pinyin readings (多音字). For example:

- 的: dì, de, dí
- 了: le, liǎo
- 行: háng, xíng

The hanziDB `pinyin` column contains only one reading per row. Multi-reading
characters are handled by loading supplementary pinyin data from
`data/raw/pinyin_simp.dict.yaml` (snapshotted from Rime's pinyin_simp dictionary).
`FrequencySorter` stores all known readings per character and checks all of them
during pinyin matching, so e.g. 的 is found for both `de` and `di`, and 沈 is
found for both `chen` and `shen`. The approach:

1. `LoadFromCsv` loads the primary reading from hanziDB
2. `LoadSupplementaryPinyin` adds additional readings from the Rime dictionary
3. `MatchesPinyin` and `CharactersForSyllable` check all stored readings
4. Rime remains the source of displayed candidate order

## Data Limitations

1. **Simplified characters only**: hanziDB is based on simplified Chinese. This is
   fine for the initial implementation but may need extension for traditional Chinese
   support later.

2. **~10K characters**: hanziDB covers about 10,000 characters. Rime's built-in
   pinyin dictionary covers many more. Characters outside hanziDB will need a fallback
   ordering strategy.

3. **Single pinyin per entry**: Each row has one pronunciation. The Rime pinyin
   dictionary is the better source for pinyin→character mapping, so supplementary
   Rime readings are loaded for 多音字 support.

## Quantitative Analysis

### Dataset Size
- **9,900 characters** (after header row)
- **9,812** have tone-marked pinyin; **88** have toneless (e.g., 的=de, 了=le, 们=men)

### Pinyin Distribution (toneless)
- **403 unique toneless syllables** in hanziDB
- Most populated: `yi` (157 chars), `ji` (148), `yu` (128), `zhi` (116), `fu` (116)
- 38 syllables have >50 characters — these benefit most from stroke disambiguation
- 10 syllables have exactly 1 character — strokes unnecessary for these

### Stroke Disambiguation Effectiveness

Analysis of how many strokes (using all 5 types: h/s/p/n/z) are needed to make each
character the first (highest-frequency) candidate among its pinyin homonyms:

| Strokes needed | Characters | Percentage |
|---------------|-----------|------------|
| 0 | 403 | 4.1% |
| 1 | 1,058 | 10.7% |
| 2 | 1,953 | 19.7% |
| 3 | 1,956 | 19.8% |
| 4 | 1,351 | 13.6% |
| 5 | 1,094 | 11.1% |
| 6+ | 1,814 | 18.3% |
| Never (stroke-indistinguishable) | 271 | 2.7% |

**By frequency rank:**

| Rank range | Chars | Avg strokes | 0-stroke % |
|-----------|-------|-------------|------------|
| Top 500 | 500 | 1.0 | 46.2% |
| 501-1000 | 500 | 1.6 | 18.0% |
| 1001-3000 | 2,000 | 2.6 | 3.7% |
| 3001-9900 | 6,900 | 4.4 | 0.1% |

**Key insight**: Common characters (top 500) need on average just 1 stroke for
disambiguation. The top 1,000 characters cover the vast majority of daily typing,
and those average 1.3 strokes. This validates the input method's design — stroke
disambiguation is effective and fast for common characters.

The 271 characters (2.7%) that can never be uniquely identified by strokes alone
would require the J/K/L/F/D candidate navigation (step 5-7 in the README).

## Integration Plan

1. **Snapshot** `hanzi_db.csv` into the project repository at a specific commit
2. **Build a frequency lookup table**: `character → frequency_rank` as a TSV file
   for efficient runtime loading
3. **Use Rime's pinyin dictionary** for the actual pinyin→character translation
4. **Implement a custom C++ filter** (in a forked Rime client) to narrow candidates
   by pinyin and strokes while preserving Rime order
5. **Combine with stroke data** from rime-stroke to implement stroke filtering

## Alternative: Build a Custom Combined Dictionary

Instead of using Rime's default pinyin dictionary + a separate custom filter for
reordering, we could build a single custom dictionary that:

- Contains all characters with their pinyin encodings
- Has weights pre-set to match hanziDB frequency_rank
- Includes stroke data as auxiliary encoding

This would be more performant but harder to maintain. The separate filter approach
is recommended for the initial implementation.

## References

- [hanziDB.csv repo](https://github.com/ruddfawcett/hanziDB.csv)
- [hanziDB.org](http://hanzidb.org/about)
- [Jun Da's Chinese Character Frequency List](https://lingua.mtsu.edu/chinese-computing/statistics/char/list.php)
