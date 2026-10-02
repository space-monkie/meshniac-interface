/*
  MinimalHost — the smallest useful host for a Meshniac module.

  Wiring (module main board J2): HMI_TX (U2 IO26) -> host RX, HMI_RX (U2 IO25)
  <- host TX, +5 V and GND. 115200 8N1, no flow control. The pins below are
  the ones the m1 controller uses on an ESP32 DevKit; change them for yours.

  What it does:
    - answers the module's pings (the library does that for you),
    - learns its own node ID from the module,
    - sends a demo reading on the mesh every 60 s once the module is in
      normal mode,
    - prints node-directed payloads and mesh status lines it receives,
    - takes a few commands on the USB serial port so you can set the
      network credentials without a phone app:
        unlock <keycode>              e.g. unlock 3535
        mesh nb <name> <32hex|->      Nearby network (ESP-NOW); "-" clears it
        mesh lr <name> <32hex|->      Long-range network (LoRa)
        status                        ask the module for mesh_status

  The 32-hex key is the AES-128 key for that network. The phone app derives
  it from a human password (PBKDF2-HMAC-SHA256, salt "meshniac/v1/<nb|lr>/<name>",
  100000 iterations, 16 bytes); any node that holds the same key is a member.
*/

#include <Arduino.h>
#include <ArduinoJson.h>
#include <meshniac_interface.h>

static const int MODULE_RX_PIN = 26;   // host RX  <- module HMI_TX
static const int MODULE_TX_PIN = 25;   // host TX  -> module HMI_RX

MeshniacInterface mesh;

static unsigned long lastReadingMillis = 0;
static bool statusRequested = false;

// Called by the library after every line from the module has been parsed.
static void onModuleData()
{
  if (mesh.trigger_msg1 == "node_payload") {
    Serial.println("payload for this node: " + mesh.trigger_msg2);
    mesh.trigger_msg1 = "";
  } else if (mesh.trigger_msg1 == "mesh_status_updated") {
    Serial.println("mesh_status: " + mesh.mesh_status_json);
    mesh.trigger_msg1 = "";
  } else if (mesh.trigger_msg1 == "mesh_config_success") {
    Serial.println("network set: " + mesh.trigger_msg2 + " \"" + mesh.trigger_msg3 +
                   "\" key id " + mesh.trigger_msg4);
    mesh.trigger_msg1 = "";
  } else if (mesh.trigger_msg1 == "mesh_config_failed") {
    Serial.println("network NOT set: " + mesh.trigger_msg2 + " reason " + mesh.trigger_msg3);
    mesh.trigger_msg1 = "";
  } else if (mesh.trigger_msg1 == "device_unlocked_success") {
    Serial.println("unlocked");
    mesh.trigger_msg1 = "";
  } else if (mesh.trigger_msg1 == "device_unlocked_failed") {
    Serial.println("wrong key code");
    mesh.trigger_msg1 = "";
  }
}

static String nextToken(String& rest)
{
  rest.trim();
  int sp = rest.indexOf(' ');
  String tok = (sp < 0) ? rest : rest.substring(0, sp);
  rest = (sp < 0) ? "" : rest.substring(sp + 1);
  return tok;
}

static void handleCommand(String line)
{
  line.trim();
  String cmd = nextToken(line);
  if (cmd == "unlock") {
    mesh.verify_unlock_code(nextToken(line));
  } else if (cmd == "mesh") {
    String code = mesh.get_key_code_if_unlocked();
    if (code == "locked") { Serial.println("unlock first"); return; }
    String radio = nextToken(line);
    String name  = nextToken(line);
    String key   = nextToken(line);
    if ((radio != "nb" && radio != "lr") || name == "") {
      Serial.println("usage: mesh nb|lr <name> <32hex|->");
      return;
    }
    if (key == "-") key = "";
    mesh.config_mesh(code, radio, name, key);
  } else if (cmd == "status") {
    mesh.req_mesh_status();
  } else if (cmd != "") {
    Serial.println("commands: unlock <keycode> | mesh nb|lr <name> <32hex|-> | status");
  }
}

void setup()
{
  Serial.begin(115200);
  mesh.onDataProcessed(onModuleData);
  mesh.begin(115200, MODULE_RX_PIN, MODULE_TX_PIN);
  Serial.println("MinimalHost up; waiting for the module");
}

void loop()
{
  // USB serial commands
  static String cmdLine;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') { handleCommand(cmdLine); cmdLine = ""; }
    else if (cmdLine.length() < 120) cmdLine += c;
  }

  // one status request a few seconds after boot
  if (!statusRequested && millis() > 5000) {
    statusRequested = true;
    mesh.req_mesh_status();
  }

  // a demo reading every 60 s, only once the module has told us our node ID
  // and is in normal mode (the library drops payloads in other modes)
  if (mesh.node_id != "" && mesh.get_module_mode() == 3 &&
      millis() - lastReadingMillis > 60000UL) {
    lastReadingMillis = millis();
    StaticJsonDocument<200> js;
    js["sT"] = "t1";       // channel tag
    js["v"]  = "24.5";     // value as text, as the hosts do
    mesh.mesh_transmit_to_network(js);
    Serial.println("sent demo reading from " + mesh.node_id);
  }
}
