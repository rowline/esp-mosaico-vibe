// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Speech from a server on the LAN (CONFIG_MOSAICO_TTS_URL): an OpenAI-style
 * /v1/audio/speech that streams 16-bit mono PCM, resampled here to 16 kHz.
 * One request per message, fetched on its own task into a buffer that
 * tts_remote_read() drains without waiting.
 */
typedef struct tts_remote tts_remote_t;

/* Starts fetching text said in language ("chinese", "english"). NULL if the
 * task or its buffer can't be had. */
tts_remote_t *tts_remote_start(const char *text, const char *language);

#define TTS_REMOTE_LATER (-1)   /* nothing yet: still on its way */
#define TTS_REMOTE_DONE (-2)    /* all of it has been read */
#define TTS_REMOTE_FAILED (-3)  /* the server couldn't be reached or stopped short */

/* Up to cap samples, or one of the codes above. */
int tts_remote_read(tts_remote_t *r, int16_t *pcm, size_t cap);

/* Whether any speech arrived. */
bool tts_remote_said(const tts_remote_t *r);

/* Stops the fetch and lets go of r. */
void tts_remote_stop(tts_remote_t *r);
