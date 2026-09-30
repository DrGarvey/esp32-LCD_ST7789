/* ============================================================================
 *  界面构建
 * ----------------------------------------------------------------------------
 *  这个文件对外只有两个函数（见 ui.h）：
 *    ui_create()  开机时调用一次，把界面搭出来。之后再没人碰它。
 *    ui_update()  之后反复调用，只改「内容」，不动界面结构。
 *
 *  为什么要这么分？
 *    搭结构（创建控件、定位置）成本高，只在开机做一次；
 *    改内容（换一行字、换一个颜色）成本很低，可以每秒做几十次。
 *    把这两件事分开，界面就不会动不动重建，也不会闪。
 *
 *  组织方式：ui_create() 是总入口，它把界面拆成几块，每块交给一个小函数负责。
 *  这样界面变复杂时，你只是在下面多加一个 create_xxx() 函数，
 *  而不是往一个函数里继续塞代码。
 *
 *  LVGL 的控件是一棵树：屏幕是根节点，控件都是挂在它下面的子节点，
 *  控件还能有自己的子控件。子控件的位置相对父控件计算。
 * ========================================================================== */

#include "ui.h"
#include <stdio.h>

/* ---- 配色 ----
 * ⚠ lv_color_hex() 收的是 24 位 RGB888（0xRRGGBB），不是 RGB565！
 *   写成 RGB565 的 0xF800 会被当成「R=0x00, G=0xF8, B=0x00」而显示成绿色。
 *   要按 RGB565 写的话请用 lv_color_make(r8, g8, b8)。 */
#define COLOR_BG        0x101820    /* 背景：深蓝灰 */
#define COLOR_TITLE     0xFFFFFF    /* 标题：白 */
#define COLOR_STATUS    0x93C5FD    /* 状态文字：浅蓝 */
#define COLOR_COUNTER   0x4ADE80    /* 计数：亮绿 */

/* ---- 色块尺寸 ----
 * 三个色块等间距排列，这里定义单个尺寸和间隔，
 * 下面的位置是算出来的，所以改这两个值不用手改坐标。 */
#define BOX_SIZE        70
#define BOX_GAP         8

/* 三色块，用来一眼验证颜色顺序是否正确 */
static const uint32_t k_box_colors[3] = { 0xFF0000, 0x00FF00, 0x0000FF };
static const char    *k_box_names[3]  = { "RED", "GREEN", "BLUE" };

/* ---------------------------------------------------------------------------
 * ui_update() 要改的控件
 * ---------------------------------------------------------------------------
 * ui_create() 创建控件时把指针存进这些变量，ui_update() 才能顺着它们找到控件。
 *
 * 为什么必须存成 static 全局变量？
 *   create_xxx() 里的局部变量（比如 lv_obj_t *title）在函数返回时就没了，
 *   而控件本身是 LVGL 动态分配的、还活着，我们只是【丢了找它的线索】。
 *   存到 static 变量里，线索就一直在。
 * ------------------------------------------------------------------------- */
static lv_obj_t *s_title_label;     /* 顶部标题 */
static lv_obj_t *s_status_label;    /* 底部状态文字 */
static lv_obj_t *s_box[3];          /* 三个色块（容器） */
static lv_obj_t *s_box_label[3];    /* 三个色块里的文字 */

/* ---------------------------------------------------------------------------
 * 定时器回调要用的状态
 * 必须放在函数外面：局部变量在函数返回后就销毁了，回调函数就找不到了。
 * 命名上的 s_ 前缀是习惯，表示 "static 变量"。
 * ------------------------------------------------------------------------- */
static lv_obj_t *s_counter_label;   /* 指向底部那行计数文字 */
static unsigned  s_seconds;         /* 已运行秒数 */

/* ---------------------------------------------------------------------------
 * 定时器回调
 * ---------------------------------------------------------------------------
 * 这个函数不是我们主动调的，是 LVGL 每秒钟替我们调一次。
 *
 * 它运行在 LVGL 的后台任务里，那时锁已经被 LVGL 自己持有，
 * 所以函数内部不需要（也不能）再加锁 —— 递归锁会死等。
 * ------------------------------------------------------------------------- */
static void counter_timer_cb(lv_timer_t *timer)
{
    s_seconds++;                                    /* 秒数 +1 */

    /* 和 printf 用法一样，%u 处填一个无符号整数 */
    lv_label_set_text_fmt(s_counter_label, "Running: %u s", s_seconds);
}

/* ---------------------------------------------------------------------------
 * 第 1 块：标题
 * ---------------------------------------------------------------------------
 * 参数 parent 是父节点，标题会挂在它下面。
 * 把 parent 作为参数传进来，这个函数才能复用到别的地方。
 * ------------------------------------------------------------------------- */
static void create_title(lv_obj_t *parent)
{
    /* lv_label_create 创建一个「文字控件」，父节点是 parent。
     * 这次直接把返回值存进 s_title_label —— 以后 ui_update() 靠它找这个控件。 */
    s_title_label = lv_label_create(parent);

    /* 先填一个初始文字。等 main.c 里第一次调 ui_update() 时会被覆盖掉。 */
    lv_label_set_text(s_title_label, "LVGL + esp_lvgl_port");

    /* 设置文字颜色。最后的 LV_PART_MAIN 指定「作用于控件的哪个部分」：
     * 一个控件可以分主体、滚动条、指示器等多个部分，MAIN 就是主体。 */
    lv_obj_set_style_text_color(s_title_label, lv_color_white(), LV_PART_MAIN);

    /* 定位：LV_ALIGN_TOP_MID = 相对父节点顶部居中，
     * 后面两个数是这个基础上的偏移量 (x, y)，
     * y 轴向下为正，所以 10 表示再往下 10 像素。 */
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 10);
}

/* ---------------------------------------------------------------------------
 * 第 2 块：三个色块（每个色块里还有一行标签）
 * ---------------------------------------------------------------------------
 * 这段最能体现 LVGL 的「父子嵌套」：
 *   屏幕
 *    └─ 色块（容器）
 *        └─ 标签（文字）
 * 标签的父节点是色块，所以它会贴在色块里面、随色块移动。
 * ------------------------------------------------------------------------- */
static void create_color_boxes(lv_obj_t *parent, int avail_width)
{
    /* 三个块排一行，总宽 = 3个块 + 2个间隔。
     * 用屏幕实际宽度算起始 x，这样横屏/竖屏切换时不用改这里。 */
    const int count    = 3;
    const int total_w  = count * BOX_SIZE + (count - 1) * BOX_GAP;
    const int start_x  = (avail_width - total_w) / 2;

    for (int i = 0; i < count; i++) {
        /* lv_obj_create 创建一个通用空白容器，父节点是 parent。
         * 存进 s_box[i]，ui_update() 以后靠它改颜色。 */
        s_box[i] = lv_obj_create(parent);

        /* 尺寸：宽 BOX_SIZE、高 BOX_SIZE（正方形） */
        lv_obj_set_size(s_box[i], BOX_SIZE, BOX_SIZE);

        /* 位置：x 从 start_x 开始，每个往右挪 (块宽 + 间隔)；
         * y 固定在 50，让三个块在同一水平线上 */
        lv_obj_set_pos(s_box[i], start_x + i * (BOX_SIZE + BOX_GAP), 50);

        /* 背景色。i 依次为 0/1/2，取到红/绿/蓝 */
        lv_obj_set_style_bg_color(s_box[i], lv_color_hex(k_box_colors[i]), LV_PART_MAIN);

        /* 去掉边框。容器默认带一条灰边框，不去掉色块边缘会有一圈灰 */
        lv_obj_set_style_border_width(s_box[i], 0, LV_PART_MAIN);

        /* 注意这里父节点是 s_box[i] 不是 parent —— 标签贴在色块「里面」 */
        s_box_label[i] = lv_label_create(s_box[i]);
        lv_label_set_text(s_box_label[i], k_box_names[i]);

        /* 居中于父节点（也就是色块）。比手算偏移量省事得多 */
        lv_obj_center(s_box_label[i]);
    }
}

/* ---------------------------------------------------------------------------
 * 第 3 块：状态文字（由 ui_update() 填内容）
 * ------------------------------------------------------------------------- */
static void create_status(lv_obj_t *parent)
{
    s_status_label = lv_label_create(parent);

    /* 和色块一样，先给个初始值，第一次 ui_update() 会覆盖它 */
    lv_label_set_text(s_status_label, "-");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_STATUS), LV_PART_MAIN);

    /* 底部靠上的位置。往上 -50 是为了给下面那行计数让出地方，
     * 这样两个标签不会叠在一起。 */
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -50);
}

/* ---------------------------------------------------------------------------
 * 第 4 块：底部的计数文字
 * ------------------------------------------------------------------------- */
static void create_counter(lv_obj_t *parent)
{
    s_counter_label = lv_label_create(parent);

    lv_label_set_text(s_counter_label, "Running: 0 s");
    lv_obj_set_style_text_color(s_counter_label, lv_color_hex(COLOR_COUNTER), LV_PART_MAIN);

    /* 底部居中。偏移 -20 表示往上 20 像素（y 轴向下为正，所以负数是向上） */
    lv_obj_align(s_counter_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* 注册定时器：每 1000 毫秒调用一次 counter_timer_cb。
     * 这行立即返回，不会在这里等；到点由 LVGL 后台任务负责调用。
     * 第三个参数 NULL 表示不给回调传附加数据。 */
    lv_timer_create(counter_timer_cb, 1000, NULL);
}

/* ---------------------------------------------------------------------------
 * 总入口：把上面的几块拼起来
 * ------------------------------------------------------------------------- */
void ui_create(lv_display_t *disp)
{
    /* 取出这块屏幕的根节点。
     * 所有控件都要挂在它下面才会被显示出来。 */
    lv_obj_t *scr = lv_display_get_screen_active(disp);

    /* 屏幕的实际宽度（横屏是 320，竖屏是 240）。
     * 从 display 的 API 取比从控件对象上取更可靠。 */
    const int scr_w = lv_display_get_horizontal_resolution(disp);

    /* 设置屏幕背景色 */
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), LV_PART_MAIN);

    /* 依次创建各块内容。
     * 想把界面做复杂，就是在这里多加几行调用而已。 */
    create_title(scr);
    create_status(scr);
    create_color_boxes(scr, scr_w);
    create_counter(scr);
}

/* ---------------------------------------------------------------------------
 * 刷新界面内容
 * ---------------------------------------------------------------------------
 * 每次调用都做三件事：
 *   1. 把 data->title 写进标题
 *   2. 把 data->status 写进状态栏
 *   3. 把三个色块的颜色和文字换掉
 *
 * ⚠ 前提：调用者已经持有 LVGL 锁。这个函数内部【不会】自己加锁，
 *   因为锁是可以嵌套的（递归锁），在已经持锁的情况下再加一次也不会死锁，
 *   但那样容易让人搞不清到底该由谁负责加锁。约定：谁调用谁加锁。
 *
 * ⚠ 这里只 set 属性，没有 create/delete 任何控件 —— 这是关键。
 *   lv_obj_delete() 会真把对象销毁掉，如果删的正是 LVGL 正在绘制的东西，
 *   后果很难预料。日常刷新界面完全不需要删控件，改内容就够了。
 * ------------------------------------------------------------------------- */
void ui_update(const ui_data_t *data)
{
    /* 标题和状态栏：换一行字而已 */
    lv_label_set_text(s_title_label, data->title);
    lv_label_set_text(s_status_label, data->status);

    /* 三个色块：颜色和文字一起换。
     * 注意色块里的文字必须跟着颜色一起改，否则会出现
     * 「绿色的块上写着 RED」这种对不上的情况。 */
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(s_box[i], lv_color_hex(data->box_colors[i]), LV_PART_MAIN);
        lv_label_set_text(s_box_label[i], data->box_names[i]);
    }
}
