// SPDX-License-Identifier: Apache-2.0
/*
 * Muse's voice on ESP-Mosaico (muse_tts.h): esp-sr's offline Chinese TTS with
 * its Xiaole voice, mapped from the voice_data partition that System Update
 * writes. Mapping changes the flash MMU, which freezes the caches, so it is
 * done at boot: Muse's chat session, which says the replies, has its stack in
 * PSRAM, and a frozen cache asserts on that. The voice itself loads with the
 * first reply.
 *
 * esp-sr says Chinese only: hanzi, digits and Chinese punctuation. Muse writes
 * markdown, emoji, links and the odd English word, so a reply is first cut
 * down to what can be said: markup, emoji and links go, punctuation becomes
 * the Chinese pauses, acronyms are spelled out the way they're read in
 * Chinese ("AI" as 诶艾) and other English words go (the caption still has
 * them). It is then said a sentence at a time, keeping each parse short.
 */
#include "tts_mosaico.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_tts.h"
#include "esp_tts_voice_template.h"
#include "muse_text.h"

static const char *TAG = "tts";

#define SPEED 3                 /* esp-sr's 0 (slowest) to 5 */
#define SAY_MAX 4096            /* a message cut down, letters spelled out */
#define SENTENCE_MAX 80         /* characters said in one go */
#define SENTENCE_SPLIT 40       /* past this, a comma ends one */

static const void *s_voice_data;   /* the voice_data partition, mapped */
static esp_tts_handle_t s_tts;
static bool s_failed;
EXT_RAM_BSS_ATTR static char s_text[SAY_MAX];   /* PSRAM: internal RAM is for Wi-Fi and TLS */
static const char *s_next;      /* the rest of s_text */
EXT_RAM_BSS_ATTR static char s_sentence[SENTENCE_MAX * 3 + 4];
static const short *s_pcm;      /* esp-sr's, until the next esp_tts_stream_play() */
static int s_pcm_left;
static int64_t s_synth_us;
static uint32_t s_samples;

/* On the chat session's task: reads the mapped voice, touches no flash. */
static bool init(void)
{
    if (s_tts || s_failed) {
        return s_tts != NULL;
    }
    s_failed = true;
    esp_log_level_set("tts_parser", ESP_LOG_WARN);   /* it logs every character's pinyin */
    int64_t t0 = esp_timer_get_time();
    esp_tts_voice_t *voice = esp_tts_voice_set_init(&esp_tts_voice_template, (void *)s_voice_data);
    s_tts = voice ? esp_tts_create(voice) : NULL;
    if (!s_tts || voice->sample_rate != 16000 || voice->bit_width != 16) {
        ESP_LOGE(TAG, "voice set didn't load (%d Hz, %d bit)", voice ? voice->sample_rate : 0,
                 voice ? voice->bit_width : 0);
        if (s_tts) {
            esp_tts_destroy(s_tts);
            s_tts = NULL;
        }
        if (voice) {
            esp_tts_voice_set_free(voice);
        }
        return false;
    }
    ESP_LOGI(TAG, "voice %s ready in %d ms", voice->voice_name ? voice->voice_name : "?",
             (int)((esp_timer_get_time() - t0) / 1000));
    s_failed = false;
    return true;
}

/* ---- Cutting a reply down to what can be said ---- */

typedef struct {
    char *buf;
    size_t len, cap;
    bool paused;            /* the last thing put was a pause, or nothing yet */
    bool comma;             /* that pause is the "，" at buf + len - 3 */
    int hanzi;
} out_t;

static void put(out_t *o, const char *s, size_t n)
{
    if (o->len + n < o->cap) {
        memcpy(o->buf + o->len, s, n);
        o->len += n;
        o->buf[o->len] = '\0';
        o->paused = false;
    }
}

static void put_str(out_t *o, const char *s)
{
    put(o, s, strlen(s));
}

/* A pause: "，" or the end of a sentence ("。", "！", "？"), one at a time;
 * an end right after a comma takes its place. */
static void put_pause(out_t *o, const char *mark)
{
    bool comma = !strcmp(mark, "，");
    if (!o->paused) {
        put_str(o, mark);
        o->paused = true;
        o->comma = comma;
    } else if (o->comma && !comma) {
        memcpy(o->buf + o->len - 3, mark, 3);   /* all three are three bytes */
        o->comma = false;
    }
}

static bool hanzi(int32_t cp)
{
    return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF);
}

/* Letters as they're read out in Chinese. */
static const char *const LETTERS[26] = {
    "诶", "比", "西", "迪", "伊", "艾弗", "吉", "艾尺", "艾", "杰", "开", "艾勒", "艾姆",
    "恩", "欧", "批", "丘", "阿", "艾斯", "踢", "优", "维", "达布留", "艾克斯", "歪", "贼",
};

static const struct {
    const char *word, *say;
} WORDS[] = {
    { "muse", "缪斯" },
    { "wifi", "歪发爱" },
    { "ok", "欧开" },
    { "app", "诶批批" },
};

/* An English word (letters, maybe hyphenated): a few are said, acronyms are
 * spelled, the rest left out. */
static void word(out_t *o, const char *w, size_t n)
{
    char lower[24];
    size_t m = 0;
    bool upper = true;
    for (size_t i = 0; i < n; i++) {
        if (w[i] == '-') {
            continue;
        }
        upper &= isupper((unsigned char)w[i]) != 0;
        if (m < sizeof(lower) - 1) {
            lower[m++] = (char)tolower((unsigned char)w[i]);
        }
    }
    lower[m] = '\0';
    for (size_t i = 0; i < sizeof(WORDS) / sizeof(WORDS[0]); i++) {
        if (!strcmp(lower, WORDS[i].word)) {
            put_str(o, WORDS[i].say);
            return;
        }
    }
    if (upper && m <= 5) {
        for (size_t i = 0; i < m; i++) {
            put_str(o, LETTERS[lower[i] - 'a']);
        }
    }
}

static bool starts(const char *p, const char *prefix)
{
    return strncasecmp(p, prefix, strlen(prefix)) == 0;
}

/* in, cut down to hanzi, digits and Chinese pauses, into out. */
static void speakable(const char *in, out_t *o)
{
    const char *p = in;
    while (*p) {
        if (starts(p, "http://") || starts(p, "https://") || starts(p, "www.")) {
            while (*p && *p != ' ' && *p != '\n' && !(*p & 0x80)) {
                p++;   /* a link, up to a space or the next hanzi */
            }
            continue;
        }
        if (isdigit((unsigned char)*p)) {
            /* A number: thousands commas go, a decimal point stays, a percent goes first. */
            char num[32];
            size_t n = 0;
            while (n < sizeof(num) - 1) {
                if (isdigit((unsigned char)*p)) {
                    num[n++] = *p++;
                } else if (*p == ',' && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2])
                           && isdigit((unsigned char)p[3]) && !isdigit((unsigned char)p[4])) {
                    p++;
                } else if (*p == '.' && isdigit((unsigned char)p[1])) {
                    num[n++] = *p++;
                } else {
                    break;
                }
            }
            num[n] = '\0';
            if (*p == '%') {
                put_str(o, "百分之");
                p++;
            }
            put_str(o, num);
            if ((*p == ':' || *p == '~') && isdigit((unsigned char)p[1])) {
                put_str(o, *p == ':' ? "点" : "到");   /* 10:30, 3~5 */
                p++;
            }
            continue;
        }
        if (isalpha((unsigned char)*p)) {
            const char *w = p;
            while (isalpha((unsigned char)*p) || (*p == '-' && isalpha((unsigned char)p[1]))) {
                p++;
            }
            word(o, w, p - w);
            continue;
        }
        size_t len;
        int32_t cp = muse_text_decode(p, &len);
        const char *at = p;
        p += len;
        if (hanzi(cp)) {
            put(o, at, len);
            o->hanzi++;
            continue;
        }
        switch (cp) {
        case '.': case '!': case '?': case '\n':
        case 0x3002: case 0xFF01: case 0xFF1F: case 0x2026:   /* 。！？… */
            put_pause(o, cp == '!' || cp == 0xFF01 ? "！" : cp == '?' || cp == 0xFF1F ? "？" : "。");
            break;
        case ',': case ';': case ':': case '(': case ')':
        case 0x3001: case 0xFF0C: case 0xFF1B: case 0xFF1A:   /* 、，；： */
        case 0xFF08: case 0xFF09: case 0x2014: case 0x2013:   /* （）—– */
            put_pause(o, "，");
            break;
        case 0x2103:   /* ℃ */
            put_str(o, "摄氏度");
            break;
        case 0x00B0:   /* ° */
            put_str(o, *p == 'C' ? "摄氏度" : *p == 'F' ? "华氏度" : "度");
            p += *p == 'C' || *p == 'F';
            break;
        case 0xFF5E:   /* ～ */
            put_str(o, "到");
            break;
        default:
            break;   /* spaces, quotes, markup, emoji, other scripts */
        }
    }
}

/* ---- Saying it ---- */

/* The next sentence of s_text into s_sentence: up to a full stop, or a comma
 * once it's long, or SENTENCE_MAX characters. False when there's none left. */
static bool next_sentence(void)
{
    while (*s_next) {
        const char *start = s_next, *p = s_next, *comma = NULL;
        int chars = 0;
        bool sayable = false;
        while (*p) {
            size_t len;
            int32_t cp = muse_text_decode(p, &len);
            p += len;
            chars++;
            sayable |= hanzi(cp) || (cp >= '0' && cp <= '9');
            if (cp == 0x3002 || cp == 0xFF01 || cp == 0xFF1F) {
                break;
            }
            if (cp == 0xFF0C) {
                comma = p;
                if (chars >= SENTENCE_SPLIT) {
                    break;
                }
            }
            if (chars >= SENTENCE_MAX) {
                p = comma ? comma : p;
                break;
            }
        }
        s_next = p;
        if (!sayable) {
            continue;
        }
        size_t n = p - start;
        memcpy(s_sentence, start, n);
        s_sentence[n] = '\0';
        if (esp_tts_parse_chinese(s_tts, s_sentence)) {
            return true;
        }
        ESP_LOGW(TAG, "esp-sr couldn't parse a %d-character sentence", chars);
        esp_tts_stream_reset(s_tts);
    }
    return false;
}

static bool tts_begin(const char *text)
{
    if (!init()) {
        return false;
    }
    out_t o = { .buf = s_text, .cap = sizeof(s_text), .paused = true };
    s_text[0] = '\0';
    speakable(text, &o);
    s_next = o.hanzi ? s_text : "";   /* not Chinese: it's shown instead */
    s_pcm_left = 0;
    s_synth_us = 0;
    s_samples = 0;
    if (!next_sentence()) {
        ESP_LOGI(TAG, "nothing to say in Chinese in a %u-byte message", (unsigned)strlen(text));
        return false;
    }
    return true;
}

static size_t tts_read(int16_t *pcm, size_t cap)
{
    while (!s_pcm_left) {
        int64_t t0 = esp_timer_get_time();
        int len = 0;
        const short *data = esp_tts_stream_play(s_tts, &len, SPEED);
        s_synth_us += esp_timer_get_time() - t0;
        if (len > 0) {
            s_pcm = data;
            s_pcm_left = len;
            break;
        }
        esp_tts_stream_reset(s_tts);
        if (!next_sentence()) {
            return 0;
        }
    }
    size_t n = (size_t)s_pcm_left < cap ? (size_t)s_pcm_left : cap;
    memcpy(pcm, s_pcm, n * sizeof(int16_t));
    s_pcm += n;
    s_pcm_left -= (int)n;
    s_samples += n;
    return n;
}

static void tts_end(void)
{
    if (!s_tts) {
        return;
    }
    esp_tts_stream_reset(s_tts);
    s_pcm_left = 0;
    s_next = "";
    if (s_samples) {
        ESP_LOGI(TAG, "said %.1f s of speech in %.1f s of synthesis", s_samples / 16000.0, s_synth_us / 1e6);
        s_samples = 0;
    }
}

static const muse_tts_t s_voice = {
    .name = "esp-sr Xiaole",
    .begin = tts_begin,
    .read = tts_read,
    .end = tts_end,
};

const muse_tts_t *tts_mosaico_start(void)
{
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "voice_data");
    if (!part) {
        ESP_LOGW(TAG, "no voice_data partition: replies stay unspoken (install with system-update)");
        return NULL;
    }
    esp_partition_mmap_handle_t map;
    esp_err_t err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &s_voice_data, &map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mapping voice_data: %s", esp_err_to_name(err));
        return NULL;
    }
    if (*(const uint32_t *)s_voice_data == 0xFFFFFFFF) {
        ESP_LOGW(TAG, "voice_data is empty: replies stay unspoken (install with system-update)");
        esp_partition_munmap(map);
        return NULL;
    }
    return &s_voice;
}
