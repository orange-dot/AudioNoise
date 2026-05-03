//
// Distortion/Overdrive effect - waveshaping with multiple modes
//
// Provides soft clipping (overdrive) through hard clipping (fuzz)
// with optional tone control via low-pass filter.
//
#ifndef AUDIONOISE_EFFECT_INTERNAL
#error "distortion.h is implementation-private; include effect_registry.h from runners"
#endif

static struct {
	float drive;
	float tone_freq;
	float level;
	int mode;  // 0=soft (tanh), 1=hard clip, 2=asymmetric
	struct biquad tone_filter;
} distortion;

static inline void distortion_describe(float pot[4])
{
	const char *mode_names[] = { "soft", "hard", "asymmetric" };

	fprintf(stderr, " drive=%gx", linear(pot[0], 1, 50));
	fprintf(stderr, " tone=%g Hz", pot_frequency(pot[1]));
	fprintf(stderr, " level=%g", pot[2]);
	fprintf(stderr, " mode=%s\n", mode_names[distortion.mode]);
}

static inline void distortion_init(float pot[4])
{
	// pot[0]: drive/gain (1x - 50x)
	distortion.drive = linear(pot[0], 1, 50);

	// pot[1]: tone (roll off high frequencies, 220Hz - 6.5kHz)
	distortion.tone_freq = pot_frequency(pot[1]);
	biquad_lpf(&distortion.tone_filter, distortion.tone_freq, 0.707f);

	// pot[2]: output level (0 - 100%)
	distortion.level = pot[2];

	// pot[3]: mode selection
	if (pot[3] < 0.33f)
		distortion.mode = 0;  // soft clip (tanh)
	else if (pot[3] < 0.66f)
		distortion.mode = 1;  // hard clip
	else
		distortion.mode = 2;  // asymmetric

}

// Soft clipping using tanh approximation
static inline float soft_clip(float x)
{
	// Fast tanh approximation: x / (1 + |x|)
	// Gives smooth saturation curve
	return x / (1.0f + fabsf(x));
}

// Hard clipping
static inline float hard_clip(float x)
{
	if (x > 1.0f) return 1.0f;
	if (x < -1.0f) return -1.0f;
	return x;
}

// Asymmetric clipping (tube-like even harmonics)
static inline float asymmetric_clip(float x)
{
	if (x > 0)
		return soft_clip(x);
	else
		return soft_clip(x * 0.7f) * 0.7f;
}

static inline float distortion_step(float in)
{
	// Apply drive
	float driven = in * distortion.drive;

	// Apply waveshaping based on mode
	float shaped;
	switch (distortion.mode) {
	case 0:
		shaped = soft_clip(driven);
		break;
	case 1:
		shaped = hard_clip(driven);
		break;
	case 2:
	default:
		shaped = asymmetric_clip(driven);
		break;
	}

	// Apply tone filter
	float filtered = biquad_step(&distortion.tone_filter, shaped);

	// Apply output level
	return filtered * distortion.level;
}
