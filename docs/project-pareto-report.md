# Project Pareto Refactor Report

Date: 2026-10-28
Branch: `dev`
Baseline: `d89fdc389557f9ce29f184c7573a6e4a1e8d4b4b` plus the working-tree smart-transpose work

Toolchain: C++23, CMake 3.25+, extensions disabled, warnings treated as errors,
and checked-in CMake Presets.

## Pareto criteria

This refactor treats a change as Pareto-improving only when:

1. Existing user-visible behavior and MIDI output remain unchanged unless explicitly measured as an improvement.
2. The Windows product builds successfully.
3. Core tests pass on the MinGW/Windows artifact.
4. Maintainability, duplicate code, dependency propagation, or documented complexity improves.
5. Remaining risks are named instead of hidden behind a score.

## Structural comparison

| Metric | Before | After |
|---|---:|---:|
| Duplicate function LOC | 348 | 158 |
| Duplicated LOC ratio | 3.8% | 1.8% |
| Clone groups | 12 | 6 |
| Dependency NCCD | 0.77 | 0.73 |
| Propagation cost | 0.086 | 0.079 |
| Architecture violations | No rules | 0 |
| `MidiParser::parse_track` cognitive complexity | 124 | 58 |
| `MidiParser::parse_track` LOC | 270 | 169 |
| `PlaybackEngine::playback_thread` LOC | 208 | 166 |
| `MainFrame::OnTimer` cognitive complexity | 59 | Split into focused helpers |

`src/core/PlaybackEngine.cpp` fell from 1187 to 651 lines by extracting the pure planner. The planner itself is a new, independently tested module.

## Behavior-preserving evidence

- Six external public-domain MIDI files parse successfully through the refactored parser.
- The 36-key and 37-key legacy note CSVs for all six files retain the same SHA-256 before and after the parser/KeyManager refactors.
- FF14 and Yysls keymaps retain 37 and 36 entries respectively, including `C4 -> Q` and `C#3 -> Shift+Z`.
- UTF-8 BOM keymap loading passes after the encoding reader refactor.

## Tests

- `transpose_planner_tests`: pitch mapping, legacy baseline, conflict policy, decomposition, corrected skew, Pareto selection.
- `key_manager_tests`: built-in presets, map snapshot round-trip, reset behavior, UTF-8 BOM file loading.
- `keymap_scheme_config_tests`: shared config map/pitch round-trip and index lookup.
- `midi_parser_tests`: six external MIDI fixtures, track alignment, note ranges/durations/channels, expected note volume.
- `transpose_regression_tests`: end-to-end 36/37-key octave choice, retained duration, stable event hash and Pareto property.

Run all suites with `cmake --build <build-dir> --target validate`.

CTest is automatically run through Wine for Unix cross-compiled builds. The
new `.github/workflows/validate.yml` workflow runs the same target on pushes
and pull requests to `main`/`dev`; tag release workflows also validate before
packaging.

## Architecture gate

`architecture.rules` now prevents:

- util depending on MIDI/core/UI/app;
- MIDI depending on core/UI/app;
- core depending on UI/app;
- UI depending on app.

Run:

```text
ripwire . --arch=architecture.rules
```

Current result: `violations=0`, `new_violations=0`.

`quality-delta` currently reports `gating=0`. One minor header-verbosity finding
remains because the `MainFrame` class gained three focused helper declarations;
this is retained because moving them back into `OnTimer` would restore the much
larger control-flow tangle.

## Conscious trade-offs

- `KeyboardSimulator::send_key_events` and `PlaybackEngine::playback_thread` remain relatively complex. They contain Win32 timing and input behavior, and there is no safe automated game-input oracle yet. Splitting them without real timing tests would trade readability for regression risk.
- FF14 and Yysls preset tables remain separate data definitions. They are different mapping data, not accidental logic duplication.
- The logical-key conflict mode remains experimental. It improves some 36-key scores slightly, but shared-main-key release behavior has not been validated in the game.
