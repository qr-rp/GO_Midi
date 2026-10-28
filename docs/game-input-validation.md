# Win32 Game Input Validation

Date: 2026-10-29

This checklist covers behavior that automated Linux/Wine tests cannot prove:
Windows input injection, game focus handling, key release behavior, and actual
instrument rendering.

## Prerequisites

- Windows 10/11, 64-bit, with the application running as administrator.
- The target game running with the intended keyboard layout and no IME active.
- A 36-key keymap using MIDI range `48-83`.
- A 37-key keymap using MIDI range `48-84`.
- The six regression MIDI files from `tests/data/public_domain_midi/`.

## Required scenarios

Run every scenario at both 36 and 37 keys:

1. `frere-jacques.mid`: melody continuity and repeated notes.
2. `chopin-etude-10-4.mid`: fast overlapping piano notes.
3. `beethoven-symphony-5.mid`: dense chords and sustained harmony.
4. `bach-brandenburg-3.mid`: worst-case dense counterpoint and polyphony.

## Checks

1. Import the MIDI, select the game window, and start playback.
2. Confirm the game remains the intended input target while playback runs.
3. Listen for missing melody notes, clipped sustains, wrong octaves, and lost
   notes inside chords.
4. Pause, resume, seek, and stop repeatedly. No key may remain held afterward.
5. Press the global F12 hotkey from outside the application. Playback must
   toggle and no modifier or mouse button may remain logically pressed.
6. Switch between the 36-key and 37-key keymaps. Confirm the output changes
   only where the added highest note can preserve a folded note.
7. Bind and clear several notes in the piano-roll editor, then press `Ctrl+Z`
   and `Ctrl+Y`. Confirm the game output follows the edited mapping.
8. Close the application while stopped. Confirm no injected key remains held.

## Acceptance criteria

- No stuck or continuously repeating key after pause, stop, seek, hotkey use,
  keymap editing, focus changes, or application exit.
- No notes are sent to a different window than the configured target.
- Missed onsets are rare and traceable to intentional polyphony/conflict
  reduction rather than dropped input.
- Dense tracks may sound thinner because of the 36/37-key limit, but the melody
  and harmonic outline remain recognizable.
- The 37-key result is never subjectively worse than the 36-key result for the
  same track and mapping policy.

## Recorded result

Record the following for each run:

- Application commit and build type.
- Windows version and game/build version.
- MIDI file, keyboard size, keymap, transpose, and playback speed.
- Approximate playback interval reviewed.
- Stuck-key incidents and lost-note examples with timestamps.
- Subjective comparison against the original MIDI and synthesized reference.
- Application log path.
