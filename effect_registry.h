//
// Shared effect registry for AudioNoise runners.
//
#ifndef AUDIONOISE_EFFECT_REGISTRY_H
#define AUDIONOISE_EFFECT_REGISTRY_H

#include <stddef.h>
#include <stdio.h>

struct effect {
	const char *name;
	void (*describe)(float[4]);
	void (*init)(float[4]);
	float (*step)(float);
};

const struct effect *audionoise_find_effect(const char *name);
const struct effect *audionoise_effects(size_t *count);
void audionoise_effect_tick(void);
void audionoise_print_effects(FILE *out);

#endif
