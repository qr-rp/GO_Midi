# Memory Pareto Refactor Report

Date: 2026-10-29  
Baseline commit: `433f6e8`

## Scope

This pass reduces memory without changing MIDI output, playback semantics, or
the UI:

- Candidate transposition planning no longer creates unused event/note vectors
  for every rejected octave.
- The playback engine requests events without retaining the unused `PlanNote`
  payload.
- `RawNote` is reduced from 28 to 20 bytes.
- `PlaybackEvent` is reduced from 32 to 24 bytes.
- Final event and note vectors reserve exactly the number of valid notes.
- MIDI parsing frees per-track tick tuples as conversion completes and avoids
  excessive vector preallocation.
- Parsed note storage is moved into the playback source instead of copied.
- Piano-roll undo history stores single-note deltas instead of full keymaps.

## Benchmark workload

```bash
cmake --build build_mingw --target planner_memory_benchmark -j2
wine build_mingw/planner_memory_benchmark.exe \
  tests/data/public_domain_midi/bach-brandenburg-3.mid
```

Workload: `bach-brandenburg-3.mid`, 27,388 source notes, 36-key conflict-aware
plan, MinGW Release under Wine. Values are a three-run range.

| Metric | Before | After | Change |
|---|---:|---:|---:|
| Parse peak working set | 9,720 KB | 8,892–8,904 KB | about -820 KB |
| Planned working set | 11,568 KB | 10,528–10,536 KB | about -1,036 KB |
| Planned peak working set | 12,812 KB | 11,708–11,736 KB | about -1,080 KB |
| Peak pagefile usage | 5,720 KB | 4,612 KB | -1,108 KB |
| Planner elapsed time | 33–34 ms | 17 ms | about -50% |
| Output notes | 12,911 | 12,911 | unchanged |
| Output events | 25,822 | 25,822 | unchanged |

## Full application measurement

The full wxWidgets application remains around a 49 MB idle working set and
about 54.5 MB after opening the file picker and loading the same MIDI. Roughly
5 MB is retained by the Wine/Windows shell file dialog itself: opening and
cancelling the dialog produces the same increase without loading a MIDI file.
That portion is outside the application's data model and cannot be removed
safely from this repository.

The planner benchmark is therefore the controlled measurement for application
owned MIDI, histogram, working-note, candidate, and playback-event memory.

## Correctness gates

- Full CTest `validate` suite.
- Six public-domain MIDI files at 36 and 37 keys.
- Stable output/conflict/fold metrics.
- ASan/UBSan model tests.
- Piano-roll event dispatch tests.
