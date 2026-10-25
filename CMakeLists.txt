set(mbus_srcs
    "src/mbus.c"
    "src/mbus-protocol.c"
    "src/mbus-protocol-aux.c"
    "src/mbus-serial.c"
    "src/mbus-link.c"
    "src/mbus-meter-record.c"
    "src/mbus-meter-frame.c"
    "src/mbus-meter.c"
    "src/mbus-meter-port.c"
)

if(CONFIG_MBUS_ENABLE_TCP)
    list(APPEND mbus_srcs "src/mbus-tcp.c")
endif()

idf_component_register(
    SRCS ${mbus_srcs}
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart
)

if(CONFIG_MBUS_ENABLE_TCP)
    idf_component_optional_requires(PRIVATE lwip)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-pointer-sign)
