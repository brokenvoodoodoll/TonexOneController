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


#ifndef _MAIN_H
#define _MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#define APP_VERSION		"2.0.4.2"

#define I2C_MASTER_NUM_1                0
#define I2C_MASTER_NUM_2                1

extern SemaphoreHandle_t I2CMutex_1;
extern SemaphoreHandle_t I2CMutex_2;

#if CONFIG_TONEX_CONTROLLER_HARDWARE_PLATFORM_WAVESHARE_ZERO
    // I2C bus 1
    #define I2C_MASTER_1_SCL_IO  GPIO_NUM_10
    #define I2C_MASTER_1_SDA_IO  GPIO_NUM_11

    // I2C bus 2
    #define I2C_MASTER_2_SCL_IO  -1
    #define I2C_MASTER_2_SDA_IO  -1

    #define EXTERNAL_IO_EXPANDER_BUS       I2C_MASTER_NUM_1
    #define EXTERNAL_IO_EXPANDER_MUTEX     I2CMutex_1

    // direct IO pins
    #define FOOTSWITCH_1		GPIO_NUM_4
    #define FOOTSWITCH_2		GPIO_NUM_6
    #define FOOTSWITCH_3		GPIO_NUM_2
    #define FOOTSWITCH_4		GPIO_NUM_1

    // Midi
    #define UART_RX_PIN         GPIO_NUM_5
    #define UART_TX_PIN         GPIO_NUM_7

    // leds
    #define LED_OUTPUT_GPIO_NUM    GPIO_NUM_21

#elif CONFIG_TONEX_CONTROLLER_HARDWARE_PLATFORM_DEVKITC
    // I2C bus 1
    #define I2C_MASTER_1_SCL_IO  GPIO_NUM_10
    #define I2C_MASTER_1_SDA_IO  GPIO_NUM_11

    // I2C bus 2
    #define I2C_MASTER_2_SCL_IO  -1
    #define I2C_MASTER_2_SDA_IO  -1

    #define EXTERNAL_IO_EXPANDER_BUS       I2C_MASTER_NUM_1
    #define EXTERNAL_IO_EXPANDER_MUTEX     I2CMutex_1

    // direct IO pins
    #define FOOTSWITCH_1		GPIO_NUM_4
    #define FOOTSWITCH_2		GPIO_NUM_6
    #define FOOTSWITCH_3		GPIO_NUM_2
    #define FOOTSWITCH_4		GPIO_NUM_1

    // Midi
    #define UART_RX_PIN         GPIO_NUM_5
    #define UART_TX_PIN         GPIO_NUM_7

    // leds
    #define LED_OUTPUT_GPIO_NUM   GPIO_NUM_48
#else
    #error "Unknown hardware platform!"
#endif

esp_err_t i2c_master_reset(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
