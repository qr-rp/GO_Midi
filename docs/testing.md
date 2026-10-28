# Automated validation

## One command

Windows / MSVC:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGO_MIDI_BUILD_TESTS=ON
cmake --build build --target validate -j
```

Linux cross-compile with MinGW and Wine:

```bash
cmake -S . -B build_mingw -DCMAKE_TOOLCHAIN_FILE=mingw-toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DGO_MIDI_BUILD_TESTS=ON
cmake --build build_mingw --target validate -j
```

When cross-compiling on Unix, CMake automatically uses Wine as the CTest
emulator if `wine` is available.

## Test suites

- `transpose_planner_tests`: pure planner, conflict modes, octave folding,
  decomposition, skew correction, legacy compatibility, Pareto selection.
- `key_manager_tests`: FF14/Yysls presets, map snapshot round-trip, reset,
  UTF-8 keymap loading.
- `keymap_scheme_config_tests`: config map/pitch round-trip and scheme lookup.
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

## External fixtures

The six MIDI files under `tests/data/public_domain_midi/` are unmodified files
from the public-domain-midi dataset. Per-file source URLs, licenses and SHA-256
hashes are recorded in that directory's `README.md`.
