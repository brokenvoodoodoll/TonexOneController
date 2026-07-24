idf_component_register(SRCS
                       "midi_control.c"
                       "control.c"
                       "footswitches.c"
                       "CH422G.c"
                       "main.c"
                       "tonex_params.c"
                       "SX1509.c"
                       "usb_comms.c"
                       "usb_tonex_one.c"
                       "usb_tonex_common.c"
                       "usb_tonex.c"
                       "CH422G.c"
                       "midi_serial.c"
                       "leds.c"
                       "midi_helper.c"
                       "midi_helper_tonex.c"
                       "LP5562.c"
                       "platform_devkitc.c"
                       "platform_wszero.c"
                       INCLUDE_DIRS "." "./"
                       REQUIRES bt nvs_flash console usb fatfs app_update usb_host_midi esp_http_server
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
