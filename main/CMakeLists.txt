idf_component_register(SRCS "main.c" "emote_board.c"
                       INCLUDE_DIRS "."
                       REQUIRES BSP)

# 把打包工具生成的 emote-assets.bin 直接烧进 emote_gen 分区（mmap 用）。
# 这样 idf.py flash 一次就能把固件和表情包一起烧进去，不用手动 esptool。
#
# spiffs_create_partition_assets 由依赖 esp_mmap_assets 提供，
# 和官方 test_apps 的用法一致：COPY_PREBUILT_BIN 把现成的 .bin 原样烧进分区。
set(EMOTE_ASSETS_BIN "${CMAKE_CURRENT_LIST_DIR}/../Emote Pack/emote-assets.bin")

spiffs_create_partition_assets(
    emote_gen
    "${CMAKE_CURRENT_LIST_DIR}"
    FLASH_IN_PROJECT
    COPY_PREBUILT_BIN ${EMOTE_ASSETS_BIN}
)
