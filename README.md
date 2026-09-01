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
| HOST m1 (`HOST_m1-t2-r16_v1`, ESP32 DevKit — flashed field hardware) | `esp32doit-devkit-v1` | `#v1.0.0` |
| HOST m2 (`host_m2`, ESP32-S3 — no hardware exists yet) | `m2_esp32s3` | `#v1.0.0` |

Consumed via `platformio.ini`:

    lib_deps =
        https://github.com/space-monkie/meshniac-interface.git#v1.0.0

m1 is flashed field hardware: it stays on its pinned tag. If m2 needs
interface changes, cut a new tag and move ONLY m2's pin — divergence is
deliberate and visible, never silent copy drift.

## RED-LINES (read before editing anything here)

- **Byte-exact m1 parity.** The wire protocol (envelope, mTyp grammar,
  reply table) is the contract with flashed m1 hardware and the module
  firmware — including the `cofig_wifi_reply` typo (yes, "cofig"; it is
  load-bearing on the wire; never "fix" it).
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

## Developing against a local checkout

While actively hacking on the library, point the consumer at this clone
instead of a tag:

    lib_deps =
        symlink:///Users/bobbyjose/Documents/code/github/iotnauts.flutter.dev@gmail.com/meshniac-interface

then restore the tag pin before committing the consumer. Never commit a
`symlink://` pin.

## Rights

Proprietary. All rights reserved. Private repository — do not publish or
redistribute.
