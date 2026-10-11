# Changelog

All notable changes to MeshniacInterface. Versions are git tags; hosts pin
the library by tag (`https://github.com/space-monkie/meshniac-interface.git#vX.Y.Z`).

## v2.2.0 — 2026-10-11
Wire addition (minor version). Pairs with module firmware
`m1-mod-2026.10.11-reg1`. With that module firmware the LoRa radio stays
off until a region is chosen with `config_lora_region`, which only a
host on v2.2.0 can send: hosts on v2.1.0 or older keep working with it
but cannot turn its LoRa radio on. Older module firmware does not answer
`config_lora_region` (the call then gets no reply). The module build
named under v2.1.0 (`m1-mod-2026.10.09-urg1`) was never released on its
own; its changes ship in `m1-mod-2026.10.11-reg1`.
- LoRa radio region: new `config_lora_region(key_code, region)` →
  `config_lora_region_reply` (`status`, `region`, failed: `reason` =
  `key_code` | `unknown_region` | `radio` | `bad_input` | `not_pending` |
  `storage`). `region` is the short id of one profile from the module
  firmware's fixed list; the module checks it before the key code, stores
  and applies it and pushes `mesh_status`. Triggers
  `lora_region_config_success` (`trigger_msg2` = region) and
  `lora_region_config_failed` (`trigger_msg2` = region, `trigger_msg3` =
  reason). The USB debug print masks the key code. Hosts must call it only
  from their own Bluetooth / console input, never from a cloud or mesh
  message.
- `mesh_status`: the library now reads the module's `lora_region` into
  `mesh_lora_region`; `mesh_lora_region_reported` is false when the line
  has no such field (older module firmware), so "old module" is not
  mistaken for "LoRa off". The module's new `lr_air_pm` (LoRa transmit
  time over the last hour, in thousandths) passes through in the raw line.
- Module firmware that knows `config_lora_region` keeps its LoRa radio off
  until a region is chosen, so a host must be on this version to turn
  LoRa on with it; older module firmware does not answer the message.
- `examples/MinimalHost`: `region <id>` command and the two triggers.
- Docs: `docs/host_interface_reference.md` §5 command row, secrets list,
  §6 `mesh_status` fields and triggers, §7 bring-up step 8.

## v2.1.0 — 2026-10-09
Wire addition (minor version: hosts still on v2.0.x and older module
firmware keep working alongside it). Pairs with module firmware
`m1-mod-2026.10.09-urg1`.
- Wider message ids: every frame this library creates (`pl`, `en`, `bl`,
  storage and alarm) now gets a `pl_id` of 1 … 2147483646 (2^31-2), never
  0, from the ESP32 hardware random generator (was `random(100000)`,
  0 … 99999). The chance that a new message is mistaken for a duplicate
  (the module remembers the last 200 ids) drops from about 0.2 % to about
  0.00001 %. Same range the backend and app already use; the module's own
  generator moves to it in the same batch. No API change; still a JSON
  number. Hosts get it only when they move their pin to this tag.
- Optional urgent mark on three send calls:
  `mesh_transmit_to_network(js, bool urgent = false)`,
  `mesh_transmit_to_node_custom(to_node, js, bool urgent = false)` and
  `mesh_transmit_as_alarm(js, bool urgent = true)`. When urgent the
  envelope gets `"urg":1` (a JSON number, top level, never inside `js`);
  otherwise the key is absent and the frame is unchanged. The module
  firmware of the same batch sends marked frames in its urgent LoRa lane.
  Defaulted trailing parameters (no overloads), so existing host calls
  compile unchanged; just moving a host to this tag makes every alarm
  urgent and nothing else. `mesh_get_to_storage`, the `en` pair and the
  `bl` pair are unchanged. Older module firmware ignores the key (no
  urgent lane; serial hops pass it on, radio hops drop it).
- Key codes and passwords no longer appear in the library's USB debug
  prints. The printed copy of each config frame shows them as `"***"`:
  `verify_unlock_code` (`key_code`), `config_key_code`
  (`current_key_code`, `new_key_code`), `config_wifi` (`key_code`,
  `password`), `config_timezone` (`key_code`), `manuf_dev_conf`
  (`key_code`, `manuf_code`), `mqtt_config` (`key_code`, `mqtt_passwd`)
  and `config_mesh` (`key_code`; `key` was already masked).
  `mqtt2_config` keeps its one-line summary. The received
  `keycode_verify_reply_to_host` line is no longer printed whole (it
  carried the correct key code on success, the attempted one on failure);
  the print is now `mTyp` + `data` (success / failed). The frames sent to
  the module are byte-identical; only the debug line now comes right after
  the send instead of right before it. No API change.
- Docs (`docs/host_interface_reference.md`): a sealed `pl` / `en` radio
  frame is 211 B on air (was written as 209 B); `mesh_status` lists the
  module's new counters `rx_noid` and `lr_q_spill_u` (the library passes
  the line through unchanged); the ping reply is shown as this library
  actually sends it, `"reply_espNowMcu_HMI "` with a trailing space, and
  what that means on the module (no code change).

## v2.0.1 — 2026-10-02
- Example sketch `examples/MinimalHost` (answers pings, sends a demo
  reading, sets network credentials from the USB serial port).
- `library.properties` so the library also installs in the Arduino IDE
  from a GitHub zip.
- `LICENSE.txt` (proprietary notice), this changelog, public-facing README.
- No code change relative to v2.0.0 (comments and docs only).

## v2.0.0 — 2026-09-30
Wire change: mesh membership by network name + password.
- New `config_mesh(key_code, radio nb|lr, name, key)` → `config_mesh_reply`
  and `req_mesh_status()` → `mesh_status`; fields `mesh_nb_name`,
  `mesh_nb_key_fp`, `mesh_lr_name`, `mesh_lr_key_fp`, `mesh_status_json`;
  triggers `mesh_config_success`, `mesh_config_failed`, `mesh_status_updated`.
- Removed the five-slot peer lists (`modify_address`, `modify_lora_address`,
  the `b1..b5` / `l1..l5` fields, the `*_broadcast_to_all_allowed_node`
  helpers) and `config_mesh_key`.
- Radio frames on the module are now AES-128-CCM authenticated; the host
  never sees or prints a key (fingerprints only).

## v1.0.0 — 2026-09-01
- Extracted from the meshniac monorepo, byte-identical to the copies the
  m1 and m2 hosts carried on that date.
