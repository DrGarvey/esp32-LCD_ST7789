/* ============================================================================
 *  表情逐个轮播演示
 * ----------------------------------------------------------------------------
 *  流程：
 *    上电 -> 初始化屏幕 + 表情播放器 -> 挂载 emote_gen 分区里的资源包
 *         -> 按顺序循环播放包里的每一个表情，每个停 3 秒。
 *
 *  表情来自乐鑫开源的 Emote Pack（你在打包工具里导出成了 emote-assets.bin），
 *  一共 23 个。这里只是「逐个播放」，以后要按按键 / 传感器切换，
 *  把 while 循环里那句 emote_gen_player_anim_now 换成「按条件挑一个 index」即可。
 * ========================================================================== */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "emote_board.h"

static const char *TAG = "emote_demo";

/* 每个表情停留多久（毫秒） */
#define EMOTE_DWELL_MS  3000

void app_main(void)
{
    emote_gen_player_handle_t player = emote_board_init();
    if (player == NULL) {
        ESP_LOGE(TAG, "表情播放器初始化失败");
        return;
    }

    size_t count = emote_gen_player_get_index_count(player);
    ESP_LOGI(TAG, "资源包共 %u 个表情：", (unsigned)count);
    for (size_t i = 0; i < count; i++) {
        const emote_gen_player_index_entry_t *e =
            emote_gen_player_get_index_entry(player, i);
        if (e != NULL) {
            ESP_LOGI(TAG, "  [%2u] %s", (unsigned)i, e->name);
        }
    }

    if (count == 0) {
        ESP_LOGE(TAG, "资源包是空的。请确认 emote_gen 分区已烧录 emote-assets.bin");
        return;
    }

    size_t idx = 0;
    while (1) {
        ESP_LOGI(TAG, "播放 [%u/%u]", (unsigned)idx, (unsigned)count);
        /* repeat=true：当前表情的循环段一直放，直到切下一个 */
        esp_err_t err = emote_gen_player_anim_now(player, idx, true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "切换表情失败: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(EMOTE_DWELL_MS));
        idx = (idx + 1) % count;
    }
}
