// SPDX-License-Identifier: Apache-2.0
/*
 * The board's own Home Link commands, through Muse's weak
 * muse_gadget_platform_add_commands() and muse_gadget_platform_command()
 * hooks: the Interaction module's sensors (presence.read), LEDs (lights.set)
 * and IR LED (ir.send_nec), by way of mosaico_presence, whose task owns the
 * module. They're advertised whether or not a module is in, and say so when
 * it isn't. The hook also tells Muse which languages the voice reads.
 *
 * An entry in the register's commands_v2 is {"description", "required":
 * {name: {"type", "description"}}, "optional": {...}, "timeout_ms"}; a result
 * is {"ok": true, "payload": {...}} or {"ok": false, "error": {"code",
 * "message"}}, as Muse's own commands answer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "mosaico_presence.h"

#define LIGHTS_MAX_S 3600

static cJSON *param(const char *type, const char *description)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", type);
    cJSON_AddStringToObject(p, "description", description);
    return p;
}

static void add_command(cJSON *commands, const char *name, const char *description,
                        cJSON *required, cJSON *optional)
{
    cJSON *command = cJSON_CreateObject();
    cJSON_AddStringToObject(command, "description", description);
    cJSON_AddItemToObject(command, "required", required ? required : cJSON_CreateObject());
    cJSON_AddItemToObject(command, "optional", optional ? optional : cJSON_CreateObject());
    cJSON_AddItemToObject(commands, name, command);
}

static cJSON *fail(const char *code, const char *message)
{
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", false);
    cJSON *error = cJSON_AddObjectToObject(result, "error");
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    return result;
}

/* A module result as a command result: what went wrong is in the message. */
static cJSON *module_result(esp_err_t err)
{
    if (err == ESP_ERR_INVALID_STATE) {
        return fail("no_module", "no Interaction module is plugged in");
    }
    if (err == ESP_ERR_TIMEOUT) {
        return fail("busy", "the module didn't answer in time: try again");
    }
    if (err != ESP_OK) {
        return fail("module_failed", esp_err_to_name(err));
    }
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    return result;
}

/* An optional integer parameter: false, with *why, if it's there but wrong. */
static bool int_param(cJSON *params, const char *name, bool required, int lo, int hi, int *out,
                      bool *given, const char **why, char *buf, size_t cap)
{
    cJSON *item = params ? cJSON_GetObjectItem(params, name) : NULL;
    *given = item != NULL;
    if (!item) {
        if (required) {
            snprintf(buf, cap, "%s is required", name);
            *why = buf;
        }
        return !required;
    }
    if (!cJSON_IsNumber(item) || item->valueint < lo || item->valueint > hi) {
        snprintf(buf, cap, "%s must be %d-%d", name, lo, hi);
        *why = buf;
        return false;
    }
    *out = item->valueint;
    return true;
}

/* "#rrggbb" (or "off") into r, g, b. */
static bool parse_colour(const char *text, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (strcmp(text, "off") == 0) {
        *r = *g = *b = 0;
        return true;
    }
    unsigned rr, gg, bb;
    char end;
    if (sscanf(text, "#%2x%2x%2x%c", &rr, &gg, &bb, &end) != 3 || strlen(text) != 7) {
        return false;
    }
    *r = rr;
    *g = gg;
    *b = bb;
    return true;
}

static cJSON *presence_read(void)
{
    mosaico_presence_status_t st;
    mosaico_presence_status(&st);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON *payload = cJSON_AddObjectToObject(result, "payload");
    cJSON_AddBoolToObject(payload, "module", st.module);
    if (st.module) {
        cJSON_AddBoolToObject(payload, "someone_there", st.someone);
        if (st.last_motion_us) {
            cJSON_AddNumberToObject(payload, "last_motion_s_ago",
                                    (double)((esp_timer_get_time() - st.last_motion_us) / 1000000));
        } else {
            cJSON_AddNullToObject(payload, "last_motion_s_ago");
        }
        if (st.light >= 0) {
            cJSON_AddNumberToObject(payload, "light_level", st.light);
        } else {
            cJSON_AddNullToObject(payload, "light_level");
        }
    } else {
        cJSON_AddNullToObject(payload, "someone_there");
        cJSON_AddNullToObject(payload, "last_motion_s_ago");
        cJSON_AddNullToObject(payload, "light_level");
    }
    return result;
}

static cJSON *lights_set(cJSON *params)
{
    cJSON *colour = params ? cJSON_GetObjectItem(params, "color") : NULL;
    uint8_t r, g, b;
    if (!cJSON_IsString(colour) || !colour->valuestring || !parse_colour(colour->valuestring, &r, &g, &b)) {
        return fail("invalid_params", "color must be #rrggbb or off");
    }
    int seconds = 0;
    bool given;
    const char *why = NULL;
    char buf[64];
    if (!int_param(params, "seconds", false, 0, LIGHTS_MAX_S, &seconds, &given, &why, buf, sizeof(buf))) {
        return fail("invalid_params", why);
    }
    return module_result(mosaico_presence_lights(r, g, b, (uint32_t)seconds * 1000));
}

static cJSON *ir_send_nec(cJSON *params)
{
    int address = 0, command = 0;
    bool given;
    const char *why = NULL;
    char buf[64];
    if (!int_param(params, "address", true, 0, 255, &address, &given, &why, buf, sizeof(buf))
        || !int_param(params, "command", true, 0, 255, &command, &given, &why, buf, sizeof(buf))) {
        return fail("invalid_params", why);
    }
    return module_result(mosaico_presence_ir_send_nec((uint8_t)address, (uint8_t)command));
}

void muse_gadget_platform_add_commands(cJSON *commands)
{
    /* What the voice reads: the speech server does Chinese and English,
     * esp-sr on the board Chinese only. */
    cJSON *say = cJSON_GetObjectItem(commands, "voice.say");
    cJSON *description = say ? cJSON_GetObjectItem(say, "description") : NULL;
    if (cJSON_IsString(description)) {
        const char *server = "";
#ifdef CONFIG_MOSAICO_TTS_URL
        server = CONFIG_MOSAICO_TTS_URL;
#endif
        const char *languages = server[0]
            ? " It reads Chinese, and English while the speech server on the LAN is "
              "reachable; when that is away, English words are skipped, so prefer Chinese."
            : " It reads Chinese only: English words are skipped, so write Chinese.";
        size_t n = strlen(description->valuestring) + strlen(languages) + 1;
        char *text = malloc(n);
        if (text) {
            snprintf(text, n, "%s%s", description->valuestring, languages);
            cJSON_ReplaceItemInObject(say, "description", cJSON_CreateString(text));
            free(text);
        }
    }

    add_command(commands, "presence.read",
                "Read the Interaction module's sensors: whether someone is in front of the "
                "gadget (its motion sensor saw movement in the last minute), how many "
                "seconds ago it last saw movement, and the ambient light level, 0 (dark) "
                "to 100 (bright), relative rather than lux. The module plugs into one of "
                "the gadget's slots; without it every reading is null.",
                NULL, NULL);

    cJSON *required = cJSON_CreateObject();
    cJSON_AddItemToObject(required, "color",
                          param("string", "The colour as #rrggbb (for example #ff8000), or off."));
    cJSON *optional = cJSON_CreateObject();
    cJSON_AddItemToObject(optional, "seconds",
                          param("integer", "How long to keep the colour, 1 to 3600; left out or "
                                           "0, it stays until changed."));
    add_command(commands, "lights.set",
                "Light the Interaction module's six LEDs one colour, or turn them off. The "
                "gadget's own welcome (warm white for 6 s when someone arrives) leaves a "
                "colour set here alone. Without the module this fails.",
                required, optional);

    required = cJSON_CreateObject();
    cJSON_AddItemToObject(required, "address", param("integer", "The NEC device address byte, 0-255."));
    cJSON_AddItemToObject(required, "command", param("integer", "The NEC command byte, 0-255."));
    add_command(commands, "ir.send_nec",
                "Send one NEC infrared code from the Interaction module's IR LED, to control "
                "a TV, air conditioner, fan or other appliance with an NEC remote: the "
                "device's address byte and the key's command byte. The module must be "
                "pointed at the appliance. Without the module this fails.",
                required, NULL);
}

cJSON *muse_gadget_platform_command(const char *command, cJSON *params)
{
    if (strcmp(command, "presence.read") == 0) {
        return presence_read();
    }
    if (strcmp(command, "lights.set") == 0) {
        return lights_set(params);
    }
    if (strcmp(command, "ir.send_nec") == 0) {
        return ir_send_nec(params);
    }
    return NULL;
}
