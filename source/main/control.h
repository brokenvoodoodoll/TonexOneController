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

#pragma once

void control_init(void);
void control_load_config(void);

enum ConfigItems
{
    CONFIG_ITEM_TOGGLE_BYPASS,
    CONFIG_ITEM_LOOP_AROUND,
    CONFIG_ITEM_FOOTSWITCH_MODE,
    CONFIG_ITEM_SCREEN_ROTATION,
    CONFIG_ITEM_SAVE_PRESET_TO_SLOT,
    CONFIG_ITEM_EXT_FOOTSW_PRESET_LAYOUT,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT1_SW,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT1_CC,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT1_VAL1,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT1_VAL2,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT2_SW,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT2_CC,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT2_VAL1,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT2_VAL2,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT3_SW,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT3_CC,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT3_VAL1,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT3_VAL2,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT4_SW,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT4_CC,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT4_VAL1,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT4_VAL2,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT5_SW,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT5_CC,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT5_VAL1,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT5_VAL2,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT6_SW,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT6_CC,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT6_VAL1,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT6_VAL2,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT7_SW,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT7_CC,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT7_VAL1,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT7_VAL2,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT8_SW,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT8_CC,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT8_VAL1,
    CONFIG_ITEM_EXT_FOOTSW_EFFECT8_VAL2,
    CONFIG_ITEM_INT_FOOTSW_EFFECT1_SW,
    CONFIG_ITEM_INT_FOOTSW_EFFECT1_CC,
    CONFIG_ITEM_INT_FOOTSW_EFFECT1_VAL1,
    CONFIG_ITEM_INT_FOOTSW_EFFECT1_VAL2,
    CONFIG_ITEM_INT_FOOTSW_EFFECT2_SW,
    CONFIG_ITEM_INT_FOOTSW_EFFECT2_CC,
    CONFIG_ITEM_INT_FOOTSW_EFFECT2_VAL1,
    CONFIG_ITEM_INT_FOOTSW_EFFECT2_VAL2,
    CONFIG_ITEM_INT_FOOTSW_EFFECT3_SW,
    CONFIG_ITEM_INT_FOOTSW_EFFECT3_CC,
    CONFIG_ITEM_INT_FOOTSW_EFFECT3_VAL1,
    CONFIG_ITEM_INT_FOOTSW_EFFECT3_VAL2,
    CONFIG_ITEM_INT_FOOTSW_EFFECT4_SW,
    CONFIG_ITEM_INT_FOOTSW_EFFECT4_CC,
    CONFIG_ITEM_INT_FOOTSW_EFFECT4_VAL1,
    CONFIG_ITEM_INT_FOOTSW_EFFECT4_VAL2,
    CONFIG_ITEM_DISABLE_BPM_FLASHER,
};

enum ScreenRotation
{
    SCREEN_ROTATION_0,
    SCREEN_ROTATION_180,
    // 90 and 270 one day maybe but needs big UI changes
    //SCREEN_ROTATION_90,
    //SCREEN_ROTATION_270,
    SCREEN_ROTATION_MAX,
};

enum SavePresetSlot
{
    SAVE_PRESET_SLOT_CURRENT,
    SAVE_PRESET_SLOT_A,
    SAVE_PRESET_SLOT_B,
    SAVE_PRESET_SLOT_C
};

enum FootswitchLayouts
{
    FOOTSWITCH_LAYOUT_1X2,                // next/previous
    FOOTSWITCH_LAYOUT_1X3,                // 1 row of 3 switches, bank via 1+2 and 2+3
    FOOTSWITCH_LAYOUT_1X4,                // 1 row of 4 switches, bank via 1+2 and 3+4
    FOOTSWITCH_LAYOUT_1X5A,               // 1 row of 5 switches, bank via 1+2 and 4+5
    FOOTSWITCH_LAYOUT_1X5B,               // 1 row of 5 switches, bank via last 2
    FOOTSWITCH_LAYOUT_1X6A,               // 1 row of 6 switches, bank via 1+2 and 5+6
    FOOTSWITCH_LAYOUT_1X6B,               // 1 row of 6 switches, bank via last 2
    FOOTSWITCH_LAYOUT_1X7A,               // 1 row of 7 switches, bank via 1+2 and 6+7
    FOOTSWITCH_LAYOUT_1X7B,               // 1 row of 7 switches, bank via last 2
    FOOTSWITCH_LAYOUT_2X3,                // 2 row2 of 3 switches, bank via 1+2 and 2+3
    FOOTSWITCH_LAYOUT_2X4,                // 2 rows of 4 switches, bank via 1+2 and 3+4
    FOOTSWITCH_LAYOUT_2X5A,               // 2 rows of 5 switches, bank via 1+2 and 4+5
    FOOTSWITCH_LAYOUT_2X5B,               // 2 rows of 5 switches, bank via last 2
    FOOTSWITCH_LAYOUT_2X6A,               // 2 rows of 6 switches, bank via 1+2 and 5+6
    FOOTSWITCH_LAYOUT_2X6B,               // 2 rows of 6 switches, bank via last 2
    FOOTSWITCH_LAYOUT_1X4_BINARY,         // 4 bit binary
    FOOTSWITCH_LAYOUT_LAST,
    FOOTSWITCH_LAYOUT_DISABLED = 0xFF
};

enum ParamTypes
{
    MODELLER_PARAM_TYPE_SWITCH,        // on/off
    MODELLER_PARAM_TYPE_SELECT,        // 0,1,2,3 etc
    MODELLER_PARAM_TYPE_RANGE          // floating point range
};

#define MAX_PARAM_NAME          12

// special cases for handling effect switches that use Midi but don't change a parameter
#define TONEX_UNKNOWN           0xFFFF

typedef struct
{
    float Value;
    float Min;
    float Max;
    char Name[MAX_PARAM_NAME];
    uint8_t Type;
    uint8_t Data1;  // usage depends on connected modeller
    uint8_t Data2;  // usage depends on connected modeller
    uint8_t Data3;  // usage depends on connected modeller
} tModellerParameter;

typedef struct __attribute__ ((packed))
{
    uint8_t Switch;
    uint8_t CC;
    uint8_t Value_1;
    uint8_t Value_2;
} tExternalFootswitchEffectConfig;

typedef struct __attribute__ ((packed))
{
    uint8_t Red;
    uint8_t Blue;
    uint8_t Green;
} tLedColour;

#define MAX_EXTERNAL_EFFECT_FOOTSWITCHES        8
#define MAX_INTERNAL_EFFECT_FOOTSWITCHES        4
#define SWITCH_NOT_USED                         0xFF
#define MAX_SUPPORTED_PRESETS                   150
#define MAX_PRESET_NAME_LENGTH                  33
#define MAX_PC_MAP                              128 // 0 to 127, matches Midi values

// thread safe public API
void control_request_preset_up(void);
void control_request_preset_down(void);
void control_request_preset_index(uint8_t index);
void control_request_bank_index(uint8_t index);
void control_request_ab_bank_up(void);
void control_request_ab_bank_down(void);
void control_set_usb_status(uint32_t status);
void control_save_user_data(uint8_t reboot);
void control_sync_preset_name(uint16_t index, char* name);
void control_sync_preset_details(uint16_t index, char* name);
void control_set_user_text(char* text);
void control_trigger_tap_tempo(void);
void control_set_preset_order(uint8_t* order);
uint8_t* control_get_preset_order(void);
void control_set_pc_map(uint8_t* map);
uint8_t* control_get_pc_map(void);
void control_set_sync_complete(void);
uint8_t control_get_sync_complete(void);
uint32_t control_get_current_preset_index(void);
void control_get_current_preset_name(char* dest);
esp_err_t control_get_connected_modeller_params_locked_access(tModellerParameter** param_ptr);
esp_err_t control_release_connected_modeller_params_locked_access(void);

// config API
void control_set_default_config(void);
void control_set_config_item_int(uint32_t item, uint32_t status);
void control_set_config_item_string(uint32_t item, char* name);
void control_set_config_item_object(uint32_t item, void* object);

uint32_t control_get_config_item_int(uint32_t item);
void control_get_config_item_string(uint32_t item, char* name);
void control_get_config_item_object(uint32_t item, void* object);
