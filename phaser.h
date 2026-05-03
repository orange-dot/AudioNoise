#ifndef AUDIONOISE_EFFECT_INTERNAL
#error "phaser.h is implementation-private; include effect_registry.h from runners"
#endif

struct {
	struct lfo_state lfo;
	struct biquad_coeff coeff;
	float s0[2], s1[2], s2[2], s3[2];
	float center_f, octaves, Q, feedback;
} phaser;

void phaser_describe(float pot[4])
{
	float ms = cubic(pot[0], 25, 2000);
	float feedback = linear(pot[1], 0, 0.75);
	float f = pot_frequency(pot[2]);
	float octaves = 0.5;
	float Q = linear(pot[3], 0.25, 2);

	float lo = pow2(-octaves) * f;
	float hi = pow2( octaves) * f;

	fprintf(stderr, " lfo=%g ms", ms);
	fprintf(stderr, " f=%.0f (%.0f - %.0f) Hz", f, lo, hi);
	fprintf(stderr, " feedback=%g", feedback);
	fprintf(stderr, " Q=%g\n", Q);
}

void phaser_init(float pot[4])
{
	float ms = cubic(pot[0], 25, 2000);		// 25ms .. 2s
	set_lfo_ms(&phaser.lfo, ms);
	phaser.feedback = linear(pot[1], 0, 0.75);

	phaser.center_f = pot_frequency(pot[2]);		// 220Hz .. 6.5kHz
	phaser.octaves = 0.5;				// 155Hz .. 9kHz
	phaser.Q = linear(pot[3], 0.25, 2);
}

float phaser_step(float in)
{
	float lfo = lfo_step(&phaser.lfo, lfo_triangle);
	float freq = pow2(lfo*phaser.octaves) * phaser.center_f;
	float out;

	_biquad_allpass_filter(&phaser.coeff, freq, phaser.Q);

	out = in + phaser.feedback * phaser.s3[0];
	out = biquad_step_df1(&phaser.coeff, out, phaser.s0, phaser.s1);
	out = biquad_step_df1(&phaser.coeff, out, phaser.s1, phaser.s2);
	out = biquad_step_df1(&phaser.coeff, out, phaser.s2, phaser.s3);

	return limit_value(in + out);
}
