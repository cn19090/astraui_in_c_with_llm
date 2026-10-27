//
// astra demo: 随面板自动适配的示例应用。
//
// 只依赖 astra 的 HAL，不包含任何平台头文件，所以同一份代码可以跑在
// raylib、SDL、SSD1306 或测试桩上。
//
// 布局完全由 astra_panel.h 决定（宏 ASTRA_PANEL）：
//   - 面板够高时用磁贴页作根菜单，否则退化为列表页；
//   - 磁贴图标由 30x30 主图按 ASTRA_PANEL_ICON_SCALE 整数放大，
//     保证在大屏上不会变成一个小点。
//
// astra 是 astra UI（C++）的 C99 移植版本，上游：
//   https://github.com/AstraThreshold/oled-ui-astra
// 本示例的菜单结构与视觉设计沿用上游的示例工程。
//
#include "astra_demo.h"

#include <string.h>

#include "astra_config.h"
#include "astra_panel.h"
#include "hal.h"
#include "item.h"
#include "launcher.h"
#include "logo.h"

/* 长按阈值：按住超过 500 ms 视为长按。 */
#define LONG_PRESS_MS 500UL

/* 开机动画速度：88 让 Logo 比框架默认的 70 更利落。 */
#define LOGO_SPEED 88.0f

/* 开机 Logo 的推进帧数：60 FPS 下约 1.8 秒。 */
#define LOGO_FRAMES 110

/* 磁贴主图的尺寸（所有分辨率的素材都用这一套）。
   注意 stride 必须是 (宽 + 7) / 8：draw_bmp 就是这么算行宽的，30 px 宽占
   4 字节而不是 3 字节，按 w*h/8 算会少分配 8 字节并读到数组外面。 */
#define ICON_SRC_W 30
#define ICON_SRC_H 30
#define ICON_SRC_STRIDE ((ICON_SRC_W + 7) / 8)
#define ICON_SRC_BYTES (ICON_SRC_STRIDE * ICON_SRC_H)

/* ------------------------------------------------------------------------ */
/* 磁贴图标：30x30 像素，每行 4 字节，MSB 是最左边的像素，1 表示点亮。
   由你自己的位图导出成 1bpp/XBM 后贴进来即可。 */
static const uint8_t PIC_SYSTEM[120] = {
    0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0,
    0x00, 0x00, 0x0E, 0xC0, 0x00, 0x00, 0x3C, 0xC0, 0x00, 0x00, 0x78, 0xC0,
    0x00, 0x00, 0xF8, 0xC0, 0x00, 0x00, 0xF0, 0xC1, 0x00, 0x00, 0xF0, 0xC1,
    0x00, 0x00, 0xF0, 0xC3, 0x00, 0x00, 0xF0, 0xC3, 0x00, 0x00, 0xF0, 0xC7,
    0x00, 0x00, 0xF0, 0xC7, 0x00, 0x00, 0xF0, 0xC7, 0x00, 0x00, 0xF8, 0xC7,
    0x00, 0x00, 0xF8, 0xC7, 0x00, 0x00, 0xFC, 0xC7, 0x08, 0x00, 0xFE, 0xC7,
    0x18, 0x00, 0xFF, 0xC3, 0x78, 0xC0, 0xFF, 0xC3, 0xF0, 0xFF, 0xFF, 0xC1,
    0xF0, 0xFF, 0xFF, 0xC1, 0xE0, 0xFF, 0xFF, 0xC0, 0xC0, 0xFF, 0x7F, 0xC0,
    0x80, 0xFF, 0x3F, 0xC0, 0x00, 0xFE, 0x0F, 0xC0, 0x00, 0xF8, 0x03, 0xC0,
    0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0};

static const uint8_t PIC_DISPLAY[120] = {
    0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0,
    0x00, 0x06, 0x18, 0xC0, 0x00, 0x06, 0x18, 0xC0, 0x00, 0x06, 0x18, 0xC0,
    0x00, 0x0F, 0x18, 0xC0, 0x80, 0x1F, 0x18, 0xC0, 0x80, 0x1F, 0x3C, 0xC0,
    0x80, 0x1F, 0x3C, 0xC0, 0x80, 0x1F, 0x3C, 0xC0, 0x80, 0x1F, 0x18, 0xC0,
    0x00, 0x0F, 0x18, 0xC0, 0x00, 0x06, 0x18, 0xC0, 0x00, 0x06, 0x18, 0xC0,
    0x00, 0x06, 0x18, 0xC0, 0x00, 0x06, 0x18, 0xC0, 0x00, 0x06, 0x3C, 0xC0,
    0x00, 0x06, 0x7E, 0xC0, 0x00, 0x0F, 0x7E, 0xC0, 0x00, 0x0F, 0x7E, 0xC0,
    0x00, 0x0F, 0x7E, 0xC0, 0x00, 0x06, 0x7E, 0xC0, 0x00, 0x06, 0x3C, 0xC0,
    0x00, 0x06, 0x18, 0xC0, 0x00, 0x06, 0x18, 0xC0, 0x00, 0x06, 0x18, 0xC0,
    0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0};

static const uint8_t PIC_NETWORK[120] = {
    0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0, 0x10, 0x00, 0x00, 0xC0,
    0x38, 0x00, 0x00, 0xC0, 0x38, 0x0C, 0x00, 0xC0, 0x7C, 0x3F, 0x00, 0xC0,
    0x10, 0x33, 0x00, 0xC0, 0x90, 0x61, 0x00, 0xC0, 0x90, 0x61, 0x00, 0xC0,
    0xD0, 0xC0, 0x00, 0xC0, 0xD0, 0xC0, 0x00, 0xC0, 0x70, 0x80, 0x01, 0xC0,
    0x70, 0x80, 0x01, 0xC6, 0x70, 0x80, 0x01, 0xC6, 0x30, 0x00, 0x03, 0xC3,
    0x30, 0x00, 0x03, 0xC3, 0x10, 0x00, 0x03, 0xC3, 0x10, 0x00, 0x86, 0xC1,
    0x10, 0x00, 0x86, 0xC1, 0x10, 0x00, 0xCC, 0xC0, 0x10, 0x00, 0xCC, 0xC0,
    0x10, 0x00, 0x78, 0xC0, 0x10, 0x00, 0x30, 0xC0, 0x10, 0x00, 0x80, 0xC1,
    0x10, 0x00, 0x80, 0xC7, 0xF0, 0xFF, 0xFF, 0xCF, 0x00, 0x00, 0x80, 0xC7,
    0x00, 0x00, 0x80, 0xC1, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0};

static const uint8_t PIC_ABOUT[120] = {
    0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0,
    0x00, 0x00, 0x00, 0xC0, 0x00, 0xE0, 0x01, 0xC0, 0x00, 0xE0, 0x01, 0xC0,
    0x00, 0xF3, 0x33, 0xC0, 0x80, 0xFF, 0x7F, 0xC0, 0xC0, 0xFF, 0xFF, 0xC0,
    0xC0, 0x1F, 0xFE, 0xC0, 0x80, 0x07, 0x78, 0xC0, 0x80, 0x03, 0x70, 0xC0,
    0xC0, 0x03, 0xF0, 0xC0, 0xF0, 0xC1, 0xE0, 0xC3, 0xF0, 0xE1, 0xE1, 0xC3,
    0xF0, 0xE1, 0xE1, 0xC3, 0xF0, 0xC1, 0xE0, 0xC3, 0xC0, 0x03, 0xF0, 0xC0,
    0x80, 0x03, 0x70, 0xC0, 0x80, 0x07, 0x78, 0xC0, 0xC0, 0x1F, 0xFE, 0xC0,
    0xC0, 0xFF, 0xFF, 0xC0, 0x80, 0xFF, 0x7F, 0xC0, 0x00, 0xF3, 0x33, 0xC0,
    0x00, 0xE0, 0x01, 0xC0, 0x00, 0xE0, 0x01, 0xC0, 0x00, 0x00, 0x00, 0xC0,
    0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0};

/* 素材行宽由 draw_bmp 决定（(w + 7) / 8），图标数组必须与之一致：少一个字节
   就会读到数组外面，画面在最后一列出现随机像素。 */
typedef char icon_src_size_check
    [(sizeof(PIC_SYSTEM) == ICON_SRC_BYTES) ? 1 : -1];

/* ------------------------------------------------------------------------ */
/* 磁贴图标：按面板放大                                                       */
/* ------------------------------------------------------------------------ */

/* 目标尺寸 = 主图 × 整数倍。框架把 tile_pic_width/height 同时当作源位图尺寸
   和目标尺寸（draw_bmp 用它算行宽），所以放大的结果必须真的是一张这么大的
   位图。这里启动时把 30x30 主图按整像素块展开一次，之后每帧直接整块贴。 */
#define ICON_DST_W (ICON_SRC_W * ASTRA_PANEL_ICON_SCALE)
#define ICON_DST_H (ICON_SRC_H * ASTRA_PANEL_ICON_SCALE)
#define ICON_DST_STRIDE ((ICON_DST_W + 7) / 8)
#define ICON_DST_BYTES (ICON_DST_STRIDE * ICON_DST_H)

#if ASTRA_PANEL_HAS_TILES
static uint8_t s_icon_system[ICON_DST_BYTES];
static uint8_t s_icon_display[ICON_DST_BYTES];
static uint8_t s_icon_network[ICON_DST_BYTES];
static uint8_t s_icon_about[ICON_DST_BYTES];

static void icon_scale_up(const uint8_t *src, uint8_t *dst)
{
#if ASTRA_PANEL_ICON_SCALE == 1
  memcpy(dst, src, ICON_SRC_BYTES);
#else
  const int dst_stride = ICON_DST_STRIDE;
  memset(dst, 0, ICON_DST_BYTES);

  for (int sy = 0; sy < ICON_SRC_H; ++sy)
  {
    const uint8_t *row = src + sy * ICON_SRC_STRIDE;
    for (int sx = 0; sx < ICON_SRC_W; ++sx)
    {
      if (((row[sx / 8] >> (7 - (sx % 8))) & 1u) == 0) continue;

      /* one source pixel -> a solid SCALE x SCALE block */
      for (int dy = 0; dy < ASTRA_PANEL_ICON_SCALE; ++dy)
      {
        uint8_t *out = dst + (sy * ASTRA_PANEL_ICON_SCALE + dy) * dst_stride;
        for (int dx = 0; dx < ASTRA_PANEL_ICON_SCALE; ++dx)
        {
          const int x = sx * ASTRA_PANEL_ICON_SCALE + dx;
          out[x / 8] |= (uint8_t)(0x80u >> (x % 8));
        }
      }
    }
  }
#endif
}
#endif /* ASTRA_PANEL_HAS_TILES */

/* ------------------------------------------------------------------------ */
/* 应用状态                                                                  */
/* ------------------------------------------------------------------------ */

static astra_launcher s_launcher; /* 页面导航：当前页、选择器、相机 */
static astra_selector s_selector;
static astra_camera s_camera;
static astra_menu *s_root; /* 根页面 */

static bool s_key_down[ASTRA_DEMO_KEY_NUM];             /* 上一帧的电平，用于边沿检测 */
static unsigned long s_key_down_at[ASTRA_DEMO_KEY_NUM]; /* 按下的时刻，用于长按判定 */
static bool s_key_level[ASTRA_DEMO_KEY_NUM];            /* 外壳注入的当前电平 */

/* ------------------------------------------------------------------------ */
/* 导航                                                                      */
/* ------------------------------------------------------------------------ */

/* 上下移动高亮项：direction 取 -1（上）或 +1（下），越界回绕。 */
static void move_selection(int direction)
{
  /* 步骤 1：取当前页面。init() 之后它一定存在。 */
  astra_menu *page = astra_launcher_current_page(&s_launcher);

  /* 步骤 2：本示例每个页面固定 3~4 项，所以 count 恒大于 0。 */
  const int count = astra_menu_item_count(page);

  /* 步骤 3：算新下标；C 的 % 对负数返回负值，这里补回一圈完成回绕。 */
  int index = (page->select_index + direction) % count;
  if (index < 0)
    index += count;

  /* 步骤 4：把选择器动画推向新位置（不是瞬移）。 */
  astra_selector_go(&s_selector, (uint8_t)index);
}

/* 进入高亮项的下级页面。叶子项没有下级页面，调用无副作用。 */
void astra_demo_activate(void)
{
  /* 步骤 1：取当前页面与其下级页面。 */
  astra_menu *page = astra_launcher_current_page(&s_launcher);
  astra_menu *next = astra_menu_next(page);

  /* 步骤 2：叶子项（如 "Volume"）没有下级页面，直接返回。 */
  if (next == NULL)
    return;

  /* 步骤 3：播放下级页面的入场动画并切换过去。 */
  astra_launcher_open(&s_launcher);
}

/* 返回上一级页面。已经在根页面时无处可退。 */
void astra_demo_back(void)
{
  /* 步骤 1：取当前页面。 */
  astra_menu *page = astra_launcher_current_page(&s_launcher);

  /* 步骤 2：根页面没有上级，直接返回。 */
  if (page == s_root)
    return;

  /* 步骤 3：播放退出动画并回到上级页面。 */
  astra_launcher_close(&s_launcher);
}

/* 输入层入口：外壳把平台按键翻译成两个逻辑键的电平塞进来。
   HAL 只负责屏幕，输入是应用自己的事。 */
void astra_demo_key_set(astra_demo_key key, bool down)
{
  if (key < 0 || key >= ASTRA_DEMO_KEY_NUM)
    return;

  s_key_level[key] = down;
}

/* 两个逻辑键：短按切换高亮项，长按进入/退出下级页面。 */
static void handle_keys(void)
{
  /* 步骤 1：取一次时间戳和两个键的当前电平（来自输入层）。 */
  const unsigned long now = hal_millis();
  const bool k_prev = s_key_level[ASTRA_DEMO_KEY_PREV];
  const bool k_next = s_key_level[ASTRA_DEMO_KEY_NEXT];

  /* 步骤 2：捕获上升沿——刚按下的那一刻记下时间。 */
  if (k_prev && !s_key_down[ASTRA_DEMO_KEY_PREV])
    s_key_down_at[ASTRA_DEMO_KEY_PREV] = now;
  if (k_next && !s_key_down[ASTRA_DEMO_KEY_NEXT])
    s_key_down_at[ASTRA_DEMO_KEY_NEXT] = now;

  /* 步骤 3：「上一个」松开时结算——长按返回上级，短按上移一项。 */
  if (!k_prev && s_key_down[ASTRA_DEMO_KEY_PREV])
  {
    if ((now - s_key_down_at[ASTRA_DEMO_KEY_PREV]) >= LONG_PRESS_MS)
      astra_demo_back();
    else
      move_selection(-1);
  }

  /* 步骤 4：「下一个」松开时结算——长按进入下级，短按下移一项。 */
  if (!k_next && s_key_down[ASTRA_DEMO_KEY_NEXT])
  {
    if ((now - s_key_down_at[ASTRA_DEMO_KEY_NEXT]) >= LONG_PRESS_MS)
      astra_demo_activate();
    else
      move_selection(1);
  }

  /* 步骤 5：保存本帧电平，供下一帧做边沿检测。 */
  s_key_down[ASTRA_DEMO_KEY_PREV] = k_prev;
  s_key_down[ASTRA_DEMO_KEY_NEXT] = k_next;
}

/* ------------------------------------------------------------------------ */
/* 生命周期                                                                  */
/* ------------------------------------------------------------------------ */

/* 建立菜单并播放开机动画。 */
bool astra_demo_init(void)
{
  /* 步骤 1：没有注入后端就无从初始化。 */
  if (!hal_check())
    return false;

  /* 步骤 2：恢复出厂配置，保证重复 init/destroy 不会累积改动。 */
  astra_ui_config_reset();
  astra_config *ui = astra_ui_config();

  /* 步骤 3：开机动画速度。 */
  ui->logo_animation_speed = LOGO_SPEED;

  /* 步骤 4：行高跟随后端真实字号，否则文字会被裁掉或互相压住：
     list_text_height 是文字高度，list_line_height 是行间距（行距 >= 字高 + 边距）。 */
  ui->list_text_height = (float)hal_get_font_height();
  if (ui->list_line_height < ui->list_text_height + ui->list_text_margin)
    ui->list_line_height = ui->list_text_height + ui->list_text_margin;

  /* 步骤 5：清空按键历史，避免把上一次运行残留的电平当成一次按下。 */
  memset(s_key_down, 0, sizeof(s_key_down));
  memset(s_key_down_at, 0, sizeof(s_key_down_at));
  memset(s_key_level, 0, sizeof(s_key_level));

  /* 步骤 6：先停顿 200 ms 让屏幕稳定，再播开机动画。 */
  hal_delay(200);
  astra_draw_logo(LOGO_FRAMES);

  /* 步骤 7：Logo 动画会把字体切成版权字号且不还原，这里切回正文字体，
     否则整个菜单都会用最小的字号渲染。 */
  hal_set_font(ASTRA_FONT_BODY);

  /* 步骤 8：清空对象池，把 32 个槽位还给下面的建树过程。 */
  astra_menu_pool_reset();

  /* 步骤 9：建根页面。 */
  s_root = astra_menu_create("root");
  if (s_root == NULL)
    return false;

#if ASTRA_PANEL_HAS_TILES
  /* 步骤 10：把 30x30 主图按面板整数放大，尺寸与 config 里的 tile_pic_* 一致。 */
  icon_scale_up(PIC_SYSTEM, s_icon_system);
  icon_scale_up(PIC_DISPLAY, s_icon_display);
  icon_scale_up(PIC_NETWORK, s_icon_network);
  icon_scale_up(PIC_ABOUT, s_icon_about);

  /* 步骤 11：建四个磁贴页。 */
  astra_menu *system_page = astra_menu_create_tile("System", s_icon_system);
  astra_menu *display_page = astra_menu_create_tile("Display", s_icon_display);
  astra_menu *network_page = astra_menu_create_tile("Network", s_icon_network);
  astra_menu *about_page = astra_menu_create_tile("About", s_icon_about);
#else
  /* 步骤 11：面板太矮，装不下图标 + 标题，根菜单直接用列表页。 */
  astra_menu *system_page = astra_menu_create("System");
  astra_menu *display_page = astra_menu_create("Display");
  astra_menu *network_page = astra_menu_create("Network");
  astra_menu *about_page = astra_menu_create("About");
#endif

  /* 步骤 12：把四个入口挂到根页面。 */
  astra_menu_add_item(s_root, system_page);
  astra_menu_add_item(s_root, display_page);
  astra_menu_add_item(s_root, network_page);
  astra_menu_add_item(s_root, about_page);

  /* 步骤 13：给每个页面填子项。 */
  astra_menu_add_item(system_page, astra_menu_create("Brightness"));
  astra_menu_add_item(system_page, astra_menu_create("Volume"));
  astra_menu_add_item(system_page, astra_menu_create("Sleep Mode"));
  astra_menu_add_item(system_page, astra_menu_create("Reset"));

  astra_menu_add_item(display_page, astra_menu_create("Contrast"));
  astra_menu_add_item(display_page, astra_menu_create("Theme"));
  astra_menu_add_item(display_page, astra_menu_create("Rotation"));

  astra_menu_add_item(network_page, astra_menu_create("Wi-Fi"));
  astra_menu_add_item(network_page, astra_menu_create("Bluetooth"));
  astra_menu_add_item(network_page, astra_menu_create("Status"));

  astra_menu_add_item(about_page, astra_menu_create("astra"));
  astra_menu_add_item(about_page, astra_menu_create("Version 1.0"));
  astra_menu_add_item(about_page, astra_menu_create("C99"));

  /* 步骤 14：把根页面、选择器和相机交给 launcher，开始可以渲染。 */
  astra_launcher_init(&s_launcher, s_root, &s_selector, &s_camera);
  return true;
}

/* 一帧：处理按键，然后渲染并把显存推给后端。 */
void astra_demo_update(void)
{
  /* 步骤 1：处理两个逻辑键的边沿事件。 */
  handle_keys();

  /* 步骤 2：清屏 → 画页面 → 画选择器 → 推进相机 → present。 */
  astra_launcher_update(&s_launcher);
}

void astra_demo_destroy(void)
{
  /* 步骤 1：先丢掉所有指向对象池的指针。 */
  s_root = NULL;
  memset(&s_launcher, 0, sizeof(s_launcher));
  memset(&s_selector, 0, sizeof(s_selector));
  memset(&s_camera, 0, sizeof(s_camera));
  memset(s_key_down, 0, sizeof(s_key_down));
  memset(s_key_level, 0, sizeof(s_key_level));

  /* 步骤 2：回收对象池。后端由外壳持有，这里刻意不动它。 */
  astra_menu_pool_reset();
}
