/* ============================================================================
 *  小狗表情界面
 * ----------------------------------------------------------------------------
 *  对外只有两个函数（见 ui.h）：
 *    ui_create()  开机调一次，把两只眼睛和一张嘴建出来。
 *    ui_update()  之后反复调用，只改数字，不重建控件。
 *
 *  为什么不每次重建控件？
 *    重建（create / delete）要申请释放内存、要重算布局，代价高，
 *    而且画面会闪。改属性（set_size / set_style / align）代价很低，
 *    一秒改几十次也没问题。所以：结构建一次，内容随便改。
 * ========================================================================== */

#include "ui.h"

/* ---- 配色 ----
 * ⚠ lv_color_hex() 收的是 24 位 RGB888（0xRRGGBB），不是 RGB565！ */
#define COLOR_BG        0x000000    /* 背景：纯黑 */
#define COLOR_FACE      0xFFFFFF    /* 五官：白 */

/* ---- 五官在屏幕上的位置 ----
 * 写的是【相对屏幕中心】的偏移量，正数往下。
 * 用相对位置而不是绝对坐标，横屏竖屏切换时这两行不用改。 */
#define EYE_CENTER_Y    (-38)       /* 两眼中心线：屏幕中心往上 38 */
#define MOUTH_CENTER_Y  ( 42)       /* 嘴的视觉中心：屏幕中心往下 42 */

/* ---- 嘴的线条粗细 ----
 * 直线嘴拿它当高度，弧形嘴拿它当弧线宽度，这样两种嘴粗细一致。 */
#define MOUTH_THICK     8

/* ---------------------------------------------------------------------------
 * 要反复改的控件
 * ---------------------------------------------------------------------------
 * 这些指针必须存成 static 全局变量：create_xxx() 里的局部变量在函数返回时
 * 就没了，而控件本身是 LVGL 动态分配的、还活着，我们只是【丢了找它的线索】。
 * ------------------------------------------------------------------------- */
static lv_obj_t *s_eye[2];        /* 左眼、右眼 */
static lv_obj_t *s_mouth_line;    /* 直线嘴（mouth_dir == 0 时显示） */
static lv_obj_t *s_mouth_arc;     /* 弧形嘴（mouth_dir != 0 时显示） */

/* ---------------------------------------------------------------------------
 * 眼睛
 * ---------------------------------------------------------------------------
 * 用一个圆角矩形画。圆角半径设成 LV_RADIUS_CIRCLE（一个很大的值）之后，
 * LVGL 会自动把它限制到「短边的一半」，于是：
 *     宽 == 高  ->  正圆
 *     宽 >  高  ->  胶囊形（左右两个半圆 + 中间一段直线）
 * 所以一个矩形控件就够了，改数字就能在「圆眼」和「眯眼」之间连续变化，
 * 不用换控件类型，以后做动画也能平滑过渡。
 * ------------------------------------------------------------------------- */
static void create_eyes(lv_obj_t *parent)
{
    for (int i = 0; i < 2; i++) {
        s_eye[i] = lv_obj_create(parent);

        /* 圆角拉满 -> 正圆或胶囊 */
        lv_obj_set_style_radius(s_eye[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);

        /* 填充色 = 五官色；不要边框（容器默认带一条灰边框） */
        lv_obj_set_style_bg_color(s_eye[i], lv_color_hex(COLOR_FACE), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_eye[i], 0, LV_PART_MAIN);

        /* 去掉内边距。lv_obj 默认带 pad，不去掉的话实际可见的部分
         * 会比设定值小一圈，眯眼时甚至可能被 pad 挤没。 */
        lv_obj_set_style_pad_all(s_eye[i], 0, LV_PART_MAIN);
    }
}

/* ---------------------------------------------------------------------------
 * 嘴巴
 * ---------------------------------------------------------------------------
 * 准备了两个控件，同一时刻只显示其中一个：
 *     s_mouth_line  扁的圆角矩形  =  直线嘴 ─
 *     s_mouth_arc   lv_arc 的弧   =  弯的嘴 ⌣ / ⌢
 *
 * 为什么不干脆全用弧？
 *     0 度画不出直线，所以直线只能另外用矩形表示。两个控件切换一下很简单。
 *
 * ⚠ lv_arc 这个控件默认会画三样东西：
 *       背景弧（我们要的） + 值弧（进度条那种） + 旋钮（一个小圆点）
 *   后两样必须关掉，否则嘴上会凭空多出一个圆点。
 * ------------------------------------------------------------------------- */
static void create_mouth(lv_obj_t *parent)
{
    /* ---------------- 直线嘴 ---------------- */
    s_mouth_line = lv_obj_create(parent);

    lv_obj_set_style_bg_color(s_mouth_line, lv_color_hex(COLOR_FACE), LV_PART_MAIN);
    lv_obj_set_style_radius(s_mouth_line, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_mouth_line, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_mouth_line, 0, LV_PART_MAIN);

    /* ---------------- 弧形嘴 ---------------- */
    s_mouth_arc = lv_arc_create(parent);

    /* 控件自身的方块底色去掉，不然屏幕上会出现一个方块 */
    lv_obj_set_style_bg_opa(s_mouth_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_mouth_arc, 0, LV_PART_MAIN);

    /* 背景弧 —— 这才是我们要画的嘴 */
    lv_obj_set_style_arc_color(s_mouth_arc, lv_color_hex(COLOR_FACE), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_mouth_arc, MOUTH_THICK, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_mouth_arc, true, LV_PART_MAIN);

    /* 关掉旋钮（那个小圆点）和值弧。
     * 这两句漏了，嘴上就会多出一个圆点，很容易以为是画错了。 */
    lv_obj_remove_style(s_mouth_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_opa(s_mouth_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);

    /* 内边距会从弧的半径里被扣掉，不去掉的话弧会比设定的小一圈 */
    lv_obj_set_style_pad_all(s_mouth_arc, 0, LV_PART_MAIN);
}

/* ---------------------------------------------------------------------------
 * 搭骨架：只在开机时调一次
 * ------------------------------------------------------------------------- */
void ui_create(lv_display_t *disp)
{
    /* 屏幕根节点，所有控件都要挂在它下面才会显示出来 */
    lv_obj_t *scr = lv_display_get_screen_active(disp);

    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), LV_PART_MAIN);

    create_eyes(scr);
    create_mouth(scr);

    /* 注意这里【没有】设定眼睛和嘴的尺寸、位置。
     * 那些全在 ui_update() 里，由第一次调用决定。
     * 所以 ui_create() 之后必须马上调一次 ui_update()，否则屏幕是全黑的。 */
}

/* ---------------------------------------------------------------------------
 * 换表情
 * ---------------------------------------------------------------------------
 * ⚠ 前提：调用者已经持有 LVGL 锁。这个函数自己不加锁。
 *   约定：谁调用谁加锁。
 *
 * 这里只 set 属性，不 create / delete 任何控件 —— 这是关键。
 * ------------------------------------------------------------------------- */
void ui_update(const ui_data_t *f)
{
    /* ===================== 眼睛 ===================== */

    /* 1. 定形状 */
    for (int i = 0; i < 2; i++) {
        lv_obj_set_size(s_eye[i], f->eye_w, f->eye_h);
    }

    /* 2. 定位置。两眼关于屏幕中心左右对称，所以左眼偏移取负、右眼取正。
     *    偏移量 = 眼距的一半 + 眼宽的一半
     *    （眼距算的是【内侧空隙】，所以还要再让出半只眼的宽度） */
    const int32_t dx = f->eye_gap / 2 + f->eye_w / 2;

    /* LV_ALIGN_CENTER：先把控件中心对准父控件（这里是屏幕）的中心，
     * 再加上后面的偏移量。 */
    lv_obj_align(s_eye[0], LV_ALIGN_CENTER, -dx, EYE_CENTER_Y + f->eye_dy);
    lv_obj_align(s_eye[1], LV_ALIGN_CENTER,  dx, EYE_CENTER_Y + f->eye_dy);

    /* ===================== 嘴巴 ===================== */
    if (f->mouth_dir == 0) {
        /* ---------------- 直线嘴 ---------------- */
        lv_obj_remove_flag(s_mouth_line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag   (s_mouth_arc,  LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_size(s_mouth_line, f->mouth_w, MOUTH_THICK);
        lv_obj_align(s_mouth_line, LV_ALIGN_CENTER, 0, MOUTH_CENTER_Y);
    }
    else {
        /* ---------------- 弧形嘴 ---------------- */
        lv_obj_add_flag   (s_mouth_line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_mouth_arc,  LV_OBJ_FLAG_HIDDEN);

        /* 弧是画在控件的【内切圆】上的，所以控件必须做成正方形：
         *     边长 = 嘴宽  ->  半径 = 嘴宽/2  ->  弧深 = 嘴宽/2 */
        lv_obj_set_size(s_mouth_arc, f->mouth_w, f->mouth_w);

        /* 角度按【顺时针】算，0° 在 3 点钟方向：
         *     0° -> 90°(6点钟) -> 180°(9点钟)
         *        经过屏幕底部，所以是下半弧 ⌣，也就是笑
         *   180° -> 270°(12点钟) -> 360°(3点钟)
         *        经过屏幕顶部，所以是上半弧 ⌢，也就是撇嘴
         * （360° 内部会被换算成 0°，环形的角度处理是对的，不用管它） */
        if (f->mouth_dir > 0) {
            lv_arc_set_bg_angles(s_mouth_arc, 0, 180);      /* 笑 */
        }
        else {
            lv_arc_set_bg_angles(s_mouth_arc, 180, 360);    /* 撇嘴 */
        }

        /* 控件是正方形的，但只有一半看得见。所以要挪一下控件，
         * 让【看得见的那一半】落在 MOUTH_CENTER_Y 上，
         * 而不是让控件中心落在那里。
         * 挪的距离 = 半径/2 = 嘴宽/4 */
        const int32_t shift = f->mouth_w / 4;
        const int32_t off   = (f->mouth_dir > 0)
                            ? (MOUTH_CENTER_Y - shift)   /* 下半弧在中心之下 */
                            : (MOUTH_CENTER_Y + shift);  /* 上半弧在中心之上 */
        lv_obj_align(s_mouth_arc, LV_ALIGN_CENTER, 0, off);
    }
}
