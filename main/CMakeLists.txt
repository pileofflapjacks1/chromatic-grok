# Modified by pileofflapjacks1, 2026: link the grok component.
# Original: ModRetro oss-chromatic-console-mcu v4.6 (63ed3b6), GPL-3.0.
cmake_minimum_required(VERSION 3.22)

idf_component_register(
    SRCS
        "main.c" "gfx.c" "fpga_tx.c" "fpga_rx.c" "fpga_common.c" "pwrmgr.c"
    INCLUDE_DIRS "."
    REQUIRES
        images esp_driver_uart esp_driver_spi esp_driver_gpio nvs_flash fatfs
        common battery button osd menu_mgr settings mutex
        console crc grok
)
