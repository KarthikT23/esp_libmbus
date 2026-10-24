idf_component_register(
    SRCS "src/mbus.c"
         "src/mbus-protocol.c"
         "src/mbus-protocol-aux.c"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-pointer-sign)
