# Changelog

All notable changes to MeshniacInterface. Versions are git tags; hosts pin
the library by tag (`https://github.com/space-monkie/meshniac-interface.git#vX.Y.Z`).

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
