# Meshniac host interface — what a host must implement

Updated: 2026-09-30 · Describes the wire that `MeshniacInterface` (this
library, tag v2.0.0) drives, for anyone writing a host: the M3 dev-board
firmware, a Raspberry Pi on M3's DIRECT header, a customer's MCU. The
library is the reference implementation; when this page and
`src/meshniac_interface.cpp` disagree, the code wins and this page gets
fixed in the same commit.

Sources: this library; module firmware
`meshniac-v3-lora-/…/gateway/GATEWAY_main_board_combined_v5/src/main.cpp`;
module schematic *Cloudify Log — Main Board v1.5* (2025-03-08);
`meshniac-v3-lora-/docs/10_architecture.md`.

> The README red-lines apply: the wire protocol is a contract with flashed
> m1 hardware and the module firmware. Do not "fix" `cofig_wifi_reply`; the
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
| Sizes | Host RX/TX driver buffers 2048 B, 20-event queue; host accumulates ≤ 4096 B waiting for `\n` then discards; host parses into a 3000 B JSON document. `bl` payload `js` up to ~1200 B. **Radio path: `js` ≤ 128 B hard limit** (packed structs, AES-128-CCM authenticated frames, 209 B on air for pl/en) |
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
| `pl_id` | random 0–99999 payload id (U2 de-duplicates via a pl_id ring shared across radio and serial ingress) |
| `prv_id`, `frID` | this node's 12-hex node ID (uppercase) |
| `dTyp` | `net` (network), `nde` (specific node), `str` (storage) |
| `js` | application JSON (≤ 128 B on radio; `StaticJsonDocument<200>` for pl/en in this library; ~1200 B for bl) |
| `ts` | IST-epoch string from the host's `rtc_ist` (seeded by the module, §6) |
| `pubT` | topic: `net/<node>`, `nde/<target node>`, `str/<node>`, `alm/<node>` (alarm, sent as `pl` with `"dTyp":"net"`) |

**Routing fact (bench 2026-07-23):** the module forwards only
`pl`-envelope messages from MQTT `nde/<node>` to the host; a bare
`andro_smRomReq` on `nde/<node>` is dropped at the module. Cloud → device
commands must ride the `pl` envelope.

Library helpers: `mesh_transmit_to_network`,
`mesh_transmit_to_node_custom(to_node,…)`, `mesh_transmit_as_alarm`,
`mesh_get_to_storage` (returns the string, does not send), and the
`mesh_en_*` / `mesh_bl_*` variants. Node-directed traffic addressed to
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
| `sm_deviceMode` | `mode` | `deviceMode` |
| `reply_espNowMcu_HMI` | — (answer to the module's ping) | — |

Unlock model: after a successful `keycode_verify…` the host treats itself
as unlocked for `unlock_window` (600 s) and reuses the code
(`get_key_code_if_unlocked()`).

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
| `mesh_status` | `nb_name`, `nb_key_fp`, `lr_name`, `lr_key_fp` (`""` while that radio is unconfigured), counters `rx_ok`, `rx_bad_tag`, `rx_replay`, `rx_wrong_net`, `rx_bad_len`, `tx_drop_nb`, `lr_queue_drops`, `fw` (module firmware version) | Stores the four strings and the raw line (`mesh_status_json`, for forwarding to the app); `mesh_status_updated`. Sent ~3 s after module boot, after every applied `config_mesh`, and on `req_mesh_status` |
| `keycode_verify_reply_to_host`, `config_*_reply`, `cofig_wifi_reply` | see §5 | `*_success` / `*_failed` triggers (`config_mesh_reply` → `mesh_config_success` with `trigger_msg2..4` = radio, name, key_fp, or `mesh_config_failed` with radio, reason) |
| `andro_smRomReq` | `reqType` (e.g. `req_smRom_dataset1`) | App request relayed via the gateway (inside a `pl` envelope); host answers from its data |

## 7. Minimal host behaviour (a bring-up script on a bare UART)

1. Power the module with 5 V; open the UART at 115200 8N1; send nothing and
   read lines — the module pushes `timestampToSM`, `status_to_sm` and
   periodic `ping_espNowMcu_HMI`.
2. Answer every `ping_espNowMcu_HMI` with `{"mTyp":"reply_espNowMcu_HMI"}`
   so the module sees a live host.
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
