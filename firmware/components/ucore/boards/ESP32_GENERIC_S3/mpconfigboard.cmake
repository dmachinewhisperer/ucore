set(IDF_TARGET esp32s3)

# No SPIRAM in the default profile — many S3 dev kits (incl. plain
# ESP32-S3-WROOM-1U / WROOM-1 variants without -N8R8 etc.) ship without
# PSRAM. Boards that do have PSRAM should use a SPIRAM variant.
set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/ESP32_GENERIC_S3/sdkconfig.board
)
