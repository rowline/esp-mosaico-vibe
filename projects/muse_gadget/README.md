# muse_gadget

Meta's Muse Gadget SDK firmware (`../muse-gadget-sdk/esp32`, branch
`esp-mosaico`) built as an ESP-Mosaico application. Vibe Mode stays in the first
2 MB, so the gadget installs and updates through `mosaico.py` like any other app.
This stage has no screen or audio: status shows in the log, and the AI key is
the Muse button.

The SDK checkout is not part of this repository. Clone
https://github.com/facebookincubator/muse-gadget-sdk to
`projects/muse-gadget-sdk` and check out the `esp-mosaico` branch, which for
now exists only in a local checkout.

## Build and install

From the workspace root, with `source .tools/env.sh` on this Mac:

```sh
python3 submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/muse_gadget build
python3 mosaico.py iris system-update --project projects/muse_gadget
python3 mosaico.py iris logs --project projects/muse_gadget --timeout 20
```

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
