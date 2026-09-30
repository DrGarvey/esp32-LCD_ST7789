#ifndef __BSP_LVGL_H
#define __BSP_LVGL_H

#include "lvgl.h"

/* LVGL 绘制缓冲区高度（行数）。
 * 40 行 = 240 x 40 像素 = 19200 字节，放内部 RAM 让 DMA 直接搬。
 * 调大刷屏更快，但吃内存；调小则相反。 */
#define LCD_LVGL_BUF_LINES  40

#ifdef __cplusplus
extern "C" {
#endif

/* 一次性做完：LCD 底层初始化 → 启动 LVGL → 把这块屏注册给 LVGL。
 * 成功后返回 LVGL 的 display 句柄，失败返回 NULL。
 * 调用这一个函数就够了。 */
lv_display_t *bsp_lvgl_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LVGL_H */
