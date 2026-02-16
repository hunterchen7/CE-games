# TI-84 CE Chess Engine Benchmark Results

Benchmarked on eZ80 @ 48 MHz (cycle-accurate emulator).
5 test positions, averaged over 1000 iterations per position.

## Commits

| #   | Commit    | Optimization                               |
| --- | --------- | ------------------------------------------ |
| 0   | `6198639` | **Baseline** (pre-optimizations)           |
| 1   | `1b2436d` | O(1) piece-list updates (square index map) |
| 2   | `898f712` | Staged movegen (captures then quiets)      |
| 3   | `09a9d02` | Skip castling attack probes when in check  |
| 4   | `0868f82` | Precompute check/pin info                  |
| 5   | `b8d7cf9` | Track bishop counts incrementally          |

## Memory

| Commit         | `board_t` | `undo_t` | `move_t` |
| -------------- | --------- | -------- | -------- |
| baseline       | 186 B     | 14 B     | 3 B      |
| piece_index    | 314 B     | 14 B     | 3 B      |
| staged_movegen | 314 B     | 14 B     | 3 B      |
| castle_skip    | 314 B     | 14 B     | 3 B      |
| check_pin      | 314 B     | 14 B     | 3 B      |
| bishop_count   | 316 B     | 14 B     | 3 B      |

## Movegen (avg cy/call, 5 positions x 1000 iters)

| Commit         | P0     | P1     | P2      | P3     | P4      | **Avg**    | Delta          |
| -------------- | ------ | ------ | ------- | ------ | ------- | ---------- | -------------- |
| baseline       | 77,620 | 86,886 | 115,007 | 87,781 | 94,848  | **92,428** | —              |
| piece_index    | 77,616 | 86,886 | 115,003 | 87,781 | 94,844  | **92,426** | -2 (-0.0%)     |
| staged_movegen | 77,616 | 86,886 | 115,003 | 87,781 | 94,844  | **92,426** | -2 (-0.0%)     |
| castle_skip    | 85,264 | 94,644 | 115,095 | 95,579 | 102,520 | **98,620** | +6,192 (+6.7%) |
| check_pin      | 85,264 | 94,644 | 115,095 | 95,579 | 102,520 | **98,620** | +6,192 (+6.7%) |
| bishop_count   | 85,264 | 94,644 | 115,095 | 95,579 | 102,520 | **98,620** | +6,192 (+6.7%) |

## Make/Unmake (avg cy/pair, 5 positions x 1000 iters)

| Commit         | P0     | P1     | P2     | P3     | P4     | **Avg**    | Delta        |
| -------------- | ------ | ------ | ------ | ------ | ------ | ---------- | ------------ |
| baseline       | 14,412 | 16,779 | 14,467 | 16,779 | 23,593 | **17,206** | —            |
| piece_index    | 14,724 | 17,091 | 14,779 | 17,091 | 20,714 | **16,880** | -326 (-1.9%) |
| staged_movegen | 14,724 | 17,091 | 14,779 | 17,091 | 20,714 | **16,880** | -326 (-1.9%) |
| castle_skip    | 14,724 | 17,091 | 14,779 | 17,091 | 20,714 | **16,880** | -326 (-1.9%) |
| check_pin      | 14,724 | 17,091 | 14,779 | 17,091 | 20,714 | **16,880** | -326 (-1.9%) |
| bishop_count   | 14,786 | 17,153 | 14,841 | 17,153 | 20,907 | **16,968** | -238 (-1.4%) |

## Eval (avg cy/call, 5 positions x 1000 iters)

| Commit         | P0      | P1      | P2      | P3      | P4      | **Avg**     | Delta          |
| -------------- | ------- | ------- | ------- | ------- | ------- | ----------- | -------------- |
| baseline       | 135,084 | 149,067 | 163,343 | 158,146 | 152,963 | **151,720** | —              |
| piece_index    | 135,088 | 149,071 | 163,347 | 158,150 | 152,967 | **151,724** | +4 (+0.0%)     |
| staged_movegen | 135,088 | 149,071 | 163,347 | 158,150 | 152,967 | **151,724** | +4 (+0.0%)     |
| castle_skip    | 135,088 | 149,071 | 163,347 | 158,150 | 152,967 | **151,724** | +4 (+0.0%)     |
| check_pin      | 135,088 | 149,071 | 163,347 | 158,150 | 152,967 | **151,724** | +4 (+0.0%)     |
| bishop_count   | 125,484 | 139,797 | 154,227 | 149,392 | 145,175 | **142,815** | -8,905 (-5.9%) |

## Perft (startpos, total cycles)

| Commit         | depth 1 | depth 2    | depth 3         | Delta (d3)          |
| -------------- | ------- | ---------- | --------------- | ------------------- |
| baseline       | 569,086 | 12,695,258 | **270,543,361** | —                   |
| piece_index    | 546,478 | 11,679,130 | **256,768,371** | -13,774,990 (-5.1%) |
| staged_movegen | 546,478 | 11,679,130 | **256,768,371** | -13,774,990 (-5.1%) |
| castle_skip    | 554,126 | 11,838,438 | **260,005,599** | -10,537,762 (-3.9%) |
| check_pin      | 554,126 | 11,838,438 | **260,005,599** | -10,537,762 (-3.9%) |
| bishop_count   | 555,366 | 11,864,478 | **260,588,017** | -9,955,344 (-3.7%)  |

## Single Op (startpos, single call)

| Commit         | movegen (cy) | attacked(e1) (cy) | mk/unmk avg (cy) | eval (cy) |
| -------------- | ------------ | ----------------- | ---------------- | --------- |
| baseline       | 77,658       | 7,572             | 16,326           | 135,130   |
| piece_index    | 77,654       | 7,572             | 15,200           | 135,134   |
| staged_movegen | 77,654       | 7,572             | 15,200           | 135,134   |
| castle_skip    | 85,302       | 7,572             | 15,200           | 135,134   |
| check_pin      | 85,302       | 7,572             | 15,200           | 135,134   |
| bishop_count   | 85,302       | 7,572             | 15,262           | 125,530   |

## 5s Search on eZ80 (50 positions)

Searched each of 50 benchmark positions for 5 seconds on the eZ80 (48 MHz, cycle-accurate emulator).
Post-optimization engine (commit `293b90d`, includes movegen optimizations).

| Pos | Nodes | Depth |   ms | Pos | Nodes | Depth |   ms |
| --- | ----: | ----: | ---: | --- | ----: | ----: | ---: |
| P0  |   539 |     3 | 5903 | P25 |   690 |     1 | 8167 |
| P1  |   509 |     1 | 5150 | P26 |  1111 |     2 | 7905 |
| P2  |  2945 |     5 | 5885 | P27 |   835 |     1 | 8833 |
| P3  |     0 |     0 | 5157 | P28 |   153 |     1 | 9000 |
| P4  |  1132 |     3 | 6777 | P29 |   450 |     1 | 7888 |
| P5  |   350 |     1 | 5137 | P30 |  2234 |     5 | 6685 |
| P6  |  3222 |     6 | 6110 | P31 |  2665 |     6 | 6258 |
| P7  |  3345 |     6 | 6036 | P32 |  1383 |     4 | 6612 |
| P8  |  2382 |     5 | 5730 | P33 |  1511 |     4 | 5786 |
| P9  |  3847 |     5 | 5760 | P34 |  2414 |     4 | 6418 |
| P10 |  4035 |     5 | 5731 | P35 |   521 |     3 | 6912 |
| P11 |   442 |     3 | 6462 | P36 |   562 |     2 | 6922 |
| P12 |  1355 |     3 | 6699 | P37 |  1372 |     3 | 5349 |
| P13 |   992 |     3 | 5169 | P38 |  1648 |     3 | 6871 |
| P14 |  1771 |     3 | 5588 | P39 |  1024 |     0 | 5241 |
| P15 |  1795 |     5 | 5083 | P40 |   742 |     3 | 7667 |
| P16 |  1370 |     5 | 5377 | P41 |  3052 |     5 | 6257 |
| P17 |  3370 |    11 | 5693 | P42 |   966 |     3 | 6307 |
| P18 |  3442 |     6 | 5974 | P43 |  1537 |     3 | 5215 |
| P19 |   219 |     1 | 5793 | P44 |  3662 |     5 | 5922 |
| P20 |  4783 |     5 | 6089 | P45 |  1816 |     4 | 6186 |
| P21 |   192 |     1 | 9007 | P46 |  2435 |     7 | 5276 |
| P22 |  1871 |     5 | 5099 | P47 |  4792 |     7 | 6180 |
| P23 |  3647 |     5 | 5749 | P48 |  1173 |     3 | 7028 |
| P24 |  2101 |     5 | 5718 | P49 |  1698 |     3 | 5291 |

- **Total: 90,102 nodes** across 50 positions
- **Average: 1,802 nodes/position** in 5 seconds (~360 NPS)
- Depth range: 0-11 (simple endgames reach d6-11, complex middlegames d1-3)
- Time overshoot due to time check granularity (every 1024 nodes)

## Tournament vs Stockfish

### Node-limited (1800 nodes, 0.1s/move, XXL book)

Simulates eZ80 playing strength (~1800 nodes per move based on 5s search bench).

| SF Elo | W   | D   | L   | Score   | Pct | Elo diff |
| ------ | --- | --- | --- | ------- | --- | -------- |
| 1700   | 13  | 3   | 14  | 14.5/30 | 48% | -12      |
| 1800   | 7   | 8   | 15  | 11.0/30 | 37% | -95      |
| 1900   | 4   | 9   | 17  | 8.5/30  | 28% | -161     |
| 2000   | 5   | 10  | 15  | 10.0/30 | 33% | -120     |
| 2100   | 4   | 7   | 19  | 7.5/30  | 25% | -191     |

**Estimated eZ80 Elo: ~1700** (50% mark vs SF-1700)

### Unleashed (0.1s/move, no node limit, XXL book)

Desktop Arm64 search strength — shows the engine's algorithmic ceiling. (m5 macbook pro)

| SF Elo | W   | D   | L   | Score   | Pct | Elo diff |
| ------ | --- | --- | --- | ------- | --- | -------- |
| 1700   | 28  | 1   | 1   | 28.5/30 | 95% | +512     |
| 1800   | 27  | 2   | 1   | 28.0/30 | 93% | +458     |
| 1900   | 22  | 5   | 3   | 24.5/30 | 82% | +260     |
| 2000   | 26  | 3   | 1   | 27.5/30 | 92% | +417     |
| 2100   | 20  | 3   | 7   | 21.5/30 | 72% | +161     |
| 2200   | 19  | 8   | 3   | 23.0/30 | 77% | +207     |
| 2300   | 17  | 6   | 7   | 20.0/30 | 67% | +120     |
| 2400   | 16  | 7   | 7   | 19.5/30 | 65% | +108     |
| 2500   | 16  | 10  | 4   | 21.0/30 | 70% | +147     |
| 2600   | 12  | 11  | 7   | 17.5/30 | 58% | +58      |
| 2650   | 9   | 10  | 11  | 14.0/30 | 47% | -23      |
| 2700   | 5   | 13  | 12  | 11.5/30 | 38% | -83      |
| 2800   | 0   | 14  | 16  | 7.0/30  | 23% | -207     |
| 2900   | 1   | 8   | 21  | 5.0/30  | 17% | -280     |
| 3000   | 0   | 7   | 23  | 3.5/30  | 12% | -352     |

**Estimated desktop Elo: ~2650** (50% mark between SF-2600 and SF-2700)

## Notes

- **Staged movegen** shows no change in movegen/perft benchmarks because it only reorders moves (captures first, then quiets). The benefit is in alpha-beta search where it improves move ordering and pruning.
- **Castle skip** increases movegen cost by +6,192 cy/call (+6.7%) because the additional `in_check` test at the start of movegen adds overhead that exceeds the savings from skipping castling attack probes. These 5 test positions are mostly not in check, so the optimization rarely triggers.
- **Bishop count** saves 8,905 cy/eval (-5.9%) by tracking bishop counts incrementally in make/unmake instead of counting them during eval. The make/unmake cost increases slightly (+88 cy/pair).
- **Piece index** is the biggest perft win: -5.1% total cycles at depth 3, primarily through faster make/unmake operations.
- At 48 MHz, perft(3) from startpos takes ~5.4s on the baseline and ~5.4s on the final version. The net cycle savings from all optimizations is -3.7% for perft(3).
