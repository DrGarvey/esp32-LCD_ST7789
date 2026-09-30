/* ============================================================================
 *  程序入口
 * ----------------------------------------------------------------------------
 *  这个文件只负责「启动流程」，界面长什么样全在 ui.c 里。
 *  分开的好处：改界面不用动启动逻辑，改启动逻辑也不会误碰界面代码。
 *
 *  完整流程：
 *    上电 -> ESP-IDF 调用 app_main()
 *           |-- 初始化        屏幕 + LVGL 后台任务
 *           |-- 加锁          之后操作界面才安全
 *           |-- ui_create()   搭界面（只做这一次）
 *           |-- 解锁          交给后台任务持续刷新
 *           '-- 启动演示任务  之后反复调 ui_update() 改内容
 *
 *  app_main 返回后程序不会结束：ESP-IDF 只删掉「main 任务」，
 *  LVGL 的后台任务和我们创建的演示任务都还在跑，
 *  所以界面会一直保持并刷新。
 *  这跟 Arduino / 裸机必须写 while(1) 是两种完全不同的模式。
 * ========================================================================== */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "bsp_lvgl.h"
#include "ui.h"

/* ============================================================================
 *  测试用的演示任务（看懂之后可以整段删掉）
 * ----------------------------------------------------------------------------
 *  它模拟的是这样一个流程：
 *      别的地方算出了新数据  ->  加锁  ->  ui_update()  ->  解锁
 *
 *  以后你接了传感器、连了网络，只要把 k_pages[] 换成真实读到的数据，
 *  流程就是这个流程，一行都不用改。
 *
 *  ⚠ 为什么 ui_update() 要夹在 lock / unlock 之间？
 *     因为它会修改控件。而 LVGL 后台任务此刻可能正在读同一批控件去画屏幕。
 *     一边改一边读，画面就会撕裂、错位，甚至崩溃。
 *     这不是「可能出问题」，是「迟早出问题」——所以这两行不能省。
 * ========================================================================== */
static void ui_demo_task(void *arg)
{
    (void)arg;   /* 用不到这个参数，加一句告诉编译器「我知道它没用」 */

    /* 三页测试内容，轮流显示。
     * ⚠ 文字必须是英文：LVGL 内置字体里没有中文字形，
     *   写中文会显示成一堆小方块（真的中文要另外做字库）。 */
    static const ui_data_t k_pages[3] = {
        {
            .title      = "Page 1 / 3",
            .status     = "ui_update() wrote this",
            .box_names  = { "RED", "GREEN", "BLUE" },
            .box_colors = { 0xFF0000, 0x00FF00, 0x0000FF },
        },
        {
            .title      = "Page 2 / 3",
            .status     = "only the content changed",
            .box_names  = { "CYAN", "MAGENTA", "YELLOW" },
            .box_colors = { 0x00FFFF, 0xFF00FF, 0xFFFF00 },
        },
        {
            .title      = "Page 3 / 3",
            .status     = "the layout was never rebuilt",
            .box_names  = { "WHITE", "GRAY", "ORANGE" },
            .box_colors = { 0xFFFFFF, 0x808080, 0xFF8000 },
        },
    };
    const int page_count = sizeof(k_pages) / sizeof(k_pages[0]);

    unsigned tick = 0;

    while (1) {
        /* 先拷一份到本地变量（k_pages 是 const，不能直接改它），
         * 这样就能在每页原本的文字后面追加一个不断增长的编号。
         * 目的是一眼看出「屏幕确实在刷新」，而不是卡住了。 */
        ui_data_t data = k_pages[tick % page_count];

        char status[64];
        snprintf(status, sizeof(status), "%s  #%u", data.status, tick);
        data.status = status;   /* 指向上面这个局部数组，本函数内一直有效 */

        /* ---- 加锁 -> 改界面 -> 解锁 ---- */
        if (lvgl_port_lock(0)) {
            ui_update(&data);
            lvgl_port_unlock();
        }

        tick++;

        /* 睡 2000 毫秒再进下一轮。
         * vTaskDelay 只让「当前这个任务」睡，CPU 会去跑别的任务，不浪费。 */
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    /* ---- 1. 初始化 ----
     * 这一句内部依次完成了：
     *     SPI 总线初始化 -> ST7789 屏幕初始化 -> 点亮背光
     *     -> 启动 LVGL 及其后台任务 -> 把这块屏幕注册给 LVGL
     * 返回的是屏幕句柄，可理解为「这块屏幕在 LVGL 里的身份证」。 */
    lv_display_t *disp = bsp_lvgl_init();
    if (disp == NULL) {
        /* 返回 NULL 表示初始化失败（比如内存不足）。
         * 直接结束，避免后面拿着空句柄去用导致崩溃重启。 */
        printf("LVGL 初始化失败\n");
        return;
    }

    /* ---- 2. 加锁 ----
     * 参数 0 是特殊约定，表示「一直等，直到拿到锁为止」（不是等 0 毫秒）。
     * 拿到锁后，一直到第 4 步 unlock 之前，别的任务都碰不了界面。 */
    if (!lvgl_port_lock(0)) {
        printf("获取 LVGL 锁失败\n");
        return;
    }

    /* ---- 3. 搭界面 ----
     * 界面长什么样都在 ui.c 里，这里只负责调用。
     * 以后想把界面做复杂，改 ui.c 就行，这个文件通常不用动。 */
    ui_create(disp);

    /* ---- 4. 解锁 ----
     * 界面搭完了，开锁让后台任务去画。
     * 这行绝对不能漏！漏了锁永远不释放，后台任务会一直卡在等锁上，
     * 屏幕就再也不会刷新了。 */
    lvgl_port_unlock();

    /* ---- 5. 启动演示任务（测试用，后面可以删） ----
     * xTaskCreate 各参数依次是：
     *   任务函数、任务名字（调试用）、栈大小（字节）、传给任务的参数、
     *   优先级（数字越大越优先）、任务句柄（传 NULL = 我不需要管它）
     *
     * 为什么栈给 4096 字节？任务里用了 snprintf 这类函数，
     * 栈给太小会在运行中突然崩掉，而且崩的位置看起来跟原因毫无关系，
     * 很难查。宁可多给一点。
     *
     * 注意这一句只是「登记了一个任务」，它立刻就返回了，
     * 不会在这里等任务跑完 —— 所以下面的 app_main 继续走到结尾。 */
    xTaskCreate(ui_demo_task, "ui_demo", 4096, NULL, 5, NULL);

    /* 到这里 app_main 就返回了。ESP-IDF 会删掉「main 任务」，
     * 但 LVGL 后台任务和上面这个演示任务都还在跑，
     * 所以屏幕会一直刷新，不会黑。 */
}
