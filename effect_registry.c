#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SAMPLES_PER_SEC (48000.0)
#define AUDIONOISE_EFFECT_INTERNAL

#include "util.h"
#include "lfo.h"
#include "effect.h"
#include "biquad.h"
#include "effect_registry.h"

#include "flanger.h"
#include "echo.h"
#include "fm.h"
#include "am.h"
#include "phaser.h"
#include "discont.h"
#include "distortion.h"
#include "svfdrive.h"
#include "tube.h"
#include "growlingbass.h"
#include "pll.h"

unsigned int magnitude;
float sample_array[SAMPLE_ARRAY_SIZE];
int sample_array_index;

static void magnitude_describe(float pot[4]) { fprintf(stderr, "\n"); }
static void magnitude_init(float pot[4]) {}
static float magnitude_step(float in) { return u32_to_fraction(magnitude); }

#define EFF(x) { #x, x##_describe, x##_init, x##_step }

static const struct effect effects[] = {
	EFF(discont),
	EFF(distortion),
	EFF(svfdrive),
	EFF(echo),
	EFF(flanger),
	EFF(phaser),
	EFF(tube),
	EFF(growlingbass),
	EFF(pll),

	/* "Helper" effects */
	EFF(am),
	EFF(fm),
	EFF(magnitude),
};

const struct effect *audionoise_find_effect(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(effects); i++) {
		if (!strcmp(name, effects[i].name))
			return effects + i;
	}
	return NULL;
}

const struct effect *audionoise_effects(size_t *count)
{
	if (count)
		*count = ARRAY_SIZE(effects);
	return effects;
}

void audionoise_effect_tick(void)
{
	effect_delay += 0.001 * (target_effect_delay - effect_delay);
}

void audionoise_print_effects(FILE *out)
{
	for (size_t i = 0; i < ARRAY_SIZE(effects); i++)
		fprintf(out, "%s%s", i ? " " : "", effects[i].name);
	fprintf(out, "\n");
}
