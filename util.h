// Various utility functions mainly for
// imprecise but fast floating point

//
// Sized integer types I'm used to from the kernel.
//
// I dislike 'uint32_t' as being unwieldly (and historically not
// available in all environments, so you end up with a mess of
// configuration), and 'uint' as not having a well-defined size.
//
// I'm not using the 64-bit types yet, but the RP2354 has 32x32
// multiplies giving a 64-bit result, so I'm considering doing
// some fixed-point math, and this preps for it.
//
#include "types.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define TWO_POW_32 (4294967296.0f)
#define LN2 0.69314718055994530942

#define pow2(x) (float)pow(2,x)

// Turn 0..1 into a range
#define linear(pot, a, b)	((a)+(pot)*((b)-(a)))
#define cubic(pot, a, b)	linear((pot)*(pot)*(pot), a, b)

// "reasonable frequency range": 220Hz - 6.5kHz with pot center at 1kHz
#define pot_frequency(pot)	cubic(pot, 220, 6460)

//
// Smoothly limit x to -1 .. 1
//
static inline float limit_value(float x)
{
	return x / (1 + fabsf(x));
}

static inline float u32_to_fraction(u32 val)
{
	return (1.0/TWO_POW_32) * val;
}

static inline u32 fraction_to_u32(float val)
{
	return (u32) (val * TWO_POW_32);
}

// Max ~1.25s delays at ~52kHz
#define SAMPLE_ARRAY_SIZE 65536
#define SAMPLE_ARRAY_MASK (SAMPLE_ARRAY_SIZE-1)
extern float sample_array[SAMPLE_ARRAY_SIZE];
extern int sample_array_index;

static inline void sample_array_write(float val)
{
	u32 idx = SAMPLE_ARRAY_MASK & ++sample_array_index;
	sample_array[idx] = val;
}

static inline float sample_array_read(float delay)
{
	int i = (int) delay;
	float frac = delay - i;
	int idx = sample_array_index - i;

	float a = sample_array[SAMPLE_ARRAY_MASK & idx];
	float b = sample_array[SAMPLE_ARRAY_MASK & ++idx];
	return a + (b-a)*frac;
}

// We can calculate sin/cos at the same time using
// the table lookup. It's "GoodEnough(tm)" and with
// 256 entries it's good to about 5.3 digits of
// precision if I tested it right.
//
// Don't use this for real work. For audio? It's fine.
#include "gensin.h"

#define QUARTER_SINE_STEPS (1<< QUARTER_SINE_STEP_SHIFT)

struct sincos { float sin, cos; };

// positive phase numbers only, please..
struct sincos fastsincos(float phase)
{
	phase *= 4;
	int quadrant = (int)phase;
	phase -= quadrant;

	phase *= QUARTER_SINE_STEPS;
	int idx = (int) phase;
	phase -= idx;

	float a = quarter_sin[idx];
	float b = quarter_sin[idx+1];

	float x = a + (b-a)*phase;

	idx = QUARTER_SINE_STEPS - idx;
	a = quarter_sin[idx];
	b = quarter_sin[idx-1];

	float y = a + (b-a)*phase;

	if (quadrant & 1) {
		float tmp = -x; x = y; y = tmp;
	}
	if (quadrant & 2) {
		x = -x; y = -y;
	}

	return (struct sincos) { x, y };
}
