# Changelog

All notable changes to MeshniacInterface. Versions are git tags; hosts pin
the library by tag (`https://github.com/space-monkie/meshniac-interface.git#vX.Y.Z`).

## Unreleased
- Wider message ids: every frame this library creates (`pl`, `en`, `bl`,
  storage and alarm) now gets a `pl_id` of 1 … 2147483646 (2^31-2), never
  0, from the ESP32 hardware random generator (was `random(100000)`,
  0 … 99999). The chance that a new message is mistaken for a duplicate
  (the module remembers the last 200 ids) drops from about 0.2 % to about
  0.00001 %. Same range the backend and app already use; the module's own
  generator moves to it in the same batch. No API change; still a JSON
  number. Hosts get it only when they move their pin to the next tag.
- Optional urgent mark on three send calls:
  `mesh_transmit_to_network(js, bool urgent = false)`,
  `mesh_transmit_to_node_custom(to_node, js, bool urgent = false)` and
  `mesh_transmit_as_alarm(js, bool urgent = true)`. When urgent the
  envelope gets `"urg":1` (a JSON number, top level, never inside `js`);
  otherwise the key is absent and the frame is unchanged. The module
  firmware of the same batch sends marked frames in its urgent LoRa lane.
  Defaulted trailing parameters (no overloads), so existing host calls
  compile unchanged; just moving a host to the next tag makes every alarm
  urgent and nothing else. `mesh_get_to_storage`, the `en` pair and the
  `bl` pair are unchanged. Older module firmware ignores the key (no
  urgent lane; serial hops pass it on, radio hops drop it).

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
