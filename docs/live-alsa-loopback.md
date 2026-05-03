# Live ALSA Loopback Pedal

This fork experiment treats AudioNoise as an external ALSA pedal sidecar:

```text
Mamut standalone -> hw:Loopback,0,0 -> live_alsa -> real hw: output
```

No Mamut code changes are required. The chain uses strict `hw:` ALSA devices
only. Do not use `plughw:` for this path: it would silently reintroduce ALSA
format or sample-rate conversion, which this runner is intentionally avoiding.

## Loopback Setup

Load the ALSA loopback driver:

```sh
sudo modprobe snd-aloop
```

ALSA loopback subdevices mirror across the two sides: a writer on
`hw:Loopback,0,N` is read from `hw:Loopback,1,N`, and vice versa. The command
below uses subdevice `0`.

Run Mamut into the loopback playback side:

```sh
cargo run --locked -p mamut-standalone -- play \
  --headless \
  --audio-device hw:Loopback,0,0 \
  --alsa-period-frames 128 \
  --alsa-buffer-frames 512 \
  --alsa-start-threshold-frames 512 \
  molten-horizon
```

Mamut's default ALSA start threshold is 1024 frames. If the buffer is reduced
to 512 frames, the default threshold exceeds the buffer, so pass
`--alsa-start-threshold-frames 512` with the matching buffer size.

Probe AudioNoise before running the pedal:

```sh
./live_alsa \
  --probe \
  --input hw:Loopback,1,0 \
  --output hw:<card>,<device> \
  --rate 44100 \
  --channels 2 \
  --period 128 \
  --buffer 512
```

Run one effect:

```sh
./live_alsa \
  --effect svfdrive \
  --input hw:Loopback,1,0 \
  --output hw:<card>,<device> \
  --rate 44100 \
  --channels 2 \
  --period 128 \
  --buffer 512 \
  --pot0 0.45 --pot1 0.55 --pot2 0.45 --pot3 0.25
```

## Format And Channel Contract

The Mamut-facing default is 44.1 kHz stereo. `live_alsa` requests the same
rate, channel count, period size, and buffer size from capture and playback.
Capture and playback formats are negotiated independently: `FLOAT_LE` is tried
first, then `S32_LE`. You can override either side with `--capture-format f32`,
`--capture-format s32`, `--playback-format f32`, or `--playback-format s32`.

Internally, AudioNoise still uses its existing S32-shaped DSP path:

```text
ALSA F32/S32 -> S32-shaped process_input -> effect.step -> process_output -> ALSA F32/S32
```

Incoming F32 samples are clamped to `[-1.0, 1.0]` before scaling to S32.

Stereo is a v1 compromise. With `--channels 2`, `live_alsa` averages left and
right as `0.5 * (L + R)`, runs one mono AudioNoise effect instance, then writes
the same processed sample to both output channels. `--channels 1` is available
for mono sources, but the documented Mamut chain is stereo.

`--bypass` is not pedal true-bypass. It is a noise-gated mono dry bypass:
stereo input is folded to mono, `process_input` still updates the gate,
`effect.step` is skipped, `--wet` is ignored, and the dry result is broadcast
to the output channels. A raw frame-copy passthrough is not part of this v1.

## Hardware Constraints

Because this runner uses strict `hw:` devices and disables ALSA resampling,
many real audio cards will reject one of the requested parameters. This is
expected. The probe path prints requested parameters, accepted parameters on
success, and device constraints on negotiation failure.

The practical first target is often another `hw:Loopback,*` PCM or a hardware
interface known to accept the requested 44.1 kHz stereo period/buffer shape.
If playback fails while capture succeeds, the output hardware cannot match the
strict rate/channel/period/buffer request. Format may differ between capture
and playback, but ALSA conversion is intentionally unavailable.

AudioNoise effect timing constants are still calibrated for 48 kHz. At
44.1 kHz, time-based effects run about 8.84% fast: a nominal 100 ms delay acts
like roughly 91.9 ms. Compensate with effect pots for this experiment.

Short periods such as 128 frames on a normal desktop kernel can xrun under
load. `live_alsa` reports capture/playback xrun counters every five seconds
and again on clean exit.
