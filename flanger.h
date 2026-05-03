// Flanger effect based on the MIT-licensed DaisySP library by Electrosmith
// which in turn seems to be based on Soundpipe by Paul Batchelor
#ifndef AUDIONOISE_EFFECT_INTERNAL
#error "flanger.h is implementation-private; include effect_registry.h from runners"
#endif

static inline void flanger_describe(float pot[4])
{
	fprintf(stderr, " freq=%g Hz", pot[0]*pot[0]*10);
	fprintf(stderr, " delay=%g ms", pot[1]*4);
	fprintf(stderr, " depth=%g", pot[2]);
	fprintf(stderr, " feedback=%g\n", pot[3]);
}

static inline void flanger_init(float pot[4])
{
	effect_set_lfo(pot[0]*pot[0]*10);	// lfo = 0 .. 10Hz
	effect_set_delay(pot[1] * 4);		// delay = 0 .. 4 ms
	effect_set_depth(pot[2]);		// depth = 0 .. 100%
	effect_set_feedback(pot[3]);		// feedback = 0 .. 100%
}

static inline float flanger_step(float in)
{
	float d = 1 + effect_delay * (1 + lfo_step(&effect_lfo, lfo_sinewave) * effect_depth);
	float out;

	out = sample_array_read(d);
	sample_array_write(limit_value(in + out * effect_feedback));

	return (in + out) / 2;
}
