// The GameCube intro splash: callbacks and soundtrack; see port/gc_intro.h.
// Adapted from Rinnegatamante's vitaGL custom_splashscreen sample.
#if defined(PORT_VITA) && defined(STRIKERS_VITA_VITAGL)
#include "port/gc_intro.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitasdk.h>
#include <vitaGL.h>

#include "gc_intro_scene.h"

#define AUDIO_SAMPLES 2048
#define AUDIO_RATE 48000
#define FADE_SECONDS 0.8f

typedef struct {
	uint8_t *pcm;
	uint32_t pcm_size;
	SceUID thread, start_sema;
	int port;
	volatile int stop;
	int started;
	int16_t buffers[2][AUDIO_SAMPLES * 2] __attribute__((aligned(64)));
} IntroAudio;

static IntroAudio audio;
static float fade_start = -1.f;
static int skip_requested;

static int load_audio(void) {
	SceUID fd = sceIoOpen(GC_INTRO_DIR "gcintro_audio.pcm", SCE_O_RDONLY, 0);
	if (fd < 0)
		return -1;
	SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
	if (size <= 0 || size > 8 * 1024 * 1024 || sceIoLseek(fd, 0, SCE_SEEK_SET) < 0) {
		sceIoClose(fd);
		return -1;
	}
	audio.pcm = malloc((size_t)size);
	if (!audio.pcm) {
		sceIoClose(fd);
		return -1;
	}
	uint32_t done = 0;
	while (done < (uint32_t)size) {
		int read = sceIoRead(fd, audio.pcm + done, (uint32_t)size - done);
		if (read <= 0)
			break;
		done += read;
	}
	sceIoClose(fd);
	audio.pcm_size = done;
	return done == (uint32_t)size ? 0 : -1;
}

static uint32_t fill_audio(unsigned index, uint32_t offset) {
	uint32_t left = audio.pcm_size - offset;
	uint32_t amount = left < sizeof(audio.buffers[0]) ? left : sizeof(audio.buffers[0]);
	memset(audio.buffers[index], 0, sizeof(audio.buffers[index]));
	memcpy(audio.buffers[index], audio.pcm + offset, amount);
	return offset + amount;
}

static int audio_thread(SceSize argc, void *args) {
	(void)argc;
	(void)args;
	if (sceKernelWaitSema(audio.start_sema, 1, NULL) < 0 || audio.stop)
		return sceKernelExitThread(0);
	const uint32_t blocks = (audio.pcm_size + sizeof(audio.buffers[0]) - 1) / sizeof(audio.buffers[0]);
	uint32_t offset = 0;
	for (uint32_t i = 0; i < 2 && i < blocks; i++)
		offset = fill_audio(i, offset);
	uint32_t played = 0;
	for (uint32_t i = 0; i < blocks && !audio.stop; i++) {
		unsigned index = i & 1;
		if (sceAudioOutOutput(audio.port, audio.buffers[index]) < 0)
			break;
		played++;
		// sceAudioOutOutput returns when playback starts, not when the buffer is free.
		if (i > 0 && i + 1 < blocks)
			offset = fill_audio(index ^ 1, offset);
	}
	if (played)
		sceAudioOutOutput(audio.port, NULL);
	return sceKernelExitThread(0);
}

static int start_audio(void) {
	// BGM, not MAIN: the game's own mixer owns a MAIN port and may open it before the intro ends.
	audio.port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_SAMPLES, AUDIO_RATE,
		SCE_AUDIO_OUT_MODE_STEREO);
	if (audio.port < 0)
		return -1;
	audio.start_sema = sceKernelCreateSema("GC Intro Audio Start", 0, 0, 1, NULL);
	if (audio.start_sema < 0)
		return -1;
	audio.thread = sceKernelCreateThread("GC Intro Audio", audio_thread, 0x60, 0x4000, 0,
		SCE_KERNEL_CPU_MASK_USER_ALL, NULL);
	if (audio.thread < 0)
		return -1;
	if (sceKernelStartThread(audio.thread, 0, NULL) < 0) {
		sceKernelDeleteThread(audio.thread);
		audio.thread = -1;
		return -1;
	}
	return 0;
}

static void stop_audio(void) {
	audio.stop = 1;
	if (audio.start_sema >= 0)
		sceKernelSignalSema(audio.start_sema, 1);
	if (audio.thread >= 0) {
		sceKernelWaitThreadEnd(audio.thread, NULL, NULL);
		sceKernelDeleteThread(audio.thread);
	}
	if (audio.start_sema >= 0)
		sceKernelDeleteSema(audio.start_sema);
	if (audio.port >= 0)
		sceAudioOutReleasePort(audio.port);
	free(audio.pcm);
	memset(&audio, 0, sizeof(audio));
	audio.thread = audio.start_sema = audio.port = -1;
}

static GLboolean splash_init(const vglSplashDetails *details) {
	memset(&audio, 0, sizeof(audio));
	audio.thread = audio.start_sema = audio.port = -1;
	fade_start = -1.f;
	skip_requested = 0;
	if (gcSceneInit(details) < 0) {
		fprintf(stderr, "[gcintro] scene unavailable; no intro\n");
		return GL_FALSE;
	}
	// Silent rather than absent if the soundtrack is missing or the port is busy.
	if (load_audio() < 0 || start_audio() < 0) {
		fprintf(stderr, "[gcintro] soundtrack unavailable; playing silently\n");
		stop_audio();
	}
	fprintf(stderr, "[gcintro] intro ready: %.3f seconds\n", (double)gcSceneDuration());
	return GL_TRUE;
}

static GLboolean splash_render(const vglSplashDetails *details) {
	if (!audio.started && audio.start_sema >= 0) {
		audio.started = 1;
		sceKernelSignalSema(audio.start_sema, 1);
	}
	if (!skip_requested) {
		SceCtrlData pad;
		memset(&pad, 0, sizeof(pad));
		if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0 &&
			(pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START)))
			skip_requested = 1;
	}
	const float time = details->elapsed_seconds;
	if (details->must_stop && fade_start < 0.f && (skip_requested || time >= gcSceneDuration()))
		fade_start = time;
	const float fade = fade_start < 0.f ? 1.f : fmaxf(0.f, 1.f - (time - fade_start) / FADE_SECONDS);
	gcSceneDraw(details, fade);
	return fade_start >= 0.f && fade <= 0.f;
}

static GLboolean splash_term(const vglSplashDetails *details) {
	(void)details;
	stop_audio();
	gcSceneTerm();
	return GL_TRUE;
}

int PortGcIntroSetup(void) {
	const char *v = getenv("STRIKERS_VITA_GC_INTRO");
	if (v != NULL && strcmp(v, "0") == 0)
		return 0;
	SceIoStat st;
	if (sceIoGetstat(GC_INTRO_DIR "scene.bin", &st) < 0)
		return 0;
	vglSetupCustomSplashscreen(splash_init, splash_term, splash_render, GL_TRUE);
	return 1;
}
#endif
