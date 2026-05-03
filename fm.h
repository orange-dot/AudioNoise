//
// Silly frequency modulation signal generator "effect"
// It doesn't actually care about the input, it's useful
// mainly for testing the LFO
//
#ifndef AUDIONOISE_EFFECT_INTERNAL
#error "fm.h is implementation-private; include effect_registry.h from runners"
#endif

static struct lfo_state base_lfo, modulator_lfo;
static float fm_volume, fm_base_freq, fm_freq_range;

static inline void fm_describe(float pot[4])
{
	float freq = pot_frequency(pot[1]);
	float range = pot[2];

	fprintf(stderr, " volume=%g", pot[0]);
	fprintf(stderr, " freq=%.0f (%.0f-%.0f) Hz",
			freq,
			freq * pow2(-range),
			freq * pow2( range));
	fprintf(stderr, " lfo=%g Hz\n", 1 + 10*pot[3]);
}

static inline void fm_init(float pot[4])
{
	fm_volume = pot[0];
	fm_base_freq = pot_frequency(pot[1]);		// 220Hz - 6.5kHz
	fm_freq_range = pot[2];				// 110Hz -  13kHz
	set_lfo_freq(&modulator_lfo, 1 + 10*pot[3]);	// 1..11 Hz
}

static inline float fm_step(float in)
{
	float lfo = lfo_step(&modulator_lfo, lfo_sinewave);
	float multiplier = pow2(lfo * fm_freq_range);
	float freq = fm_base_freq * multiplier;
	set_lfo_freq(&base_lfo, freq);
	return lfo_step(&base_lfo, lfo_sinewave) * fm_volume;
}
