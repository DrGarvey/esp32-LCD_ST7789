#ifndef __LCD_ST7789_H
#define __LCD_ST7789_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_lcd_types.h"

/* ============================================================================
 *                          接 线 表（请严格照此连接）
 * ----------------------------------------------------------------------------
 *   LCD 模组引脚    接 ESP32-S3        说明
 *   ----------------------------------------------------------------------
 *   VCC            3V3               电源正极
 *   GND            GND               共地（一定要接，否则花屏）
 *   DIN            GPIO 11           SPI 数据（MOSI / SDA）
 *   CLK            GPIO 12           SPI 时钟（SCLK / SCL）
 *   CS             GPIO 10           片选，低电平有效
 *   DC             GPIO 13           命令/数据选择（也叫 RS）
 *   RST            GPIO 9            复位，低电平有效
 *   BL             GPIO 4            背光，用 LEDC PWM 调光
 * ----------------------------------------------------------------------------
 * ⚠ 本板是 Octal PSRAM 模组，以下引脚已被占用，绝对不要接 LCD：
 *      GPIO 26~32  → SPI Flash
 *      GPIO 33~37  → 八线 PSRAM
 *      GPIO 19/20  → USB-JTAG
 *      GPIO 43/44  → 串口 UART0
 *   GPIO 11/12/10 正好是 ESP32-S3 的 FSPI 硬件 SPI 引脚（IOMUX），
 *   走硬件通路信号最干净，所以优先选它们。
 * ========================================================================== */

/* ---- 引脚定义：必须与上面的接线表一一对应 ---- */
#define LCD_SCLK        12      /* SPI 时钟   */
#define LCD_MOSI        11      /* SPI 数据   */
#define LCD_CS          10      /* 片选       */
#define LCD_DC          13      /* 命令/数据  */
#define LCD_RST         9       /* 复位       */
#define LCD_BL          4       /* 背光 PWM   */

/* ============================================================================
 * 屏幕方向与分辨率
 * ----------------------------------------------------------------------------
 * ST7789 面板的物理像素是固定的：240(宽) x 320(高)。
 *
 * 所谓"横屏"并不是把画面拉伸，而是让控制器把 X 轴和 Y 轴对调
 * （改 ST7789 内部的 MADCTL 寄存器），于是逻辑分辨率就变成 320x240。
 * 这是硬件级旋转，不消耗 CPU、不损失性能。
 * ========================================================================== */

/* 屏幕方向：1 = 横屏(320x240)，0 = 竖屏(240x320) */
#define LCD_LANDSCAPE       1

/* 面板的物理分辨率，不随方向改变 */
#define LCD_PANEL_NATIVE_W  240
#define LCD_PANEL_NATIVE_H  320

/* 逻辑分辨率：上层代码（包括 LVGL）看到的尺寸，随方向变化 */
#if LCD_LANDSCAPE
#define LCD_WIDTH       320
#define LCD_HEIGHT      240
#else
#define LCD_WIDTH       240
#define LCD_HEIGHT      320
#endif

/* ---- 旋转的具体参数 ----
 * 这三项最终写进 ST7789 的 MADCTL 寄存器：
 *   swap_xy  : 对调 X/Y 轴        （横屏必须 true，竖屏 false）
 *   mirror_x : 水平方向镜像
 *   mirror_y : 垂直方向镜像
 *
 * ⚠ 如果烧录后画面方向不对，只改下面两个 mirror 值即可，四种组合试一遍：
 *     现象                      改法
 *     ----------------------    -----------------------------------------
 *     横屏但上下颠倒            MIRROR_X 改 false，MIRROR_Y 改 true
 *     横屏但左右镜像（字是反的）MIRROR_X 改 false，MIRROR_Y 改 false
 *     （上面两种都对不上时，把两个都设 true 再试）
 *     仍然是竖屏               确认 LCD_LANDSCAPE 是 1
 */
#define LCD_SWAP_XY     true
#define LCD_MIRROR_X    true
#define LCD_MIRROR_Y    false

/* ---- SPI 时钟频率 ----
 * 40MHz 是常规值。如果你看到花屏、雪花点、颜色错乱，
 * 基本就是杜邦线太长导致信号完整性不够，把它改成 (20 * 1000 * 1000) 即可。
 */
#define LCD_SPI_CLK_HZ  (40 * 1000 * 1000)

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 对外暴露的 BSP 接口 ---- */

/* 初始化 SPI 总线 + ST7789 面板 + 背光 PWM。调用一次即可。 */
void bsp_lcd_init(void);

/* 用指定颜色填满整个屏幕。color 是标准 RGB565，例如 0xF800 = 红 */
void bsp_lcd_fill_color(uint16_t color);

/* 背光开关：true 全亮，false 全灭 */
void bsp_lcd_backlight(bool on);

/* 背光亮度调节：percent 取 0~100，超过 100 按 100 处理 */
void bsp_lcd_set_backlight(uint8_t percent);

/* 取出底层句柄，交给 esp_lvgl_port 注册显示用（必须先 bsp_lcd_init） */
esp_lcd_panel_handle_t    bsp_lcd_get_panel_handle(void);
esp_lcd_panel_io_handle_t bsp_lcd_get_io_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_ST7789_H */
