# Automated validation

## One command

Windows / MSVC with the checked-in preset:

```powershell
cmake --preset windows-ninja
cmake --build --preset windows-ninja --target validate -j
```

Linux cross-compile with MinGW and Wine:

```bash
cmake --preset mingw-cross
cmake --build --preset mingw-cross --target validate -j
```

When cross-compiling on Unix, CMake automatically uses Wine as the CTest
emulator if `wine` is available.

## Test suites

- `transpose_planner_tests`: pure planner, conflict modes, octave folding,
  decomposition, skew correction, legacy compatibility, Pareto selection.
- `key_manager_tests`: FF14/Yysls presets, map snapshot round-trip, reset,
  UTF-8 keymap loading.
- `keymap_scheme_config_tests`: config map/pitch round-trip and scheme lookup.
- `piano_roll_model_tests`: all MIDI range layouts, C/C# boundaries, black-key
  hit priority, logical/physical conflicts, navigation, and undo/redo history.
- `piano_roll_ctrl_tests`: wxWidgets event dispatch for selection, binding,
  conflict updates, undo/redo, right-click clearing, and keyboard navigation.
- `midi_parser_tests`: six external MIDI files covering track alignment,
  pitch/channel/duration ranges and note counts.
- `transpose_regression_tests`: end-to-end 36/37-key plans for six external
  MIDI files. It pins octave choice, retained-duration percentage, event hash,
  and verifies the selected candidate is Pareto-optimal.

## Continuous integration

- `.github/workflows/validate.yml` runs `validate` for pushes and pull requests
  targeting `main` or `dev`.
- `.github/workflows/dev.yml` and `.github/workflows/release.yml` run the same
  validation target before preparing release assets.

## Manual Windows gate

Wine can exercise the GUI and CTest binaries, but it cannot prove that a game
receives the generated input correctly. Perform the checklist in
[`game-input-validation.md`](game-input-validation.md) on Windows before a
release that changes keyboard simulation, timing, transpose selection, or
keymap editing.

## External fixtures

The six MIDI files under `tests/data/public_domain_midi/` are unmodified files
from the public-domain-midi dataset. Per-file source URLs, licenses and SHA-256
hashes are recorded in that directory's `README.md`.
