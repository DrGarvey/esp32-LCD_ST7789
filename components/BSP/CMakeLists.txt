set(src_dirs
            LED
            LCD_ST7789)

set(include_dirs
            LED
            LCD_ST7789)

# ESP-IDF v6 把驱动拆成了独立的 esp_driver_* 组件，
# 头文件（driver/spi_master.h、driver/ledc.h）必须显式依赖才找得到，
# 只写 driver 是不够的。
#
# esp_lvgl_port : 图形库的移植层，依赖在 main/idf_component.yml 里声明
set(requires
            driver
            esp_lcd
            esp_driver_spi
            esp_driver_ledc
            esp_driver_gpio
            esp_lvgl_port)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)
