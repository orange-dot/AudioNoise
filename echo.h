//
// Minimal echo effect
//
#ifndef AUDIONOISE_EFFECT_INTERNAL
#error "echo.h is implementation-private; include effect_registry.h from runners"
#endif

static inline void echo_describe(float pot[4])
{
	fprintf(stderr, " delay=%g ms", pot[0] * 1000);
	fprintf(stderr, " lfo=%g ms", pot[2]*4);
	fprintf(stderr, " feedback=%g\n", pot[3]);
}

static inline void echo_init(float pot[4])
{
	effect_set_delay(pot[0] * 1000);	// delay = 0 .. 1s
	effect_set_lfo_ms(pot[2]*4);	// LFO = 0 .. 4ms
	effect_set_feedback(pot[3]);	// feedback = 0 .. 100%
}

static inline float echo_step(float in)
{
	float d = 1 + effect_delay;
	float out;

	out = sample_array_read(d);
	sample_array_write(limit_value(in + out * effect_feedback));

	return (in + out)/ 2;
}
