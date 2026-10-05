# muse_gadget

Meta's Muse Gadget SDK firmware (`../muse-gadget-sdk/esp32`, branch
`esp-mosaico`) built as an ESP-Mosaico application. Vibe Mode stays in the first
2 MB, so the gadget installs and updates through `mosaico.py` like any other app.
Muse's screen UI runs on the 480 px AMOLED (`components/mosaico_board`), the AI
key is the talk button and BOOT the menu button, and a camera module in the
left slot serves Muse's `camera.capture` (`components/mosaico_camera`). An
Interaction module in either slot greets whoever walks up
(`components/mosaico_presence`).

The SDK is the submodule `projects/muse-gadget-sdk`: the `esp-mosaico` branch
of https://github.com/rowline/muse-gadget-sdk, a fork of
https://github.com/facebookincubator/muse-gadget-sdk. Fetch it with
`git submodule update --init projects/muse-gadget-sdk`.

## Build and install

From the workspace root, with `source .tools/env.sh` on this Mac:

```sh
python3 submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/muse_gadget build
python3 mosaico.py iris system-update --project projects/muse_gadget
python3 mosaico.py iris logs --project projects/muse_gadget --timeout 20
```

`system-update` is needed when the partition table changes; otherwise
`python3 mosaico.py iris app-update --project projects/muse_gadget` replaces
only the app and keeps Muse's pairing and Wi-Fi.

Never use `idf.py flash`: it replaces the retained bootloader and partition
table. `system_update.cmake` blocks those targets.

The SDK token is read from `~/.config/muse/sdk_token` into
`build/sdkconfig.token` at configure time and is never committed. After
changing the token, delete `build/sdkconfig` and rebuild.

## Settings for one network

Settings that only fit one network go in `sdkconfig.local`, which is
git-ignored and loaded last when present; `sdkconfig.local.example` shows the
format. Where Meta's services are blocked, the board can reach them through a
computer on the LAN that forwards traffic: set
`CONFIG_HOMEHUB_UPLINK_GATEWAY` to that computer's address and
`CONFIG_HOMEHUB_UPLINK_DNS` to a DNS server outside the LAN. On a Mac running
Clash Verge in TUN mode with `dns-hijack: any:53`, the Mac also needs
`sudo sysctl -w net.inet.ip.forwarding=1`, which resets on reboot, and a fixed
address in the router. Once the board joins Wi-Fi, the log shows
`link.uplink: internet via <gateway>`.

## Chinese captions

Muse's caption font, unscii, is ASCII only. Captions and replies fall back to
`components/mosaico_board/fonts/cjk_16.bin`: GB2312's 6763 hanzi and its
punctuation from Source Han Sans CN at 16 px (SIL Open Font License,
`fonts/OFL.txt`), about 0.9 MB in the image and loaded into PSRAM when the UI
starts. `fonts/make_cjk_font.py` rebuilds it. To preview in the SDK's
simulator, build it with `-DMUSE_SIM_BOARD=esp_mosaico` and run it with
`MUSE_SIM_WIDE_FONT` set to that file.

## Voice

With the speaker on, replies are said aloud and the captions follow the
speech; with it off, they're shown at reading pace as upstream. The voices,
in `components/mosaico_tts`:

- A speech server on the LAN, when `CONFIG_MOSAICO_TTS_URL` names one (in
  `sdkconfig.local`): an OpenAI-style `/v1/audio/speech` that streams 16-bit
  PCM, such as a computer's Qwen3-TTS. It says Chinese and English alike. The
  board asks for 16 kHz, paced at 1.5 times speech after the first second:
  streamed as fast as it was synthesized, speech held Wi-Fi's receive buffers
  in internal DMA RAM until the board crashed.
- esp-sr's offline Chinese TTS (Xiaole voice), when the server can't be
  reached or isn't set. Its 2.9 MB voice sits in its own `voice_data`
  partition, which System Update writes from esp-sr's
  `esp_tts_voice_data_xiaole.dat`; app-update leaves it in place. It reads
  Chinese only: acronyms are spelled the Chinese way ("AI" as 诶艾), other
  English words are skipped, and a reply with no Chinese in it is shown, not
  said.

Links, markdown and emoji are taken out before either voice reads a reply,
and the captions leave markdown out too.

## Camera

The module's sensor (OV3640 or SC101IOT) streams YUV422; a capture takes one
frame after auto-exposure settles, encodes it as JPEG turned upright (720 x 1280
from the SC101IOT) and shows it on the screen for 4 s. The sensor is powered
only during a capture. The camera's data and flash pins share the USB
Serial/JTAG pads, so that port is off and Muse's serial tools use the UART
console.

## Presence greeting

With an Interaction module in either slot, `components/mosaico_presence`
watches its PIR sensor:

- Motion after a minute without any turns the screen on and lights the
  module's six LEDs warm white for 6 s.
- If nobody was seen for 10 minutes before that, Muse also greets during those
  6 s: the happy animation, a chirp and a caption for the time of day, such as
  `晚上好，Rollin！`. The caption goes out with the lights.
- Further motion keeps the screen from auto-sleeping but neither turns it on
  nor relights the LEDs, so a screen turned off with BOOT stays off until the
  room has been empty for a minute.
- After a minute without motion, the next motion counts as a new arrival;
  Muse's own auto-sleep turns the screen off.

The name, time zone and NTP server are the `CONFIG_MOSAICO_PRESENCE_*`
options; the name is set in `sdkconfig.mosaico`. Muse doesn't set the clock,
so the component starts SNTP once Wi-Fi is up; until then the greeting is
`你好，Rollin！`. A module plugged in while Muse runs is found within seconds;
one pulled out stops working until the next restart. PIR sees movement, not
someone sitting still, so sitting motionless for a minute and then moving
counts as arriving again.

## Pairing

1. Join the Mac's hotspot from the phone (2.4 GHz; ESP32-S31 has no 5 GHz).
2. In the Muse app: Settings > Devices > Developer mode, then Add Device and
   pick `MuseGadget-XXXXXX`.
3. When the log asks for confirmation, short-press the AI key within 60 s.
4. Give the hotspot's Wi-Fi name and password in the app.

Hold the AI key for 5 s while running to forget pairing and Wi-Fi. Holding it
while powering on enters Vibe Mode instead.

## What differs from upstream Muse

| | Upstream Muse | Here |
| --- | --- | --- |
| ESP-IDF | v6.0.1 | workspace pin (6.2, ESP32-S31) |
| Partition table | at 0x10000, dual OTA slots | Vibe Mode layout at 0x8000, one app slot |
| Updates and rollback | Muse OTA; rollback if Muse is unreachable for 5 min | Vibe Mode writes and accepts images (`HOMEHUB_OTA_ENABLED=n`, `HOMEHUB_OTA_ROLLBACK_GUARD=n`) |
| Image signing | dev key | off |
| Start-up | Muse only | `components/mosaico_platform` starts ESP-Iris through Muse's weak `muse_gadget_platform_start()` hook |

SDK changes on the `esp-mosaico` branch, one commit each:

- `devices/sdkconfig.esp-mosaico`, the board overlay;
- the `muse_gadget_platform_start()` hook and `HOMEHUB_OTA_ROLLBACK_GUARD`;
- `HOMEHUB_UPLINK_GATEWAY` and `HOMEHUB_UPLINK_DNS` (`main/uplink.c` and an
  lwIP next-hop hook).
