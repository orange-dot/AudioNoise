# Mamut Spike Plan

Local branch: `mamut-svf-stability-alsa-spikes`

This branch is for documentation, spikes, and throwaway experiments that explore
what can move from the Mamut audio line into `AudioNoise` without changing the
small pedal-DSP character of the project.

No implementation code is planned in this first pass.

## Scope

The first three candidate transfers are:

- nonlinear state-variable filter / filter-drive effect
- effect stability harness
- ALSA live runner

These are deliberately narrow. They should strengthen `AudioNoise` as a C audio
playground without turning it into Mamut, a plugin host, or a full synth.

## 1. Nonlinear SVF / Filter-Drive Effect

Source reference:

- Mamut: `crates/mamut-dsp/src/lib.rs`, `StateVariableFilter`
- AudioNoise baseline: `biquad.h`, `distortion.h`, `phaser.h`

What AudioNoise already has:

- RBJ-style biquad helpers for LPF, HPF, BPF, notch, and allpass
- phaser built from allpass biquad stages
- waveshaping distortion and a more experimental tube path

What Mamut adds:

- a compact state-variable filter path
- cutoff, resonance, drive, and strain controls in one filter body
- soft saturation inside the filter state, not just before or after it
- a color that fits synth/pedal experiments without needing a cab-sim or FFT path

Proposed C spike shape:

- add a small standalone filter state type, probably `struct svf_state`
- keep the API sample-at-a-time, matching the rest of `AudioNoise`
- expose four pot mappings:
  - cutoff
  - resonance
  - drive
  - strain or output blend
- add one effect entry, likely `filterdrive` or `svfdrive`

Constraints:

- no heap allocation
- no external dependencies
- no Mamut patch schema
- no PC4/MIDI mapping in this first slice
- output must remain finite under extreme pot values

Acceptance bar for a later implementation:

- builds with the existing `make` path
- processes existing `input.raw` like the other effects
- has a short audible demo default in `Makefile`
- passes the stability harness described below

## 2. Effect Stability Harness

Source reference:

- Mamut: DSP unit tests that assert filters, chorus, reverb, envelopes, and macro
  sweeps stay finite over long runs
- AudioNoise baseline: `tests/lfo.c`, `tests/sincos.c`

What AudioNoise already has:

- numerical sanity tests for LFO and sine/cosine lookup behavior

What is missing:

- a common effect-level test harness that drives every effect through hostile
  inputs and verifies basic audio safety properties

Proposed C spike shape:

- add a test-only driver that can instantiate one effect at a time
- feed fixed patterns:
  - silence
  - impulse
  - full-scale sine
  - alternating full-scale samples
  - deterministic pseudo-noise
  - pot sweeps
- check:
  - no NaN
  - no infinity
  - output stays in a sane bounded range after `process_output`
  - silence does not create permanent runaway output
  - delay-based effects do not read outside their ring buffer

Constraints:

- deterministic tests
- no Python dependency for the core checks
- no golden audio files required in the first version
- keep slow exhaustive tests separate from normal `make test`

Acceptance bar for a later implementation:

- `make test` still runs quickly
- the new harness can include all current effects
- the future SVF/filter-drive effect must be covered from day one

## 3. ALSA Live Runner

Source reference:

- Mamut standalone ALSA work: exclusive `hw:` playback, period/buffer tuning, xrun
  visibility, and runtime-safe patch switching lessons
- audio-runtime-lab: Linux audio topics around ALSA open/negotiation, periods,
  xruns, mmap basics, and realtime-safe audio paths
- AudioNoise baseline: `convert.c` raw mono file/pipe processor and `ffplay`
  playback path

What AudioNoise already has:

- raw s32le mono input/output processing
- `ffmpeg` / `ffplay` workflow
- optional pot-control fd for changing pot values while processing

What Mamut/audio-runtime-lab can add:

- a small live Linux runner that captures from ALSA, processes one AudioNoise
  effect, and plays back to ALSA
- explicit sample rate, period frames, buffer frames, and device selectors
- xrun reporting instead of silent glitching
- a real pedal-like loop for testing without changing the effect API

Proposed C spike shape:

- add a separate runner, probably `live_alsa.c`
- keep `convert.c` as the offline/reference processor
- use mono capture and mono playback first
- make device selection explicit:
  - input `hw:<card>,<device>`
  - output `hw:<card>,<device>`
- expose period and buffer options on the command line
- reuse the same effect table and four-pot model

Constraints:

- Linux-only
- ALSA-only for the first slice
- no JACK, no PipeWire API, no plugin host
- no realtime priority changes until xrun behavior is measurable
- no code in the audio callback/path that blocks on UI or logging

Acceptance bar for a later implementation:

- offline `convert` behavior remains unchanged
- live runner prints selected devices and negotiated hardware params
- xruns are counted and reported
- Ctrl-C exits cleanly
- one known-good command is documented for the local interface

## Explicit Non-Goals

- no direct port of the full Mamut synth
- no Mamut patch format in AudioNoise
- no PC4 controller map in this branch
- no GUI/TUI work in this branch
- no `dspc` source import
- no dependency-heavy framework layer

## Suggested Work Order

1. Add the stability harness first, while behavior is still simple.
2. Add the SVF/filter-drive effect and immediately put it under the harness.
3. Add the ALSA live runner after offline behavior is stable.

That order keeps the audio behavior testable before live I/O complicates
debugging.
