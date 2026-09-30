#ifndef __EMOTE_BOARD_H
#define __EMOTE_BOARD_H

#include "emote_gen_player.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化屏幕(ST7789V) + 表情播放器 + 挂载 emote_gen 分区里的资源包。
 * 成功返回播放器句柄，失败返回 NULL。 */
emote_gen_player_handle_t emote_board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __EMOTE_BOARD_H */
