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

// meshniac_interface.cpp
#include "meshniac_interface.h"

// Helper function to convert String to long, returns 0 if conversion fails
long stringToLongOrZero(String str) {
    if (str.length() == 0) {
        return 0;
    }
    char* endPtr;
    long result = strtol(str.c_str(), &endPtr, 10);
    // If endPtr points to the beginning, conversion failed
    if (endPtr == str.c_str()) {
        return 0;
    }
    return result;
}

MeshniacInterface::MeshniacInterface()
{
}

void MeshniacInterface::begin(long baud, int rxPin, int txPin)
{
    _rxPin = rxPin;
    _txPin = txPin;

    // Configure UART
    uart_config_t uart_config = {};
    uart_config.baud_rate = (int)baud;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.rx_flow_ctrl_thresh = 122;
    
    #ifdef UART_SCLK_DEFAULT
    uart_config.source_clk = UART_SCLK_DEFAULT;
    #elif defined(UART_SCLK_APB)
    uart_config.source_clk = UART_SCLK_APB;
    #endif
    
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
    // Install UART driver with event queue
    uart_driver_install(UART_NUM_1, 2048, 2048, 20, &uart_queue, 0);
    
    // Create task to handle UART events
    xTaskCreate(uart_event_task, "uart_event_task", 10000, this, 5, &uart_task_handle);
}

// UART Event Task - handles incoming serial data automatically
// Accumulates bytes until a newline delimiter is found, then processes the complete message.
void MeshniacInterface::uart_event_task(void* pvParameters) {
    MeshniacInterface* mesh = (MeshniacInterface*)pvParameters;
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*)malloc(2048);
    String accumulatedData = "";  // Buffer to accumulate incoming chunks

    while(true) {
        // Wait for UART event
        if(xQueueReceive(mesh->uart_queue, (void*)&event, portMAX_DELAY)) {
            switch(event.type) {
                case UART_DATA:
                    {
                        int len = uart_read_bytes(UART_NUM_1, dtmp, event.size, portMAX_DELAY);
                        if(len > 0) {
                            dtmp[len] = '\0'; // Null terminate
                            accumulatedData += String((char*)dtmp);

                            // Process all complete newline-delimited messages in the buffer
                            int newlinePos;
                            while((newlinePos = accumulatedData.indexOf('\n')) != -1) {
                                String completeLine = accumulatedData.substring(0, newlinePos);
                                accumulatedData = accumulatedData.substring(newlinePos + 1);

                                completeLine.trim(); // Remove \r or extra whitespace
                                if(completeLine.length() > 0) {
                                    mesh->processIncomingData(completeLine);
                                }
                            }

                            // Safety: prevent unbounded accumulation if no newline ever arrives
                            if(accumulatedData.length() > 4096) {
                                Serial.println("UART accumulation buffer overflow, discarding data");
                                accumulatedData = "";
                            }
                        }
                    }
                    break;

                case UART_FIFO_OVF:
                    Serial.println("UART FIFO overflow!");
                    uart_flush_input(UART_NUM_1);
                    xQueueReset(mesh->uart_queue);
                    accumulatedData = "";  // Clear partial data on overflow
                    break;

                case UART_BUFFER_FULL:
                    Serial.println("UART ring buffer full!");
                    uart_flush_input(UART_NUM_1);
                    xQueueReset(mesh->uart_queue);
                    accumulatedData = "";  // Clear partial data on overflow
                    break;

                default:
                    break;
            }
        }
    }
    free(dtmp);
    vTaskDelete(NULL);
}

// Check if data is waiting in the buffer
int MeshniacInterface::available() {
  size_t available_bytes;
  uart_get_buffered_data_len(UART_NUM_1, &available_bytes);
  return (int)available_bytes;
}

int MeshniacInterface::read() {
  uint8_t data;
  int len = uart_read_bytes(UART_NUM_1, &data, 1, 20 / portTICK_PERIOD_MS);
  return (len > 0) ? data : -1;
}

// Read the incoming buffer as a String
String MeshniacInterface::readString() {
  uint8_t buffer[2048];
  int len = uart_read_bytes(UART_NUM_1, buffer, sizeof(buffer) - 1, 100 / portTICK_PERIOD_MS);
  if (len > 0) {
    buffer[len] = '\0';
    return String((char*)buffer);
  }
  return "";
}

// Print data followed by a newline
void MeshniacInterface::println(String data) {
  data += "\n";
  uart_write_bytes(UART_NUM_1, data.c_str(), data.length());
}

// Overload to handle numbers
void MeshniacInterface::println(int data) {
  String str = String(data) + "\n";
  uart_write_bytes(UART_NUM_1, str.c_str(), str.length());
}

// Safe UART1 transmit - replaces Serial1.println
// Uses ESP-IDF uart_write_bytes since uart_driver_install owns UART_NUM_1
void MeshniacInterface::uart1_println(const String &data) {
  String toSend = data + "\n";
  uart_write_bytes(UART_NUM_1, toSend.c_str(), toSend.length());
}


void MeshniacInterface::mesh_transmit_to_network(StaticJsonDocument<200> jsonPayload)
{
    unsigned long timestamp_long_local = rtc_ist.getEpoch();
    String timestamp_string_local = String(timestamp_long_local);
    String pubT_local = "net/" + node_id;

    int randomPayloadID = random(100000);
    StaticJsonDocument<800> json_injectSM_data;
    json_injectSM_data["mTyp"] = "pl";
    json_injectSM_data["pl_id"] = randomPayloadID;
    json_injectSM_data["prv_id"] = node_id;
    json_injectSM_data["dTyp"] = "net"; // available options --> nde,net,str
    json_injectSM_data["js"] = jsonPayload;
    json_injectSM_data["ts"] = timestamp_string_local;
    json_injectSM_data["pubT"] = pubT_local;
    json_injectSM_data["frID"] = node_id;
    char char_injectSM_data[800];
    serializeJson(json_injectSM_data, char_injectSM_data);
    String string_injectSM_data = String(char_injectSM_data);
    if (module_mode == 3) //1->configure mode ,2->loading mode, 3->normal modes
    {
        Serial.println(string_injectSM_data);
        uart1_println(string_injectSM_data);
    }
}

void MeshniacInterface::mesh_transmit_to_node_custom(String to_node, StaticJsonDocument<200> jsonPayload)
{
    String pubT_local1 = "nde/" + to_node;
    int randomPayloadID1 = random(100000);
    unsigned long timestamp_long_local = rtc_ist.getEpoch();
    String timestamp_string_local = String(timestamp_long_local);

    StaticJsonDocument<800> json_formedPayload1;
    json_formedPayload1["mTyp"] = "pl";
    json_formedPayload1["pl_id"] = randomPayloadID1;
    json_formedPayload1["prv_id"] = node_id;
    json_formedPayload1["dTyp"] = "nde"; // available options --> nde,net,str
    json_formedPayload1["js"] = jsonPayload;
    json_formedPayload1["ts"] = timestamp_string_local;
    json_formedPayload1["pubT"] = pubT_local1;
    json_formedPayload1["frID"] = node_id;
    char char_formedPayload1[800];
    serializeJson(json_formedPayload1, char_formedPayload1);

    if (module_mode == 3) //1->configure mode ,2->loading mode, 3->normal modes
    {
        if (to_node != "000000000000" && to_node != "")
        {
            Serial.println(char_formedPayload1);
            uart1_println(char_formedPayload1);
        }
    }
}

String MeshniacInterface::mesh_get_to_storage(StaticJsonDocument<200> jsonPayload, String timestampString)
{
    String pubT_local = "str/" + node_id;
    int randomPayloadID = random(100000);
    StaticJsonDocument<800> json_injectSM_data;
    json_injectSM_data["mTyp"] = "pl";
    json_injectSM_data["pl_id"] = randomPayloadID;
    json_injectSM_data["prv_id"] = node_id;
    json_injectSM_data["dTyp"] = "str"; // available options --> nde,net,str
    json_injectSM_data["js"] = jsonPayload;
    json_injectSM_data["ts"] = timestampString;
    json_injectSM_data["pubT"] = pubT_local;
    json_injectSM_data["frID"] = node_id;
    char char_injectSM_data[800];
    serializeJson(json_injectSM_data, char_injectSM_data);
    String string_injectSM_data = String(char_injectSM_data);
    return string_injectSM_data;
}

void MeshniacInterface::mesh_transmit_as_alarm(StaticJsonDocument<200> jsonPayload)
{
    unsigned long timestamp_long_local = rtc_ist.getEpoch();
    String timestamp_string_local = String(timestamp_long_local);

    String pubT_local = "alm/" + node_id;

    int randomPayloadID = random(100000);
    StaticJsonDocument<800> json_injectSM_data;
    json_injectSM_data["mTyp"] = "pl";
    json_injectSM_data["pl_id"] = randomPayloadID;
    json_injectSM_data["prv_id"] = node_id;
    json_injectSM_data["dTyp"] = "net"; // available options --> nde,net,str
    json_injectSM_data["js"] = jsonPayload;
    json_injectSM_data["ts"] = timestamp_string_local;
    json_injectSM_data["pubT"] = pubT_local;
    json_injectSM_data["frID"] = node_id;
    char char_injectSM_data[800];
    serializeJson(json_injectSM_data, char_injectSM_data);
    String string_injectSM_data_ch1 = String(char_injectSM_data);

    if (module_mode == 3) // 1->configure mode ,2->loading mode, 3->normal modes
    {
        Serial.println(" alarm net data");
        Serial.println(string_injectSM_data_ch1);
        uart1_println(string_injectSM_data_ch1);
    }
}

// ─────────────────────────────────────────────
// ESP-NOW Only transmit functions (mTyp: "en")
// Same payload structure as "pl" but the mesh
// layer will NOT forward these over LoRa.
// ─────────────────────────────────────────────

void MeshniacInterface::mesh_en_transmit_to_network(StaticJsonDocument<200> jsonPayload)
{
    unsigned long timestamp_long_local = rtc_ist.getEpoch();
    String timestamp_string_local = String(timestamp_long_local);
    String pubT_local = "net/" + node_id;

    int randomPayloadID = random(100000);
    StaticJsonDocument<800> json_injectSM_data;
    json_injectSM_data["mTyp"] = "en";
    json_injectSM_data["pl_id"] = randomPayloadID;
    json_injectSM_data["prv_id"] = node_id;
    json_injectSM_data["dTyp"] = "net";
    json_injectSM_data["js"] = jsonPayload;
    json_injectSM_data["ts"] = timestamp_string_local;
    json_injectSM_data["pubT"] = pubT_local;
    json_injectSM_data["frID"] = node_id;
    char char_injectSM_data[800];
    serializeJson(json_injectSM_data, char_injectSM_data);
    String string_injectSM_data = String(char_injectSM_data);
    if (module_mode == 3)
    {
        Serial.println(string_injectSM_data);
        uart1_println(string_injectSM_data);
    }
}

void MeshniacInterface::mesh_en_transmit_to_node_custom(String to_node, StaticJsonDocument<200> jsonPayload)
{
    String pubT_local1 = "nde/" + to_node;
    int randomPayloadID1 = random(100000);
    unsigned long timestamp_long_local = rtc_ist.getEpoch();
    String timestamp_string_local = String(timestamp_long_local);

    StaticJsonDocument<800> json_formedPayload1;
    json_formedPayload1["mTyp"] = "en";
    json_formedPayload1["pl_id"] = randomPayloadID1;
    json_formedPayload1["prv_id"] = node_id;
    json_formedPayload1["dTyp"] = "nde";
    json_formedPayload1["js"] = jsonPayload;
    json_formedPayload1["ts"] = timestamp_string_local;
    json_formedPayload1["pubT"] = pubT_local1;
    json_formedPayload1["frID"] = node_id;
    char char_formedPayload1[800];
    serializeJson(json_formedPayload1, char_formedPayload1);

    if (module_mode == 3)
    {
        if (to_node != "000000000000" && to_node != "")
        {
            Serial.println(char_formedPayload1);
            uart1_println(char_formedPayload1);
        }
    }
}

// ─────────────────────────────────────────────────────
// Big Payload transmit functions (mTyp: "bl")
// WiFi/MQTT ONLY — NO ESP-NOW (exceeds 250-byte limit),
// NO LoRa. Supports js_data up to 1200 bytes.
// Uses DynamicJsonDocument for large payloads.
// ─────────────────────────────────────────────────────

void MeshniacInterface::mesh_bl_transmit_to_network(DynamicJsonDocument& jsonPayload)
{
    unsigned long timestamp_long_local = rtc_ist.getEpoch();
    String timestamp_string_local = String(timestamp_long_local);
    String pubT_local = "net/" + node_id;

    int randomPayloadID = random(100000);
    DynamicJsonDocument json_injectSM_data(2048);
    json_injectSM_data["mTyp"] = "bl";
    json_injectSM_data["pl_id"] = randomPayloadID;
    json_injectSM_data["prv_id"] = node_id;
    json_injectSM_data["dTyp"] = "net"; // available options --> nde,net,str
    json_injectSM_data["js"] = jsonPayload.as<JsonObject>();
    json_injectSM_data["ts"] = timestamp_string_local;
    json_injectSM_data["pubT"] = pubT_local;
    json_injectSM_data["frID"] = node_id;
    char char_injectSM_data[2048];
    serializeJson(json_injectSM_data, char_injectSM_data, sizeof(char_injectSM_data));
    String string_injectSM_data = String(char_injectSM_data);
    if (module_mode == 3) //1->configure mode ,2->loading mode, 3->normal modes
    {
        Serial.println(string_injectSM_data);
        uart1_println(string_injectSM_data);
    }
}

void MeshniacInterface::mesh_bl_transmit_to_node_custom(String to_node, DynamicJsonDocument& jsonPayload)
{
    String pubT_local1 = "nde/" + to_node;
    int randomPayloadID1 = random(100000);
    unsigned long timestamp_long_local = rtc_ist.getEpoch();
    String timestamp_string_local = String(timestamp_long_local);

    DynamicJsonDocument json_formedPayload1(2048);
    json_formedPayload1["mTyp"] = "bl";
    json_formedPayload1["pl_id"] = randomPayloadID1;
    json_formedPayload1["prv_id"] = node_id;
    json_formedPayload1["dTyp"] = "nde"; // available options --> nde,net,str
    json_formedPayload1["js"] = jsonPayload.as<JsonObject>();
    json_formedPayload1["ts"] = timestamp_string_local;
    json_formedPayload1["pubT"] = pubT_local1;
    json_formedPayload1["frID"] = node_id;
    char char_formedPayload1[2048];
    serializeJson(json_formedPayload1, char_formedPayload1, sizeof(char_formedPayload1));

    if (module_mode == 3) //1->configure mode ,2->loading mode, 3->normal modes
    {
        if (to_node != "000000000000" && to_node != "")
        {
            Serial.println(char_formedPayload1);
            uart1_println(char_formedPayload1);
        }
    }
}

void MeshniacInterface::verify_unlock_code(String user_provided_key_code){
    /*
        This function sends keycode proveded by the user to the main mcu
        of the module so that it erifies if the provided key_code is 
        correct and the main module
        will reply a json if the provided key_code is correct 
        
        ---------format---------
        1)To Module main_MCU
        {
            "mTyp":"verify_key_code_from_host_to_mainMCU",
            "key_code":"keycode"
        }
        2)Reply to the host
        
        {
            "mTyp":"keycode_verify_reply_to_host",
            "data":"success",
            "key_code":"2544"
        }

    */  
    
   

    // Serial.println(smRom_andro);
    // Serial1.println(smRom_andro);

    StaticJsonDocument<600> msg_for_main_mcu;
    msg_for_main_mcu["mTyp"] = "verify_key_code_from_host_to_mainMCU";
    msg_for_main_mcu["key_code"] = user_provided_key_code;
    
    char msg_for_main_mcu_charJson[600];
    serializeJson(msg_for_main_mcu, msg_for_main_mcu_charJson);
    Serial.println(msg_for_main_mcu_charJson);
    uart1_println(msg_for_main_mcu_charJson);
}

void MeshniacInterface::deserialize_module_data(String module_data_json_str){
    /*
    UART1 — 
        timestampToSM
        json{"mTyp":"timestampToSM","timestamp":"946686266","dateString":"Jan 01 2000","timeString":"00:24:26","nodeID":"20F0ECECC1C3","timezone":"Asia/Kolkata","offset":"19800"}
        status_to_sm
        json{"mTyp":"status_to_sm","wifi":"0","mqtt":"0","rtc_gw":"0","rtc_local_nw":"0","isRTC_valid":"0","timestring":"00:24:26","datestring":"Jan 01 2000","deviceCount":"1","gw_mac":"B4:8A:0A:9A:C5:EC","meshInet":"0","meshConn":"1"}
        status_update_wifi
        json{"mTyp":"status_update_wifi","status_wifi":"bad","ssid":"IOTNAUTS#1"}
        deviceMode
        json{"mTyp":"deviceMode","mode":"normal"}
        reply_HMI_espNowMCU
        json{"mTyp":"reply_HMI_espNowMCU"}
        ping_espNowMcu_HMI
        json{"mTyp":"ping_espNowMcu_HMI"}
        acceptedMsg
        json{"mTyp":"acceptedMsg"}
        pl
        json{"mTyp":"pl","pl_id":21800,"prv_id":"22F0FDECB3F3","dTyp":"nde","js":{"t1":25.5,"t2":26.1},"ts":"946686280","pubT":"nde/20F0ECECC1C3","frID":"22F0FDECB3F3"}
        keycode_verify_reply_to_host
        json{"mTyp":"keycode_verify_reply_to_host","data":"success","key_code":"3535"}
        module_eeprom_to_host
        json{"mTyp":"module_eeprom_to_host","node_id":"20F0ECECC1C3","dvTyp":"","key_code":3535,"timezone":"Asia/Kolkata","offset":"19800","gain":"0","commsValDev2":"0"}
        config_key_code_reply
        json{"mTyp":"config_key_code_reply","status":"success","key_code":"3535"}
        cofig_wifi_reply
        json{"mTyp":"cofig_wifi_reply","status":"success","ssid":"IOTNAUTS#1"}
        config_timezone_reply
        json{"mTyp":"config_timezone_reply","status":"success","timezone":"Africa/Accra","offset":"0"}
        config_node_id_reply
        json{"mTyp":"config_node_id_reply","status":"success","node_id":"0B16212C3742"}
        config_mqtt_reply
        json{"mTyp":"config_mqtt_reply","status":"success","mqttSrvr":"mqtt1.iotnauts.in"}
        config_mesh_reply
        json{"mTyp":"config_mesh_reply","status":"success","radio":"nb","name":"shed-a","key_fp":"A31F7C09"}
        json{"mTyp":"config_mesh_reply","status":"failed","radio":"nb","name":"shed-a","reason":"bad_input"}
        mesh_status (unsolicited ~3 s after module boot and after every applied config_mesh; also on req_mesh_status)
        json{"mTyp":"mesh_status","nb_name":"shed-a","nb_key_fp":"A31F7C09","lr_name":"","lr_key_fp":"","rx_ok":12,"rx_bad_tag":0,"rx_replay":0,"rx_wrong_net":3,"rx_bad_len":0,"tx_drop_nb":0,"lr_queue_drops":0,"fw":"m1-mod-2026.09.30-mesh1"}
        andro_smRomReq (forwarded from app via gateway)
        json{"mTyp":"andro_smRomReq","reqType":"req_smRom_dataset1"}
    
    */
    Serial.println("*******");
    Serial.println("Debug#1");
    Serial.println("*******");

    // StaticJsonDocument<3000> module_data_json;
    DynamicJsonDocument module_data_json(3000);
    DeserializationError error = deserializeJson(module_data_json, module_data_json_str);
    if (error)
    {
        Serial.print(F("debug 7 -> deserialize_module_data() -> deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }
    String dTyp = module_data_json["dTyp"];
    String msgType = module_data_json["mTyp"];
    Serial.println(msgType);
    if(msgType == "keycode_verify_reply_to_host"){
         /*
        
        
        ---------format---------
        1)To Module main_MCU
        {
            "mTyp":"verify_key_code_from_host_to_mainMCU",
            "key_code":"keycode"
        }
        2)Reply to the HMI
        
        {
            "mTyp":"keycode_verify_reply_to_host",
            "data":"success/failed", or 
            "key_code":"2544"
        }

    */  
        String is_key_code_match = module_data_json["data"];
        Serial.println(module_data_json_str);
        if(is_key_code_match == "success"){
            // key_code provided the use is match
            String correct_key_code = module_data_json["key_code"];
            key_code_after_successful_unlock = correct_key_code;
            is_unlocked = true;
            unlocked_at = millis();
            // // --- YOUR NEW LOGIC START ---
            // if (_keycodeCallback != nullptr) {
            //     _keycodeCallback("success", key_code_after_successful_unlock); // Execute the function in main.cpp
            // }
            // // --- YOUR NEW LOGIC END ---
            trigger_msg1 = "device_unlocked_success";
            trigger_msg2 = key_code_after_successful_unlock;
        }
        if(is_key_code_match == "failed"){
            // key_code provided the use is match
            String attempted_key_code = module_data_json["key_code"];
            String key_code_after_attempt_failed = attempted_key_code;
            is_unlocked = false;
            unlocked_at = millis();
            // // --- YOUR NEW LOGIC START ---
            // if (_keycodeCallback != nullptr) {
            //     _keycodeCallback("failed", key_code_after_attempt_failed); // Execute the function in main.cpp
            // }
            // // --- YOUR NEW LOGIC END ---
            trigger_msg1 = "device_unlocked_failed";
            trigger_msg2 = key_code_after_successful_unlock;
        }
    }
    if(msgType == "module_eeprom_to_host"){
        // store the Module's Main_mcu's eeprom
        String node_id_temp = module_data_json["node_id"];
        String key_code_temp = module_data_json["key_code"];
        String timezone_temp = module_data_json["timezone"];
        String offset_temp = module_data_json["offset"];
        // String gain_temp = module_data_json["gain"];
        // String key_code_temp = module_data_json["commsValDev2"];

        node_id = node_id_temp;
        key_code_after_successful_unlock = key_code_temp;
        timezone = timezone_temp;
        offset_string = offset_temp;

        /* notify main.cpp that a fresh eeprom snapshot (node_id / key_code /
           tz / offset) was stored so it can refresh the app. The b1..b5 /
           l1..l5 slot lists left this message in v2.0.0 (mesh membership) */
        trigger_msg1 = "module_eeprom_updated";
     
        
    }
    if(msgType == "config_key_code_reply"){
            /*
        This function is used to change the key code. Will send a json to the 
        module's main_mcu. If the current keycode is correct then the key code will
        be changed.

        ---------format---------
        1)To Module main_MCU
        {
            "mTyp":"config_key_code",
            "current_key_code":"<current key code>",
            "new_key_code":"<new key code>"
        }

        2)Reply to the host
        {
            "mTyp":"config_key_code_reply",
            "status":"success/failed"
            "key_code":"<new key code>"
        }
    */
        String status = module_data_json["status"];
        if(status == "success"){
            String key_code_temp = module_data_json["key_code"];
            key_code_after_successful_unlock = key_code_temp; 
            trigger_msg1 = "keycode_config_success";
            trigger_msg2 = key_code_after_successful_unlock;
        }
        if(status == "failed"){
            trigger_msg1 = "keycode_config_failed";
            trigger_msg2 = module_data_json["key_code"].as<String>();
        }

    }
    // if(msgType == "cofig_wifi_reply"){
    // /*
    //     This function is for configuring the wifi in the module's gateway_mcu.
    //     host -> module's gateway mcu -> host

    //        ---------format---------
    //     1)To Module gateway_mcu
    //     {
    //         "mTyp":"config_wifi",
    //         "key_code":"<key_code>"
    //         "ssid":"<ssid>",
    //         "password":"<password>"
    //     }

    //     2)Reply to the host
    //     {
    //         "mTyp":"cofig_wifi_reply",
    //         "status":"success/failed"
    //         "ssid":"<ssid>"
    //     }
    // */
    //     String status = module_data_json["status"];
    //     if(status == "success"){
            
    //     }
    // }
    if(msgType == "cofig_wifi_reply"){
            // /*
    //     This function is for configuring the wifi in the module's gateway_mcu.
    //     host -> module's gateway mcu -> host

    //        ---------format---------
    //     1)To Module gateway_mcu
    //     {
    //         "mTyp":"config_wifi",
    //         "key_code":"<key_code>"
    //         "ssid":"<ssid>",
    //         "password":"<password>"
    //     }

    //     2)Reply to the host
    //     {
    //         "mTyp":"cofig_wifi_reply",
    //         "status":"success/failed"
    //         "ssid":"<ssid>"
    //     }
    // */
        String status = module_data_json["status"];
        String ssid_reply = module_data_json["ssid"];
        if(status == "success"){
            trigger_msg1 = "wifi_config_success";
            trigger_msg2 = ssid_reply;
        }
        if(status == "failed"){
            trigger_msg1 = "wifi_config_failed";
            trigger_msg2 = ssid_reply;
        }
    }
    if(msgType == "config_timezone_reply"){
        /*
            This function is to configure the timezone


                ---------format---------
            1)To Module gateway_mcu
            {
                "mTyp":"config_timezone",
                "key_code":"<key_code>"
                "timezone":"<timezone>",
                "offset":"<offset>"
            }

            2)Reply to the host
            {
                "mTyp":"config_timezone_reply",
                "status":"success/failed"
                "timezone":"<timezone>"
                "offset":"<offset>"
            }
        */
        String status = module_data_json["status"];
        if(status == "success"){
            String offset_temp = module_data_json["offset"];
            String timezone_temp = module_data_json["timezone"];
            offset_string = offset_temp;  // FIX: was offset_string = offset_string (self-assignment bug)
            timezone = timezone_temp;

            trigger_msg1 = "timezone_config_success";
            trigger_msg2 = timezone;
            trigger_msg3 = offset_string;
        }
        if(status == "failed"){
            trigger_msg1 = "timezone_config_failed";
            trigger_msg2 = module_data_json["timezone"].as<String>();
        }
    }
    if(msgType == "config_node_id_reply"){
        /*
        This function is to configure the Node_id


            ---------format---------
        1)To Module gateway_mcu
        {
            "mTyp":"config_node_id",
            "key_code":"<key_code>"
            "manuf_code":"<manuf_code>",
            "node_id":"<new_node_id>"
        }

        2)Reply to the host
        {
            "mTyp":"config_node_id_reply",
            "status":"success/failed",
            "node_id":"<new_node_id>",
        }
      */ 
        String status = module_data_json["status"];
        if(status == "success"){
            String node_id_temp = module_data_json["node_id"];
            node_id = node_id_temp;
            trigger_msg1 = "node_id_config_success";
            trigger_msg2 = node_id_temp;
            
        }
        if(status == "failed"){
            trigger_msg1 = "node_id_config_failed";
            trigger_msg2 = module_data_json["node_id"].as<String>();
        }
        
    }
     if(msgType == "config_mqtt_reply"){
      /*
        This function is to configure the MQTT


            ---------format---------
        1)To Module gateway_mcu
        {
            "mTyp":"config_mqtt",
            "key_code":"<key_code>"
            "mqttSrvr":"<mqttSrvr>",
            "mqtt_usrnm":"<mqtt_usrnm>",
            "mqtt_passwd":"<mqtt_passwd>"
        }

        2)Reply to the host
        {
            "mTyp":"config_mqtt_reply",
            "status":"success/failed",
            "mqttSrvr":"<mqttSrvr>",
        }
    */
        String status = module_data_json["status"];
        if(status == "success"){
            String mqttSrvr_temp = module_data_json["mqttSrvr"];
            trigger_msg1 = "mqtt_config_success";
            trigger_msg2 = mqttSrvr_temp;
        }
        if(status == "failed"){
            String mqttSrvr_temp = module_data_json["mqttSrvr"];
            trigger_msg1 = "mqtt_config_failed";
            trigger_msg2 = mqttSrvr_temp;
        }

    }
    if(msgType == "config_mqtt2_reply"){
      /*
        2nd (LAN) MQTT broker config reply - see mqtt2_config().
        Wire freeze: docs/design/mqtt2_dual_client_v1.md
        {
            "mTyp":"config_mqtt2_reply",
            "status":"success/failed",
            "mqttSrvr2":"<mqttSrvr2>",
        }
    */
        String status = module_data_json["status"];
        if(status == "success"){
            String mqttSrvr2_temp = module_data_json["mqttSrvr2"];
            trigger_msg1 = "mqtt2_config_success";
            trigger_msg2 = mqttSrvr2_temp;
        }
        if(status == "failed"){
            String mqttSrvr2_temp = module_data_json["mqttSrvr2"];
            trigger_msg1 = "mqtt2_config_failed";
            trigger_msg2 = mqttSrvr2_temp;
        }

    }
    if(msgType == "config_mesh_reply"){
        /*
            Reply after a config_mesh attempt: MAIN_MCU's verdict, with key_fp
            added by the ESP_NOW MCU once it has applied the key. Format in
            config_mesh(). Wire freeze: docs/design/mesh_membership_v1.md §4.6

                ---------format---------
            Reply from module:
            {
                "mTyp":"config_mesh_reply",
                "status":"success/failed",
                "radio":"nb|lr",
                "name":"<name>",
                "key_fp":"<8 hex>"      (success; "" when that radio was cleared)
                "reason":"bad_input"    (failed; may be absent)
            }
        */
        String status = module_data_json["status"];
        String radio_temp = module_data_json["radio"] | "";
        String name_temp = module_data_json["name"] | "";
        if(status == "success"){
            String key_fp_temp = module_data_json["key_fp"] | "";
            if(radio_temp == "nb"){
                mesh_nb_name = name_temp;
                mesh_nb_key_fp = key_fp_temp;
            }
            if(radio_temp == "lr"){
                mesh_lr_name = name_temp;
                mesh_lr_key_fp = key_fp_temp;
            }
            trigger_msg1 = "mesh_config_success";
            trigger_msg2 = radio_temp;
            trigger_msg3 = name_temp;
            trigger_msg4 = key_fp_temp;
        }
        if(status == "failed"){
            String reason_temp = module_data_json["reason"] | "";
            trigger_msg1 = "mesh_config_failed";
            trigger_msg2 = radio_temp;
            trigger_msg3 = reason_temp;
        }
    }
    if(msgType == "mesh_status"){
        /*
            Mesh membership + radio counters from the ESP_NOW MCU: pushed ~3 s
            after module boot, after every applied config_mesh, and on
            req_mesh_status. Fingerprints are "" while that radio is
            unconfigured. The raw line is kept verbatim (mesh_status_json) so
            a host can forward it to the app unchanged.

                ---------format---------
            {
                "mTyp":"mesh_status",
                "nb_name":"<name>","nb_key_fp":"<8 hex>",
                "lr_name":"<name>","lr_key_fp":"<8 hex>",
                "rx_ok":n,"rx_bad_tag":n,"rx_replay":n,"rx_wrong_net":n,"rx_bad_len":n,
                "tx_drop_nb":n,"lr_queue_drops":n,
                "fw":"<module firmware version>"
            }
        */
        String nb_name_temp = module_data_json["nb_name"] | "";
        String nb_key_fp_temp = module_data_json["nb_key_fp"] | "";
        String lr_name_temp = module_data_json["lr_name"] | "";
        String lr_key_fp_temp = module_data_json["lr_key_fp"] | "";
        mesh_nb_name = nb_name_temp;
        mesh_nb_key_fp = nb_key_fp_temp;
        mesh_lr_name = lr_name_temp;
        mesh_lr_key_fp = lr_key_fp_temp;
        mesh_status_json = module_data_json_str;
        trigger_msg1 = "mesh_status_updated";
    }
    if(msgType == "deviceMode"){
        //1->configure mode ,2->loading mode, 3->normal modes
        String deviceMode_new_local = module_data_json["mode"];
        if(deviceMode_new_local == "normal"){
            module_mode = 3;
        }
        if(deviceMode_new_local == "bluetooth_loading"){
            module_mode = 2;
        }
        if(deviceMode_new_local == "bluetooth"){
            module_mode = 1;
        }
    }
    // if(msgType = "deviceMode"){
    //     String deviceMode = module_data_json["mode"];

    //     if(deviceMode == "bluetooth"){
    //       module_mode = 1;
    //     }
    //     if(deviceMode == "bluetooth_loading"){
    //       module_mode = 2;
    //     }
    //     if(deviceMode == "normal"){
    //       module_mode = 3;
    //     }
       
    // }
     if (msgType == "timestampToSM")
    {
        String node_id_local = module_data_json["nodeID"];
        String dateString_local = module_data_json["dateString"];
        String timeString_local = module_data_json["timeString"];
        String timestamp_string_local = module_data_json["timestamp"];
        String timezone_local = module_data_json["timezone"];
        String offset_string_local = module_data_json["offset"];

        node_id = node_id_local;
        timezone = timezone_local;
        offset_string = offset_string_local;
        String dateString = dateString_local;
        String timeString = timeString_local;
        // timestamp_string = timestamp_string_local;
        unsigned long timestamp_int = timestamp_string_local.toInt();
        unsigned long UTC_timestamp_int = timestamp_int - 19800;
        long offset_local = stringToLongOrZero(offset_string);

        unsigned long offsetted_timestamp_final = UTC_timestamp_int + offset_local;
        Serial.println(node_id);
        // Serial.println(timestamp_int);
        Serial.println(dateString);
        Serial.println(timeString);
        rtc.setTime(timestamp_int);
        rtc.offset = offset_local - 19800;
        rtc_ist.setTime(timestamp_int);
        Serial.println(rtc.getTime("%B %d %Y %H:%M:%S"));
    }
    if (msgType == "acceptedMsg")
    {
        Serial.println("received nde msh#############################");
        Serial.println("received nde msh#############################");
        Serial.println(module_data_json_str);
        Serial.println("received nde msh#############################");
        Serial.println("received nde msh#############################");
    }
    if (msgType == "status_to_sm")
    {
        String wifi_temp = module_data_json["wifi"];
        String mqtt_temp = module_data_json["mqtt"];
        String rtc_gw_temp = module_data_json["rtc_gw"];
        String rtc_local_nw_temp = module_data_json["rtc_local_nw"];
        String isRTC_valid_temp = module_data_json["isRTC_valid"];
        String deviceCount_temp = module_data_json["deviceCount"];
        String gw_mac_local = module_data_json["gw_mac"];
        String mesh_inet_local = module_data_json["meshInet"];
        String mesh_conn_local = module_data_json["meshConn"];

        wifi = wifi_temp;
        mqtt = mqtt_temp;
        rtc_gw = rtc_gw_temp;
        rtc_local_nw = rtc_local_nw_temp;
        isRTC_valid = isRTC_valid_temp;
        deviceCount = deviceCount_temp;
        gw_mac = gw_mac_local;
        mesh_inet = mesh_inet_local;
        mesh_conn = mesh_conn_local;
    }
    // if (msgType == "status_update_wifi")
    // {
    //     String wifi_temp = module_data_json["wifi"];
    //     String wifi_ssid_local = module_data_json["ssid"];

    //     wifi = wifi_temp;
    //     ssid = wifi_ssid_local;
    // }
    if (msgType == "status_update_wifi")
    {
        const char* status_wifi_local = module_data_json["status_wifi"]; // "good"/"bad"
        String wifi_ssid_local = module_data_json["ssid"];
        if (status_wifi_local != nullptr) {
            wifi = (String(status_wifi_local) == "good") ? "1" : "0";
        }
        ssid = wifi_ssid_local;
    }
    if (msgType == "reply_HMI_espNowMCU")
    {
        // ping_reply_received_1 = millis();
        Serial.print("ping_reply_received_1 value updated = ");
        // Serial.print(ping_reply_received_1);
    }
        if (msgType == "ping_espNowMcu_HMI")
    {
        StaticJsonDocument<800> json_transmit_reply;
        json_transmit_reply["mTyp"] = "reply_espNowMcu_HMI ";
        char char_transmit_reply[800];
        serializeJson(json_transmit_reply, char_transmit_reply);
        String String_transmit_reply = String(char_transmit_reply);
        // Serial1.println(json_string);
        println(String_transmit_reply);
    }
    if (msgType == "pl" || msgType == "en" || msgType == "bl")
    {
       /*This section is for accepted NDE messages*/
       String node_id_pubt = "nde/" + node_id;
      if (module_data_json["pubT"] == node_id_pubt)
      {
        Serial.println("############################");
        Serial.println("############################");
        Serial.println("node_id and pubt match");
        Serial.println("############################");
        Serial.println("############################");
        Serial2.println(module_data_json_str);

        /* surface node-directed payloads to the host application
           (relay dev-sourced conditions + remote switch commands) */
        trigger_msg1 = "node_payload";
        trigger_msg2 = module_data_json_str;
      }
    }
    if(msgType == "andro_smRomReq"){
        trigger_msg1 = "andro_smRomReq";
        trigger_msg2 = module_data_json_str;

    }

}
// String MeshniacInterface::get_key_code_if_unlocked(){
//     signed long time_difference = millis() - unlocked_at;
//     if(is_unlocked == true && (time_difference < unlock_window)){
//         return key_code_after_successful_unlock;
//     }
//     return "locked";
// }

String MeshniacInterface::get_key_code_if_unlocked()  {
    unsigned long time_elapsed = millis() - unlocked_at;

    // Check both conditions in the guard clause
    if (!is_unlocked || (time_elapsed >= unlock_window)) {
        is_unlocked = false;
        return "locked";
    }

    return key_code_after_successful_unlock;
}

void MeshniacInterface::config_key_code(String current_key_code, String new_key_code){
    /*
        This function is used to change the key code. Will send a json to the 
        module's main_mcu. If the current keycode is correct then the key code will
        be changed.

        ---------format---------
        1)To Module main_MCU
        {
            "mTyp":"config_key_code",
            "current_key_code":"<current key code>",
            "new_key_code":"<new key code>"
        }

        2)Reply to the host
        {
            "mTyp":"config_key_code_reply",
            "status":"success/failed"
            "key_code":"<new key code>"
        }
    */
    Serial.println("debug#5 -> inside keycode_config config_key_code()");

    StaticJsonDocument<600> msg_for_main_mcu;
    msg_for_main_mcu["mTyp"] = "config_key_code";
    msg_for_main_mcu["current_key_code"] = current_key_code;
    msg_for_main_mcu["new_key_code"] = new_key_code;
    
    char msg_for_main_mcu_charJson[600];
    serializeJson(msg_for_main_mcu, msg_for_main_mcu_charJson);
    Serial.println(msg_for_main_mcu_charJson);
    uart1_println(msg_for_main_mcu_charJson);
}
void MeshniacInterface::config_wifi(String current_keycode, String ssid, String password){
    /*
        This function is for configuring the wifi in the module's gateway_mcu.
        host -> module's gateway mcu -> host

           ---------format---------
        1)To Module gateway_mcu
        {
            "mTyp":"config_wifi",
            "key_code":"<key_code>"
            "ssid":"<ssid>",
            "password":"<password>"
        }

        2)Reply to the host
        {
            "mTyp":"cofig_wifi_reply",
            "status":"success/failed"
            "ssid":"<ssid>"
        }
    */
    StaticJsonDocument<600> msg_for_main_mcu;
    msg_for_main_mcu["mTyp"] = "config_wifi";
    msg_for_main_mcu["key_code"] = current_keycode;
    msg_for_main_mcu["ssid"] = ssid;
    msg_for_main_mcu["password"] = password;
    
    char msg_for_main_mcu_charJson[600];
    serializeJson(msg_for_main_mcu, msg_for_main_mcu_charJson);
    Serial.println(msg_for_main_mcu_charJson);
    uart1_println(msg_for_main_mcu_charJson);
        
}

void MeshniacInterface::config_timezone(String current_keycode,String timezone, String offset){
    /*
        This function is to configure the timezone


            ---------format---------
        1)To Module gateway_mcu
        {
            "mTyp":"config_timezone",
            "key_code":"<key_code>"
            "timezone":"<timezone>",
            "offset":"<offset>"
        }

        2)Reply to the host
        {
            "mTyp":"config_timezone_reply",
            "status":"success/failed",
            "timezone":"<timezone>",
            "offset":"<offset>"
        }
    */

    StaticJsonDocument<600> msg_for_main_mcu;
    msg_for_main_mcu["mTyp"] = "config_timezone";
    msg_for_main_mcu["key_code"] = current_keycode;
    msg_for_main_mcu["timezone"] = timezone;
    msg_for_main_mcu["offset"] = offset;
    
    char msg_for_main_mcu_charJson[600];
    serializeJson(msg_for_main_mcu, msg_for_main_mcu_charJson);
    Serial.println(msg_for_main_mcu_charJson);
    uart1_println(msg_for_main_mcu_charJson);
}

void MeshniacInterface::manuf_dev_conf(String current_keycode, String manuf_code, String new_node_id){
        /*
        This function is to configure the Node_id


            ---------format---------
        1)To Module gateway_mcu
        {
            "mTyp":"config_node_id",
            "key_code":"<key_code>"
            "manuf_code":"<manuf_code>",
            "node_id":"<new_node_id>"
        }

        2)Reply to the host
        {
            "mTyp":"config_node_id_reply",
            "status":"success/failed",
            "node_id":"<new_node_id>",
        }
    */
    StaticJsonDocument<600> msg_for_main_mcu;
    msg_for_main_mcu["mTyp"] = "config_node_id";
    msg_for_main_mcu["key_code"] = current_keycode;
    msg_for_main_mcu["manuf_code"] = manuf_code;
    msg_for_main_mcu["node_id"] = new_node_id;
    
    char msg_for_main_mcu_charJson[600];
    serializeJson(msg_for_main_mcu, msg_for_main_mcu_charJson);
    Serial.println(msg_for_main_mcu_charJson);
    uart1_println(msg_for_main_mcu_charJson);
}
 
void MeshniacInterface::mqtt_config(String current_keycode, String mqttSrvr, String mqtt_usrnm, String mqtt_passwd){
    /*
        This function is to configure the MQTT


            ---------format---------
        1)To Module gateway_mcu
        {
            "mTyp":"config_mqtt",
            "key_code":"<key_code>"
            "mqttSrvr":"<mqttSrvr>",
            "mqtt_usrnm":"<mqtt_usrnm>",
            "mqtt_passwd":"<mqtt_passwd>"
        }

        2)Reply to the host
        {
            "mTyp":"config_mqtt_reply",
            "status":"success/failed",
            "mqttSrvr":"<mqttSrvr>",
        }
    */
    StaticJsonDocument<600> msg_for_main_mcu;
    msg_for_main_mcu["mTyp"] = "config_mqtt";
    msg_for_main_mcu["key_code"] = current_keycode;
    msg_for_main_mcu["mqttSrvr"] = mqttSrvr;
    msg_for_main_mcu["mqtt_usrnm"] = mqtt_usrnm;
    msg_for_main_mcu["mqtt_passwd"] = mqtt_passwd;
    
    char msg_for_main_mcu_charJson[600];
    serializeJson(msg_for_main_mcu, msg_for_main_mcu_charJson);
    Serial.println(msg_for_main_mcu_charJson);
    uart1_println(msg_for_main_mcu_charJson);
}

void MeshniacInterface::mqtt2_config(String current_keycode, String mqttSrvr2, String mqtt_usrnm2, String mqtt_passwd2){
    /*
        This function is to configure the 2nd (LAN) MQTT broker
        Wire freeze: docs/design/mqtt2_dual_client_v1.md

            ---------format---------
        1)To Module gateway_mcu
        {
            "mTyp":"config_mqtt2",
            "key_code":"<key_code>"
            "mqttSrvr2":"<mqttSrvr2>",
            "mqtt_usrnm2":"<mqtt_usrnm2>",
            "mqtt_passwd2":"<mqtt_passwd2>"
        }

        2)Reply to the host
        {
            "mTyp":"config_mqtt2_reply",
            "status":"success/failed",
            "mqttSrvr2":"<mqttSrvr2>",
        }
    */
    StaticJsonDocument<600> msg_for_main_mcu;
    msg_for_main_mcu["mTyp"] = "config_mqtt2";
    msg_for_main_mcu["key_code"] = current_keycode;
    msg_for_main_mcu["mqttSrvr2"] = mqttSrvr2;
    msg_for_main_mcu["mqtt_usrnm2"] = mqtt_usrnm2;
    msg_for_main_mcu["mqtt_passwd2"] = mqtt_passwd2;

    char msg_for_main_mcu_charJson[600];
    serializeJson(msg_for_main_mcu, msg_for_main_mcu_charJson);
    /* debug print REDACTED, unlike the mqtt_config idiom: this JSON
       carries the LAN broker password (rotation tool - keep it off USB) */
    Serial.println("config_mqtt2 -> mqttSrvr2=" + mqttSrvr2 + " (creds redacted)");
    uart1_println(msg_for_main_mcu_charJson);
}

void MeshniacInterface::config_mesh(String current_keycode, String radio, String name, String key_hex){
    /*
        This function configures mesh membership for ONE radio: "nb" (nearby,
        ESP-NOW) or "lr" (long-range, LoRa). A network is a name + a 32-hex
        AES-128 key shared by every member; the module derives the on-air
        header from the name and authenticates every frame with the key.
        key_hex = "" clears that radio (then name may be "" too).
        Wire freeze: docs/design/mesh_membership_v1.md §4.6 (v2.0.0, 2026-09-30)

            ---------format---------
        1)To Module ESP_NOW MCU (U2 validates, forwards to main_MCU without the key)
        {
            "mTyp":"config_mesh",
            "key_code":"<key_code>",
            "radio":"nb|lr",
            "name":"<network name, <= 32 chars>",
            "key":"<32 hex chars, e.g. A31F7C09E25B88D46AF03EC7119D45B8, or "" = clear>"
        }

        2)Reply to the host
        {
            "mTyp":"config_mesh_reply",
            "status":"success/failed",
            "radio":"nb|lr",
            "name":"<name>",
            "key_fp":"<first 8 hex of SHA-256(key), "" when cleared>"   (success only)
            "reason":"bad_input/..."                                       (failed only)
        }
        After every applied change the module also pushes an unsolicited mesh_status.
    */
    StaticJsonDocument<300> msg_for_main_mcu;
    msg_for_main_mcu["mTyp"] = "config_mesh";
    msg_for_main_mcu["key_code"] = current_keycode;
    msg_for_main_mcu["radio"] = radio;
    msg_for_main_mcu["name"] = name;
    msg_for_main_mcu["key"] = key_hex;

    char msg_for_main_mcu_charJson[300];
    serializeJson(msg_for_main_mcu, msg_for_main_mcu_charJson);
    uart1_println(msg_for_main_mcu_charJson);

    /* debug print REDACTED (same idiom as config_mqtt2): the mesh key never
       goes to USB. Re-serialise the same document with the key masked. */
    msg_for_main_mcu["key"] = "***";
    serializeJson(msg_for_main_mcu, msg_for_main_mcu_charJson);
    Serial.println(msg_for_main_mcu_charJson);
}

void MeshniacInterface::req_mesh_status(){
    /*
        Ask the module (ESP_NOW MCU) for its mesh membership + radio counters.
        Not key-code gated: the reply carries names and fingerprints only.
        The module also pushes mesh_status unsolicited ~3 s after boot and
        after every applied config_mesh.

            ---------format---------
        1)To Module ESP_NOW MCU
        {
            "mTyp":"req_mesh_status"
        }

        2)Reply to the host: mesh_status (see deserialize_module_data)
    */
    StaticJsonDocument<300> msg_for_main_mcu;
    msg_for_main_mcu["mTyp"] = "req_mesh_status";

    char msg_for_main_mcu_charJson[300];
    serializeJson(msg_for_main_mcu, msg_for_main_mcu_charJson);
    Serial.println(msg_for_main_mcu_charJson);
    uart1_println(msg_for_main_mcu_charJson);
}

int MeshniacInterface::get_module_mode(){
    return module_mode;
}

void MeshniacInterface::enable_config_mode(){
    module_mode = 2;

    String deviceMode = "bluetooth_loading";
    StaticJsonDocument<100> json_deviceStatus;
    json_deviceStatus["mTyp"] = "sm_deviceMode";
    json_deviceStatus["mode"] = deviceMode;

    char char_deviceMode[100];
    serializeJson(json_deviceStatus, char_deviceMode);
    Serial.print("#######");
    Serial.print(char_deviceMode);
    Serial.print("#######");
    uart1_println(char_deviceMode);
    Serial2.println(char_deviceMode);
}

void MeshniacInterface::disable_config_mode(){
    module_mode = 3;

    String deviceMode = "normal";
    StaticJsonDocument<100> json_deviceStatus;
    json_deviceStatus["mTyp"] = "sm_deviceMode";
    json_deviceStatus["mode"] = deviceMode;

    char char_deviceMode[100];
    serializeJson(json_deviceStatus, char_deviceMode);
    Serial.print("#######");
    Serial.print(char_deviceMode);
    Serial.print("#######");
    uart1_println(char_deviceMode);
    Serial2.println(char_deviceMode);
}

void MeshniacInterface::processIncomingData(String data) {
    // Optional: notify before processing
    if(_rawCallback) {
        _rawCallback(data);
    }
    
    // INTERNAL PROCESSING
    deserialize_module_data(data);
    
    // Notify after processing
    if(_processedCallback) {
        _processedCallback(); // No data needed, state is already updated
    }
}