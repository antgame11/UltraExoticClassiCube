file(GLOB SRCS
    "../../../src/*.c"
    "../../../src/esp32/*.c"
)

# Register component
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "../../../src"
    REQUIRES nvs_flash console driver esp_timer esp_wifi spiffs vfs esp_http_server
    LDFRAGMENTS "linker.lf"
    EMBED_FILES "index.html"
)

# Add custom compiler defines for the build
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    -DPLAT_ESP32
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O2
    -Wno-error
    -Wno-unused-const-variable
    -Wno-unused-variable
)
