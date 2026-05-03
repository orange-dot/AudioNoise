//
// Silly amplitude modulation signal generator "effect"
// It doesn't actually care about the input, it's useful
// mainly for testing the LFO (and generating signals
// for testing other effects)
//
#ifndef AUDIONOISE_EFFECT_INTERNAL
#error "am.h is implementation-private; include effect_registry.h from runners"
#endif

static struct {
	struct lfo_state base_lfo, mod_lfo;
	float depth, volume;
} am;

static inline void am_describe(float pot[4])
{
	fprintf(stderr, " volume=%g", pot[0]);
	fprintf(stderr, " freq=%.0f Hz", pot_frequency(pot[1]));
	fprintf(stderr, " depth=%g", pot[2]);
	fprintf(stderr, " lfo=%g Hz\n", 1 + 10*pot[3]);
}

static inline void am_init(float pot[4])
{
	am.volume = pot[0];
	set_lfo_freq(&am.base_lfo, pot_frequency(pot[1]));
	am.depth = pot[2];
	set_lfo_freq(&am.mod_lfo, 1 + 10*pot[3]); // 1..11 Hz
}

static inline float am_step(float in)
{
	float val = lfo_step(&am.base_lfo, lfo_sinewave);
	float mod = lfo_step(&am.mod_lfo, lfo_sinewave);
	float multiplier = 1 + mod * am.depth;

	return val * multiplier * am.volume;
}
