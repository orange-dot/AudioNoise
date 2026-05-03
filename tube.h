//
// Completely fake 'tube' sound. I'm calling it tube because (a) that
// has positive connotations and (b) the whole "exponential
// amplification" is how a triode kind of works.
//
// See
//
//	https://leachlegacy.ece.gatech.edu/papers/tubeamp/tubeamp.pdf
//
// that quotes K. R. Spangenberg, "Fundamentals of Electron Devices"
// from 1957 in saying that the plate current is approximately
//
//	i_p = K (\mu*v_GK + v_PK) ^ 3/2
//
// where \mu is the tube amplification factor.
//
// Handwaving wildly, and simplifying it to the point of being
// ridiculous, it means that the plate current - and thus the voltage
// drop over the plate resistor (which is the amplified signal) - is
// exponential in the incoming signal with a bias voltage
//
// I then apply a FIR filter (you need to get that FIR.raw from
// somewhere) with a completely random multiplier.
//
// It's all very ridiculous, in other words. Do I look like I know
// what I'm doing?
//
// The real problem is that this is much too expensive for the poor
// RP2354. It's too expensive even on my desktop in this simplistic
// form. And sure, that whole FIR filter can be fairly trivially
// vectorized and it would work just fine on my real PC with some
// trivial optimizations, but no, that doesn't save this.
//
// So this violates the whole point of this project, but I wanted to
// at least see what it would look like.
//

#ifndef AUDIONOISE_EFFECT_INTERNAL
#error "tube.h is implementation-private; include effect_registry.h from runners"
#endif

static struct {
	float boost, volume;
	float lf, hf;
	struct biquad bass, treble;
	union {
		s32 i;
		float f;
	} FIR[1024];
	int nr, loaded;
	float data[1024];
} tube;

static inline void tube_describe(float pot[4])
{
	if (!tube.loaded) {
		int fd = open("FIR.raw", O_RDONLY);
		if (fd < 0) {
			perror("FIR.raw");
			exit(1);
		}
		int n = read(fd, tube.FIR, sizeof(tube.FIR));
		if (n < 0) {
			perror("FIR.raw");
			exit(1);
		}
		close(fd);

		for (int i = 0; i < n / 4; i++)
			tube.FIR[i].f = tube.FIR[i].i / 2147483648.0;

		tube.loaded = 1;
	}
	fprintf(stderr, " volume=%g", cubic(pot[0], 0.1, 2));
	fprintf(stderr, " boost=%g", linear(pot[1], 1, 20));
	fprintf(stderr, " lf=%.0f Hz", pot_frequency(pot[2]/2));
	fprintf(stderr, " hf=%.0f Hz\n", pot_frequency(0.5+pot[3]));
}

static inline void tube_init(float pot[4])
{
	// I need to think more about this
	tube.volume = cubic(pot[0], 0.1, 2);
	tube.boost = linear(pot[1], 1, 20);
	tube.lf = pot_frequency(pot[2]/2);
	tube.hf = pot_frequency(0.5+pot[3]);

	biquad_hpf(&tube.bass, tube.lf, 1);
	biquad_lpf(&tube.treble, tube.hf, 1);
}

static float tube_step(float in)
{
	in *= tube.boost;
	if (in+1 > 0)
		in = (float)pow(in + 1, 1.5)-1;
	else
		in = -1;
	in *= tube.volume;

#if 1 // ENABLE_TONECTRL
	in = biquad_step(&tube.bass, in);
	in = biquad_step(&tube.treble, in);
#endif

#if 1 // ENABLE_FIR
	tube.data[++tube.nr & 1023] = in;
	float sum = 0;
	for (int i = 0; i < 1024; i++)
		sum += tube.FIR[i].f * tube.data[(tube.nr-i)&1023];

	// I need to figure out what the proper thing here is
	in = sum / 10;
#endif

	return in;
}
