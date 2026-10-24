idf_component_register(
    SRCS "src/mbus.c"
         "src/mbus-protocol.c"
         "src/mbus-protocol-aux.c"
         "src/mbus-serial.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-pointer-sign)
