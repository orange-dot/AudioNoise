#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "effect_registry.h"
#include "process.h"
#include "types.h"

enum format_choice {
	FORMAT_AUTO,
	FORMAT_F32,
	FORMAT_S32,
};

struct live_config {
	const char *input_device;
	const char *output_device;
	const char *effect_name;
	const struct effect *effect;
	int effect_seen;
	int bypass;
	int probe;
	unsigned int rate;
	unsigned int channels;
	snd_pcm_uframes_t period_size;
	snd_pcm_uframes_t buffer_size;
	enum format_choice capture_format;
	enum format_choice playback_format;
	float pots[4];
	float wet;
};

struct pcm_side {
	snd_pcm_t *pcm;
	snd_pcm_format_t format;
	unsigned int rate;
	unsigned int channels;
	snd_pcm_uframes_t period_size;
	snd_pcm_uframes_t buffer_size;
};

struct xrun_stats {
	unsigned long capture;
	unsigned long playback;
	unsigned long periods;
};

static volatile sig_atomic_t stop_requested;

static const struct live_config default_config = {
	.rate = 44100,
	.channels = 2,
	.period_size = 128,
	.buffer_size = 512,
	.capture_format = FORMAT_AUTO,
	.playback_format = FORMAT_AUTO,
	.pots = { 0.5f, 0.5f, 0.5f, 0.5f },
	.wet = 1.0f,
};

static const char *format_choice_name(enum format_choice choice)
{
	switch (choice) {
	case FORMAT_AUTO:
		return "auto(FLOAT_LE,S32_LE)";
	case FORMAT_F32:
		return "FLOAT_LE";
	case FORMAT_S32:
		return "S32_LE";
	}
	return "unknown";
}

static const char *pcm_format_name(snd_pcm_format_t format)
{
	const char *name = snd_pcm_format_name(format);

	return name ? name : "UNKNOWN";
}

static int is_hw_device(const char *device)
{
	return device && !strncmp(device, "hw:", 3);
}

static void request_stop(int signo)
{
	(void)signo;
	stop_requested = 1;
}

static int install_signal_handlers(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = request_stop;
	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGINT, &sa, NULL) || sigaction(SIGTERM, &sa, NULL)) {
		perror("sigaction");
		return -1;
	}

	return 0;
}

static void usage(FILE *out, const char *argv0)
{
	fprintf(out, "Usage: %s [options]\n", argv0);
	fprintf(out, "\n");
	fprintf(out, "Required for run/probe:\n");
	fprintf(out, "  --input DEVICE           ALSA capture device, strict hw: only\n");
	fprintf(out, "  --output DEVICE          ALSA playback device, strict hw: only\n");
	fprintf(out, "\n");
	fprintf(out, "Effect options:\n");
	fprintf(out, "  --effect NAME            Effect name; required unless --probe or --bypass\n");
	fprintf(out, "  --pot0 VALUE             Pot value 0.0..1.0, default 0.5\n");
	fprintf(out, "  --pot1 VALUE             Pot value 0.0..1.0, default 0.5\n");
	fprintf(out, "  --pot2 VALUE             Pot value 0.0..1.0, default 0.5\n");
	fprintf(out, "  --pot3 VALUE             Pot value 0.0..1.0, default 0.5\n");
	fprintf(out, "  --wet VALUE              Wet mix 0.0..1.0, default 1.0\n");
	fprintf(out, "  --bypass                 Noise-gated mono dry bypass; ignores --wet\n");
	fprintf(out, "\n");
	fprintf(out, "ALSA options:\n");
	fprintf(out, "  --rate HZ                Sample rate, default 44100\n");
	fprintf(out, "  --channels N             Channels, 1 or 2, default 2\n");
	fprintf(out, "  --period FRAMES          Period size, default 128\n");
	fprintf(out, "  --buffer FRAMES          Buffer size, default 512\n");
	fprintf(out, "  --capture-format FMT     auto, f32, or s32; default auto\n");
	fprintf(out, "  --playback-format FMT    auto, f32, or s32; default auto\n");
	fprintf(out, "\n");
	fprintf(out, "Other:\n");
	fprintf(out, "  --probe                  Open/configure PCMs and exit; no effect required\n");
	fprintf(out, "  --help                   Show this help\n");
	fprintf(out, "\n");
	fprintf(out, "Effects: ");
	audionoise_print_effects(out);
}

static int next_arg(int argc, char **argv, int *i, const char *option, const char **value)
{
	if (*i + 1 >= argc) {
		fprintf(stderr, "%s requires a value\n", option);
		return -1;
	}

	*value = argv[++*i];
	return 0;
}

static int parse_uint(const char *option, const char *value, unsigned int min,
		      unsigned int max, unsigned int *out)
{
	char *end = NULL;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(value, &end, 10);
	if (errno || end == value || *end || parsed < min || parsed > max) {
		fprintf(stderr, "%s must be an integer in [%u,%u], got '%s'\n",
			option, min, max, value);
		return -1;
	}

	*out = (unsigned int)parsed;
	return 0;
}

static int parse_uframes(const char *option, const char *value,
			 snd_pcm_uframes_t min, snd_pcm_uframes_t max,
			 snd_pcm_uframes_t *out)
{
	char *end = NULL;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(value, &end, 10);
	if (errno || end == value || *end || parsed < min || parsed > max) {
		fprintf(stderr, "%s must be an integer in [%lu,%lu], got '%s'\n",
			option, (unsigned long)min, (unsigned long)max, value);
		return -1;
	}

	*out = (snd_pcm_uframes_t)parsed;
	return 0;
}

static int parse_unit_float(const char *option, const char *value, float *out)
{
	char *end = NULL;
	float parsed;

	errno = 0;
	parsed = strtof(value, &end);
	if (errno || end == value || *end || !isfinite(parsed) ||
	    parsed < 0.0f || parsed > 1.0f) {
		fprintf(stderr, "%s must be a number in [0.0,1.0], got '%s'\n",
			option, value);
		return -1;
	}

	*out = parsed;
	return 0;
}

static int parse_format_choice(const char *option, const char *value,
			       enum format_choice *out)
{
	if (!strcmp(value, "auto")) {
		*out = FORMAT_AUTO;
		return 0;
	}
	if (!strcmp(value, "f32") || !strcmp(value, "float") ||
	    !strcmp(value, "float_le")) {
		*out = FORMAT_F32;
		return 0;
	}
	if (!strcmp(value, "s32") || !strcmp(value, "s32_le")) {
		*out = FORMAT_S32;
		return 0;
	}

	fprintf(stderr, "%s must be auto, f32, or s32, got '%s'\n", option, value);
	return -1;
}

static int parse_args(int argc, char **argv, struct live_config *cfg)
{
	*cfg = default_config;

	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		const char *value = NULL;

		if (!strcmp(arg, "--help")) {
			usage(stdout, argv[0]);
			exit(0);
		} else if (!strcmp(arg, "--probe")) {
			cfg->probe = 1;
		} else if (!strcmp(arg, "--bypass")) {
			cfg->bypass = 1;
		} else if (!strcmp(arg, "--input")) {
			if (next_arg(argc, argv, &i, arg, &value))
				return -1;
			cfg->input_device = value;
		} else if (!strcmp(arg, "--output")) {
			if (next_arg(argc, argv, &i, arg, &value))
				return -1;
			cfg->output_device = value;
		} else if (!strcmp(arg, "--effect")) {
			if (next_arg(argc, argv, &i, arg, &value))
				return -1;
			cfg->effect_name = value;
			cfg->effect_seen = 1;
		} else if (!strcmp(arg, "--rate")) {
			if (next_arg(argc, argv, &i, arg, &value) ||
			    parse_uint(arg, value, 1, 384000, &cfg->rate))
				return -1;
		} else if (!strcmp(arg, "--channels")) {
			if (next_arg(argc, argv, &i, arg, &value) ||
			    parse_uint(arg, value, 1, 2, &cfg->channels))
				return -1;
		} else if (!strcmp(arg, "--period")) {
			if (next_arg(argc, argv, &i, arg, &value) ||
			    parse_uframes(arg, value, 1, 1048576, &cfg->period_size))
				return -1;
		} else if (!strcmp(arg, "--buffer")) {
			if (next_arg(argc, argv, &i, arg, &value) ||
			    parse_uframes(arg, value, 1, 1048576, &cfg->buffer_size))
				return -1;
		} else if (!strcmp(arg, "--capture-format")) {
			if (next_arg(argc, argv, &i, arg, &value) ||
			    parse_format_choice(arg, value, &cfg->capture_format))
				return -1;
		} else if (!strcmp(arg, "--playback-format")) {
			if (next_arg(argc, argv, &i, arg, &value) ||
			    parse_format_choice(arg, value, &cfg->playback_format))
				return -1;
		} else if (!strcmp(arg, "--wet")) {
			if (next_arg(argc, argv, &i, arg, &value) ||
			    parse_unit_float(arg, value, &cfg->wet))
				return -1;
		} else if (!strcmp(arg, "--pot0") || !strcmp(arg, "--pot1") ||
			   !strcmp(arg, "--pot2") || !strcmp(arg, "--pot3")) {
			unsigned int pot = (unsigned int)(arg[5] - '0');
			if (next_arg(argc, argv, &i, arg, &value) ||
			    parse_unit_float(arg, value, &cfg->pots[pot]))
				return -1;
		} else {
			fprintf(stderr, "Unrecognized option '%s'\n", arg);
			return -1;
		}
	}

	return 0;
}

static int validate_config(struct live_config *cfg)
{
	if (cfg->effect_seen) {
		cfg->effect = audionoise_find_effect(cfg->effect_name);
		if (!cfg->effect) {
			fprintf(stderr, "Unknown effect '%s'\n", cfg->effect_name);
			fprintf(stderr, "Effects: ");
			audionoise_print_effects(stderr);
			return -1;
		}
	}

	if (!cfg->probe && !cfg->bypass && !cfg->effect) {
		fprintf(stderr, "--effect is required unless --probe or --bypass is set\n");
		return -1;
	}

	if (!cfg->input_device || !cfg->output_device) {
		fprintf(stderr, "--input and --output are required unless --help is used\n");
		return -1;
	}

	if (!is_hw_device(cfg->input_device) || !is_hw_device(cfg->output_device)) {
		fprintf(stderr, "Only strict hw: ALSA devices are supported; plughw: and default are intentionally rejected\n");
		return -1;
	}

	if (cfg->buffer_size < cfg->period_size) {
		fprintf(stderr, "--buffer must be greater than or equal to --period\n");
		return -1;
	}

	return 0;
}

static void print_requested(const char *label, const char *device,
			    enum format_choice choice, const struct live_config *cfg)
{
	fprintf(stderr,
		"%s requested: device=%s format=%s rate=%u channels=%u period_size=%lu buffer_size=%lu\n",
		label, device, format_choice_name(choice), cfg->rate, cfg->channels,
		(unsigned long)cfg->period_size, (unsigned long)cfg->buffer_size);
}

static void print_hw_constraints(snd_pcm_t *pcm, const char *label)
{
	snd_pcm_hw_params_t *params;
	snd_pcm_format_mask_t *mask;
	unsigned int min_rate = 0, max_rate = 0;
	unsigned int min_channels = 0, max_channels = 0;
	snd_pcm_uframes_t min_period = 0, max_period = 0;
	snd_pcm_uframes_t min_buffer = 0, max_buffer = 0;
	int dir = 0;
	int err;

	snd_pcm_hw_params_alloca(&params);
	snd_pcm_format_mask_alloca(&mask);

	err = snd_pcm_hw_params_any(pcm, params);
	if (err < 0) {
		fprintf(stderr, "%s constraints unavailable: %s\n", label, snd_strerror(err));
		return;
	}

	snd_pcm_hw_params_get_format_mask(params, mask);
	fprintf(stderr, "%s formats:", label);
	for (int f = 0; f <= SND_PCM_FORMAT_LAST; f++) {
		if (snd_pcm_format_mask_test(mask, (snd_pcm_format_t)f))
			fprintf(stderr, " %s", pcm_format_name((snd_pcm_format_t)f));
	}
	fprintf(stderr, "\n");

	if (!snd_pcm_hw_params_get_rate_min(params, &min_rate, &dir) &&
	    !snd_pcm_hw_params_get_rate_max(params, &max_rate, &dir))
		fprintf(stderr, "%s rates: %u..%u\n", label, min_rate, max_rate);
	if (!snd_pcm_hw_params_get_channels_min(params, &min_channels) &&
	    !snd_pcm_hw_params_get_channels_max(params, &max_channels))
		fprintf(stderr, "%s channels: %u..%u\n", label, min_channels, max_channels);
	if (!snd_pcm_hw_params_get_period_size_min(params, &min_period, &dir) &&
	    !snd_pcm_hw_params_get_period_size_max(params, &max_period, &dir))
		fprintf(stderr, "%s period_size: %lu..%lu\n", label,
			(unsigned long)min_period, (unsigned long)max_period);
	if (!snd_pcm_hw_params_get_buffer_size_min(params, &min_buffer) &&
	    !snd_pcm_hw_params_get_buffer_size_max(params, &max_buffer))
		fprintf(stderr, "%s buffer_size: %lu..%lu\n", label,
			(unsigned long)min_buffer, (unsigned long)max_buffer);
}

static int configure_sw_params(snd_pcm_t *pcm, const char *label,
			       const struct pcm_side *side, snd_pcm_stream_t stream)
{
	snd_pcm_sw_params_t *sw;
	int err;

	if (stream != SND_PCM_STREAM_PLAYBACK)
		return 0;

	snd_pcm_sw_params_alloca(&sw);
	err = snd_pcm_sw_params_current(pcm, sw);
	if (err < 0) {
		fprintf(stderr, "%s sw_params_current failed: %s\n", label, snd_strerror(err));
		return err;
	}

	err = snd_pcm_sw_params_set_start_threshold(pcm, sw, side->period_size);
	if (err < 0) {
		fprintf(stderr, "%s start_threshold=%lu failed: %s\n",
			label, (unsigned long)side->period_size, snd_strerror(err));
		return err;
	}

	err = snd_pcm_sw_params(pcm, sw);
	if (err < 0)
		fprintf(stderr, "%s sw_params failed: %s\n", label, snd_strerror(err));
	return err;
}

static void print_accepted(const char *label, const struct pcm_side *side)
{
	fprintf(stderr,
		"%s accepted: format=%s rate=%u channels=%u period_size=%lu buffer_size=%lu\n",
		label, pcm_format_name(side->format), side->rate, side->channels,
		(unsigned long)side->period_size, (unsigned long)side->buffer_size);
}

static int try_hw_params(snd_pcm_t *pcm, const char *label,
			 const struct live_config *cfg, snd_pcm_format_t format,
			 struct pcm_side *side, snd_pcm_stream_t stream)
{
	snd_pcm_hw_params_t *params;
	unsigned int rate = cfg->rate;
	unsigned int channels = cfg->channels;
	snd_pcm_uframes_t period_size = cfg->period_size;
	snd_pcm_uframes_t buffer_size = cfg->buffer_size;
	int err;

	snd_pcm_hw_params_alloca(&params);

	err = snd_pcm_hw_params_any(pcm, params);
	if (err < 0)
		return err;
	err = snd_pcm_hw_params_set_rate_resample(pcm, params, 0);
	if (err < 0)
		return err;
	err = snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0)
		return err;
	err = snd_pcm_hw_params_set_format(pcm, params, format);
	if (err < 0)
		return err;
	err = snd_pcm_hw_params_set_channels(pcm, params, channels);
	if (err < 0)
		return err;
	err = snd_pcm_hw_params_set_rate(pcm, params, rate, 0);
	if (err < 0)
		return err;
	err = snd_pcm_hw_params_set_period_size(pcm, params, period_size, 0);
	if (err < 0)
		return err;
	err = snd_pcm_hw_params_set_buffer_size(pcm, params, buffer_size);
	if (err < 0)
		return err;
	err = snd_pcm_hw_params(pcm, params);
	if (err < 0)
		return err;

	side->pcm = pcm;
	side->format = format;
	side->rate = rate;
	side->channels = channels;
	side->period_size = period_size;
	side->buffer_size = buffer_size;

	err = configure_sw_params(pcm, label, side, stream);
	if (err < 0)
		return err;

	return 0;
}

static int configure_pcm(const char *label, const char *device,
			 snd_pcm_stream_t stream, enum format_choice choice,
			 const struct live_config *cfg, struct pcm_side *side)
{
	snd_pcm_format_t formats[2];
	int format_count = 0;
	snd_pcm_t *pcm = NULL;
	int err, last_err = -EINVAL;

	switch (choice) {
	case FORMAT_AUTO:
		formats[format_count++] = SND_PCM_FORMAT_FLOAT_LE;
		formats[format_count++] = SND_PCM_FORMAT_S32_LE;
		break;
	case FORMAT_F32:
		formats[format_count++] = SND_PCM_FORMAT_FLOAT_LE;
		break;
	case FORMAT_S32:
		formats[format_count++] = SND_PCM_FORMAT_S32_LE;
		break;
	}

	err = snd_pcm_open(&pcm, device, stream, 0);
	if (err < 0) {
		fprintf(stderr, "%s device unavailable: %s: %s\n",
			label, device, snd_strerror(err));
		return -1;
	}

	print_requested(label, device, choice, cfg);
	for (int i = 0; i < format_count; i++) {
		memset(side, 0, sizeof(*side));
		err = try_hw_params(pcm, label, cfg, formats[i], side, stream);
		if (err == 0) {
			print_accepted(label, side);
			return 0;
		}
		last_err = err;
	}

	fprintf(stderr, "%s hw_params failed: %s\n", label, snd_strerror(last_err));
	print_hw_constraints(pcm, label);
	snd_pcm_close(pcm);
	return -1;
}

static size_t sample_bytes(snd_pcm_format_t format)
{
	return (size_t)snd_pcm_format_physical_width(format) / 8;
}

static size_t frame_bytes(const struct pcm_side *side)
{
	return sample_bytes(side->format) * side->channels;
}

static float clamp_unit(float in)
{
	if (!isfinite(in))
		return 0.0f;
	if (in > 1.0f)
		return 1.0f;
	if (in < -1.0f)
		return -1.0f;
	return in;
}

static s32 unit_float_to_s32(float in)
{
	in = clamp_unit(in);
	if (in >= 1.0f)
		return INT32_MAX;
	if (in <= -1.0f)
		return INT32_MIN;
	return (s32)(in * 2147483648.0f);
}

static float s32_to_unit_float(s32 sample)
{
	return sample * (1.0f / 2147483648.0f);
}

static float read_unit_sample(const void *buffer, snd_pcm_format_t format,
			      size_t sample_index)
{
	if (format == SND_PCM_FORMAT_FLOAT_LE)
		return clamp_unit(((const float *)buffer)[sample_index]);

	return s32_to_unit_float(((const s32 *)buffer)[sample_index]);
}

static void write_output_sample(void *buffer, snd_pcm_format_t format,
				size_t sample_index, s32 sample)
{
	if (format == SND_PCM_FORMAT_FLOAT_LE)
		((float *)buffer)[sample_index] = s32_to_unit_float(sample);
	else
		((s32 *)buffer)[sample_index] = sample;
}

static void process_frames(const struct live_config *cfg,
			   const struct pcm_side *capture,
			   const struct pcm_side *playback,
			   const void *input, void *output,
			   snd_pcm_sframes_t frames)
{
	for (snd_pcm_sframes_t frame = 0; frame < frames; frame++) {
		size_t in_index = (size_t)frame * capture->channels;
		float mono = read_unit_sample(input, capture->format, in_index);

		if (capture->channels == 2)
			mono = 0.5f * (mono + read_unit_sample(input, capture->format, in_index + 1));

		audionoise_effect_tick();

		float dry = process_input(unit_float_to_s32(mono));
		float out = dry;

		if (!cfg->bypass) {
			float wet = cfg->effect->step(dry);
			out = dry + (wet - dry) * cfg->wet;
		}

		s32 sample = process_output(out);
		size_t out_index = (size_t)frame * playback->channels;

		write_output_sample(output, playback->format, out_index, sample);
		if (playback->channels == 2)
			write_output_sample(output, playback->format, out_index + 1, sample);
	}
}

static int recover_pcm(snd_pcm_t *pcm, const char *label, int err,
		       unsigned long *xrun_count)
{
	if (err == -EINTR)
		return 0;
	if (err == -EPIPE || err == -ESTRPIPE)
		(*xrun_count)++;

	int recovered = snd_pcm_recover(pcm, err, 1);
	if (recovered < 0 && (err == -EPIPE || err == -ESTRPIPE))
		recovered = snd_pcm_prepare(pcm);

	if (recovered < 0) {
		fprintf(stderr, "%s recover failed after %s: %s\n",
			label, snd_strerror(err), snd_strerror(recovered));
		return -1;
	}

	return 0;
}

static int write_all_frames(const struct pcm_side *playback, const void *buffer,
			    snd_pcm_sframes_t frames, struct xrun_stats *stats)
{
	snd_pcm_sframes_t offset = 0;
	size_t bytes_per_frame = frame_bytes(playback);

	while (offset < frames && !stop_requested) {
		const char *ptr = (const char *)buffer + (size_t)offset * bytes_per_frame;
		snd_pcm_sframes_t written = snd_pcm_writei(playback->pcm, ptr, frames - offset);

		if (written < 0) {
			if (written == -EINTR && stop_requested)
				return 0;
			if (recover_pcm(playback->pcm, "playback", (int)written, &stats->playback))
				return -1;
			continue;
		}
		if (written == 0) {
			fprintf(stderr, "playback write made no progress\n");
			return -1;
		}

		offset += written;
	}

	return 0;
}

static int report_due(struct timespec *next_report)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now))
		return 0;

	if (now.tv_sec < next_report->tv_sec ||
	    (now.tv_sec == next_report->tv_sec && now.tv_nsec < next_report->tv_nsec))
		return 0;

	next_report->tv_sec = now.tv_sec + 5;
	next_report->tv_nsec = now.tv_nsec;
	return 1;
}

static int run_probe(const struct live_config *cfg)
{
	struct pcm_side capture = { 0 };
	struct pcm_side playback = { 0 };
	int ret = 1;

	if (configure_pcm("capture", cfg->input_device, SND_PCM_STREAM_CAPTURE,
			  cfg->capture_format, cfg, &capture))
		goto out;

	if (configure_pcm("playback", cfg->output_device, SND_PCM_STREAM_PLAYBACK,
			  cfg->playback_format, cfg, &playback)) {
		fprintf(stderr, "playback device cannot match requested rate/channels/period_size/buffer_size; format may differ, but strict hw: disables ALSA conversion\n");
		goto out;
	}

	fprintf(stderr, "probe OK\n");
	ret = 0;

out:
	if (capture.pcm)
		snd_pcm_close(capture.pcm);
	if (playback.pcm)
		snd_pcm_close(playback.pcm);
	return ret;
}

static int run_live(const struct live_config *cfg)
{
	struct pcm_side capture = { 0 };
	struct pcm_side playback = { 0 };
	struct xrun_stats stats = { 0 };
	struct timespec next_report = { 0 };
	void *input = NULL;
	void *output = NULL;
	int ret = 1;

	if (install_signal_handlers())
		return 1;

	if (configure_pcm("capture", cfg->input_device, SND_PCM_STREAM_CAPTURE,
			  cfg->capture_format, cfg, &capture))
		goto out;

	if (configure_pcm("playback", cfg->output_device, SND_PCM_STREAM_PLAYBACK,
			  cfg->playback_format, cfg, &playback)) {
		fprintf(stderr, "playback device cannot match requested rate/channels/period_size/buffer_size; format may differ, but strict hw: disables ALSA conversion\n");
		goto out;
	}

	input = calloc(capture.period_size, frame_bytes(&capture));
	output = calloc(playback.period_size, frame_bytes(&playback));
	if (!input || !output) {
		perror("calloc");
		goto out;
	}

	if (snd_pcm_prepare(capture.pcm) < 0 || snd_pcm_prepare(playback.pcm) < 0) {
		fprintf(stderr, "failed to prepare ALSA PCMs\n");
		goto out;
	}

	if (cfg->bypass) {
		fprintf(stderr, "Live bypass: noise-gated mono dry, wet ignored\n");
	} else {
		fprintf(stderr, "Live %s: ", cfg->effect->name);
		cfg->effect->describe((float *)cfg->pots);
		cfg->effect->init((float *)cfg->pots);
	}

	if (!clock_gettime(CLOCK_MONOTONIC, &next_report))
		next_report.tv_sec += 5;

	while (!stop_requested) {
		snd_pcm_sframes_t frames = snd_pcm_readi(capture.pcm, input, capture.period_size);

		if (frames < 0) {
			if (frames == -EINTR && stop_requested)
				break;
			if (recover_pcm(capture.pcm, "capture", (int)frames, &stats.capture))
				goto out;
			continue;
		}
		if (frames == 0)
			continue;

		process_frames(cfg, &capture, &playback, input, output, frames);
		if (write_all_frames(&playback, output, frames, &stats))
			goto out;

		stats.periods++;
		if (report_due(&next_report))
			fprintf(stderr, "xruns: cap=%lu play=%lu periods=%lu\n",
				stats.capture, stats.playback, stats.periods);
	}

	ret = 0;

out:
	fprintf(stderr, "final: xruns cap=%lu play=%lu periods=%lu\n",
		stats.capture, stats.playback, stats.periods);
	free(input);
	free(output);
	if (capture.pcm) {
		snd_pcm_drop(capture.pcm);
		snd_pcm_close(capture.pcm);
	}
	if (playback.pcm) {
		snd_pcm_drop(playback.pcm);
		snd_pcm_close(playback.pcm);
	}
	return ret;
}

int main(int argc, char **argv)
{
	struct live_config cfg;

	if (parse_args(argc, argv, &cfg) || validate_config(&cfg))
		return 1;

	if (cfg.probe)
		return run_probe(&cfg);

	return run_live(&cfg);
}
