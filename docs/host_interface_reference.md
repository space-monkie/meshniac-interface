# Meshniac host interface — what a host must implement

Updated: 2026-10-11 · Describes the wire that `MeshniacInterface` (this
library, tag v2.2.0) drives, for anyone writing a host: the M3 dev-board
firmware, a Raspberry Pi on M3's DIRECT header, a customer's MCU. The
library is the reference implementation; when this page and
`src/meshniac_interface.cpp` disagree, the code wins and this page gets
fixed in the same commit.

Sources: this library; module firmware
`meshniac-v3-lora-/…/gateway/GATEWAY_main_board_combined_v5/src/main.cpp`;
module schematic *Cloudify Log — Main Board v1.5* (2025-03-08);
`meshniac-v3-lora-/docs/10_architecture.md`.

> The README red-lines apply: the wire protocol is a contract with the
> module firmware and every host that pins this library. Do not "fix" `cofig_wifi_reply`; the
> device domain runs on IST epoch (+19800); pin the library by tag. Module
> wire changes bump the library tag and ALL host pins together.

## 1. Physical layer

| Item | Value |
|---|---|
| Connector | Module main board **J2** (2×8, 2.54 mm): pins 1–4 = +5 V, **7/8 = HMI_RX** (U2 IO25, U2 *receives* → host TX), **9/10 = HMI_TX** (U2 IO26, U2 *transmits* → host RX), 13–16 = GND, 5/6/11/12 NC. **J4** (2×8): +5 V and GND only. "HMI" = the host, in module vocabulary |
| Power | Module takes **+5 V** on J2/J4 (or its own USB-C); three AMS1117-3.3 on the module; bursts > 1 A. Keep ≥ 4.5 V at the pins; no series diode in the host's 5 V path |
| Levels | 3.3 V TTL UART. No 5 V on the signal pins |
| Settings | 115200 8N1 (U2: `Serial2.begin(115200, SERIAL_8N1, 25, 26)`; m1 host: `mesh.begin(115200, RXD1=26, TXD1=25)`), no parity, **no flow control** (`UART_HW_FLOWCTRL_DISABLE`) |
| Framing | Newline-delimited JSON. The host trims `\r`/whitespace and ignores empty lines. All module inter-MCU readers are line-framed with a per-call drain budget (4 lines / 512 B) |
| Sizes | Host RX/TX driver buffers 2048 B, 20-event queue; host accumulates ≤ 4096 B waiting for `\n` then discards; host parses into a 3000 B JSON document. `bl` payload `js` up to ~1200 B. **Radio path: `js` ≤ 128 B hard limit** (packed structs, AES-128-CCM authenticated frames, 211 B on air for pl/en) |
| Overflow | On `UART_FIFO_OVF` / `UART_BUFFER_FULL` the host flushes input and drops the partial line |
| Side channel | This library mirrors node-directed payloads and mode changes to `Serial2`; on m1 nothing is attached (dead write). A host may leave `Serial2` un-begun |

## 2. Topology — one wire, two hops

```
HOST ◄─UART (J2)─► U2 (ESP-NOW + LoRa RA-02 433 MHz) ◄─Serial1─► U4 (DS3231 RTC, Preferences, config authority) ◄─Serial2─► U6 (WiFi/MQTT/NTP)
```

A module MCU cannot answer the host directly: every request travels
host → U2 → U4 (→ U6) and the reply returns the same way. Message names
are **endpoint** names (`…_to_mainMCU`, `…_to_host`), not hop names. The
module firmware is one codebase selected by build flag
(`ESP_NOW_LIBRARY_U2` / `MAIN_MCU_LIBRARY_U4` / `GATEWAY_MCU_LIBRARY_U6`);
each MCU has its own TC2030 programming pads on the module board.

## 3. Module modes and gating

Three modes, **always entered on the host's request**. They gate the
*module's* mesh traffic (not the host's BLE stack). m1 auto-exits config
mode after 5 min without an app connection.

| Host sends | Module echoes | `module_mode` | Library's name |
|---|---|---|---|
| `{"mTyp":"sm_deviceMode","mode":"bluetooth_loading"}` (`enable_config_mode()`) | `{"mTyp":"deviceMode","mode":"bluetooth_loading"}` | 2 | loading mode |
| — | `{"mTyp":"deviceMode","mode":"bluetooth"}` | 1 | configure mode |
| `{"mTyp":"sm_deviceMode","mode":"normal"}` (`disable_config_mode()`) | `{"mTyp":"deviceMode","mode":"normal"}` | 3 | normal mode |

This library **only transmits payloads (`pl`/`en`/`bl`) while
`module_mode == 3`**. The module never talks to a phone itself — BLE lives
on the host (m1: NimBLE).

## 4. Payload envelope (host → module and module → host)

```json
{"mTyp":"pl","pl_id":21800,"prv_id":"22F0FDECB3F3","dTyp":"nde",
 "js":{"t1":25.5,"t2":26.1},"ts":"946686280","pubT":"nde/20F0ECECC1C3","frID":"22F0FDECB3F3"}
```

| Field | Meaning |
|---|---|
| `mTyp` | transport: **`pl`** = full mesh (ESP-NOW + LoRa + MQTT) · **`en`** = ESP-NOW + MQTT, no LoRa · **`bl`** = MQTT only (big payloads, `js` up to ~1200 B, no radio) |
| `pl_id` | random 1–2147483646 (2^31-2), never 0, payload id (U2 de-duplicates via a pl_id ring shared across radio and serial ingress). A JSON integer; receivers accept 1–2147483647, anything else is "no valid id" — module firmware from `m1-mod-2026.10.11-reg1` (the first released build with it; the earlier `-urg1` build was never released) refuses radio frames without one (counted `rx_noid`, §6) |
| `urg` | `1` (a JSON number, top level, never inside `js`) = urgent: present only when urgent; set by the sender, carried by every hop; alarms default urgent; the module maps it into on-air header byte 31 (flags bit 0) and sends the frame in its urgent LoRa lane (storage frames never). Absent = normal. Readers also accept `"1"` and `true` |
| `prv_id`, `frID` | this node's 12-hex node ID (uppercase) |
| `dTyp` | `net` (network), `nde` (specific node), `str` (storage) |
| `js` | application JSON (≤ 128 B on radio; `StaticJsonDocument<200>` for pl/en in this library; ~1200 B for bl) |
| `ts` | IST-epoch string from the host's `rtc_ist` (seeded by the module, §6) |
| `pubT` | topic: `net/<node>`, `nde/<target node>`, `str/<node>`, `alm/<node>` (alarm, sent as `pl` with `"dTyp":"net"`) |

**Routing fact (bench 2026-07-23):** the module forwards only
`pl`-envelope messages from MQTT `nde/<node>` to the host; a bare
`andro_smRomReq` on `nde/<node>` is dropped at the module. Cloud → device
commands must ride the `pl` envelope.

Library helpers: `mesh_transmit_to_network(js, urgent = false)`,
`mesh_transmit_to_node_custom(to_node, js, urgent = false)`,
`mesh_transmit_as_alarm(js, urgent = true)` (`urgent` adds `"urg":1`),
`mesh_get_to_storage` (returns the string, does not send), and the
`mesh_en_*` / `mesh_bl_*` variants (storage, `en` and `bl` have no urgent
option). Node-directed traffic addressed to
`nde/<own node id>` is surfaced as `node_payload` (`trigger_msg1/2` and the
callbacks).

## 5. Host → module commands

Replies come back as `<name>_reply` with `"status":"success|failed"` (plus
echoed values). Config ops travel host → U2 → U4 (→ U6 for WiFi/MQTT),
key-code-gated.

| Command `mTyp` | Fields | Reply `mTyp` |
|---|---|---|
| `verify_key_code_from_host_to_mainMCU` | `key_code` | `keycode_verify_reply_to_host` (`data`: success/failed, `key_code`); U4 sends `module_eeprom_to_host` **before** the reply |
| `config_key_code` | `current_key_code`, `new_key_code` | `config_key_code_reply` (`key_code`) |
| `config_wifi` | `key_code`, `ssid`, `password` | **`cofig_wifi_reply`** (`ssid`) — typo is wire-frozen |
| `config_timezone` | `key_code`, `timezone`, `offset` | `config_timezone_reply` (`timezone`, `offset`) |
| `config_node_id` (`manuf_dev_conf()`) | `key_code`, `manuf_code`, `node_id` | `config_node_id_reply` (`node_id`) |
| `config_mqtt` | `key_code`, `mqttSrvr`, `mqtt_usrnm`, `mqtt_passwd` | `config_mqtt_reply` (`mqttSrvr`) |
| `config_mqtt2` (2nd / LAN broker) | `key_code`, `mqttSrvr2`, `mqtt_usrnm2`, `mqtt_passwd2` | `config_mqtt2_reply` (`mqttSrvr2`) |
| `config_mesh` | `key_code`, `radio` (`nb` = nearby ESP-NOW · `lr` = long-range LoRa), `name` (network name, ≤ 32 chars), `key` (32 hex = AES-128, same on every member of that network; `""` = clear that radio, then `name` may be `""`) | `config_mesh_reply` (`radio`, `name`; success: `key_fp` = first 8 hex of SHA-256(key), `""` when cleared; failed: `reason` = `key_code` | `bad_input` | `not_pending`). U2 validates the request and pushes an unsolicited `mesh_status` after every applied change |
| `req_mesh_status` | — (not key-code gated) | `mesh_status` (§6) |
| `config_lora_region` (v2.2.0) | `key_code`, `region` (the short id of one LoRa radio region profile from the module firmware's fixed list — never a typed frequency; every node of one network must use the same id) | `config_lora_region_reply` (`region`; failed: `reason` = `key_code` · `unknown_region` (id not in this module's list) · `radio` (the module's fitted radio cannot use it) · `bad_input` (missing / too long; then `region` is `""`) · `not_pending` · `storage` (could not be saved)). U2 checks the id before the key code, stores and applies the profile, restarts its LoRa radio and pushes an unsolicited `mesh_status`. Never sent from the cloud or over the mesh: hosts send it only from their own Bluetooth / console input. Module firmware that knows this message keeps its LoRa radio **off** until a region is chosen; older module firmware does not answer it. The library sends a region longer than 8 characters as `""` (the module then answers `bad_input`) and does not send a frame that cannot fit its 400-char buffer (a key code far too long): that call gets no reply, only a USB line |
| `sm_deviceMode` | `mode` | `deviceMode` |
| `reply_espNowMcu_HMI` | — (answer to the module's ping). This library sends it as `{"mTyp":"reply_espNowMcu_HMI "}` — **with a trailing space inside the value** (unchanged up to v2.2.0). U2 matches the name without the space, so it never records the library's reply: from 5 min after module boot U2 prints an "HMI ping reply timeout" line every 10 s (its restart there is commented out, so nothing else happens). A new host should send it without the space (§7) | — |

Unlock model: after a successful `keycode_verify…` the host treats itself
as unlocked for `unlock_window` (600 s) and reuses the code
(`get_key_code_if_unlocked()`).

Secrets: `key_code` (also in `config_lora_region`, v2.2.0), `current_key_code`, `new_key_code`, `password`,
`manuf_code`, `mqtt_passwd`, `mqtt_passwd2` and `key` travel in clear on
this UART (the reply lines `keycode_verify_reply_to_host`,
`config_key_code_reply` and `module_eeprom_to_host` carry `key_code` too).
Keep them out of logs. The library's own USB debug prints (v2.1.0) show
them as `"***"`; `config_mqtt2` prints a one-line summary and the received
`keycode_verify_reply_to_host` prints `mTyp` + `data` only. The bytes sent
to the module are unchanged.

## 6. Module → host messages

| `mTyp` | Content | Host action |
|---|---|---|
| `timestampToSM` | `timestamp` (IST epoch), `dateString`, `timeString`, `nodeID`, `timezone`, `offset` | Seeds `rtc` (UTC = ts − 19800, plus offset) and `rtc_ist`; learns its own `node_id` |
| `status_to_sm` | `wifi`, `mqtt`, `rtc_gw`, `rtc_local_nw`, `isRTC_valid`, `deviceCount`, `gw_mac`, `meshInet`, `meshConn`, time strings | Status LEDs / display |
| `status_update_wifi` | `status_wifi` good/bad, `ssid` | WIFI LED, SSID on display |
| `deviceMode` | `mode` | Updates `module_mode` |
| `ping_espNowMcu_HMI` | — | Host must answer `reply_espNowMcu_HMI` |
| `reply_HMI_espNowMCU` | — | Ping reply received |
| `acceptedMsg` | — | Module accepted a node message |
| `pl` / `en` / `bl` | full envelope | If `pubT == nde/<own id>` → surface as `node_payload` |
| `module_eeprom_to_host` | `node_id`, `key_code`, `timezone`, `offset`, `dvTyp`, `gain`, `commsValDev2` | Stores the module's persisted settings; `module_eeprom_updated` |
| `mesh_status` | `nb_name`, `nb_key_fp`, `lr_name`, `lr_key_fp` (`""` while that radio is unconfigured), counters `rx_ok`, `rx_bad_tag`, `rx_replay`, `rx_wrong_net`, `rx_bad_len`, `tx_drop_nb`, `lr_queue_drops` (LoRa queue overwrites; from `m1-mod-2026.10.11-reg1` the normal queue only), `fw` (module firmware version). From module `m1-mod-2026.10.11-reg1` also `rx_noid` (frames refused: no valid `pl_id`) and `lr_q_spill_u` (urgent frames moved to the normal LoRa queue because the 24-slot urgent queue was full); older module firmware omits both. From module `m1-mod-2026.10.11-reg1` (v2.2.0) also `lora_region` (the active LoRa radio region profile id; `""` = none chosen, the LoRa radio is off) and `lr_air_pm` (the LoRa radio's own transmit time over the last hour, in thousandths, 0–1000; shown only, nothing is limited by it); older module firmware omits both. From module `m1-mod-2026.10.12-rb1` also `lrb` — the module's second LoRa radio as it last reported: `""` = none reported since the module started (not fitted), `lost` (it reported before but has been silent for 30 s), `wait` (on, but not used yet — not yet on the module's region), `ok`, `off` (no region chosen / no long-range network), `fail` (does not start or stopped answering), `radio` (it cannot use the active region) — and, while it reports, `lrb_tx` (frames sent on it), `lrb_rx` (frames heard on it), `lrb_drop` (urgent frames that never went out on it) and `lrb_air_pm` (its transmit time over the last hour, thousandths, shown only); older module firmware omits all of them. With these the line can exceed 512 characters (up to ~600): forward it whole — a host that only forwards lines under 512 drops it | Stores the four strings and the raw line (`mesh_status_json`, for forwarding to the app; keys the library does not read, e.g. the counters, pass through unchanged); from v2.2.0 also `mesh_lora_region` + `mesh_lora_region_reported` (false when the line has no `lora_region` — older module firmware — so it is not mistaken for "LoRa off"); `mesh_status_updated`. Sent ~3 s after module boot, after every applied `config_mesh` / `config_lora_region`, when the second radio's state changes (rb1), and on `req_mesh_status` |
| `keycode_verify_reply_to_host`, `config_*_reply`, `cofig_wifi_reply` | see §5 | `*_success` / `*_failed` triggers (`config_mesh_reply` → `mesh_config_success` with `trigger_msg2..4` = radio, name, key_fp, or `mesh_config_failed` with radio, reason; `config_lora_region_reply` (v2.2.0) → `lora_region_config_success` with `trigger_msg2` = region and `mesh_lora_region` set, or `lora_region_config_failed` with `trigger_msg2..3` = region, reason) |
| `andro_smRomReq` | `reqType` (e.g. `req_smRom_dataset1`) | App request relayed via the gateway (inside a `pl` envelope); host answers from its data |

## 7. Minimal host behaviour (a bring-up script on a bare UART)

1. Power the module with 5 V; open the UART at 115200 8N1; send nothing and
   read lines — the module pushes `timestampToSM`, `status_to_sm` and
   periodic `ping_espNowMcu_HMI`.
2. Answer every `ping_espNowMcu_HMI` with `{"mTyp":"reply_espNowMcu_HMI"}`
   (no trailing space — this library's own reply has one, see §5) so the
   module sees a live host.
3. Learn the node ID from `timestampToSM.nodeID`; drive LEDs from
   `status_to_sm`.
4. To send data, build the §4 envelope with `ts` from the module-seeded
   clock and write it as one line ending in `\n`, only in normal mode; keep
   `js` ≤ 128 B if it must ride the radio.
5. To configure, first `verify_key_code_from_host_to_mainMCU`, then the
   `config_*` command; expect the `*_reply`.
6. Do no timezone math of your own; treat `ts` as IST epoch exactly as the
   module does.
7. Radio traffic needs a network: after unlocking, send `config_mesh` once
   per radio (`nb`, `lr`) with the name + 32-hex key shared by that
   network's members; expect `config_mesh_reply`, then `mesh_status`. Until
   a radio is configured the module does not transmit on it (`tx_drop_nb`
   counts the nearby drops).
8. LoRa also needs a radio region (v2.2.0): module firmware that knows
   `config_lora_region` keeps its LoRa radio off until one is chosen. After
   unlocking, send `config_lora_region` with the region id used by every
   node of that network; expect `config_lora_region_reply`, then
   `mesh_status` with `lora_region`. Send it only from local input (your
   own console / Bluetooth), never from a cloud or mesh message.

## 8. Host ↔ phone (only if a host keeps app compatibility)

BLE is on the host, never on the module. m1: NimBLE, advertises
`"bl" + node_id`, service `4fafc201-…`, characteristic `beb5483e-…`;
framing 4-hex length header + payload + `\n`, 180-byte chunks, 8-slot TX
queue; requests `{"mTyp":"andro_smRomReq","reqType":…}`.
`host_m2/src/protocol` (monorepo) carries this m1-identically on an
ESP32-S3.

## 9. M3 dev board (monorepo `docs/design/m3_host_dev_board_v1.md`)

- **Sniffer**: the S3 mirrors every line in both directions to USB CDC with
  timestamps.
- **CLI**: typed commands mapped to §5 so bench provisioning needs no app.
- **DIRECT header**: pull the two J-ISO jumpers and any 3.3 V UART becomes
  the host; §7 is the script to run there.
