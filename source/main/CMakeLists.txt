idf_component_register(SRCS
                       "control.c"
                       "footswitches.c"
                       "main.c"
                       "tonex_params.c"
                       "usb_comms.c"
                       "usb_tonex_one.c"
                       "usb_tonex_common.c"
                       "usb_tonex.c"
                       "midi_serial.c"
                       "midi_helper.c"
                       "midi_helper_tonex.c"
                       "platform_devkitc.c"
                       "platform_wszero.c"
                       INCLUDE_DIRS "." "./"
                       REQUIRES bt nvs_flash console usb fatfs app_update usb_host_midi esp_http_server
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
