# BSP 组件：只编译底层驱动（LCD、LED）。
#
# 注意：这里不再依赖 esp_lvgl_port——表情引擎 esp_emote_gfx 用的是 LVGL 8，
#       而 esp_lvgl_port 是 LVGL 9，两者冲突。LVGL/渲染的初始化交给表情播放器，
#       本组件只负责点亮 ST7789V 面板，并提供 panel/IO 句柄给上层。
set(srcs
            "LED/led.c"
            "LCD_ST7789/lcd_st7789.c")

set(include_dirs
            LED
            LCD_ST7789)

# ESP-IDF v6 把驱动拆成了独立的 esp_driver_* 组件，
# 头文件（driver/spi_master.h、driver/ledc.h）必须显式依赖才找得到，
# 只写 driver 是不够的。
set(requires
            driver
            esp_lcd
            esp_driver_spi
            esp_driver_ledc
            esp_driver_gpio)

idf_component_register(SRCS ${srcs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)
