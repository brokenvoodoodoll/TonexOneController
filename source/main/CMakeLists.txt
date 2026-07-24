idf_component_register(SRCS
                       "control.c"
                       "footswitches.c"
                       "main.c"
                       "tonex_params.c"
                       "usb_comms.c"
                       "usb_tonex_one.c"
                       "usb_tonex_common.c"
                       "usb_tonex.c"
                       "midi_helper.c"
                       "midi_helper_tonex.c"
                       INCLUDE_DIRS "." "./"
                       REQUIRES usb nvs_flash driver
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
