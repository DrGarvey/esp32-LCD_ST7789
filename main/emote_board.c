/* ============================================================================
 *  表情播放器与屏幕的衔接层
 * ----------------------------------------------------------------------------
 *  表情引擎 esp_emote_gfx 本身不带屏幕驱动：它只负责算出像素，然后通过
 *  flush_cb 回调把像素「吐」给我们的面板去画。所以这里做的事情就是：
 *
 *    1. 用你自己的 ST7789V 驱动把屏幕点亮（bsp_lcd_init）。
 *    2. 把 gfx 的「刷屏完成」通知串起来（on_color_trans_done）。
 *    3. 建播放器、挂载 emote_gen 分区里的 .bin 资源包。
 *
 *  分辨率问题：你的包是 240x240 画的，而屏是 320x240 横屏。所以 gfx 内部
 *  按 240x240 建画布，flush 时整体往右平移 40px，让表情在屏上居中。
 * ========================================================================== */

#include "emote_board.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#include "lcd_st7789.h"   /* 你自己的 ST7789V 面板驱动 */

static const char *TAG = "emote_board";

/* ---- 面板逻辑分辨率（横屏）与表情包画布 ---- */
#define EMOTE_PANEL_W   320
#define EMOTE_PANEL_H   240
#define EMOTE_CANVAS_W  240
#define EMOTE_CANVAS_H  240
/* 240x240 在 320x240 上居中，左右各留 40px 黑边 */
#define EMOTE_X_OFFSET  ((EMOTE_PANEL_W - EMOTE_CANVAS_W) / 2)

static esp_lcd_panel_handle_t    s_panel_handle;
static esp_lcd_panel_io_handle_t s_io_handle;

/* ---------------------------------------------------------------------------
 * gfx 渲染出一块区域后，交给面板去画。
 * data 已经是面板要的 RGB565 字节序（gfx 里 swap=true 换好了），直接搬即可。
 * ------------------------------------------------------------------------- */
static void emote_flush_cb(int x1, int y1, int x2, int y2, const void *data,
                           emote_gen_player_handle_t manager)
{
    (void)manager;
    esp_lcd_panel_draw_bitmap(s_panel_handle,
                              x1 + EMOTE_X_OFFSET, y1,
                              x2 + EMOTE_X_OFFSET, y2,
                              data);
}

/* ---------------------------------------------------------------------------
 * 一次刷屏完成 -> 告诉 gfx 可以继续出下一块。缺了它 gfx 会卡住不刷。
 * ------------------------------------------------------------------------- */
static bool emote_on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                      esp_lcd_panel_io_event_data_t *edata,
                                      void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    emote_gen_player_notify_flush_finished((emote_gen_player_handle_t)user_ctx);
    return true;
}

emote_gen_player_handle_t emote_board_init(void)
{
    /* 1. 面板：SPI + ST7789 + 背光（复用你现有的 BSP 驱动） */
    bsp_lcd_init();
    s_panel_handle = bsp_lcd_get_panel_handle();
    s_io_handle    = bsp_lcd_get_io_handle();

    /* 2. 先整屏刷黑。表情画布只有 240x240，左右 40px 黑边靠这一步铺底。 */
    bsp_lcd_fill_color(0x0000);

    /* 3. 创建播放器。gfx 内部按 240x240 建画布，flush 时平移到屏中间。 */
    emote_gen_player_config_t cfg = {
        .flags = {
            .swap          = true,   /* 你的屏是大端，需要换字节（等价 lcd_swap_color） */
            .double_buffer = true,
            .buff_dma      = true,
            .buff_spiram   = false,
        },
        .gfx_emote = {
            .h_res = EMOTE_CANVAS_W,
            .v_res = EMOTE_CANVAS_H,
            .fps   = 25,
        },
        .buffers = {
            .buf_pixels = EMOTE_CANVAS_W * 16,
        },
        .task = {
            .task_priority    = 7,
            .task_stack       = 5 * 1024,
            .task_affinity    = 0,
            .task_stack_in_ext = false,
        },
        .flush_cb  = emote_flush_cb,
        .update_cb = NULL,
    };

    emote_gen_player_handle_t player = emote_gen_player_init(&cfg);
    if (player == NULL) {
        ESP_LOGE(TAG, "emote_gen_player_init failed");
        return NULL;
    }

    /* 4. 注册刷屏完成回调：gfx 靠它知道 DMA 搬运结束、可以出下一帧。 */
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = emote_on_color_trans_done,
    };
    esp_lcd_panel_io_register_event_callbacks(s_io_handle, &cbs, player);

    /* 5. 挂载 emote_gen 分区里的资源包（mmap 直接映射 flash，不占 RAM）。 */
    emote_gen_player_data_t assets = {
        .type   = EMOTE_GEN_PLAYER_SOURCE_PARTITION,
        .source = { .partition_label = "emote_gen" },
        .flags  = { .mmap_enable = 1 },
    };
    esp_err_t err = emote_gen_player_mount_assets(player, &assets);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount_assets failed: %s", esp_err_to_name(err));
        emote_gen_player_deinit(player);
        return NULL;
    }

    return player;
}
