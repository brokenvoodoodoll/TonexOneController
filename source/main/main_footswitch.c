#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "usb_comms.h"
#include "usb_tonex_one.h"
#include "tonex_params.h"

static const char *TAG = "mwp_main";

#define NUM_BUTTONS 6
static const gpio_num_t BUTTON_PINS[NUM_BUTTONS] = {GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6};
static uint8_t btn_state[NUM_BUTTONS] = {1, 1, 1, 1, 1, 1};

static void button_task(void *arg) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << BUTTON_PINS[i]),
            .pull_down_en = 0,
            .pull_up_en = 1
        };
        gpio_config(&io_conf);
    }

    while (1) {
        for (int i = 0; i < NUM_BUTTONS; i++) {
            int state = gpio_get_level(BUTTON_PINS[i]);
            if (state == 0 && btn_state[i] == 1) { // Falling edge
                ESP_LOGI(TAG, "Button %d pressed. Switching preset to %d", i + 1, i);
                usb_set_preset(i); // Assuming preset index maps to 0..5
            }
            btn_state[i] = state;
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // Simple 20ms debounce polling
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "ToneX One MWP App start");

    // Initialize required params & USB Host
    tonex_params_init();
    init_usb_comms();

    // Start button polling task
    xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL);
}
