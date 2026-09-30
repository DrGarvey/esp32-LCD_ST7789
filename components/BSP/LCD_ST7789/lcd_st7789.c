#include "lcd_st7789.h"

#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

static const char *TAG = "LCD";

/* 屏幕用 SPI2_HOST（也就是 ESP32-S3 上的 FSPI） */
#define LCD_SPI_HOST        SPI2_HOST

/* 刷屏时每次搬多少行。行数越大越快，内存占用 = LCD_WIDTH * 行数 * 2 字节 */
#define LCD_FILL_LINES      40
#define LCD_FILL_BUF_BYTES  ((size_t)LCD_WIDTH * LCD_FILL_LINES * sizeof(uint16_t))

/* 背光 PWM 参数 */
#define BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER       LEDC_TIMER_0
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BL_LEDC_RES         LEDC_TIMER_10_BIT          /* 占空比范围 0~1023 */
#define BL_LEDC_FREQ_HZ     5000                       /* 5kHz，人眼看不到闪烁 */
#define BL_DUTY_MAX         ((1 << 10) - 1)            /* 1023 */

/* 内部句柄。esp_lvgl_port 要用，所以通过 bsp_lcd_get_*_handle() 暴露出去 */
static esp_lcd_panel_handle_t    panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle    = NULL;

/* ---------------------------------------------------------------------------
 * ST7789 走 SPI 时数据是「大端」的：一个像素 0xF800 需要先发 0xF8 再发 0x00。
 * 而 ESP32 是小端，uint16_t 0xF800 在内存里是 00 F8，直接发出去颜色就错了。
 * 所以送进 LCD 之前要把高低字节对调一下。
 * （ESP-IDF 官方例程里也是这么做的：lv_draw_sw_rgb565_swap）
 * ------------------------------------------------------------------------- */
static inline uint16_t lcd_swap_color(uint16_t color)
{
    return (uint16_t)((color >> 8) | (color << 8));
}

/* ---------------------------------------------------------------------------
 * 背光初始化：用 LEDC 产生 PWM，占空比决定亮度
 * ------------------------------------------------------------------------- */
static void lcd_backlight_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_RES,
        .timer_num       = BL_LEDC_TIMER,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = LCD_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = BL_DUTY_MAX,      /* 上电就是全亮 */
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
}

void bsp_lcd_set_backlight(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (uint32_t)BL_DUTY_MAX * percent / 100;
    ESP_ERROR_CHECK(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL));
}

void bsp_lcd_backlight(bool on)
{
    bsp_lcd_set_backlight(on ? 100 : 0);
}

/* ---------------------------------------------------------------------------
 * LCD 初始化：SPI 总线 -> panel IO -> ST7789 面板 -> 背光
 * ------------------------------------------------------------------------- */
void bsp_lcd_init(void)
{
    /* 1. 先把背光点亮。背光不亮的话屏幕全黑，会误以为驱动没跑起来 */
    lcd_backlight_init();

    /* 2. 初始化 SPI 总线 */
    spi_bus_config_t buscfg = {
        .sclk_io_num     = LCD_SCLK,
        .mosi_io_num     = LCD_MOSI,
        .miso_io_num     = -1,          /* 你的模组没有 DOUT 引脚，不接 */
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_FILL_BUF_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* 3. 初始化 panel IO：负责把「命令/数据」按 DC 脚的时序发出去 */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num       = LCD_CS,
        .dc_gpio_num       = LCD_DC,
        .spi_mode          = 0,
        .pclk_hz           = LCD_SPI_CLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                             &io_cfg, &io_handle));

    /* 4. 创建 ST7789 面板 */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        /* 红蓝通道顺序。这块屏实测要用 RGB：
         * 用 BGR 时蓝色和红色会互换（白色/绿色不受影响），
         * 换屏时如果红蓝又反了，就把这里改回 BGR。 */
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle));

    /* 5. 复位 + 初始化面板 */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    /* 5b. 设置屏幕方向（横屏 / 竖屏）。
     *     这两个调用最终写进 ST7789 的 MADCTL 寄存器，是硬件级旋转，
     *     不消耗 CPU，也不影响刷屏速度。
     *     取值都来自 lcd_st7789.h 里的宏，那里有方向不对时的排查表。 */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, LCD_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, LCD_MIRROR_X, LCD_MIRROR_Y));

    /* 6. 颜色反转。ESP-IDF 的 ST7789 驱动默认不发 INVON 指令，
     *    但绝大多数 ST7789 模组需要反色，否则白色会显示成黑色。
     *    如果你发现「填白色结果是黑的」，就是这里要不要的问题。 */
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));

    /* 7. 显示偏移。240x320 的屏不需要偏移，所以这里保持注释。
     *    如果你换成 240x240 的屏，把下面这行放开： */
    // ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 80));

    /* 8. 打开显示 */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    bsp_lcd_backlight(true);

    ESP_LOGI(TAG, "LCD 初始化完成 %dx%d | SCLK=%d MOSI=%d CS=%d DC=%d RST=%d BL=%d",
             LCD_WIDTH, LCD_HEIGHT, LCD_SCLK, LCD_MOSI, LCD_CS, LCD_DC, LCD_RST, LCD_BL);
}

/* ---------------------------------------------------------------------------
 * 全屏填充。
 * 注意：不能像原来那样把单个 uint16_t 的地址传给 draw_bitmap —— 驱动会当成
 * 240*320 个像素去读，直接越界访问内存。这里改成先用一块行缓冲区填好颜色，
 * 再分次搬过去。
 * ------------------------------------------------------------------------- */
void bsp_lcd_fill_color(uint16_t color)
{
    if (panel_handle == NULL) {
        ESP_LOGE(TAG, "请先调用 bsp_lcd_init()");
        return;
    }

    /* DMA 搬运要求内存可被 DMA 访问，优先用内部 RAM */
    uint16_t *buf = heap_caps_malloc(LCD_FILL_BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buf == NULL) {
        ESP_LOGE(TAG, "填充缓冲区分配失败（%u 字节）", (unsigned)LCD_FILL_BUF_BYTES);
        return;
    }

    uint16_t swapped = lcd_swap_color(color);
    for (size_t i = 0; i < LCD_FILL_BUF_BYTES / sizeof(uint16_t); i++) {
        buf[i] = swapped;
    }

    for (int y = 0; y < LCD_HEIGHT; y += LCD_FILL_LINES) {
        int y_end = y + LCD_FILL_LINES;
        if (y_end > LCD_HEIGHT) {
            y_end = LCD_HEIGHT;
        }
        /* x_end / y_end 是「开区间」，传的是最后一行/列的下一格 */
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, y, LCD_WIDTH, y_end, buf));
    }

    free(buf);
}

/* ---------------------------------------------------------------------------
 * 底层句柄访问接口，供 esp_lvgl_port 注册显示使用
 * ------------------------------------------------------------------------- */
esp_lcd_panel_handle_t bsp_lcd_get_panel_handle(void)
{
    return panel_handle;
}

esp_lcd_panel_io_handle_t bsp_lcd_get_io_handle(void)
{
    return io_handle;
}
