#include <math.h>
#include <stdio.h>
#include <string.h>

#define SAMPLES_PER_SEC 48000.0f

#include "../util.h"
#include "../svfdrive.h"

static int check_sample(float sample, const char *name, int index)
{
	if (!isfinite(sample)) {
		fprintf(stderr, "%s produced non-finite sample at %d\n", name, index);
		return 1;
	}

	if (fabsf(sample) > 1.05f) {
		fprintf(stderr, "%s produced oversized sample %g at %d\n", name, sample, index);
		return 1;
	}

	return 0;
}

static int run_pattern(const char *name, float pot[4])
{
	u32 rng = 1;

	memset(&svfdrive, 0, sizeof(svfdrive));
	svfdrive_init(pot);

	for (int i = 0; i < 8192; i++) {
		float in;

		switch (i & 3) {
		case 0:
			in = i == 0 ? 1.0f : 0.0f;
			break;
		case 1:
			in = (i & 4) ? 1.0f : -1.0f;
			break;
		case 2:
			in = sinf((float)i * 0.03125f) * 0.85f;
			break;
		default:
			rng = rng * 1664525u + 1013904223u;
			in = u32_to_fraction(rng) * 2.0f - 1.0f;
			break;
		}

		if (check_sample(svfdrive_step(in), name, i))
			return 1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	float clean[4] = { 0.2f, 0.0f, 0.0f, 0.0f };
	float driven[4] = { 0.45f, 0.55f, 0.45f, 0.25f };
	float extreme[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float silence[4] = { 0.85f, 1.0f, 1.0f, 1.0f };
	float out = 0.0f;

	if (run_pattern("clean", clean))
		return 1;
	if (run_pattern("driven", driven))
		return 1;
	if (run_pattern("extreme", extreme))
		return 1;

	memset(&svfdrive, 0, sizeof(svfdrive));
	svfdrive_init(silence);
	for (int i = 0; i < 4096; i++) {
		out = svfdrive_step(0.0f);
		if (check_sample(out, "silence", i))
			return 1;
	}

	if (fabsf(out) > 0.0001f) {
		fprintf(stderr, "silence settled at %g\n", out);
		return 1;
	}

	printf("svfdrive stability ok\n");
	return 0;
}
