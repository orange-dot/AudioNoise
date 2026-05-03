#include <stdint.h>
#include <stdio.h>

static int write_s32le(int32_t sample)
{
	unsigned char bytes[4] = {
		(unsigned char)((uint32_t)sample & 0xff),
		(unsigned char)(((uint32_t)sample >> 8) & 0xff),
		(unsigned char)(((uint32_t)sample >> 16) & 0xff),
		(unsigned char)(((uint32_t)sample >> 24) & 0xff),
	};

	return fwrite(bytes, 1, sizeof(bytes), stdout) == sizeof(bytes) ? 0 : 1;
}

int main(void)
{
	uint32_t rng = 1;

	for (int i = 0; i < 8192; i++) {
		rng = rng * 1664525u + 1013904223u;

		int phase = i % 512;
		int tri = phase < 256 ? phase : 511 - phase;
		int64_t sample = (int64_t)(tri - 128) * 6000000;

		sample += (int64_t)((i % 97) - 48) * 7000000;
		sample += ((int64_t)(rng >> 1) - 1073741824LL) / 8;
		if ((i % 1024) == 0)
			sample += 900000000LL;

		if (sample > INT32_MAX)
			sample = INT32_MAX;
		if (sample < INT32_MIN)
			sample = INT32_MIN;

		if (write_s32le((int32_t)sample))
			return 1;
	}

	return 0;
}
