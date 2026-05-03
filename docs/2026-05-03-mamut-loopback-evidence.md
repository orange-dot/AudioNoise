# 2026-05-03 Mamut Loopback Pedal Evidence

This note records the first local validation of `live_alsa` as an external
pedal sidecar for Mamut standalone.

## Repositories

- AudioNoise repo: `/home/dev/work-base-20260421/workspace/systems/AudioNoise`
- AudioNoise HEAD: `5dbd73c Merge pull request #2 from orange-dot/audionoise-live-alsa`
- Mamut repo: `/home/dev/work-base-20260421/workspace/systems/mamut-sint-sw-remote-up-21-4`
- Mamut local HEAD during the run: `da8ee70 Restyle Sound Lab with PC4 controls`

## Device Map

Observed ALSA map:

```text
Yamaha AG06/AG03: hw:1,0
Loopback card:    hw:5
```

Validated chain:

```text
Mamut standalone playback -> hw:5,0
AudioNoise live_alsa capture <- hw:5,1
AudioNoise live_alsa playback -> hw:1,0 Yamaha AG06/AG03
```

`snd-aloop` was loaded manually. Runtime ALSA commands were executed outside the
Codex sandbox because the sandbox did not expose `/dev/snd`.

## Probe Evidence

Target probe:

```bash
./live_alsa \
  --probe \
  --input hw:5,1 \
  --output hw:1,0 \
  --rate 44100 \
  --channels 2 \
  --period 128 \
  --buffer 512
```

Observed result:

```text
capture requested: device=hw:5,1 format=auto(FLOAT_LE,S32_LE) rate=44100 channels=2 period_size=128 buffer_size=512
capture accepted: format=FLOAT_LE rate=44100 channels=2 period_size=128 buffer_size=512
playback requested: device=hw:1,0 format=auto(FLOAT_LE,S32_LE) rate=44100 channels=2 period_size=128 buffer_size=512
playback accepted: format=S32_LE rate=44100 channels=2 period_size=128 buffer_size=512
probe OK
```

This validated the independent capture/playback format adapters:

- loopback capture used `FLOAT_LE`
- Yamaha playback used `S32_LE`

Direct Yamaha playback was validated separately with `speaker-test` on
`hw:1,0`; the operator heard the sine output.

Loopback direction was also validated by writing `speaker-test` to `hw:5,0`
while `live_alsa --bypass` read `hw:5,1` and played to the Yamaha; the operator
heard the signal.

## Mamut Source Command

Mamut was launched into loopback:

```bash
cargo run --locked -p mamut-standalone -- play \
  --headless \
  --audio-device hw:5,0 \
  --alsa-period-frames 128 \
  --alsa-buffer-frames 512 \
  --alsa-start-threshold-frames 512 \
  --midi-device "mioXM DIN 1" \
  --midi-channel 1 \
  --controller-profile profiles/pc4-full.toml \
  molten-horizon
```

Mamut reported:

```text
audio: hw:5,0 (Loopback, Loopback PCM) @ 44100 Hz, 2 channels
alsa: selector=hw:5,0 period=128 buffer=512 start_threshold=512
mode: connected (mioXM:mioXM DIN 1 16:0)
```

During playing, Mamut reported incoming MIDI and active voices.

## AudioNoise Runs

Bypass:

```bash
./live_alsa \
  --bypass \
  --input hw:5,1 \
  --output hw:1,0 \
  --rate 44100 \
  --channels 2 \
  --period 128 \
  --buffer 512
```

The operator heard Mamut through bypass.

Full-wet `svfdrive`:

```bash
./live_alsa \
  --effect svfdrive \
  --input hw:5,1 \
  --output hw:1,0 \
  --rate 44100 \
  --channels 2 \
  --period 128 \
  --buffer 512 \
  --pot0 0.45 --pot1 0.55 --pot2 0.45 --pot3 0.25
```

Observed description:

```text
Live svfdrive:  cutoff=789 Hz resonance=0.55 drive=0.45 strain=0.25
```

This full-wet setting was effectively too quiet or too closed for the tested
Mamut material. Returning to bypass restored the audible signal.

`svfdrive` with dry blend:

```bash
./live_alsa \
  --effect svfdrive \
  --wet 0.25 \
  --input hw:5,1 \
  --output hw:1,0 \
  --rate 44100 \
  --channels 2 \
  --period 128 \
  --buffer 512 \
  --pot0 0.45 --pot1 0.55 --pot2 0.45 --pot3 0.25
```

The operator heard the signal and reported a clear difference from bypass.

Echo:

```bash
./live_alsa \
  --effect echo \
  --wet 0.35 \
  --input hw:5,1 \
  --output hw:1,0 \
  --rate 44100 \
  --channels 2 \
  --period 128 \
  --buffer 512 \
  --pot0 0.35 --pot1 0.45 --pot2 0.35 --pot3 0.30
```

Observed description:

```text
Live echo:  delay=350 ms lfo=1.4 ms feedback=0.3
```

The operator reported an audible difference when switching between bypass and
echo.

The Mamut runtime was also switched to `Sawyer Rezz` while AudioNoise remained
in the loopback chain. The operator heard Sawyer Rezz through bypass and echo.

## Result

The Mamut-compatible `live_alsa` sidecar is validated on this local rig:

- strict `hw:` loopback routing worked
- 44100 Hz stereo period 128 / buffer 512 worked
- capture/playback format mismatch worked through `live_alsa` adapters
- Mamut code did not need changes
- bypass, `svfdrive --wet 0.25`, and `echo --wet 0.35` were audible

Observed limits:

- `svfdrive` full-wet was not a useful default for this Mamut patch chain
- stereo is mono-folded by the v1 sidecar
- small playback xrun counts appeared at startup/restart; capture xrun count
  stayed at zero in observed reports
