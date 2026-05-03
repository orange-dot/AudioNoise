//
// Do the 'sample to float' and 'float to sample' processing
// together with basic noise gating
//

#include "types.h"

//
// process_input() carries function-scope static gate state below. Keep one
// process.h inclusion in each binary's sample-processing TU. The effect
// registry owns magnitude helpers but must not include this header.
//
extern unsigned int magnitude;

#define SAMPLE_TO_FLOAT_MULTIPLIER (1.0 / 0x80000000)
#define FLOAT_TO_SAMPLE_MULTIPLIER (0x80000000 / 1.0)

static inline float process_input(s32 sample)
{
	static int max, min;
	const float max_gate = SAMPLE_TO_FLOAT_MULTIPLIER;
	const float min_gate = max_gate / 100;
	static float noise_gate = SAMPLE_TO_FLOAT_MULTIPLIER / 100;

	//
	// We'll track max and min rather than
	// the maximum absolute value, in case
	// the input is unbalanced
	//
	if (sample > max)
		max = sample;
	if (sample < min)
		min = sample;
	magnitude = max - min;

	//
	// This is sample-rate dependent, but
	// we don't really care. This results in
	// a half-life of roughly 3ksamples or
	// roughly 60ms at 48kHz sample rate.
	//
	max -= (max >> 12)+1;
	min -= (min >> 12)-1;

	//
	// Random fixed noise-gate looking at the
	// top 10 bits of the signal magnitude (which
	// is approx 1mVrms per step)
	//
	if (magnitude >> 22) {
		noise_gate *= 1.001;
		if (noise_gate > max_gate)
			noise_gate = max_gate;
	} else {
		noise_gate *= 0.999;
		if (noise_gate < min_gate)
			noise_gate = min_gate;
	}

	return sample * noise_gate;
}

static inline s32 process_output(float out)
{
	s32 sample = (int)(out * FLOAT_TO_SAMPLE_MULTIPLIER);

	// Check for overflow on float->int conversion
	// by verifying the sign of the result
	//
	// Note that this won't catch overflows that are
	// due to out _way_ outside the [-1,1] range. So
	// we assume the effects are at least minimally
	// careful
	if (out >= 0) {
		if (sample < 0)
			sample = 0x7fffffff;
	} else {
		if (sample > 0)
			sample = 0x80000000;
	}
	return sample;
}
