//
// Entirely random effect hiding discontinuities in the
// sequence by picking two different delays, and
// multiplying them with sin**2/cos**2
//
// Approximate a pitch shifter. Not a great one, I'm
// afraid.
//
#ifndef AUDIONOISE_EFFECT_INTERNAL
#error "discont.h is implementation-private; include effect_registry.h from runners"
#endif

struct {
	struct lfo_state lfo;
	float step;
} disco;

#define DISCONT_SHIFT 12
#define DISCONT_STEPS (1 << DISCONT_SHIFT)

void discont_describe(float pot[4])
{
	fprintf(stderr, " tonestep=%g\n", pow2(linear(pot[0], -1, 1)));
}

void discont_init(float pot[4])
{
	// Which direction do we walk the samples?
	// Walking backwards lowers the pitch
	// Walking forwards raises the pitch
	// Staying at the same delay keeps the pitch the same
	//
	float step = pow2(linear(pot[0], -1, 1));	//  0.5 .. 2
	disco.step = step - 1;			// -0.5 .. 1

	// We set the LFO to be 2*DISCONT_STEPS
	// but then we basically just use half
	// of it twice
	disco.lfo.step = 1 << (31-DISCONT_SHIFT);
}

// i is discontinuous when sin**2 is 0
// ni is discontinuous when cos**2 (aka 1-sin**2) is 0
float discont_step(float in)
{
	// The 'idx << 1' is because we only use half the wave,
	// we'll use 'sin**2' that is the same in both halves
	u32 i = (disco.lfo.idx << 1) >> (32 - DISCONT_SHIFT);
	int ni = (i + DISCONT_STEPS/2) & (DISCONT_STEPS-1);
	float sin = lfo_step(&disco.lfo, lfo_sinewave);

	float step = disco.step;
	float delay = step < 0 ? 0 : 2*DISCONT_STEPS*step;

	sample_array_write(in);
	sin *= sin;
	float d1 = sample_array_read(delay - i*step) * sin;
	float d2 = sample_array_read(delay - ni*step) * (1-sin);

	return d1+d2;
}
