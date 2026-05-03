//
// Nonlinear state-variable low-pass filter.
//
// This is meant to sit between the plain biquad filters and the existing
// waveshapers: drive and strain push the filter state itself, instead of only
// clipping before or after a linear filter.
//

#ifndef AUDIONOISE_EFFECT_INTERNAL
#error "svfdrive.h is implementation-private; include effect_registry.h from runners"
#endif

#define SVFDRIVE_PI 3.14159265358979323846f

static struct {
	float low, band;
	float cutoff;
	float resonance;
	float drive;
	float strain;
} svfdrive;

static inline float svfdrive_clamp(float x, float lo, float hi)
{
	if (x < lo)
		return lo;
	if (x > hi)
		return hi;
	return x;
}

static inline float svfdrive_mix(float a, float b, float amount)
{
	amount = svfdrive_clamp(amount, 0.0f, 1.0f);
	return a + (b - a) * amount;
}

static inline float svfdrive_soft_clip(float x, float asymmetry)
{
	float offset = svfdrive_clamp(asymmetry, -1.0f, 1.0f) * 0.35f;
	float zero = tanhf(offset * 1.25f);

	return tanhf((x + offset) * 1.25f) - zero;
}

static inline void svfdrive_describe(float pot[4])
{
	fprintf(stderr, " cutoff=%.0f Hz", pot_frequency(svfdrive_clamp(pot[0], 0.0f, 1.0f)));
	fprintf(stderr, " resonance=%g", svfdrive_clamp(pot[1], 0.0f, 1.0f));
	fprintf(stderr, " drive=%g", svfdrive_clamp(pot[2], 0.0f, 1.0f));
	fprintf(stderr, " strain=%g\n", svfdrive_clamp(pot[3], 0.0f, 1.0f));
}

static inline void svfdrive_init(float pot[4])
{
	svfdrive.cutoff = pot_frequency(svfdrive_clamp(pot[0], 0.0f, 1.0f));
	svfdrive.resonance = svfdrive_clamp(pot[1], 0.0f, 1.0f);
	svfdrive.drive = svfdrive_clamp(pot[2], 0.0f, 1.0f);
	svfdrive.strain = svfdrive_clamp(pot[3], 0.0f, 1.0f);
}

static inline float svfdrive_step(float in)
{
	float cutoff = svfdrive_clamp(svfdrive.cutoff, 20.0f, SAMPLES_PER_SEC * 0.42f);
	float resonance = svfdrive.resonance;
	float drive = svfdrive.drive;
	float strain = svfdrive.strain;
	float frequency = sinf(SVFDRIVE_PI * cutoff / SAMPLES_PER_SEC) * 1.92f;
	float damping = 1.95f - resonance * 1.34f - strain * 0.24f;
	float input_gain = 1.0f + drive * 1.9f + strain * 0.35f;
	float stage_input = svfdrive_soft_clip(in * input_gain, strain * 0.20f);
	float blend = 0.42f + drive * 0.24f + strain * 0.12f;

	frequency = svfdrive_clamp(frequency, 0.0f, 1.8f);
	damping = svfdrive_clamp(damping, 0.12f, 1.95f);
	blend = svfdrive_clamp(blend, 0.0f, 1.0f);

	for (int i = 0; i < 2; i++) {
		float high = stage_input - svfdrive.low - damping * svfdrive.band;

		svfdrive.band += frequency * high * 0.5f;
		svfdrive.band = svfdrive_soft_clip(svfdrive.band * (1.0f + drive * 0.10f), strain * 0.05f);

		svfdrive.low += frequency * svfdrive.band * 0.5f;
		svfdrive.low = svfdrive_mix(
			svfdrive.low,
			svfdrive_soft_clip(svfdrive.low * (1.0f + drive * 0.18f), strain * 0.08f),
			blend);

		stage_input = svfdrive.low;
	}

	float out = svfdrive_mix(svfdrive.low, svfdrive.low + svfdrive.band * 0.10f, strain * 0.32f);
	out = svfdrive_soft_clip(out, strain * 0.14f + resonance * 0.04f);

	return limit_value(out);
}
