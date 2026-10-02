# MeshniacInterface

HOST-side UART bridge to the Meshniac flexible mesh network module
(U2 ESP-NOW + LoRa radio ◄─► U4 main ◄─► U6 gateway). The HOST MCU talks
to U2 over Serial1; this class owns that link: the UART event task,
framing, JSON message handling (ArduinoJson), RTC seeding (ESP32Time),
and the callback hooks into the host firmware.

Extracted 2026-09-01 from the `meshniac-v3-lora` monorepo, byte-identical
to the `lib/meshniac_interface/` copies both HOSTs carried on that date.
History before v1.0.0 lives in that monorepo.

## Consumers

| Project | Env | Pin |
|---|---|---|
| HOST m1 (`HOST_m1-t2-r16_v1`, ESP32 DevKit — bench hardware; NOTHING deployed as of 2026-09-30) | `esp32doit-devkit-v1` | `#v2.0.0` |
| HOST m2 (`host_m2`, ESP32-S3 — no hardware exists yet) | `m2_esp32s3` | `#v2.0.0` |
| HOST M3 (`host_m3`, ESP32-S3 dev board — design phase, fw scaffold only) | `m3_esp32s3` | `#v2.0.0` |

Consumed via `platformio.ini`:

    lib_deps =
        https://github.com/space-monkie/meshniac-interface.git#v2.0.0

Nothing is deployed (Bobby, 2026-09-30): every host is bench hardware, so a
wire change may move ALL host pins in the same release. If a host must stay
behind, cut a new tag and move only the pins that take it — divergence is
deliberate and visible, never silent copy drift. Current wire: **v2.0.0
(2026-09-30)** — mesh membership by network name + password
(`config_mesh` / `mesh_status`); slot lists and `config_mesh_key` removed.
Design: the monorepo's `docs/design/mesh_membership_v1.md`.

## RED-LINES (read before editing anything here)

- **Byte-exact wire parity.** The wire protocol (envelope, mTyp grammar,
  reply table) is the contract between every host that pins this library
  and the module firmware — including the `cofig_wifi_reply` typo (yes,
  "cofig"; it is load-bearing on the wire; never "fix" it).
- **IST epoch (+19800).** The whole device domain runs on IST epoch.
  This library holds two homes of that math: the `- 19800` UTC
  conversion in the timestamp handler and the `rtc.offset` line near it.
  Both are on the ±19800 grep list in the monorepo's
  `docs/40_conventions.md` — check that list before touching any
  timestamp code, here or there.
- **Sync-comment discipline.** Status/press-logic rules are duplicated
  across monorepo files; grep the sync-comment homes listed in
  `docs/40_conventions.md` before changing related logic here.
- Keep diffs minimal — never reformat untouched code.

## Releasing a change

1. Edit; keep the diff reviewable.
2. Bump `version` in `library.json` (semver: fix = patch, feature =
   minor, protocol break = major) and commit.
3. `git tag vX.Y.Z && git push origin main --tags`
4. Bump the `#vX.Y.Z` pin only in the consumer(s) that should take it,
   in the monorepo.

Wire-change log:

- **2026-09-30 — v2.0.0.** The module wire changed: mesh membership
  (`config_mesh` / `config_mesh_reply` / `req_mesh_status` / `mesh_status`)
  replaces `config_addr`, `config_lora_addr`, `config_mesh_key` and the
  `b1..b5` / `l1..l5` slot lists (also gone from `module_eeprom_to_host`).
  Nothing was deployed, so all three hosts (m1, m2, M3) moved to
  `#v2.0.0` together.

## Developing against a local checkout

While actively hacking on the library, point the consumer at this clone
instead of a tag:

    lib_deps =
        symlink:///path/to/your/checkout/meshniac-interface

then restore the tag pin before committing the consumer. Never commit a
`symlink://` pin.

## Docs

- `CHANGELOG.md` — per-tag changes.
- `docs/host_interface_reference.md` — what a host must implement on the
  wire: J2 pins, 5 V power, framing, envelope, mode gating, every
  command/reply pair, minimal bring-up sequence. Derived from this library
  plus the module firmware and schematic. When a wire change ships, update
  it in the same commit as the tag bump.

## Install

PlatformIO (recommended) — pin a tag in `platformio.ini`:

    lib_deps =
        https://github.com/space-monkie/meshniac-interface.git#v2.0.1

Arduino IDE — download the repository as a zip from the tag you want, then
Sketch → Include Library → Add .ZIP Library. Needs ArduinoJson 6 and
ESP32Time from the Library Manager. Start from `examples/MinimalHost`.

Not published to the PlatformIO or Arduino registries yet.

## Rights

Proprietary. © 2026 IoTnauts. All rights reserved — see `LICENSE.txt`.
The source is published for reference; no licence to use, copy, modify or
redistribute is granted without written permission.
