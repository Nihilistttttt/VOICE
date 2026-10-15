/*============================================================================
 * 手势状态机宿主单测（Common/gesture_sm.c 纯逻辑）
 *
 * 恢复自丢失的 hosttest（Sense/gesture.c 73 断言在目录重构中遗失）。
 * 覆盖: s1 吹/吸迟滞与去抖、中键两相、音量模式、双口滚动进出与限速、
 *       s2 短吹/短吸/长按、键盘两相、回主页全序列与断连中止（F3）。
 * 跑法: bash hosttest/run.sh
 *============================================================================*/
#include <stdio.h>
#include "gesture_sm.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL @line %d: %s\n", __LINE__, #cond); } \
} while (0)

/*--------------------------------------------------------------------------*/

static void test_s1_blow_click_hold(void)
{
    s1_sm_t s1; s1_out_t o;
    s1_sm_init(&s1);
    CHECK(s1.state == S1_IDLE && s1.btn == 0);

    /* 去抖: 第 1 tick 不按下, 第 2 tick 按下 */
    s1_sm_step(&s1, 300, 0, &o);  CHECK(o.btn == 0x00);
    s1_sm_step(&s1, 300, 0, &o);  CHECK(o.btn == 0x01);

    /* 吹住 = 左键按住（拖拽）*/
    s1_sm_step(&s1, 300, 0, &o);  CHECK(o.btn == 0x01);
    s1_sm_step(&s1, 300, 0, &o);  CHECK(o.btn == 0x01);

    /* 回落到迟滞下沿 → 释放 */
    s1_sm_step(&s1, 100, 0, &o);  CHECK(o.btn == 0x00);
    s1_sm_step(&s1, -100, 0, &o); CHECK(o.btn == 0x00);   /* 不吸: 未到 -250 */
}

static void test_s1_blow_debounce_reset(void)
{
    s1_sm_t s1; s1_out_t o;
    s1_sm_init(&s1);
    s1_sm_step(&s1, 300, 0, &o);            /* n=1 */
    s1_sm_step(&s1, 0, 0, &o);              /* 空闲 → n 清零 */
    s1_sm_step(&s1, 300, 0, &o);  CHECK(o.btn == 0x00);   /* 重新计数 */
    s1_sm_step(&s1, 300, 0, &o);  CHECK(o.btn == 0x01);   /* 第 2 个连续 tick 按下 */
}

static void test_s1_cross_port_guard(void)
{
    s1_sm_t s1; s1_out_t o;
    s1_sm_init(&s1);
    /* 另一口忙(|p2|>=100) → 不进入, 且去抖计数清零 */
    s1_sm_step(&s1, 300, 200, &o);  CHECK(o.btn == 0x00);
    s1_sm_step(&s1, 300, 200, &o);  CHECK(o.btn == 0x00);
    s1_sm_step(&s1, 300, -200, &o); CHECK(o.btn == 0x00);
    /* 窗口边界: p2=+100 恰好在空闲窗口之外 */
    s1_sm_step(&s1, 300, 100, &o);  CHECK(o.btn == 0x00);
}

static void test_s1_sip_short_middle_click(void)
{
    s1_sm_t s1; s1_out_t o;
    s1_sm_init(&s1);
    s1_sm_step(&s1, -300, 0, &o);  CHECK(o.btn == 0x00);   /* 去抖 1 */
    s1_sm_step(&s1, -300, 0, &o);  CHECK(o.btn == 0x00);   /* 进入 TIMING */
    CHECK(s1.state == S1_SIP_TIMING);

    /* 快速松口 → 短吸 = 中键两相点击 */
    s1_sm_step(&s1, 0, 0, &o);     CHECK(o.btn == 0x04);   /* 按下相 */
    s1_sm_step(&s1, 0, 0, &o);     CHECK(o.btn == 0x00);   /* 释放相 */
    CHECK(s1.state == S1_IDLE);
}

static void test_s1_sip_long_volume(void)
{
    s1_sm_t s1; s1_out_t o;
    int i;
    s1_sm_init(&s1);
    s1_sm_step(&s1, -300, 0, &o);
    s1_sm_step(&s1, -300, 0, &o);          /* 进入 TIMING, ms=0 */

    for (i = 0; i < 9; i++) s1_sm_step(&s1, -300, 0, &o);   /* ms=450 */
    CHECK(s1.state == S1_SIP_TIMING);
    CHECK(o.btn == 0x00);                  /* 按住期间无中键 */

    s1_sm_step(&s1, -300, 0, &o);          /* ms=500 → 音量模式 */
    CHECK(s1.state == S1_SIP_VOLUME);
    CHECK(o.volume_active == 1);
    CHECK(o.btn == 0x00);

    /* 音量模式松口 → 退出, 且不发中键点击（长吸≠返回）*/
    s1_sm_step(&s1, 0, 0, &o);
    CHECK(s1.state == S1_IDLE);
    CHECK(o.volume_active == 0);
    CHECK(o.btn == 0x00);
}

static void test_s1_sip_threshold_hysteresis(void)
{
    s1_sm_t s1; s1_out_t o;
    s1_sm_init(&s1);
    /* 阈值之下不进入 */
    s1_sm_step(&s1, -249, 0, &o);
    s1_sm_step(&s1, -249, 0, &o);
    CHECK(s1.state == S1_IDLE);
    /* TIMING 中回落到 -151 仍算按住（迟滞下沿 -150）*/
    s1_sm_step(&s1, -300, 0, &o);
    s1_sm_step(&s1, -300, 0, &o);
    s1_sm_step(&s1, -151, 0, &o);
    CHECK(s1.state == S1_SIP_TIMING);
    s1_sm_step(&s1, -150, 0, &o);          /* >= -150 → 松口（短吸）*/
    CHECK(s1.state == S1_IDLE);
    CHECK(o.btn == 0x04);                  /* 中键按下相 */
    s1_sm_step(&s1, 0, 0, &o);
    CHECK(o.btn == 0x00);
}

/*--------------------------------------------------------------------------*/

static void test_scroll(void)
{
    scroll_sm_t sc;
    scroll_sm_init(&sc);

    /* 双吹 = 上滚: 进入拍不输出, 第 2 拍输出 -1 */
    CHECK(scroll_sm_step(&sc, 200, 200) == 0);
    CHECK(sc.active == 1 && sc.dir == -1);
    CHECK(scroll_sm_step(&sc, 200, 200) == -1);
    CHECK(scroll_sm_step(&sc, 200, 200) == 0);    /* 限速: 隔拍输出 */
    CHECK(scroll_sm_step(&sc, 200, 200) == -1);

    /* 双口回落 → 退出 */
    CHECK(scroll_sm_step(&sc, 50, 50) == 0);
    CHECK(sc.active == 0 && sc.dir == 0);

    /* 双吸 = 下滚 */
    CHECK(scroll_sm_step(&sc, -200, -200) == 0);
    CHECK(sc.dir == 1);
    CHECK(scroll_sm_step(&sc, -200, -200) == 1);

    /* 退出阈值: |p|<100 才退, 110 不退 */
    CHECK(scroll_sm_step(&sc, -110, -110) == 0);
    CHECK(sc.active == 1);
    CHECK(scroll_sm_step(&sc, -99, -99) == 0);
    CHECK(sc.active == 0);

    /* 非同向不进入 */
    scroll_sm_init(&sc);
    scroll_sm_step(&sc, 200, -200);
    CHECK(sc.active == 0);
    scroll_sm_step(&sc, -200, 200);
    CHECK(sc.active == 0);
}

/*--------------------------------------------------------------------------*/

static void test_s2(void)
{
    s2_sm_t s2;
    s2_sm_init(&s2);

    /* 短吹 → Alt+Tab */
    s2_sm_step(&s2, 300);
    CHECK(s2_sm_step(&s2, 300) == S2_NONE);
    CHECK(s2.mode == 1);
    CHECK(s2_sm_step(&s2, 0) == S2_ALTTAB);
    CHECK(s2.mode == 0);

    /* 短吸 → 回主页 */
    s2_sm_step(&s2, -300);
    s2_sm_step(&s2, -300);
    CHECK(s2.mode == 2);
    CHECK(s2_sm_step(&s2, 0) == S2_HOME);
    CHECK(s2.mode == 0);

    /* 长按 ≥500ms → 松口无动作 */
    s2_sm_init(&s2);
    {
        int i;
        s2_sm_step(&s2, 300);
        s2_sm_step(&s2, 300);                       /* mode=1, ms=0 */
        for (i = 0; i < 11; i++) s2_sm_step(&s2, 300);   /* ms=550 */
        CHECK(s2.mode == 1);
        CHECK(s2_sm_step(&s2, 0) == S2_NONE);
        CHECK(s2.mode == 0);
    }
}

/*--------------------------------------------------------------------------*/

static void test_kb(void)
{
    kb_sm_t kb; kb_out_t ko;
    kb_sm_init(&kb);

    kb_sm_step(&kb, &ko);
    CHECK(ko.evt == 0);

    kb_sm_press(&kb, 0x04, 0x2B);
    kb_sm_step(&kb, &ko);
    CHECK(ko.evt == 1 && ko.mod == 0x04 && ko.key == 0x2B);
    kb_sm_step(&kb, &ko);
    CHECK(ko.evt == 2);
    kb_sm_step(&kb, &ko);
    CHECK(ko.evt == 0);
}

/*--------------------------------------------------------------------------*/

static void test_home_full_sequence(void)
{
    home_sm_t hm; home_out_t ho;
    int i;
    home_sm_init(&hm);
    CHECK(hm.phase == HOME_IDLE);
    CHECK(home_sm_pacing_ms(HOME_IDLE) == 50);

    home_sm_start(&hm);
    CHECK(hm.phase == HOME_FLOOD);
    CHECK(home_sm_pacing_ms(HOME_FLOOD) == 0);   /* 冲角段快发 */

    /* FLOOD ×12: 每拍 X=-128 Y=+127 */
    for (i = 0; i < 11; i++) {
        home_sm_step(&hm, &ho);
        CHECK(ho.move == 1 && ho.dx == -128 && ho.dy == 127);
        CHECK(ho.left_btn == -1);
        CHECK(hm.phase == HOME_FLOOD);
    }
    home_sm_step(&hm, &ho);                       /* 第 12 拍: 转 X */
    CHECK(ho.dx == -128 && ho.dy == 127);
    CHECK(hm.phase == HOME_X);
    CHECK(home_sm_pacing_ms(HOME_X) == 25);

    /* X ×5: 每拍 +108 */
    for (i = 0; i < 4; i++) {
        home_sm_step(&hm, &ho);
        CHECK(ho.move == 1 && ho.dx == (int8_t)(g_params.home_x_center / 5) && ho.dy == 0);
        CHECK(hm.phase == HOME_X);
    }
    home_sm_step(&hm, &ho);
    CHECK(hm.phase == HOME_Y);

    /* Y ×1: -24 */
    home_sm_step(&hm, &ho);
    CHECK(ho.move == 1 && ho.dx == 0 && ho.dy == -g_params.home_y_offset);
    CHECK(hm.phase == HOME_DOWN);
    CHECK(home_sm_pacing_ms(HOME_DOWN) == 25);

    /* DOWN: 左键置位 */
    home_sm_step(&hm, &ho);
    CHECK(ho.move == 0 && ho.left_btn == 1);
    CHECK(hm.phase == HOME_HOLD);
    CHECK(home_sm_pacing_ms(HOME_HOLD) == 30);

    /* HOLD: 左键释放 → 完成 */
    home_sm_step(&hm, &ho);
    CHECK(ho.left_btn == 0);
    CHECK(hm.phase == HOME_IDLE);

    /* 空闲态: 无输出 */
    home_sm_step(&hm, &ho);
    CHECK(ho.move == 0 && ho.left_btn == -1);
}

static void test_home_abort_and_restart(void)
{
    home_sm_t hm; home_out_t ho;
    home_sm_init(&hm);

    /* F3: 序列中途断连 → 作废 */
    home_sm_start(&hm);
    home_sm_step(&hm, &ho);
    home_sm_step(&hm, &ho);
    CHECK(hm.phase != HOME_IDLE);
    home_sm_abort(&hm);
    CHECK(hm.phase == HOME_IDLE);
    home_sm_step(&hm, &ho);
    CHECK(ho.move == 0 && ho.left_btn == -1);   /* 不再输出任何帧 */

    /* 中止后可重新启动 */
    home_sm_start(&hm);
    CHECK(hm.phase == HOME_FLOOD);
    home_sm_step(&hm, &ho);
    CHECK(ho.move == 1);
}

/*--------------------------------------------------------------------------*/

int main(void)
{
    test_s1_blow_click_hold();
    test_s1_blow_debounce_reset();
    test_s1_cross_port_guard();
    test_s1_sip_short_middle_click();
    test_s1_sip_long_volume();
    test_s1_sip_threshold_hysteresis();
    test_scroll();
    test_s2();
    test_kb();
    test_home_full_sequence();
    test_home_abort_and_restart();

    printf("\n==== gesture_sm host tests: %d passed, %d failed ====\n",
           g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
