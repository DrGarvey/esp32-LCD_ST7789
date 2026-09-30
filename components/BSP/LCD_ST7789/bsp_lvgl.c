#include "bsp_lvgl.h"
#include "lcd_st7789.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "LVGL";

lv_display_t *bsp_lvgl_init(void)
{
    /* 1. 先把 LCD 底层跑起来（SPI + ST7789 + 背光） */
    bsp_lcd_init();

    /* 2. 初始化 LVGL port。
     *    注意：这个函数内部会调用 lv_init() 并创建 LVGL 任务，
     *    所以外面【绝对不要再自己调 lv_init()】，否则会重复初始化。 */
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    /* 3. 把这块屏注册给 LVGL */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = bsp_lcd_get_io_handle(),
        .panel_handle  = bsp_lcd_get_panel_handle(),

        /* 注意单位是「像素」，不是字节 */
        .buffer_size   = LCD_WIDTH * LCD_LVGL_BUF_LINES,
        .double_buffer = false,

        .hres          = LCD_WIDTH,
        .vres          = LCD_HEIGHT,
        .monochrome    = false,
        .color_format  = LV_COLOR_FORMAT_RGB565,

        /* 屏幕方向。这三项必须在这里设，不能只靠在 bsp_lcd_init() 里设 ——
         * esp_lvgl_port 注册显示时会用它自己的这三个值去覆盖 MADCTL。
         * 取值和 lcd_st7789.c 用的是同一组宏，所以两处一致。 */
        .rotation = {
            .swap_xy  = LCD_SWAP_XY,
            .mirror_x = LCD_MIRROR_X,
            .mirror_y = LCD_MIRROR_Y,
        },

        .flags = {
            /* 缓冲区放内部 RAM，DMA 可直接搬运 */
            .buff_dma   = true,
            /* ST7789 走 SPI 时数据是大端，而 LVGL 输出小端，
             * 必须交换字节，否则颜色整个是乱的。
             * 这个标志就是干这个的（等价于官方例程里的 lv_draw_sw_rgb565_swap）。 */
            .swap_bytes = true,
        },
    };

    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "lvgl_port_add_disp 失败");
        return NULL;
    }

    ESP_LOGI(TAG, "LVGL 就绪，屏幕 %dx%d，绘制缓冲 %d 行",
             LCD_WIDTH, LCD_HEIGHT, LCD_LVGL_BUF_LINES);
    return disp;
}
