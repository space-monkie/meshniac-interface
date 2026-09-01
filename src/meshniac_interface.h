/*
 * ══════════════════════════════════════════════════════════════════════════
 *  PHYSICAL CONNECTION INFRASTRUCTURE
 * ══════════════════════════════════════════════════════════════════════════
 *
 *  ┌─────────────────┐
 *  │    HOST MCU      │
 *  │  (main.cpp +     │
 *  │  meshniac_iface) │
 *  └────────┬────────┘
 *           │ UART (Serial1)
 *           │
 *  ┌ ─ ─ ─ ┼ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┐
 *           │
 *  │        Meshniac Flexible Mesh Network Module                       │
 *           │
 *  │  ┌─────┴───────────┐    ┌───────────────┐    ┌───────────────┐     │
 *     │  ESP_NOW MCU     │    │   MAIN MCU    │    │  GATEWAY MCU  │
 *  │  │  (ESP-NOW +      │◄──►│ (LoRa + RTC + │◄──►│  (WiFi/MQTT/  │    │
 *     │   LoRa radio)    │    │  Preferences) │    │   NTP)         │
 *  │  │       U2         │    │      U4       │    │      U6       │     │
 *     └─────────────────┘    └───────────────┘    └───────────────┘
 *  │                                                                     │
 *  └ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┘
 *
 *  Connections:
 *    HOST MCU   ◄─UART──► ESP_NOW MCU (U2)   (meshniac_interface serial bridge)
 *    ESP_NOW MCU ◄─Serial─► MAIN MCU  (U4)   (inter-MCU serial)
 *    MAIN MCU   ◄─Serial─► GATEWAY MCU (U6)  (inter-MCU serial)
 *
 *  Build flags:
 *    ESP_NOW_LIBRARY_U2     → ESP_NOW MCU firmware
 *    MAIN_MCU_LIBRARY_U4   → MAIN MCU firmware
 *    GATEWAY_MCU_LIBRARY_U6 → GATEWAY MCU firmware
 *
 * ══════════════════════════════════════════════════════════════════════════
 */

// meshniac_interface.h
#ifndef MESHNIAC_INTERFACE_H
#define MESHNIAC_INTERFACE_H

#include <Arduino.h>
// #include <TFT_eSPI.h>
#include <ESP32Time.h>

// #include <Adafruit_MAX31865.h>
#include <ArduinoJson.h>
#include <functional> // Required for std::function
#include "driver/uart.h"  // ADD THIS

class MeshniacInterface
{

// In header
public:
    typedef std::function<void(String rawData)> RawDataCallback;
    typedef std::function<void()> ProcessedDataCallback;
    
    void onRawDataReceived(RawDataCallback cb) { _rawCallback = cb; }
    void onDataProcessed(ProcessedDataCallback cb) { _processedCallback = cb; }
    
private:
    RawDataCallback _rawCallback = nullptr;
    ProcessedDataCallback _processedCallback = nullptr;

     // UART event handling - ADD THESE THREE LINES
    QueueHandle_t uart_queue;
    TaskHandle_t uart_task_handle;
    static void uart_event_task(void* pvParameters);
    void processIncomingData(String data);  // ← THIS IS WHAT YOU'RE MISSING!

public:
    // 1. DEFINE THE TYPE FIRST
    typedef std::function<void(String status, String code)> KeycodeCallback;

    // 2. PUBLIC METHODS
    void onKeycodeVerify(KeycodeCallback cb) {
        _keycodeCallback = cb;
    }

private:
    KeycodeCallback _keycodeCallback = nullptr; // Variable to store the pointer
    // Private member variables
    int _rxPin;
    int _txPin;
    // Private methods

public:
   

    // Constructor
    MeshniacInterface();

    // int RXD1 = 26;
    // int TXD1 = 25;
    // Serial1.begin(115200, SERIAL_8N1, mesh.RXD1, mesh.TXD1);

    // TFT_eSPI tft = TFT_eSPI();
    ESP32Time rtc;
    ESP32Time rtc_ist;
    // Preferences preferences;

    // String sm_eeprom_init = "";
    String allowed_devices_b1 = "";
    String allowed_devices_b2 = "";
    String allowed_devices_b3 = "";
    String allowed_devices_b4 = "";
    String allowed_devices_b5 = "";

    String broadcast_address_b1 = "";
    String broadcast_address_b2 = "";
    String broadcast_address_b3 = "";
    String broadcast_address_b4 = "";
    String broadcast_address_b5 = "";

    String lora_address_b1 = "";
    String lora_address_b2 = "";
    String lora_address_b3 = "";
    String lora_address_b4 = "";
    String lora_address_b5 = "";
   

    String node_id = "";
    String timezone = "";
    String offset_string = "";

    String trigger_msg1 ="";
    String trigger_msg2 ="";
    String trigger_msg3 ="";
    String trigger_msg4 ="";
    String trigger_msg5 ="";
    String trigger_msg6 ="";
    String trigger_msg7 ="";
    String trigger_msg8 ="";
    String trigger_msg9 ="";
    String trigger_msg10 ="";
    String trigger_msg11="";
    String trigger_msg12="";
    String trigger_msg13 ="";
    String trigger_msg14 ="";
    String trigger_msg15 ="";
    String trigger_msg16 ="";
    String trigger_msg17 ="";
    String trigger_msg18 ="";
    String trigger_msg19 ="";
    String trigger_msg20 = "";


    // String deviceMode = "normal";
    // // bluetooth button config
    int module_mode = 3;  //1->configure mode ,2->loading mode, 3->normal modes

    
    String wifi = "0";
    String mqtt = "0";
    String rtc_gw = "0";
    String rtc_local_nw = "0";
    String isRTC_valid = "0";
    String deviceCount = "0";
    String ssid = "";
    String mesh_inet = "";
    String mesh_conn = "";
    String gw_mac = "";

    String key_code_after_successful_unlock = "99999";
    bool is_unlocked = false;
    unsigned long unlocked_at = millis();
    unsigned long unlock_window = 600000;



   
    void begin(long baud, int rxPin, int txPin);
   
    void mesh_transmit_to_network(StaticJsonDocument<200> jsonPayload);
    void mesh_transmit_to_node_custom(String to_node, StaticJsonDocument<200> jsonPayload);
    void mesh_broadcast_to_all_allowed_node(StaticJsonDocument<200> jsonPayload);
    String mesh_get_to_storage(StaticJsonDocument<200> jsonPayload, String timestampString);
    void mesh_transmit_as_alarm(StaticJsonDocument<200> jsonPayload);

    // ESP-NOW only transmit functions (mTyp: "en") — same as pl but skips LoRa
    void mesh_en_transmit_to_network(StaticJsonDocument<200> jsonPayload);
    void mesh_en_transmit_to_node_custom(String to_node, StaticJsonDocument<200> jsonPayload);
    void mesh_en_broadcast_to_all_allowed_node(StaticJsonDocument<200> jsonPayload);

    // Big payload transmit functions (mTyp: "bl") — WiFi/MQTT only (NO ESP-NOW, NO LoRa)
    // Uses DynamicJsonDocument to support js_data up to 1200 bytes
    void mesh_bl_transmit_to_network(DynamicJsonDocument& jsonPayload);
    void mesh_bl_transmit_to_node_custom(String to_node, DynamicJsonDocument& jsonPayload);
    void mesh_bl_broadcast_to_all_allowed_node(DynamicJsonDocument& jsonPayload);

    // Librified versions of Serial methods
    int available();
    int read();
    String readString();
    void println(String data);
    void println(int data); // Overload for integers
    void verify_unlock_code(String user_provided_key_code);
    void deserialize_module_data(String module_data_json_str);
    String get_key_code_if_unlocked();
    void config_key_code(String current_keycode, String new_key_code);
    void config_wifi(String current_keycode, String ssid, String password);
    void config_timezone(String current_keycode,String timezone, String offset);
    void manuf_dev_conf(String current_keycode, String manuf_code, String now_node_id);
    void mqtt_config(String current_keycode, String mqttSrvr, String mqtt_usrnm, String mqtt_passwd);
    void mqtt2_config(String current_keycode, String mqttSrvr2, String mqtt_usrnm2, String mqtt_passwd2);
    void modify_address(String current_keycode, String b1, String b2, String b3, String b4, String b5);
    void modify_lora_address(String current_keycode, String l1, String l2, String l3, String l4, String l5);
    void config_mesh_key(String current_keycode, String mesh_key_hex);
    int get_module_mode(); // 1->configure mode ,2->loading mode, 3->normal modes
    // void set_module_mode_to_config(); // 1->configure mode ,2->loading mode, 3->normal modes
    // void set_module_mode_to_loading(); // 1->configure mode ,2->loading mode, 3->normal modes
    // void set_module_mode_to_normal(); // 1->configure mode ,2->loading mode, 3->normal modes
    void enable_config_mode();  //
    void disable_config_mode();
    void uart1_println(const String &data);  // Safe UART1 transmit (replaces Serial1.println)




};

#endif // MESHNIAC_INTERFACE_H