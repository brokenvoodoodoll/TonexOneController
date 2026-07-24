/*
 Copyright (C) 2024  Greg Smith

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/


#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "sys/param.h"
#include "main.h"
#include "control.h"
#include "usb_comms.h"
#include "usb/usb_host.h"
#include "usb_tonex_one.h"
#include "footswitches.h"
#include "task_priorities.h"
#include "tonex_params.h"
#include "midi_helper.h"

#define CTRL_TASK_STACK_SIZE                (3 * 1024)

#define NVS_USERDATA_NAME                   "userdata"
#define NVS_USERDATA_SKIN_CONF              "skinconf"
#define NVS_USERDATA_BT_CONF                "btconf"
#define NVS_USERDATA_SMIDI_CONF             "smidiconf"
#define NVS_USERDATA_GENERAL_CONF           "genconf"
#define NVS_USERDATA_FOOTSW_CONF            "footconf"
#define NVS_USERDATA_WIFI_CONF              "wificonf"
#define NVS_USERDATA_PRESET_ORDER_CONF      "porderconf"
#define NVS_USERDATA_PC_MAP_CONF            "pcmapconf"

#define MAX_TEXT_LENGTH                     128
#define MAX_BT_CUSTOM_NAME                  25
#define MAX_PRESET_USER_TEXT_LENGTH         32

#define MAX_CONFIG_SAVE_RETRIES             10
#define CONTROL_QUEUE_WRITE_TIMEOUT         1000    // msec

enum CommandEvents
{
    EVENT_PRESET_DOWN,
    EVENT_PRESET_UP,
    EVENT_PRESET_INDEX,
    EVENT_BANK_INDEX,
    EVENT_AB_BANK_DOWN,
    EVENT_AB_BANK_UP,
    EVENT_SET_PRESET_NAME,
    EVENT_SET_PRESET_DETAILS,
    EVENT_SET_USB_STATUS,
    EVENT_SAVE_USER_DATA,
    EVENT_SET_USER_TEXT,
    EVENT_SET_CONFIG_ITEM_INT,
    EVENT_SET_CONFIG_ITEM_STRING,
    EVENT_TRIGGER_TAP_TEMPO,
};

typedef struct
{
    uint8_t Event;
    char Text[MAX_TEXT_LENGTH];
    uint32_t Value;
    uint32_t Item;
} tControlMessage;

typedef struct __attribute__ ((packed))
{
    uint16_t GeneralDoublePressToggleBypass: 1;
    uint16_t GeneralScreenRotation: 2;
    uint16_t GeneralLoopAround: 1;
    uint16_t GeneralSavePresetToSlot: 2;
    uint16_t GeneralEnableTouchHigherSensitivity: 1;
    uint16_t GeneralHideBPM: 1;
    uint16_t GeneralSpare: 8;
} tGeneralConfig;

typedef struct __attribute__ ((packed))
{
    uint8_t FootswitchMode;

    // external footswitches
    uint8_t ExternalFootswitchPresetLayout;
    tExternalFootswitchEffectConfig ExternalFootswitchEffectConfig[MAX_EXTERNAL_EFFECT_FOOTSWITCHES];

    // internal footswitches
    uint8_t InternalFootswitchPresetLayout;
    tExternalFootswitchEffectConfig InternalFootswitchEffectConfig[MAX_INTERNAL_EFFECT_FOOTSWITCHES];
} tFootSwitchConfig;

typedef struct __attribute__ ((packed))
{
    // preset order mapping
    uint8_t PresetOrder[MAX_SUPPORTED_PRESETS];
} tPresetOrderMappingConfig;

typedef struct __attribute__ ((packed))
{
    // program change mapping
    uint8_t PCMap[MAX_PC_MAP];
} tPCMapConfig;

typedef struct
{
    tGeneralConfig GeneralConfig;
    tFootSwitchConfig FootSwitchConfig;
    tPresetOrderMappingConfig PresetOrderMappingConfig;
    tPCMapConfig PCMapConfig;
} tConfigData;

typedef struct
{
    uint32_t LastTime;
    float BPM;
} tTapTempo;

typedef struct
{
    uint32_t PresetIndex;                        // 0-based index
    uint8_t ABSlotBank;                          // 0-based AB slot bank (Slot A = bank*2, Slot B = bank*2+1)
    char PresetNames[MAX_SUPPORTED_PRESETS][MAX_PRESET_NAME_LENGTH];
    uint32_t USBStatus;
    uint32_t BTStatus;
    uint32_t WiFiStatus;
    tConfigData ConfigData;
    tTapTempo TapTempo;
    uint8_t SyncComplete;
} tControlData;

static const char *TAG = "app_control";
static QueueHandle_t control_input_queue;
static tControlData ControlData;

#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
static uint8_t PresetIndexForOrderValue(uint8_t value);
#endif
static uint8_t SaveUserData(void);
static uint8_t LoadUserData(void);
static uint8_t SavePresetUserText(uint16_t preset_index, char* text);
static uint8_t LoadPresetUserText(uint16_t preset_index, char* text);
static void DumpUserConfig(void);

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
static uint8_t process_control_command(tControlMessage* message)
{
    ESP_LOGI(TAG, "Control command %d", message->Event);

    // check what we got
    switch (message->Event)
    {
        case EVENT_PRESET_DOWN:
        {
            if (ControlData.USBStatus != 0)
            {
                if (control_get_config_item_int(CONFIG_ITEM_LOOP_AROUND))
                {
                    uint8_t newIndex = (ControlData.PresetIndex > 0) ? (ControlData.PresetIndex - 1) : (usb_get_max_presets_for_connected_modeller() - 1);
                    uint8_t preset = ControlData.ConfigData.PresetOrderMappingConfig.PresetOrder[newIndex];

                    // send message to USB
                    usb_set_preset(preset);
                }
                else if (ControlData.PresetIndex > 0)
                {
                    uint8_t preset = ControlData.ConfigData.PresetOrderMappingConfig.PresetOrder[ControlData.PresetIndex - 1];

                    // send message to USB
                    usb_set_preset(preset);
                }
            }
        } break;

        case EVENT_PRESET_UP:
        {
            if (ControlData.USBStatus != 0)
            {
                if (control_get_config_item_int(CONFIG_ITEM_LOOP_AROUND))
                {
                    uint8_t newIndex = (ControlData.PresetIndex < (usb_get_max_presets_for_connected_modeller() - 1)) ? (ControlData.PresetIndex + 1) : 0;
                    uint8_t preset = ControlData.ConfigData.PresetOrderMappingConfig.PresetOrder[newIndex];

                    // send message to USB
                    usb_set_preset(preset);
                }
                else if (ControlData.PresetIndex < (usb_get_max_presets_for_connected_modeller() - 1))
                {
                    uint8_t preset = ControlData.ConfigData.PresetOrderMappingConfig.PresetOrder[ControlData.PresetIndex + 1];

                    // send message to USB
                    usb_set_preset(preset);
                }
            }
        } break;

        case EVENT_PRESET_INDEX:
        {
            if (ControlData.USBStatus != 0)
            {
                uint8_t preset = ControlData.ConfigData.PresetOrderMappingConfig.PresetOrder[message->Value];

                // send message to USB
                usb_set_preset(preset);
            }
        } break;

        case EVENT_BANK_INDEX:
        {
#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
            // update UI
            UI_SetBankIndex(message->Value);
#endif
        } break;

        case EVENT_AB_BANK_DOWN:
        {
            if (ControlData.USBStatus != 0)
            {
                uint8_t max_bank = (usb_get_max_presets_for_connected_modeller() / 2) - 1;

                if (ControlData.ABSlotBank > 0)
                {
                    ControlData.ABSlotBank--;
                }
                else if (control_get_config_item_int(CONFIG_ITEM_LOOP_AROUND))
                {
                    ControlData.ABSlotBank = max_bank;
                }

                // load both slots, force A/B (Double) mode, activate Slot A and refresh display
                usb_set_ab_slots(ControlData.ABSlotBank * 2, ControlData.ABSlotBank * 2 + 1);
            }
        } break;

        case EVENT_AB_BANK_UP:
        {
            if (ControlData.USBStatus != 0)
            {
                uint8_t max_bank = (usb_get_max_presets_for_connected_modeller() / 2) - 1;

                if (ControlData.ABSlotBank < max_bank)
                {
                    ControlData.ABSlotBank++;
                }
                else if (control_get_config_item_int(CONFIG_ITEM_LOOP_AROUND))
                {
                    ControlData.ABSlotBank = 0;
                }

                // load both slots, force A/B (Double) mode, activate Slot A and refresh display
                usb_set_ab_slots(ControlData.ABSlotBank * 2, ControlData.ABSlotBank * 2 + 1);
            }
        } break;

        case EVENT_SET_PRESET_NAME:
        {
            memcpy((void*)ControlData.PresetNames[message->Value], (void*)message->Text, MAX_PRESET_NAME_LENGTH);
            ControlData.PresetNames[message->Value][MAX_PRESET_NAME_LENGTH - 1] = 0;

            // update web UI
        } break;

        case EVENT_SET_PRESET_DETAILS:
        {
            ControlData.PresetIndex = message->Value;

            if (strlen(message->Text) > 0)
            {
                // update name text
                memcpy((void*)ControlData.PresetNames[message->Value], (void*)message->Text, MAX_PRESET_NAME_LENGTH);
                ControlData.PresetNames[message->Value][MAX_PRESET_NAME_LENGTH - 1] = 0;
            }

#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
            // update UI
            UI_SetPresetLabel(PresetIndexForOrderValue(message->Value), ControlData.PresetNames[message->Value]);

            UI_SetAmpSkin(ControlData.ConfigData.SkinConfig.SkinIndex[ControlData.PresetIndex]);

            char preset_user_text[MAX_PRESET_USER_TEXT_LENGTH] = {0};

            // load user text and set it
            LoadPresetUserText(message->Value, preset_user_text);
            UI_SetPresetDescription(preset_user_text);
#endif

            // update web UI
        } break;

        case EVENT_SET_USB_STATUS:
        {
            ControlData.USBStatus = message->Value;

#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
            // update UI
            UI_SetUSBStatus(ControlData.USBStatus);
#endif
        } break;

        case EVENT_SAVE_USER_DATA:
        {
            // save it
            SaveUserData();

            if (message->Value != 0)
            {
                ESP_LOGI(TAG, "Config save rebooting");
                vTaskDelay(10);
                esp_restart();
            }
        } break;

        case EVENT_SET_USER_TEXT:
        {
            char preset_user_text[MAX_PRESET_USER_TEXT_LENGTH] = {0};

            if (strlen(message->Text) > 0)
            {
                memcpy((void*)preset_user_text, (void*)message->Text, MAX_PRESET_USER_TEXT_LENGTH);
                preset_user_text[MAX_PRESET_USER_TEXT_LENGTH - 1] = 0;

                // save it
                SavePresetUserText(ControlData.PresetIndex, preset_user_text);
            }
        } break;

        case EVENT_SET_CONFIG_ITEM_INT:
        {
            switch (message->Item)
            {
                case CONFIG_ITEM_TOGGLE_BYPASS:
                {
                    ESP_LOGI(TAG, "Config set Toggle Bypass %d", (int)message->Value);
                    ControlData.ConfigData.GeneralConfig.GeneralDoublePressToggleBypass = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_LOOP_AROUND:
                {
                    ESP_LOGI(TAG, "Config set Loop Around %d", (int)message->Value);
                    ControlData.ConfigData.GeneralConfig.GeneralLoopAround = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_FOOTSWITCH_MODE:
                {
                    ESP_LOGI(TAG, "Config set Footswitch Mode %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.FootswitchMode = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_SCREEN_ROTATION:
                {
                    ESP_LOGI(TAG, "Config set screen rotation %d", (int)message->Value);
                    ControlData.ConfigData.GeneralConfig.GeneralScreenRotation = (uint8_t)message->Value & 0x03;
                } break;

                case CONFIG_ITEM_SAVE_PRESET_TO_SLOT:
                {
                    ESP_LOGI(TAG, "Config set save preset to slot %d", (int)message->Value);
                    ControlData.ConfigData.GeneralConfig.GeneralSavePresetToSlot = (uint8_t)message->Value & 0x03;
                } break;

                case CONFIG_ITEM_DISABLE_BPM_FLASHER:
                {
                    ESP_LOGI(TAG, "Config set bpm display touch sense %d", (int)message->Value);
                    ControlData.ConfigData.GeneralConfig.GeneralHideBPM = (uint8_t)message->Value & 0x01;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_PRESET_LAYOUT:
                {
                    ESP_LOGI(TAG, "Config set external footsw preset layout %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchPresetLayout = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT1_SW:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect1 sw %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[0].Switch = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT1_CC:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect1 CC %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[0].CC = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT1_VAL1:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect1 Value_1 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[0].Value_1 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT1_VAL2:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect1 Value_2 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[0].Value_2 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT2_SW:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect2 sw %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[1].Switch = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT2_CC:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect2 CC %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[1].CC = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT2_VAL1:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect2 Value_1 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[1].Value_1 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT2_VAL2:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect2 Value_2 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[1].Value_2 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT3_SW:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect3 sw %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[2].Switch = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT3_CC:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect3 CC %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[2].CC = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT3_VAL1:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect3 Value_1 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[2].Value_1 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT3_VAL2:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect3 Value_2 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[2].Value_2 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT4_SW:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect4 sw %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[3].Switch = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT4_CC:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect4 CC %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[3].CC = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT4_VAL1:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect4 Value_1 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[3].Value_1 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT4_VAL2:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect4 Value_2 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[3].Value_2 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT5_SW:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect5 sw %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[4].Switch = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT5_CC:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect5 CC %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[4].CC = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT5_VAL1:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect5 Value_1 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[4].Value_1 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT5_VAL2:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect5 Value_2 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[4].Value_2 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT6_SW:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect6 sw %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[5].Switch = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT6_CC:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect6 CC %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[5].CC = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT6_VAL1:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect6 Value_1 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[5].Value_1 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT6_VAL2:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect6 Value_2 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[5].Value_2 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT7_SW:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect7 sw %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[6].Switch = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT7_CC:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect7 CC %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[6].CC = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT7_VAL1:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect7 Value_1 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[6].Value_1 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT7_VAL2:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect7 Value_2 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[6].Value_2 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT8_SW:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect8 sw %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[7].Switch = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT8_CC:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect8 CC %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[7].CC = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT8_VAL1:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect8 Value_1 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[7].Value_1 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_EXT_FOOTSW_EFFECT8_VAL2:
                {
                    ESP_LOGI(TAG, "Config set external footsw effect8 Value_2 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[7].Value_2 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT1_SW:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect1 sw %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[0].Switch = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT1_CC:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect1 CC %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[0].CC = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT1_VAL1:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect1 Value_1 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[0].Value_1 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT1_VAL2:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect1 Value_2 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[0].Value_2 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT2_SW:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect2 sw %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[1].Switch = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT2_CC:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect2 CC %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[1].CC = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT2_VAL1:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect2 Value_1 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[1].Value_1 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT2_VAL2:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect2 Value_2 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[1].Value_2 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT3_SW:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect3 sw %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[2].Switch = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT3_CC:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect3 CC %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[2].CC = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT3_VAL1:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect3 Value_1 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[2].Value_1 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT3_VAL2:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect3 Value_2 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[2].Value_2 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT4_SW:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect4 sw %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[3].Switch = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT4_CC:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect4 CC %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[3].CC = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT4_VAL1:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect4 Value_1 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[3].Value_1 = (uint8_t)message->Value;
                } break;

                case CONFIG_ITEM_INT_FOOTSW_EFFECT4_VAL2:
                {
                    ESP_LOGI(TAG, "Config set internal footsw effect4 Value_2 %d", (int)message->Value);
                    ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[3].Value_2 = (uint8_t)message->Value;
                } break;
            }
        } break;

        case EVENT_TRIGGER_TAP_TEMPO:
        {
            uint32_t current_time = xTaskGetTickCount();
            uint32_t delta = current_time - ControlData.TapTempo.LastTime;
            float bpm;

            // debug
            //ESP_LOGI(TAG, "Tap Tempo %d %d", (int)current_time, (int)delta);

            // BPM can range from 40 to 240 bpm: 1.5 second2 maximum to 250 msec minimum between beats

            // check time since last tap
            if (delta > 1500)
            {
                // less than 40 bpm, save time and wait for another trigger
                ControlData.TapTempo.LastTime = current_time;
            }
            else
            {
                if (delta < 250)
                {
                    // clamp at maximum bpm
                    delta = 250;
                }

                // calculate bpm (60,000 is 60 seconds in msec)
                bpm = 60000.0f / (float)delta;

                ControlData.TapTempo.BPM = bpm;

                ESP_LOGI(TAG, "Tap Tempo BPM = %d", (int)bpm);

                // update pedal
                switch (usb_get_connected_modeller_type())
                {
                    case AMP_MODELLER_TONEX_ONE:        // fallthrough
                    case AMP_MODELLER_TONEX:            // fallthrough
                    default:
                    {
                        usb_modify_parameter(TONEX_GLOBAL_BPM, ControlData.TapTempo.BPM);
                    } break;
                }

                // save time for next trigger
                ControlData.TapTempo.LastTime = current_time;
            }
        } break;
    }

    return 1;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_request_preset_down(void)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_request_preset_down");

    message.Event = EVENT_PRESET_DOWN;

    // send to queue
    if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(CONTROL_QUEUE_WRITE_TIMEOUT)) != pdPASS)
    {
        ESP_LOGE(TAG, "control_request_preset_down queue send failed!");
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_request_preset_up(void)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_request_preset_up");

    message.Event = EVENT_PRESET_UP;

    // send to queue
    if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(CONTROL_QUEUE_WRITE_TIMEOUT)) != pdPASS)
    {
        ESP_LOGE(TAG, "control_request_preset_up queue send failed!");
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_request_preset_index(uint8_t index)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_request_preset_index %d", index);

    message.Event = EVENT_PRESET_INDEX;
    message.Value = index;

    // send to queue
    if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(CONTROL_QUEUE_WRITE_TIMEOUT)) != pdPASS)
    {
        ESP_LOGE(TAG, "control_request_preset_index queue send failed!");
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_request_bank_index(uint8_t index)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_request_bank_index %d", index);

    message.Event = EVENT_BANK_INDEX;
    message.Value = index;

    // send to queue
    if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(CONTROL_QUEUE_WRITE_TIMEOUT)) != pdPASS)
    {
        ESP_LOGE(TAG, "control_request_bank_index queue send failed!");
    }
}


/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_request_ab_bank_down(void)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_request_ab_bank_down");

    message.Event = EVENT_AB_BANK_DOWN;

    // send to queue
    if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(CONTROL_QUEUE_WRITE_TIMEOUT)) != pdPASS)
    {
        ESP_LOGE(TAG, "control_request_ab_bank_down queue send failed!");
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_request_ab_bank_up(void)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_request_ab_bank_up");

    message.Event = EVENT_AB_BANK_UP;

    // send to queue
    if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(CONTROL_QUEUE_WRITE_TIMEOUT)) != pdPASS)
    {
        ESP_LOGE(TAG, "control_request_ab_bank_up queue send failed!");
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_sync_preset_name(uint16_t index, char* name)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_sync_preset_name: index: %d, name: %s", (int)index, name);

    message.Event = EVENT_SET_PRESET_NAME;
    message.Value = index;

    // ensure string is null terminated
    message.Text[0] = 0;
    strncpy(message.Text, name, MAX_TEXT_LENGTH - 1);

    // send to queue
    if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(CONTROL_QUEUE_WRITE_TIMEOUT)) != pdPASS)
    {
        ESP_LOGE(TAG, "control_sync_preset_name queue send failed!");
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_sync_preset_details(uint16_t index, char* name)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_sync_preset_details index: %d, name: %s", (int)index, name);

    message.Event = EVENT_SET_PRESET_DETAILS;
    message.Value = index;

    // ensure string is null terminated
    message.Text[0] = 0;
    strncpy(message.Text, name, MAX_TEXT_LENGTH - 1);

    // send to queue
    if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(CONTROL_QUEUE_WRITE_TIMEOUT)) != pdPASS)
    {
        ESP_LOGE(TAG, "control_sync_preset_details queue send failed!");
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_set_user_text(char* text)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_set_user_text");

    message.Event = EVENT_SET_USER_TEXT;

    // ensure string is null terminated
    message.Text[0] = 0;
    strncat(message.Text, text, MAX_TEXT_LENGTH - 1);

    // send to queue
    if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(CONTROL_QUEUE_WRITE_TIMEOUT)) != pdPASS)
    {
        ESP_LOGE(TAG, "control_set_user_text queue send failed!");
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_set_usb_status(uint32_t status)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_set_usb_status");

    message.Event = EVENT_SET_USB_STATUS;
    message.Value = status;

    // send to queue
    if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(CONTROL_QUEUE_WRITE_TIMEOUT)) != pdPASS)
    {
        ESP_LOGE(TAG, "control_set_usb_status queue send failed!");
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_save_user_data(uint8_t reboot)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_save_user_data");

    message.Event = EVENT_SAVE_USER_DATA;
    message.Value = reboot;

    // send to queue
    if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(CONTROL_QUEUE_WRITE_TIMEOUT)) != pdPASS)
    {
        ESP_LOGE(TAG, "control_save_user_data queue send failed!");
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:      none
* NOTES:       none
****************************************************************************/
void control_trigger_tap_tempo(void)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_trigger_tap_tempo");

    message.Event = EVENT_TRIGGER_TAP_TEMPO;

    // send to queue
    if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(CONTROL_QUEUE_WRITE_TIMEOUT)) != pdPASS)
    {
        ESP_LOGE(TAG, "control_trigger_tap_tempo queue send failed!");
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
uint32_t control_get_current_preset_index(void)
{
    return ControlData.ConfigData.PresetOrderMappingConfig.PresetOrder[ControlData.PresetIndex];
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_get_current_preset_name(char* dest)
{
    memcpy((void*)dest, (void*)ControlData.PresetNames[control_get_current_preset_index()], MAX_PRESET_NAME_LENGTH);
    dest[MAX_PRESET_NAME_LENGTH - 1] = 0;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_set_config_item_int(uint32_t item, uint32_t status)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_set_config_item_int: %d %d", (int)item, (int)status);

    message.Event = EVENT_SET_CONFIG_ITEM_INT;
    message.Value = status;
    message.Item = item;

    for (uint32_t retries = 0; retries < MAX_CONFIG_SAVE_RETRIES; retries++)
    {
        // send to queue
        if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(100)) == pdPASS)
        {
            // all good
            return;
        }

        ESP_LOGW(TAG, "control_set_config_item_int queue send retry");
    }

    ESP_LOGE(TAG, "control_set_config_item_int queue send failed!");
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_set_config_item_string(uint32_t item, char* name)
{
    tControlMessage message;

    ESP_LOGI(TAG, "control_set_config_item_string: %d", (int)item);

    message.Event = EVENT_SET_CONFIG_ITEM_STRING;

    // ensure string is null terminated
    message.Text[0] = 0;
    strncpy(message.Text, name, MAX_TEXT_LENGTH - 1);
    message.Item = item;

    for (uint32_t retries = 0; retries < MAX_CONFIG_SAVE_RETRIES; retries++)
    {
        // send to queue
        if (xQueueSend(control_input_queue, (void*)&message, pdMS_TO_TICKS(100)) == pdPASS)
        {
            // all good
            return;
        }

        ESP_LOGW(TAG, "control_set_config_item_string queue send retry");
    }

    ESP_LOGE(TAG, "control_set_config_item_string queue send failed!");
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
uint32_t control_get_config_item_int(uint32_t item)
{
    uint32_t value = 0;

    switch (item)
    {

        case CONFIG_ITEM_TOGGLE_BYPASS:
        {
            value = ControlData.ConfigData.GeneralConfig.GeneralDoublePressToggleBypass;
        } break;

        case CONFIG_ITEM_LOOP_AROUND:
        {
            value = ControlData.ConfigData.GeneralConfig.GeneralLoopAround;
        } break;

        case CONFIG_ITEM_FOOTSWITCH_MODE:
        {
            value = ControlData.ConfigData.FootSwitchConfig.FootswitchMode;
        } break;

        case CONFIG_ITEM_SCREEN_ROTATION:
        {
            value = ControlData.ConfigData.GeneralConfig.GeneralScreenRotation;
        } break;

        case CONFIG_ITEM_SAVE_PRESET_TO_SLOT:
        {
            value = ControlData.ConfigData.GeneralConfig.GeneralSavePresetToSlot;
        } break;

        case CONFIG_ITEM_DISABLE_BPM_FLASHER:
        {
            value = ControlData.ConfigData.GeneralConfig.GeneralHideBPM;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_PRESET_LAYOUT:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchPresetLayout;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT1_SW:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[0].Switch;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT1_CC:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[0].CC;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT1_VAL1:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[0].Value_1;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT1_VAL2:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[0].Value_2;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT2_SW:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[1].Switch;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT2_CC:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[1].CC;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT2_VAL1:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[1].Value_1;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT2_VAL2:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[1].Value_2;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT3_SW:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[2].Switch;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT3_CC:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[2].CC;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT3_VAL1:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[2].Value_1;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT3_VAL2:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[2].Value_2;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT4_SW:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[3].Switch;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT4_CC:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[3].CC;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT4_VAL1:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[3].Value_1;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT4_VAL2:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[3].Value_2;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT5_SW:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[4].Switch;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT5_CC:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[4].CC;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT5_VAL1:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[4].Value_1;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT5_VAL2:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[4].Value_2;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT6_SW:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[5].Switch;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT6_CC:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[5].CC;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT6_VAL1:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[5].Value_1;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT6_VAL2:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[5].Value_2;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT7_SW:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[6].Switch;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT7_CC:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[6].CC;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT7_VAL1:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[6].Value_1;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT7_VAL2:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[6].Value_2;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT8_SW:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[7].Switch;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT8_CC:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[7].CC;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT8_VAL1:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[7].Value_1;
        } break;

        case CONFIG_ITEM_EXT_FOOTSW_EFFECT8_VAL2:
        {
            value = ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[7].Value_2;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT1_SW:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[0].Switch;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT1_CC:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[0].CC;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT1_VAL1:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[0].Value_1;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT1_VAL2:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[0].Value_2;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT2_SW:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[1].Switch;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT2_CC:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[1].CC;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT2_VAL1:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[1].Value_1;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT2_VAL2:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[1].Value_2;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT3_SW:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[2].Switch;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT3_CC:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[2].CC;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT3_VAL1:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[2].Value_1;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT3_VAL2:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[2].Value_2;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT4_SW:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[3].Switch;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT4_CC:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[3].CC;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT4_VAL1:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[3].Value_1;
        } break;

        case CONFIG_ITEM_INT_FOOTSW_EFFECT4_VAL2:
        {
            value = ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[3].Value_2;
        } break;

        default:
        {
            ESP_LOGE(TAG, "Unknown/Invalid int parameter item %d", (int)item);
        } break;
    }

    return value;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_set_preset_order(uint8_t* order)
{
    for (uint8_t index = 0; index < usb_get_max_presets_for_connected_modeller(); index++)
    {
        ControlData.ConfigData.PresetOrderMappingConfig.PresetOrder[index] = order[index];
    }

#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
    // update UI
    UI_SetPresetLabel(PresetIndexForOrderValue(ControlData.PresetIndex), ControlData.PresetNames[ControlData.PresetIndex]);
#endif
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
uint8_t* control_get_preset_order(void)
{
    return ControlData.ConfigData.PresetOrderMappingConfig.PresetOrder;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_set_pc_map(uint8_t* map)
{
    for (uint8_t index = 0; index < MAX_PC_MAP; index++)
    {
        ControlData.ConfigData.PCMapConfig.PCMap[index] = map[index];
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
uint8_t* control_get_pc_map(void)
{
    return ControlData.ConfigData.PCMapConfig.PCMap;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_set_sync_complete(void)
{
    ControlData.SyncComplete = 1;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
uint8_t control_get_sync_complete(void)
{
    return ControlData.SyncComplete;
}

#if CONFIG_TONEX_CONTROLLER_HAS_DISPLAY
/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:      none
* NOTES:       none
****************************************************************************/
static uint8_t PresetIndexForOrderValue(uint8_t value)
{
    for (uint8_t i = 0; i < usb_get_max_presets_for_connected_modeller(); i++)
    {
        if (ControlData.ConfigData.PresetOrderMappingConfig.PresetOrder[i] == value)
        {
            return i;
        }
    }
    return -1;
}
#endif

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:      none
* NOTES:       none
****************************************************************************/
static esp_err_t LoadUserConfigItem(void* item, size_t item_length, char* key)
{
    esp_err_t err;
    nvs_handle_t my_handle;
    esp_err_t result = ESP_FAIL;
    size_t required_size;

    ESP_LOGI(TAG, "LoadUserConfigItem %s Length: %d", key, (int)item_length);

    // open storage
    err = nvs_open("storage", NVS_READWRITE, &my_handle);

    if (err == ESP_OK)
    {
        // read data
        required_size = item_length;
        err = nvs_get_blob(my_handle, key, (void*)item, &required_size);

        switch (err)
        {
            case ESP_OK:
            {
                // close
                nvs_close(my_handle);

                ESP_LOGI(TAG, "LoadUserConfigItem OK");

                result = ESP_OK;
            } break;

            case ESP_ERR_NVS_NOT_FOUND:
            {
                ESP_LOGW(TAG, "LoadUserConfigItem not found");

                // close
                nvs_close(my_handle);
            } break;

            default:
            {
                ESP_LOGE(TAG, "LoadUserConfigItem Error (%s)", esp_err_to_name(err));

                // close
                nvs_close(my_handle);
            } break;
        }
    }
    else
    {
        ESP_LOGE(TAG, "LoadUserConfigItem failed to open nvs");
    }

    return result;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:      none
* NOTES:       none
****************************************************************************/
static esp_err_t SaveUserConfigItem(void* item, size_t item_length, char* key)
{
    esp_err_t err;
    nvs_handle_t my_handle;
    esp_err_t result = ESP_FAIL;
    size_t required_size;

    ESP_LOGI(TAG, "SaveUserConfigItem %s Length: %d", key, (int)item_length);

    // open storage
    err = nvs_open("storage", NVS_READWRITE, &my_handle);

    if (err == ESP_OK)
    {
        // read data
        required_size = item_length;
        err = nvs_set_blob(my_handle, key, (void*)item, required_size);

        switch (err)
        {
            case ESP_OK:
            {
                nvs_commit(my_handle);

                // close
                nvs_close(my_handle);

                ESP_LOGI(TAG, "SaveUserConfigItem OK");

                result = ESP_OK;
            } break;

            case ESP_ERR_NVS_NOT_FOUND:
            {
                ESP_LOGE(TAG, "SaveUserConfigItem Not found: %s", key);

                // close
                nvs_close(my_handle);
            } break;

            default:
            {
                ESP_LOGE(TAG, "SaveUserConfigItem Error (%s)", esp_err_to_name(err));

                // close
                nvs_close(my_handle);
            } break;
        }
    }
    else
    {
        ESP_LOGE(TAG, "SaveUserConfigItem failed to open nvs");
    }

    return result;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:      none
* NOTES:       none
****************************************************************************/
static uint8_t SaveUserData(void)
{
    ESP_LOGI(TAG, "Writing User Data");

    // save each config item
    SaveUserConfigItem((void*)&ControlData.ConfigData.GeneralConfig, sizeof(ControlData.ConfigData.GeneralConfig), NVS_USERDATA_GENERAL_CONF);
    SaveUserConfigItem((void*)&ControlData.ConfigData.FootSwitchConfig, sizeof(ControlData.ConfigData.FootSwitchConfig), NVS_USERDATA_FOOTSW_CONF);
    SaveUserConfigItem((void*)&ControlData.ConfigData.PresetOrderMappingConfig, sizeof(ControlData.ConfigData.PresetOrderMappingConfig), NVS_USERDATA_PRESET_ORDER_CONF);
    SaveUserConfigItem((void*)&ControlData.ConfigData.PCMapConfig.PCMap, sizeof(ControlData.ConfigData.PCMapConfig.PCMap), NVS_USERDATA_PC_MAP_CONF);

    return 1;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:      none
* NOTES:       none
****************************************************************************/
static uint8_t LoadUserData(void)
{
    uint8_t reset_order = 0;
    uint32_t loop;

    // load each config item
    // General
    if (LoadUserConfigItem((void*)&ControlData.ConfigData.GeneralConfig, sizeof(ControlData.ConfigData.GeneralConfig), NVS_USERDATA_GENERAL_CONF) != ESP_OK)
    {
        SaveUserConfigItem((void*)&ControlData.ConfigData.GeneralConfig, sizeof(ControlData.ConfigData.GeneralConfig), NVS_USERDATA_GENERAL_CONF);
    }

    // Footswitch
    if (LoadUserConfigItem((void*)&ControlData.ConfigData.FootSwitchConfig, sizeof(ControlData.ConfigData.FootSwitchConfig), NVS_USERDATA_FOOTSW_CONF) != ESP_OK)
    {
        SaveUserConfigItem((void*)&ControlData.ConfigData.FootSwitchConfig, sizeof(ControlData.ConfigData.FootSwitchConfig), NVS_USERDATA_FOOTSW_CONF);
    }

    // Preset order mapping
    if (LoadUserConfigItem((void*)&ControlData.ConfigData.PresetOrderMappingConfig, sizeof(ControlData.ConfigData.PresetOrderMappingConfig), NVS_USERDATA_PRESET_ORDER_CONF) != ESP_OK)
    {
        SaveUserConfigItem((void*)&ControlData.ConfigData.PresetOrderMappingConfig, sizeof(ControlData.ConfigData.PresetOrderMappingConfig), NVS_USERDATA_PRESET_ORDER_CONF);
    }

    // PC mapping
    if (LoadUserConfigItem((void*)&ControlData.ConfigData.PCMapConfig.PCMap, sizeof(ControlData.ConfigData.PCMapConfig.PCMap), NVS_USERDATA_PC_MAP_CONF) != ESP_OK)
    {
        SaveUserConfigItem((void*)&ControlData.ConfigData.PCMapConfig.PCMap, sizeof(ControlData.ConfigData.PCMapConfig.PCMap), NVS_USERDATA_PC_MAP_CONF);
    }

    if (ControlData.ConfigData.FootSwitchConfig.FootswitchMode != FOOTSWITCH_LAYOUT_DISABLED)
    {
        if (ControlData.ConfigData.FootSwitchConfig.FootswitchMode >= FOOTSWITCH_LAYOUT_LAST)
        {
            ESP_LOGW(TAG, "Config Footswitch mode invalid");
            ControlData.ConfigData.FootSwitchConfig.FootswitchMode = FOOTSWITCH_LAYOUT_1X2;
            SaveUserConfigItem((void*)&ControlData.ConfigData.FootSwitchConfig, sizeof(ControlData.ConfigData.FootSwitchConfig), NVS_USERDATA_FOOTSW_CONF);
        }
    }

    if (ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchPresetLayout != FOOTSWITCH_LAYOUT_DISABLED)
    {
        if (ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchPresetLayout >= FOOTSWITCH_LAYOUT_LAST)
        {
            ESP_LOGW(TAG, "Config External Footswitch preset layout invalid");
            ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchPresetLayout = FOOTSWITCH_LAYOUT_1X4;
            SaveUserConfigItem((void*)&ControlData.ConfigData.FootSwitchConfig, sizeof(ControlData.ConfigData.FootSwitchConfig), NVS_USERDATA_FOOTSW_CONF);
        }
    }

    if (ControlData.ConfigData.FootSwitchConfig.InternalFootswitchPresetLayout != FOOTSWITCH_LAYOUT_DISABLED)
    {
        if (ControlData.ConfigData.FootSwitchConfig.InternalFootswitchPresetLayout >= FOOTSWITCH_LAYOUT_LAST)
        {
            ESP_LOGW(TAG, "Config Internal Footswitch preset layout invalid");
            ControlData.ConfigData.FootSwitchConfig.InternalFootswitchPresetLayout = FOOTSWITCH_LAYOUT_1X4;
            SaveUserConfigItem((void*)&ControlData.ConfigData.FootSwitchConfig, sizeof(ControlData.ConfigData.FootSwitchConfig), NVS_USERDATA_FOOTSW_CONF);
        }
    }

    // check the preset order
    for (loop = 0; loop < MAX_SUPPORTED_PRESETS; loop++)
    {
        // check for any invalid values
        if (ControlData.ConfigData.PresetOrderMappingConfig.PresetOrder[loop] > MAX_SUPPORTED_PRESETS)
        {
            reset_order = 1;
            break;
        }
    }

    if (reset_order)
    {
        ESP_LOGW(TAG, "Repairing preset layout");

        // fix corrupted preset order
        for (loop = 0; loop < MAX_SUPPORTED_PRESETS; loop++)
        {
            ControlData.ConfigData.PresetOrderMappingConfig.PresetOrder[loop] = loop;
        }

        SaveUserConfigItem((void*)&ControlData.ConfigData.PresetOrderMappingConfig, sizeof(ControlData.ConfigData.PresetOrderMappingConfig), NVS_USERDATA_PRESET_ORDER_CONF);
    }

    // show the config
    DumpUserConfig();

    return 1;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:      none
* NOTES:       none
****************************************************************************/
static void DumpUserConfig(void)
{
    ESP_LOGI(TAG, "Config Toggle bypass: %d", (int)ControlData.ConfigData.GeneralConfig.GeneralDoublePressToggleBypass);
    ESP_LOGI(TAG, "Config Loop around: %d", (int)ControlData.ConfigData.GeneralConfig.GeneralLoopAround);
    ESP_LOGI(TAG, "Config Footswitch Mode: %d", (int)ControlData.ConfigData.FootSwitchConfig.FootswitchMode);
    ESP_LOGI(TAG, "Config Screen Rotation: %d", (int)ControlData.ConfigData.GeneralConfig.GeneralScreenRotation);
    ESP_LOGI(TAG, "Config Save preset to slot: %d", (int)ControlData.ConfigData.GeneralConfig.GeneralSavePresetToSlot);
    ESP_LOGI(TAG, "Config Ext Footsw Prst Layout: %d", (int)ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchPresetLayout);
    ESP_LOGI(TAG, "Config Higher Touch Sense: %d", (int)ControlData.ConfigData.GeneralConfig.GeneralEnableTouchHigherSensitivity);
    ESP_LOGI(TAG, "Config Hide BPM flasher: %d", (int)ControlData.ConfigData.GeneralConfig.GeneralHideBPM);

    for (uint8_t loop = 0; loop < MAX_EXTERNAL_EFFECT_FOOTSWITCHES; loop++)
    {
        ESP_LOGI(TAG, "Config Ext Footsw Effect %d Switch: %d", (int)loop, (int)ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[loop].Switch);
        ESP_LOGI(TAG, "Config Ext Footsw Effect %d CC: %d", (int)loop, (int)ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[loop].CC);
        ESP_LOGI(TAG, "Config Ext Footsw Effect %d Val 1: %d", (int)loop, (int)ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[loop].Value_1);
        ESP_LOGI(TAG, "Config Ext Footsw Effect %d Val 2: %d", (int)loop, (int)ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[loop].Value_2);
    }

    for (uint8_t loop = 0; loop < MAX_INTERNAL_EFFECT_FOOTSWITCHES; loop++)
    {
        ESP_LOGI(TAG, "Config Int Footsw Effect %d Switch: %d", (int)loop, (int)ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[loop].Switch);
        ESP_LOGI(TAG, "Config Int Footsw Effect %d CC: %d", (int)loop, (int)ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[loop].CC);
        ESP_LOGI(TAG, "Config Int Footsw Effect %d Val 1: %d", (int)loop, (int)ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[loop].Value_1);
        ESP_LOGI(TAG, "Config Int Footsw Effect %d Val 2: %d", (int)loop, (int)ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[loop].Value_2);
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:      none
* NOTES:       none
****************************************************************************/
static uint8_t SavePresetUserText(uint16_t preset_index, char* text)
{
    esp_err_t err;
    nvs_handle_t my_handle;
    uint8_t result = 0;
    char key[10];

    // build key from preset index
    sprintf(key, "ut%d", (int)preset_index);

    // clamp text at max length and ensure its null terminated
    text[MAX_PRESET_USER_TEXT_LENGTH - 1] = 0;

    ESP_LOGI(TAG, "Writing SavePresetUserText for preset: %d", (int)preset_index);

    // open storage
    err = nvs_open("storage", NVS_READWRITE, &my_handle);

    if (err == ESP_OK)
    {
        // write
        err = nvs_set_str(my_handle, key, text);

        switch (err)
        {
            case ESP_OK:
            {
                result = 1;

                ESP_LOGI(TAG, "Wrote SavePresetUserText OK");
            } break;

            default:
            {
                ESP_LOGE(TAG, "Error (%s) writing SavePresetUserText\n", esp_err_to_name(err));
            } break;
        }

        // commit value
        err = nvs_commit(my_handle);

        // close
        nvs_close(my_handle);
    }
    else
    {
        ESP_LOGE(TAG, "Write SavePresetUserText failed to open");
    }

    return result;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:      none
* NOTES:       none
****************************************************************************/
static uint8_t __attribute__((unused)) LoadPresetUserText(uint16_t preset_index, char* text)
{
    esp_err_t err;
    nvs_handle_t my_handle;
    uint8_t result = 0;
    char key[10];
    size_t required_size;

    // build key from preset index
    sprintf(key, "ut%d", (int)preset_index);

    // default to empty string
    text[0] = 0;

    ESP_LOGI(TAG, "Reading LoadPresetUserText for preset: %d", (int)preset_index);

    // open storage
    err = nvs_open("storage", NVS_READWRITE, &my_handle);

    if (err == ESP_OK)
    {
        // read
        required_size = MAX_PRESET_USER_TEXT_LENGTH;
        err = nvs_get_str(my_handle, key, text, &required_size);

        // clamp text at max length and ensure its null terminated
        text[MAX_PRESET_USER_TEXT_LENGTH - 1] = 0;

        switch (err)
        {
            case ESP_OK:
            {
                result = 1;

                ESP_LOGI(TAG, "Read LoadPresetUserText OK");
            } break;

            case ESP_ERR_NVS_NOT_FOUND:
            {
                ESP_LOGI(TAG, "Read LoadPresetUserText not set");
            } break;

            default:
            {
                ESP_LOGE(TAG, "Error (%s) reading LoadPresetUserText\n", esp_err_to_name(err));
            } break;
        }

        // close
        nvs_close(my_handle);
    }
    else
    {
        ESP_LOGE(TAG, "Read LoadPresetUserText failed to open");
    }

    return result;
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_set_default_config(void)
{
    ControlData.ConfigData.GeneralConfig.GeneralDoublePressToggleBypass = 0;
    ControlData.ConfigData.GeneralConfig.GeneralLoopAround = 0;

    ControlData.ConfigData.FootSwitchConfig.FootswitchMode = FOOTSWITCH_LAYOUT_1X2;

#if CONFIG_TONEX_CONTROLLER_SCREEN_ROTATION_DEFAULT_180
    ControlData.ConfigData.GeneralConfig.GeneralScreenRotation = SCREEN_ROTATION_180;
#else
    ControlData.ConfigData.GeneralConfig.GeneralScreenRotation = SCREEN_ROTATION_0;
#endif

    ControlData.ConfigData.GeneralConfig.GeneralSavePresetToSlot = SAVE_PRESET_SLOT_C;
    ControlData.ConfigData.GeneralConfig.GeneralEnableTouchHigherSensitivity = 0;
    ControlData.ConfigData.GeneralConfig.GeneralHideBPM = 0;
    ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchPresetLayout = FOOTSWITCH_LAYOUT_1X4;
    memset((void*)ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig, 0, sizeof(ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig));
    memset((void*)ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig, 0, sizeof(ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig));

    // default to no switches configured
    for (uint8_t loop = 0; loop < MAX_EXTERNAL_EFFECT_FOOTSWITCHES; loop++)
    {
        ControlData.ConfigData.FootSwitchConfig.ExternalFootswitchEffectConfig[loop].Switch = SWITCH_NOT_USED;
    }

    // default to no switches configured
    for (uint8_t loop = 0; loop < MAX_INTERNAL_EFFECT_FOOTSWITCHES; loop++)
    {
        ControlData.ConfigData.FootSwitchConfig.InternalFootswitchEffectConfig[loop].Switch = SWITCH_NOT_USED;
    }

    // default to 1:1 mappings
    for (uint8_t loop = 0; loop < MAX_SUPPORTED_PRESETS; loop++)
    {
        ControlData.ConfigData.PresetOrderMappingConfig.PresetOrder[loop] = loop;
    }

    for (uint8_t loop = 0; loop < MAX_PC_MAP; loop++)
    {
        // issue here, really need to use (loop + usb_get_first_preset_index_for_connected_modeller()) but modeller may not yet be connected
        ControlData.ConfigData.PCMapConfig.PCMap[loop] = loop;
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
esp_err_t control_get_connected_modeller_params_locked_access(tModellerParameter** param_ptr)
{
    switch (usb_get_connected_modeller_type())
    {
        case AMP_MODELLER_TONEX_ONE:        // fallthrough
        case AMP_MODELLER_TONEX:            // fallthrough
        default:
        {
            return tonex_params_get_locked_access(param_ptr);
        } break;
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
esp_err_t control_release_connected_modeller_params_locked_access(void)
{
    switch (usb_get_connected_modeller_type())
    {
        case AMP_MODELLER_TONEX_ONE:        // fallthrough
        case AMP_MODELLER_TONEX:            // fallthrough
        default:
        {
            return tonex_params_release_locked_access();
        } break;
    }
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_task(void *arg)
{
    tControlMessage message;

    ESP_LOGI(TAG, "Control task start");

    while (1)
    {
        // check for any input messages
        if (xQueueReceive(control_input_queue, (void*)&message, pdMS_TO_TICKS(20)) == pdPASS)
        {
            // process it
            process_control_command(&message);
        }

        // don't hog the CPU
        vTaskDelay(pdMS_TO_TICKS(3));
	}
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_load_config(void)
{
    esp_err_t ret;

    memset((void*)&ControlData, 0, sizeof(ControlData));

    // default config, will be overwritten or used as default
    control_set_default_config();

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init NVS");
    }

    // load the non-volatile user data
    LoadUserData();
}

/****************************************************************************
* NAME:
* DESCRIPTION:
* PARAMETERS:
* RETURN:
* NOTES:
*****************************************************************************/
void control_init(void)
{
    // create queue for commands from other threads
    control_input_queue = xQueueCreate(20, sizeof(tControlMessage));
    if (control_input_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create control input queue!");
    }

    xTaskCreatePinnedToCore(control_task, "CTRL", CTRL_TASK_STACK_SIZE, NULL, CTRL_TASK_PRIORITY, NULL, 1);
}
