# astra 硬件抽象层（HAL）详解

HAL 是框架与屏幕之间**唯一**的耦合点。框架里没有一行平台代码，全部绘制、
计时、外设操作都经过这里。实现一张函数指针表，就能把 astra 跑到一个新平台上。

**HAL 只负责屏幕**：不含任何用户输入（按键 / 触摸 / 编码器）接口。输入策略
由应用自己实现——多按键、矩阵键盘、旋钮、触摸各家的做法都不同，硬塞进框架
只会变成无用的抽象。示例应用在 `demo/astra_demo.h` 里自己定义了两个逻辑键
和短按/长按判定，外壳只需调用 `astra_demo_key_set()`。

> **来源**：本框架移植自 [astra UI](https://github.com/AstraThreshold/oled-ui-astra)。
> 上游用一个 `astra::hal` 抽象基类定义硬件契约、由派生类实现各个后端
> （Arduino / U8g2）。C 没有继承与虚函数，因此这里把同一组契约改写成
> 一张 `hal_ops` 函数指针表 + 一个 `hal` 实例结构体，语义一一对应；
> 后端也从框架内部移到了框架之外，只通过这张表接触框架。

后端代码**不属于框架**，放在你自己的工程里（本仓库的示例后端在 `port/raylib/`）。
框架本工程自带 `demo/` 作为应用示例，`include/` + `src/` 就是框架全部代码。

```
本工程（Inc/astra/）
  include/*.h     框架公开头文件 —— 无平台依赖，只依赖 <stdint.h>/<stdbool.h>
  src/*.c         框架实现
  demo/           示例应用       —— 只调用 HAL

外层演示工程（仓库根目录）
  port/raylib/    后端           —— 唯一允许出现 raylib API 的目录
  main.c          外壳           —— 选后端、建窗口、驱动应用
```

- [1. 设计模型](#1-设计模型)
- [2. 分辨率与配置](#2-分辨率与配置)
- [3. 数据结构](#3-数据结构)
- [4. 生命周期与所有权](#4-生命周期与所有权)
- [5. 画布契约](#5-画布契约)
- [6. 绘制接口](#6-绘制接口)
- [7. 字体接口](#7-字体接口)
- [8. 计时接口](#8-计时接口)
- [9. 输入（不属于 HAL）](#9-输入不属于-hal)
- [10. 外设与杂项](#10-外设与杂项)
- [11. 默认实现](#11-默认实现)
- [12. 编写一个后端](#12-编写一个后端)
- [13. 完整参考实现：SSD1306](#13-完整参考实现ssd1306)
- [14. 内置的内存后端（hal_vbuf）](#14-内置的内存后端hal_vbuf)
- [15. 测试后端](#15-测试后端)
- [16. 性能与资源](#16-性能与资源)
- [17. 检查清单](#17-检查清单)
## 1. 设计模型

C 没有虚函数，所以用一个**函数指针表 + 单例**来模拟多态：

```
           ┌──────────────────────────────┐
 hal_get() │  static hal *s_hal           │
   ───────▶│            │                 │
           │            ▼                 │
           │  ┌─────────────────────┐     │
           │  │ hal                 │     │
           │  │  ops ───────────┐   │     │
           │  │  config         │   │     │
           │  │  heap_owned     │   │     │
           │  └─────────────────┼───┘     │
           └────────────────────┼─────────┘
                                ▼
                    ┌───────────────────────┐
                    │ const hal_ops         │  ← 静态常量表
                    │  .init                │
                    │  .draw_pixel          │
                    │  .delay_ms            │
                    │  ...                  │
                    └───────────────────────┘
```

三层结构：

| 层 | 位置 | 职责 |
|---|---|---|
| 调用侧 | `hal.h` 的 `static inline hal_xxx()` | 通过单例转发，编译成一次间接调用 |
| 接口 | `hal_ops`、`hal` | 契约定义、单例状态 |
| 实现 | `<backend>_create()` + ops 表 | 平台细节 |

**后端实例化**靠"首成员嵌套"：

```c
typedef struct { hal base; /* 平台状态 */ } hal_mine;
```

`hal` 必须是第一个成员，这样 `(hal_mine *)hal_ptr` 是合法的上/下转型：
C 保证结构体首成员的地址与结构体地址相同。

---

## 2. 分辨率与配置

面板尺寸是**运行期数据**，不是编译期常量。框架从不假设 128x64：
它通过 `hal_system_config()` 读宽高，通过 `hal_get_buffer_tile_*()` 读缓冲区几何。

### 2.1 谁提供尺寸

| 角色 | 职责 |
|---|---|
| 后端 | 在 `create` 时接受配置，分配画布，把宽高写进 `hal.config` |
| 框架 | 每帧渲染都读 `config`，据此计算布局 |
| 应用 | 可选：读 `config` 调整自己的排版（字号、行高） |

### 2.2 raylib 后端的配置结构

```c
typedef struct hal_raylib_config {
  int width;       /* 面板宽，必须是 8 的倍数，默认 128 */
  int height;      /* 面板高，必须是 8 的倍数，默认 64  */
  int window_width;   /* 0 = 自动（面板 × pixel_scale） */
  int window_height;
  int pixel_scale;    /* 整数放大倍数，0 = 按窗口自动适配 */
  const char *title;      /* 窗口标题，NULL 保留当前 */
  const char *font_path;  /* TTF 路径，NULL 自动探测 */
  int font_pixel_main;            /* 0 = 由面板高度推导 */
  int font_pixel_logo_title;
  int font_pixel_logo_copyright;
  bool show_bezel;                /* 是否画面板边框 */
} hal_raylib_config;
```

三个配套函数：

```c
void hal_raylib_config_default(hal_raylib_config *cfg);  /* 填默认值 */
void hal_raylib_window_size(const hal_raylib_config *cfg, int *w, int *h);
hal *hal_raylib_create_with(const hal_raylib_config *cfg); /* NULL = 默认 */
hal *hal_raylib_create(void);                              /* 128x64 快捷方式 */
```

用法（`main.c`）：

```c
hal_raylib_config cfg;
hal_raylib_config_default(&cfg);
cfg.width = 96;
cfg.height = 48;

int ww, wh;
hal_raylib_window_size(&cfg, &ww, &wh);
InitWindow(ww, wh, cfg.title ? cfg.title : "astra");

hal_inject(hal_raylib_create_with(&cfg));
```

### 2.3 校验规则

`hal_raylib_create_with()` 在这些情况下返回 `NULL`：

- 宽或高 ≤ 0
- 不是 8 的倍数（1bpp page 格式要求）
- 宽/8 或 高/8 超过 255（HAL 契约里 tile 计数是 `uint8_t`）

宽或高传 0 表示"用默认值"，不是错误。

### 2.4 字号自动推导

`font_pixel_*` 传 0 时按面板高度推导：

```
main           = clamp(height / 5, 8, 16)
logo_title     = main + 2
logo_copyright = max(6, main - 4)
```

于是 64 px 高的面板得到熟悉的 **12 / 14 / 8**；32 px 高得到 8 / 10 / 6（下限 8，
更小的 TTF 光栅化不可读）。实测：

| 面板 | 字号 main | 说明 |
|---|---|---|
| 128x64 | 12 | 参考尺寸 |
| 64x64 | 12 | 高度相同，字号不变 |
| 128x32 | 8 | 触底到下限 |
| 96x48 | 9 | 48/5 = 9 |
| 256x64 | 12 | 只看高度 |
| 64x48 | 9 | 同上 |

### 2.5 框架侧的布局自适应

框架所有绘制都以 `hal_system_config()` 为基准，所以**几何自动正确**。
但排版参数（行高、图标间距、边距）默认是按 128x64 调的，需要按比例缩放：

```c
#include "astra_config.h"

astra_config *ui = astra_ui_config();
astra_config_fit_to_screen(ui, 128, 32);   /* 缩放排版参数 */
```

`astra_config_fit_to_screen()` 的行为：

- 缩放因子 `f = min(width / 128, height / 64)`，钳制在 `[0.4, 3.0]`
- 128x64 时 `f == 1`，**配置完全不变**（保证参考尺寸像素级一致）
- 缩放行高、边距、选择框、虚线位置、圆角、Logo 尺寸等
- **不缩放 `tile_pic_width/height`**：它描述的是位图素材本身的尺寸，
  缩小它会让 `draw_bmp` 按错误的行宽解读数据
- 重新推导 `tile_select_box_width/height`

示例应用（`demo/astra_demo.c`）按 **128x64** 固定排版，不再调用它；
缩放后仍需把 `list_text_height` 校正为后端真实字号、把 `list_line_height`
抬到至少 `字号 + 上边距`，避免文字裁切或行间重叠——示例应用就是这么做的。

### 2.6 磁贴图标与分辨率

磁贴图标是固定尺寸素材（示例为 30x30，`tile_pic_width/height` 必须等于
素材真实尺寸）。示例应用固定使用磁贴页，因为它只面向 128x64；
在更矮的面板上需要自行改用列表页，判据是：

```c
const float icon_stack = ui->tile_pic_height + ui->tile_pic_top_margin +
                         ui->tile_text_bottom_margin + 4.0f;
const bool use_tiles = (float) sys->screen_height >= icon_stack;
```

### 2.7 已验证的尺寸

外层演示工程的 `tests/test_resolution.c` 覆盖 128x64、64x64、128x32、96x48、256x64、64x48，
逐项校验：

- `hal_system_config()` 的宽高与请求一致
- 画布字节数 == `width * height / 8`
- `get_buffer_tile_width/height` == `width/8`、`height/8`
- 非法尺寸（如 100x64）被拒绝

### 2.8 前端框架之外

分辨率不属于框架的"策略"，框架只消费 HAL 报告的几何。因此：

- 同一份框架目标代码可服务任意尺寸
- `astra_test_hal` 不链接 raylib，也不关心屏幕尺寸
- 换屏只改外壳的 `cfg.width/height`，应用与框架代码零改动

---

---

## 3. 数据结构

### 2.1 `hal_ops`

全部成员（顺序无关，可用指定初始化器只填需要的项）：

```c
struct hal_ops {
  /* --- 标识与生命周期 --- */
  const char *(*type)(hal *self);
  void (*init)(hal *self);
  void (*deinit)(hal *self);

  /* --- 画布 --- */
  void    *(*get_canvas_buffer)(hal *self);
  uint8_t  (*get_buffer_tile_width)(hal *self);
  uint8_t  (*get_buffer_tile_height)(hal *self);
  void     (*canvas_update)(hal *self);
  void     (*canvas_clear)(hal *self);

  /* --- 字体 --- */
  void    (*set_font)(hal *self, const uint8_t *font);
  uint8_t (*get_font_width)(hal *self, const char *text);
  uint8_t (*get_font_height)(hal *self);
  void    (*set_draw_type)(hal *self, uint8_t type);

  /* --- 绘制 --- */
  void (*draw_pixel)(hal *self, float x, float y);
  void (*draw_english)(hal *self, float x, float y, const char *text);
  void (*draw_chinese)(hal *self, float x, float y, const char *text);
  void (*draw_v_dotted_line)(hal *self, float x, float y, float h);
  void (*draw_h_dotted_line)(hal *self, float x, float y, float l);
  void (*draw_v_line)(hal *self, float x, float y, float h);
  void (*draw_h_line)(hal *self, float x, float y, float l);
  void (*draw_bmp)(hal *self, float x, float y, float w, float h,
                   const uint8_t *bmp);
  void (*draw_box)(hal *self, float x, float y, float w, float h);
  void (*draw_rbox)(hal *self, float x, float y, float w, float h, float r);
  void (*draw_frame)(hal *self, float x, float y, float w, float h);
  void (*draw_rframe)(hal *self, float x, float y, float w, float h, float r);

  /* --- 计时 --- */
  void          (*delay_ms)(hal *self, unsigned long ms);
  unsigned long (*millis)(hal *self);
  unsigned long (*get_tick)(hal *self);
  unsigned long (*get_random_seed)(hal *self);

  /* --- 外设 --- */
  void (*beep)(hal *self, float freq);
  void (*beep_stop)(hal *self);
  void (*set_beep_vol)(hal *self, uint8_t vol);
  void (*screen_on)(hal *self);
  void (*screen_off)(hal *self);

  /* --- 杂项 --- */
  void (*print_info)(hal *self, const char *msg);
  void (*update_config)(hal *self);
};
```

### 2.2 `hal`

```c
struct hal {
  const hal_ops  *ops;                      /* 必须第一个成员 */
  astra_sys_config config;                  /* 屏幕参数       */
  bool heap_owned;                          /* 见 §3.2        */
};
```

`config` 由 `hal_construct()` 填成 `astra_sys_config_default()`
（128×64），可通过 `hal_system_config()` 读写。

结构体里**没有按键状态**：输入不属于 HAL，见第 9 章。

---

## 4. 生命周期与所有权

### 3.1 单例 API

```c
bool hal_inject(hal *h);   /* 登记为当前后端，并调用 h->ops->init(h) */
hal *hal_get(void);        /* 未登记时返回 NULL                      */
bool hal_check(void);      /* 是否已登记                             */
void hal_destroy(void);    /* deinit + 按所有权释放 + 清空单例        */
```

`hal_inject()` 的顺序是**先 init、后发布单例**，因此：

> 后端的 `init()` 里**不能**调用任何 `hal_get()` 系接口（拿到的是 `NULL`）。
> 需要绘制就直接调自己的内部函数。

`hal_inject(NULL)` 或 `ops == NULL` 返回 `false`，不改变现有状态。

### 3.2 所有权

`hal_destroy()` 只释放"堆上创建"的实例。由构造方式声明：

```c
void hal_construct(hal *h, const hal_ops *ops);        /* 静态实例，destroy 不 free */
void hal_construct_owned(hal *h, const hal_ops *ops);  /* 堆实例，destroy 会 free   */
```

| 后端实例存放位置 | 用哪个 | 原因 |
|---|---|---|
| `static hal_mine s_hal;` | `hal_construct` | 静态存储不能 `free()` |
| `calloc(1, sizeof)` | `hal_construct_owned` | 需要释放 |

两者都会：清零结构体 → 设 `ops` → 填默认 `config`。

### 3.3 `hal_destroy()` 的三步顺序

```c
void hal_destroy(void) {
  if (s_hal == NULL) return;        /* 幂等 */

  hal *h = s_hal;
  s_hal = NULL;                     /* (1) 先摘单例 */

  if (h->ops && h->ops->deinit)     /* (2) 再释放外设/GPU/字体 */
    h->ops->deinit(h);

  if (h->heap_owned) free(h);       /* (3) 最后释放结构体本身 */
}
```

每一步都不可调换：

1. **先置空**：`deinit()` 内部若还有绘制调用，`hal_get()` 会拿到 `NULL`
   而不是正在析构的指针（避免 use-after-free）。
2. **`deinit` 先于 `free`**：`deinit` 需要读取结构体里的纹理句柄、字体缓冲
   等字段，这些就在即将被 `free()` 的内存里。
3. **`free` 看所有权**：见 §3.2。

`deinit()` 必须是 `init()` 的逆操作，并且要能处理"只初始化了一半"的状态
（每个释放分支都判空）。

---

## 5. 画布契约

### 4.1 缓冲区布局

框架在 `hal_get_canvas_buffer()` 返回的内存里直接按 **1bpp page 格式** 读写：

```
每个字节 = 垂直方向的 8 个像素
bit7 = 该页最上面一行（y 最小），bit0 = 最下面一行

索引 = (y * width + x) / 8
位序 = 7 - (x % 8)
```

总大小：

```
buffer_bytes = width * height / 8
             = (tile_width * 8) * (tile_height * 8) / 8
             = tile_width * tile_height * 8
```

128×64 时为 `16 * 8 * 8 = 1024` 字节。

> 这正是 SSD1306 GDDRAM 的排布，所以大多数 OLED 驱动可以直接
> `memcpy` 到屏幕上，无需转换。如果你的驱动是"行优先 1bpp"，
> 请在 `canvas_update()` 里转换。

### 4.2 `get_buffer_tile_width` / `get_buffer_tile_height`

返回**字节数**（不是像素数）：

```c
tile_width  = width  / 8     /* 128 -> 16 */
tile_height = height / 8     /*  64 ->  8 */
```

框架用它们计算缓冲区长度（淡出动画）和分页。

### 4.3 `canvas_clear`

把整个缓冲区清零（= 全灭）。框架每帧开头调用一次。

### 4.4 `canvas_update`

把缓冲区推送到实际显示。对桌面后端是画到窗口；对 OLED 是走 I2C/SPI。
**每帧只调用一次**，位于所有绘制之后。

---

## 6. 绘制接口

### 5.1 绘制模式 `set_draw_type`

所有绘制函数的行为受当前模式影响：

| 值 | 含义 | 效果 |
|---|---|---|
| `0` | CLEAR | 把覆盖到的像素置 0（灭） |
| `1` | SOLID | 把覆盖到的像素置 1（亮）——默认 |
| `2` | INVERT | 把覆盖到的像素取反 |

框架用它实现反相高亮（选择框）、遮罩（Logo）、擦除（弹窗底衬）。
**`set_draw_type` 是状态，不是参数**，实现里存成成员变量即可。

### 5.2 坐标系

| 函数族 | 基准点 |
|---|---|
| `draw_pixel` / `draw_v_line` / `draw_h_line` / `draw_box` / `draw_frame` / `draw_bmp` | **左上角** |
| `draw_english` / `draw_chinese` | **左下角** |

文字左下角基准的含义：`y` 传入的是基线下沿，字形向上生长。
所以绘制一行文字时，调用方会写 `y = row_top + font_height`。

所有坐标都是 `float`，实现里通常 `lroundf()` 取整。**允许越界**：
框架会把元素画到屏幕外（滚动动画依赖这一点），实现必须自己裁剪。

### 5.3 各函数语义

| 函数 | 语义 |
|---|---|
| `draw_pixel(x, y)` | 单像素 |
| `draw_h_line(x, y, len)` | 从 `(x,y)` 向右 `len` 个像素 |
| `draw_v_line(x, y, len)` | 从 `(x,y)` 向下 `len` 个像素 |
| `draw_h_dotted_line(x, y, len)` | 水平虚线，每 8 像素亮 5 灭 3 |
| `draw_v_dotted_line(x, y, len)` | 垂直虚线，同节奏 |
| `draw_box(x, y, w, h)` | 填充矩形 |
| `draw_frame(x, y, w, h)` | 矩形描边（1 像素） |
| `draw_rbox(x, y, w, h, r)` | 圆角填充矩形 |
| `draw_rframe(x, y, w, h, r)` | 圆角描边 |
| `draw_bmp(x, y, w, h, bmp)` | 绘制 1bpp 位图 |
| `draw_english(x, y, text)` | ASCII / UTF-8 文本 |
| `draw_chinese(x, y, text)` | 同上（框架分两个入口，可由后端统一实现） |

### 5.4 虚线的空拍

```c
for (i = 0; i < len; ++i)
  if (!(i % 8 == 0 || (i - 1) % 8 == 0 || (i - 2) % 8 == 0))
    plot(x + i, y);
```

即每 8 个像素中，前 3 个不画。这个节奏是视觉设计的一部分，请保持一致。

### 5.5 位图格式 `draw_bmp`

```
- 1bpp，MSB 在左
- 每行按字节对齐：bytes_per_row = (w + 7) / 8
- 1 = 点亮
- 索引：bmp[row * bytes_per_row + col / 8]，位序 0x80 >> (col % 8)
```

框架的磁贴图标是 30×30 → 每行 4 字节，共 120 字节。

---

## 7. 字体接口

框架对字体的认知仅限于"一个不透明句柄 + 宽高查询"：

```c
void    hal_set_font(const uint8_t *font);      /* 切换当前字体 */
uint8_t hal_get_font_width(const char *text);   /* 该串在当前字体下的像素宽 */
uint8_t hal_get_font_height(void);              /* 当前字体的行高 */
```

### 6.1 句柄的含义由后端定义

`astra_config` 里的三个字段就是这三个句柄：

```c
const uint8_t *main_font;              /* 正文      */
const uint8_t *logo_title_font;        /* Logo 标题 */
const uint8_t *logo_copy_right_font;   /* Logo 副标题 */
```

默认都是 `NULL`。后端可以：

**方式 A：用指针当"字号标签"**（桌面后端采用）

```c
extern const uint8_t hal_raylib_font_main;        /* 值为 1 */
extern const uint8_t hal_raylib_font_logo_title;  /* 值为 2 */

static void rl_set_font(hal *self, const uint8_t *font) {
  hal_raylib *r = (hal_raylib *) self;
  if (font == &hal_raylib_font_logo_title) r->font_pixel_size = 14;
  else if (font == &hal_raylib_font_logo_copyright) r->font_pixel_size = 8;
  else r->font_pixel_size = 12;
}
```

**方式 B：指向真正的字模数据**（MCU 常用）

```c
static const uint8_t FONT_8X16[] = { /* 字模 */ };
/* hal_get_font_width 里按字模表查表 */
static void my_set_font(hal *self, const uint8_t *font) {
  ((my_hal *) self)->font = font ? font : FONT_8X16;
}
```

### 6.2 `get_font_width` 的精度要求

框架用它计算：

- 列表选择框的宽度（`字宽 + 2 * list_text_margin`）
- 磁贴标题的水平居中
- 弹窗宽度

所以必须**按实际字宽**返回（等宽字体可以直接 `len * width`，
比例字体要真算）。返回 `0` 会导致选择框塌陷为零宽。

返回值是 `uint8_t`，超长文本需截断到 255。

### 6.3 `get_font_height`

返回行高（含行距）。框架的日志输出用它算每屏行数，
`astra_anim_*` 用它算缓冲区长度。

### 6.4 UTF-8

框架的字符串一律以 `char *` 传入，编码约定为 UTF-8。后端需要自己解码
（`draw_chinese` 与 `draw_english` 可以共用一个实现）。

---

## 8. 计时接口

```c
void          hal_delay(unsigned long ms);  /* 阻塞延时 */
unsigned long hal_millis(void);             /* 开机以来的毫秒数（可回绕） */
unsigned long hal_get_tick(void);           /* 同上，供自定义调度使用 */
unsigned long hal_get_random_seed(void);    /* 真随机种子，无法提供可返回 0 */
```

要点：

- `hal_delay()` 必须是**真阻塞**。框架的动画与弹窗用它控制节奏
  （`fade_animation_speed`、`pop_speed` 等以毫秒为单位的参数）。
- `hal_millis()` 是本框架里唯一的时间来源，长按判定、弹窗停留都由它驱动，
  允许回绕（用无符号减法即可正确比较）。
- `hal_get_random_seed()` 会被 Logo 星星的 `srand()` 使用。
  若平台没有 RNG，返回 `hal_millis() * 1000` 之类的值也比 0 好——
  返回固定值会让每次开机星星位置都一样。

---

## 9. 输入（不属于 HAL）

**HAL 里没有按键接口**，这是刻意的设计决定。

上游 astra UI 把按键扫描放进 HAL，是因为它的目标是 Arduino 上"两个按钮 +
一块 OLED"这一种硬件。但这种抽象在实际项目里几乎总要被绕开：

| 真实硬件 | HAL 抽象会怎样失效 |
|---|---|
| 5 向摇杆 | 只有两个逻辑键，另一个轴无处安放 |
| 矩阵键盘 | 需要行扫描 + 消抖周期，与"电平查询"模型不匹配 |
| 旋转编码器 | 本质是事件（CW/CCW/按下），不是电平 |
| 触摸屏 | 命中测试、手势、长按阈值都在触摸驱动里，不在屏幕驱动里 |
| 只有一个按钮 | 长按/双击/三击全靠应用状态机 |

所以 astra 只提供**机械上必须通用的部分**：菜单、选择框、相机、动画和绘制。
输入由应用自己实现，示例见 `demo/astra_demo.c`。

### 9.1 示例应用的输入层

`demo/astra_demo.h` 定义了两个逻辑键和注入入口：

```c
typedef enum astra_demo_key {
  ASTRA_DEMO_KEY_PREV = 0,   /* 短按：上移一项；长按：返回上级 */
  ASTRA_DEMO_KEY_NEXT,       /* 短按：下移一项；长按：进入下级 */
  ASTRA_DEMO_KEY_NUM
} astra_demo_key;

void astra_demo_key_set(astra_demo_key key, bool down);   /* 电平语义 */
```

外壳每帧把平台输入翻译成这两个电平：

```c
/* raylib */
astra_demo_key_set(ASTRA_DEMO_KEY_PREV,
                   IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_UP));
astra_demo_key_set(ASTRA_DEMO_KEY_NEXT,
                   IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_DOWN));

/* GPIO */
astra_demo_key_set(ASTRA_DEMO_KEY_PREV, HAL_GPIO_ReadPin(...) == RESET);
astra_demo_key_set(ASTRA_DEMO_KEY_NEXT, HAL_GPIO_ReadPin(...) == RESET);
```

示例应用内部做边沿检测和长按判定（`LONG_PRESS_MS = 500`，用 `hal_millis()`
计时），并在**松开**时结算：

```
按下 ──记录时刻──▶ 持续按住 ──▶ 松开
                                  ├─ 时长 < 500 ms  → 短按：移动高亮项
                                  └─ 时长 ≥ 500 ms  → 长按：进入 / 返回
```

需要独立的"确认/返回"物理键时，直接调用 `astra_demo_activate()` /
`astra_demo_back()`，跳过时长判定。

### 9.2 自己写输入层

不用示例应用时，输入层要做的只有一件事：**把用户意图翻译成对框架的调用**。

```c
switch (event) {
  case EV_UP:       astra_selector_go(&sel, prev_index);  break;
  case EV_DOWN:     astra_selector_go(&sel, next_index);  break;
  case EV_ENTER:    astra_launcher_open(&launcher);       break;
  case EV_BACK:     astra_launcher_close(&launcher);      break;
}
```

框架不关心事件从哪来、有没有消抖、是电平还是队列——`astra_selector_go()`
和 `astra_launcher_open/close()` 都是幂等的普通函数调用，可以随时从任何地方触发
（包括中断里置标志、主循环里消费）。

---

## 10. 外设与杂项

### 10.1 蜂鸣器

```c
void hal_beep(float freq);        /* 指定频率发声 */
void hal_beep_stop(void);
void hal_set_beep_vol(uint8_t vol);
```

框架自身不发声，留给应用。没有蜂鸣器时可以不实现（默认 no-op）。

### 10.2 屏幕开关

```c
void hal_screen_on(void);
void hal_screen_off(void);
```

用于息屏。桌面后端的实现是让 `canvas_update()` 输出全黑而不销毁纹理。

### 10.3 日志

```c
void hal_print_info(const char *msg);
```

默认实现 `hal_default_print_info()` 在屏幕上滚动显示最近 N 行文字，
用于"开机自检"这类场景。**它自己完成 clear + 绘制 + update**，
因此是阻塞的，不要在渲染循环里调用。

### 10.4 配置热更新

```c
void hal_update_config(void);
```

框架在配置变化后调用，让后端有机会重新计算派生值
（如缓冲区尺寸、字体度量）。通常可以留空。

---

## 11. 默认实现

`astra_hal_default_ops` 是一张"所有能力都是安全 no-op"的表，
可作为后端实现的起点：

```c
static hal_ops make_my_ops(void) {
  hal_ops ops = astra_hal_default_ops;   /* 先拿全量默认值 */

  ops.init        = my_init;
  ops.deinit      = my_deinit;
  ops.draw_pixel  = my_draw_pixel;
  ops.canvas_update = my_flush;
  /* 只覆盖实际实现的项 */

  return ops;
}
```

**为什么必须从默认表派生**：`hal.h` 的内联转发函数直接调用
`h->ops->xxx(h, ...)`，没有 NULL 检查。若用指定初始化器从零开始填表，
漏填的项就是空指针，一调用就崩溃（而且往往在很久之后的某个动画路径上）。
从默认表派生可以彻底避免这类问题。

由于 `hal_ops` 是 `const` 且被单例共享，派生出的表必须是
`static`/全局存储，不能是函数返回的栈对象。

---

## 12. 编写一个后端

### 步骤 1：定义结构体

```c
typedef struct {
  hal base;                /* 必须是第一个成员 */
  uint8_t buffer[1024];    /* 128x64 / 8 */
  uint8_t draw_type;
  const uint8_t *font;
  bool display_on;
} hal_mine;
```

### 步骤 2：实现画布

```c
static void *mine_canvas(hal *self)       { return ((hal_mine *) self)->buffer; }
static uint8_t mine_tile_w(hal *self)     { (void) self; return 16; }
static uint8_t mine_tile_h(hal *self)     { (void) self; return 8; }
static void mine_clear(hal *self)         { memset(((hal_mine *) self)->buffer, 0, 1024); }
static void mine_flush(hal *self)         { oled_write(((hal_mine *) self)->buffer, 1024); }
```

### 步骤 3：实现像素与图元

像素是所有图元的公共底座，先写它：

```c
static void mine_plot(hal_mine *m, int x, int y) {
  if (x < 0 || y < 0 || x >= 128 || y >= 64) return;

  uint8_t *page = &m->buffer[(y * 128 + x) / 8];
  const uint8_t mask = (uint8_t) (1u << (7 - (x % 8)));

  switch (m->draw_type) {
    case 0: *page &= (uint8_t) ~mask; break;   /* clear  */
    case 2: *page ^= mask;            break;   /* invert */
    default:*page |= mask;            break;   /* solid  */
  }
}

static void mine_draw_pixel(hal *self, float x, float y) {
  mine_plot((hal_mine *) self, (int) lroundf(x), (int) lroundf(y));
}
```

其余图元都可以用 `mine_plot()` 拼出来：

```c
static void mine_h_line(hal *self, float x, float y, float len) {
  hal_mine *m = (hal_mine *) self;
  const int px = (int) lroundf(x), py = (int) lroundf(y);
  for (int i = 0; i < (int) lroundf(len); ++i) mine_plot(m, px + i, py);
}

static void mine_box(hal *self, float x, float y, float w, float h) {
  hal_mine *m = (hal_mine *) self;
  const int px = (int) lroundf(x), py = (int) lroundf(y);
  const int pw = (int) lroundf(w), ph = (int) lroundf(h);
  for (int row = 0; row < ph; ++row)
    for (int col = 0; col < pw; ++col) mine_plot(m, px + col, py + row);
}
```

> 有 u8g2 / Adafruit-GFX 等图形库时，可以直接把 `draw_*` 转成库调用，
> 但要自己处理 `draw_type`（库通常只有"亮"和"灭"）。

### 步骤 4：实现文字

最少需要 `set_font` / `get_font_width` / `get_font_height`，
以及 `draw_english`（和 `draw_chinese`）。

等宽点阵字体（8×16）示例：

```c
static uint8_t s_font_w = 8, s_font_h = 16;

static uint8_t mine_font_width(hal *self, const char *text) {
  (void) self;
  size_t n = text ? strlen(text) : 0;
  uint8_t w = (uint8_t) (n * s_font_w);
  return w > 255 ? 255 : w;
}

static void mine_draw_english(hal *self, float x, float y, const char *text) {
  hal_mine *m = (hal_mine *) self;
  int pen_x = (int) lroundf(x);
  int top = (int) lroundf(y) - s_font_h;      /* 左下角 -> 左上角 */

  for (const char *p = text; *p; ++p) {
    const uint8_t *glyph = font_lookup((unsigned char) *p);  /* 你的字模表 */
    for (int row = 0; row < s_font_h; ++row)
      for (int col = 0; col < s_font_w; ++col)
        if (glyph[row * s_font_w + col]) mine_plot(m, pen_x + col, top + row);
    pen_x += s_font_w;
  }
}
```

中文需要 UTF-8 解码 + 字模表（或用 u8g2 的 `u8g2_DrawUTF8`）。

### 步骤 5：实现计时

```c
static void mine_delay(hal *self, unsigned long ms) { (void) self; HAL_Delay(ms); }
static unsigned long mine_millis(hal *self) { (void) self; return HAL_GetTick(); }
static unsigned long mine_seed(hal *self) {
  (void) self;
  return HAL_GetTick() ^ (ADC_Read() << 8);
}
```

### 步骤 6：组装与创建

```c
static void mine_init(hal *self)   { oled_init(); }
static void mine_deinit(hal *self) { oled_off(); }

static hal_ops s_ops;   /* 必须静态存放 */

hal *hal_mine_create(void) {
  static hal_mine s_instance;
  static bool s_ready;

  if (!s_ready) {
    s_ops = astra_hal_default_ops;
    s_ops.init = mine_init;
    s_ops.deinit = mine_deinit;
    s_ops.get_canvas_buffer = mine_canvas;
    s_ops.get_buffer_tile_width = mine_tile_w;
    s_ops.get_buffer_tile_height = mine_tile_h;
    s_ops.canvas_clear = mine_clear;
    s_ops.canvas_update = mine_flush;
    s_ops.set_draw_type = mine_set_draw_type;
    s_ops.draw_pixel = mine_draw_pixel;
    s_ops.draw_h_line = mine_h_line;
    s_ops.draw_v_line = mine_v_line;
    s_ops.draw_box = mine_box;
    s_ops.draw_frame = mine_frame;
    s_ops.draw_english = mine_draw_english;
    s_ops.draw_chinese = mine_draw_chinese;
    s_ops.get_font_width = mine_font_width;
    s_ops.get_font_height = mine_font_height;
    s_ops.delay_ms = mine_delay;
    s_ops.millis = mine_millis;
    s_ops.get_tick = mine_millis;
    s_ops.get_random_seed = mine_seed;
    s_ready = true;
  }

  hal_construct(&s_instance.base, &s_ops);   /* 静态实例 → 非 owned */
  s_instance.draw_type = 1;
  s_instance.display_on = true;
  return &s_instance.base;
}
```

### 步骤 7：注入

```c
hal_inject(hal_mine_create());
```

此后所有 `hal_*()` 都走你的实现。

---

## 13. 完整参考实现：SSD1306

一个**不依赖图形库**的最小可用后端（I2C、128×64、8×16 等宽 ASCII 字体）。
放在 `port/ssd1306/hal_ssd1306.c`，与 `port/raylib/` 平级。
省略号处是从你的字模表取数据。

```c
// hal_ssd1306.c
#include <math.h>
#include <string.h>
#include "hal_ssd1306.h"

#define W 128
#define H 64
#define TILE_W (W / 8)
#define TILE_H (H / 8)
#define FONT_W 8
#define FONT_H 16

typedef struct {
  hal base;
  uint8_t buffer[TILE_W * TILE_H * 8];
  uint8_t draw_type;
  bool display_on;
} hal_ssd1306;

/* ---------- canvas ---------- */

static void *ss_canvas(hal *self) { return ((hal_ssd1306 *) self)->buffer; }
static uint8_t ss_tile_w(hal *self) { (void) self; return TILE_W; }
static uint8_t ss_tile_h(hal *self) { (void) self; return TILE_H; }

static void ss_clear(hal *self) {
  memset(((hal_ssd1306 *) self)->buffer, 0, sizeof(((hal_ssd1306 *) self)->buffer));
}

static void ss_flush(hal *self) {
  hal_ssd1306 *s = (hal_ssd1306 *) self;
  if (!s->display_on) {
    uint8_t blank[TILE_W * TILE_H * 8] = {0};
    ssd1306_send(blank, sizeof(blank));
    return;
  }
  ssd1306_send(s->buffer, sizeof(s->buffer));
}

/* ---------- primitives ---------- */

static void plot(hal_ssd1306 *s, int x, int y) {
  if (x < 0 || y < 0 || x >= W || y >= H) return;

  uint8_t *page = &s->buffer[(y * W + x) / 8];
  const uint8_t mask = (uint8_t) (1u << (7 - (x % 8)));

  switch (s->draw_type) {
    case 0: *page &= (uint8_t) ~mask; break;
    case 2: *page ^= mask;            break;
    default:*page |= mask;            break;
  }
}

static void ss_draw_type(hal *self, uint8_t type) {
  ((hal_ssd1306 *) self)->draw_type = type;
}

static void ss_pixel(hal *self, float x, float y) {
  plot((hal_ssd1306 *) self, (int) lroundf(x), (int) lroundf(y));
}

static void ss_h_line(hal *self, float x, float y, float len) {
  hal_ssd1306 *s = (hal_ssd1306 *) self;
  int px = (int) lroundf(x), py = (int) lroundf(y), n = (int) lroundf(len);
  for (int i = 0; i < n; ++i) plot(s, px + i, py);
}

static void ss_v_line(hal *self, float x, float y, float len) {
  hal_ssd1306 *s = (hal_ssd1306 *) self;
  int px = (int) lroundf(x), py = (int) lroundf(y), n = (int) lroundf(len);
  for (int i = 0; i < n; ++i) plot(s, px, py + i);
}

static void ss_h_dot(hal *self, float x, float y, float len) {
  hal_ssd1306 *s = (hal_ssd1306 *) self;
  int px = (int) lroundf(x), py = (int) lroundf(y), n = (int) lroundf(len);
  for (int i = 0; i < n; ++i)
    if (!(i % 8 == 0 || (i - 1) % 8 == 0 || (i - 2) % 8 == 0)) plot(s, px + i, py);
}

static void ss_v_dot(hal *self, float x, float y, float len) {
  hal_ssd1306 *s = (hal_ssd1306 *) self;
  int px = (int) lroundf(x), py = (int) lroundf(y), n = (int) lroundf(len);
  for (int i = 0; i < n; ++i)
    if (!(i % 8 == 0 || (i - 1) % 8 == 0 || (i - 2) % 8 == 0)) plot(s, px, py + i);
}

static void fill_rect(hal_ssd1306 *s, int x, int y, int w, int h, float r) {
  if (w <= 0 || h <= 0) return;

  float radius = r;
  const float limit = (float) (w < h ? w : h) / 2.0f;
  if (radius > limit) radius = limit;
  if (radius < 0.0f) radius = 0.0f;

  for (int row = 0; row < h; ++row) {
    for (int col = 0; col < w; ++col) {
      if (radius > 0.0f) {
        float dx = 0.0f, dy = 0.0f;
        if ((float) col < radius) dx = radius - (float) col;
        else if ((float) col > (float) (w - 1) - radius)
          dx = (float) col - ((float) (w - 1) - radius);
        if ((float) row < radius) dy = radius - (float) row;
        else if ((float) row > (float) (h - 1) - radius)
          dy = (float) row - ((float) (h - 1) - radius);
        if (dx * dx + dy * dy > radius * radius) continue;
      }
      plot(s, x + col, y + row);
    }
  }
}

static void ss_box(hal *self, float x, float y, float w, float h) {
  fill_rect((hal_ssd1306 *) self, (int) lroundf(x), (int) lroundf(y),
            (int) lroundf(w), (int) lroundf(h), 0.0f);
}

static void ss_rbox(hal *self, float x, float y, float w, float h, float r) {
  fill_rect((hal_ssd1306 *) self, (int) lroundf(x), (int) lroundf(y),
            (int) lroundf(w), (int) lroundf(h), r);
}

static void ss_frame(hal *self, float x, float y, float w, float h) {
  ss_h_line(self, x, y, w);
  ss_h_line(self, x, y + h - 1.0f, w);
  ss_v_line(self, x, y, h);
  ss_v_line(self, x + w - 1.0f, y, h);
}

static void ss_rframe(hal *self, float x, float y, float w, float h, float r) {
  (void) r;               /* 圆角描边可退化为直角，视觉差异很小 */
  ss_frame(self, x, y, w, h);
}

static void ss_bmp(hal *self, float x, float y, float w, float h, const uint8_t *bmp) {
  hal_ssd1306 *s = (hal_ssd1306 *) self;
  if (!bmp) return;

  int px = (int) lroundf(x), py = (int) lroundf(y);
  int pw = (int) lroundf(w), ph = (int) lroundf(h);
  int bpr = (pw + 7) / 8;

  for (int row = 0; row < ph; ++row)
    for (int col = 0; col < pw; ++col)
      if ((bmp[row * bpr + col / 8] & (0x80 >> (col % 8))) != 0)
        plot(s, px + col, py + row);
}

/* ---------- text ---------- */

static uint8_t ss_font_w(hal *self, const char *text) {
  (void) self;
  size_t n = text ? strlen(text) : 0;
  size_t w = n * FONT_W;
  return (uint8_t) (w > 255 ? 255 : w);
}

static uint8_t ss_font_h(hal *self) { (void) self; return FONT_H; }

static void ss_draw_text(hal *self, float x, float y, const char *text) {
  hal_ssd1306 *s = (hal_ssd1306 *) self;
  int pen_x = (int) lroundf(x);
  int top = (int) lroundf(y) - FONT_H;

  for (const char *p = text; p && *p; ++p) {
    const uint8_t *glyph = FONT_8X16_LOOKUP((unsigned char) *p); /* 你的字模 */
    if (glyph) {
      for (int row = 0; row < FONT_H; ++row)
        for (int col = 0; col < FONT_W; ++col)
          if (glyph[row] & (0x80 >> col)) plot(s, pen_x + col, top + row);
    }
    pen_x += FONT_W;
  }
}

/* ---------- system ---------- */

static void ss_delay(hal *self, unsigned long ms) { (void) self; HAL_Delay(ms); }
static unsigned long ss_millis(hal *self) { (void) self; return HAL_GetTick(); }
static unsigned long ss_seed(hal *self) { (void) self; return HAL_GetTick(); }

static void ss_screen_on(hal *self)  { ((hal_ssd1306 *) self)->display_on = true;  }
static void ss_screen_off(hal *self) { ((hal_ssd1306 *) self)->display_on = false; }

/* ---------- ops ---------- */

static hal_ops s_ops;

hal *hal_ssd1306_create(void) {
  static hal_ssd1306 s_instance;
  static bool s_ready;

  if (!s_ready) {
    s_ops = astra_hal_default_ops;

    s_ops.init                     = ssd1306_hw_init;
    s_ops.deinit                   = ssd1306_hw_off;
    s_ops.get_canvas_buffer        = ss_canvas;
    s_ops.get_buffer_tile_width    = ss_tile_w;
    s_ops.get_buffer_tile_height   = ss_tile_h;
    s_ops.canvas_clear             = ss_clear;
    s_ops.canvas_update            = ss_flush;
    s_ops.set_draw_type            = ss_draw_type;
    s_ops.draw_pixel               = ss_pixel;
    s_ops.draw_h_line              = ss_h_line;
    s_ops.draw_v_line              = ss_v_line;
    s_ops.draw_h_dotted_line       = ss_h_dot;
    s_ops.draw_v_dotted_line       = ss_v_dot;
    s_ops.draw_box                 = ss_box;
    s_ops.draw_rbox                = ss_rbox;
    s_ops.draw_frame               = ss_frame;
    s_ops.draw_rframe              = ss_rframe;
    s_ops.draw_bmp                 = ss_bmp;
    s_ops.draw_english             = ss_draw_text;
    s_ops.draw_chinese             = ss_draw_text;
    s_ops.get_font_width           = ss_font_w;
    s_ops.get_font_height          = ss_font_h;
    s_ops.delay_ms                 = ss_delay;
    s_ops.millis                   = ss_millis;
    s_ops.get_tick                 = ss_millis;
    s_ops.get_random_seed          = ss_seed;
    s_ops.screen_on                = ss_screen_on;
    s_ops.screen_off               = ss_screen_off;
    s_ready = true;
  }

  hal_construct(&s_instance.base, &s_ops);
  s_instance.draw_type = 1;
  s_instance.display_on = true;
  return &s_instance.base;
}
```

使用：

```c
hal_inject(hal_ssd1306_create());
astra_menu_pool_reset();
astra_menu *root = astra_menu_create("root");
/* ... */
astra_launcher_init(&launcher, root, &selector, &camera);

/* 按键不经过 HAL：自己扫描，自己决定语义 */
for (;;) {
  if (btn_prev_pressed()) astra_launcher_close(&launcher);
  if (btn_next_pressed()) astra_launcher_open(&launcher);
  astra_launcher_update(&launcher);
}
```

---

---

## 14. 内置的内存后端（`hal_vbuf`）

框架自带一个**不驱动任何硬件**的后端：所有绘制都落到一块 RAM 里的 1bpp 页缓冲区，
由使用者自己决定何时、如何把它推送出去。它没有任何平台依赖，能编译进 PC 程序、
单元测试和 MCU 固件。

### 14.1 用途

| 场景 | 为什么合适 |
|---|---|
| 先跑通界面，再接硬件 | 不需要写任何驱动就能看到布局与动画 |
| 单元测试 / CI | 纯内存，可断言像素，无需窗口 |
| 只关心"画出来"的宿主 | 你已经有自己的刷新逻辑（DMA、双缓冲、I²C 队列） |
| 离屏渲染 / 截图 | 一帧就是 1024 字节，随时可存成 PBM |

### 14.2 缓冲区布局

与 SSD1306 GDDRAM **完全一致**，所以你现有的"按页写 128 字节"代码可以直接复用：

```
byte_index = (y * width + x) / 8
bit        = 7 - (x % 8)      /* bit7 = 最左像素 */
size       = tile_w * tile_h * 8 = width * height / 8
```

128×64 时是 1024 字节；8 个 page，每 page 128 字节，页内 128 个像素横向排布。

### 14.3 用法

```c
#include "hal_vbuf.h"

/* 堆：实例和画布一起分配，hal_destroy() 一并释放 */
hal_inject(hal_vbuf_create(NULL));            /* NULL => 128x64 */

/* 或者自己给尺寸 */
hal_vbuf_config cfg;
hal_vbuf_config_default(&cfg);
cfg.width = 96;
cfg.height = 48;
hal_inject(hal_vbuf_create(&cfg));
```

两种把画面交出去的方式：

```c
/* 1. 回调：每次 hal_canvas_update() 结束时被调用 */
static void my_flush(void *user, const uint8_t *buffer, int w, int h) {
  ssd1306_write_pages(buffer, (size_t) w * h / 8);   /* 你的传输层 */
}
hal_vbuf_set_flush(my_flush, NULL);

/* 2. 主动取：适合放到自己的刷新任务里 */
const uint8_t *buf = hal_vbuf_canvas();
int w = hal_vbuf_width();
int h = hal_vbuf_height();
```

### 14.4 时间与输入

后端没有定时器，时间由宿主注入：

```c
hal_vbuf_advance(16);                  /* 每帧推进 16 ms 虚拟时钟 */
```

`delay_ms()` 在这里**不阻塞**，只推进虚拟时钟，所以 headless 跑 N 帧是瞬时的。
真实节奏由 `hal_vbuf_advance()` 的调用频率决定。

输入**不经过 HAL**（第 9 章）：直接用应用自己的输入层即可。

```c
astra_demo_key_set(ASTRA_DEMO_KEY_NEXT, true);   /* 按下 */
astra_demo_key_set(ASTRA_DEMO_KEY_NEXT, false);  /* 松开 */
```

### 14.5 静态存储（MCU）

```c
static hal_vbuf  s_vbuf;
static uint8_t   s_canvas[128 * 64 / 8];      /* 1024 B */

hal_vbuf_config cfg;
hal_vbuf_config_default(&cfg);
hal_vbuf_init_static(&s_vbuf, &cfg, s_canvas, sizeof(s_canvas));
hal_inject(&s_vbuf.base);

/* ... */

hal_vbuf_deinit(&s_vbuf);   /* 只清内部状态，s_canvas 仍然是你的 */
```

`hal_vbuf_init_static()` 会在数组太小时返回 `NULL`，不会越界。

### 14.6 字体

`include/hal_vbuf.h` 导出三个句柄（`hal_vbuf_font_main` / `..._logo_title` /
`..._logo_copyright`），它们都指向同一套**内置 6×12 ASCII 点阵**（0x20–0x7E）：

- `hal_get_font_height()` 恒为 12
- `hal_get_font_width()` 为 `字符数 × 6`
- 非 ASCII 码点（中文等）绘制为 6×12 空心框，表示"缺字形"

需要中文时，请实现自己的后端（框架只把字符串原样传给后端）。

### 14.7 尺寸规则

| 规则 | 说明 |
|---|---|
| `width`/`height` 必须是 8 的倍数 | 页对齐 |
| 0 表示"用默认值" | 宽 0 → 128，高 0 → 64 |
| 负值或非 8 倍数会被拒绝 | `hal_vbuf_create()` 返回 `NULL` |
| 上限 2040 | 保证 tile 数能放进 `uint8_t` 契约 |

### 14.8 与硬件后端的行为差异

| 项 | `hal_vbuf` | 真实硬件后端 |
|---|---|---|
| `canvas_update` | 调用 flush 回调 | 把缓冲区发到面板 |
| `delay_ms` | 推进虚拟时钟 | 真阻塞 |
| `millis` | 虚拟时钟 | 硬件 tick |
| 按键 | 与应用层无关 | 与应用层无关（两者都不在 HAL 里） |
| 字体 | 内置 6×12 点阵 | 由你决定（TTF 栅格化、字库芯片…） |
| 内存 | 画布 1 KiB（128×64） | 通常还要一份面板镜像 |

自测：`tests/test_vbuf.c` 覆盖 14 项，包括页布局、三种绘制模式、裁剪、
刷新回调、虚拟时钟、静态存储与销毁顺序。

---

## 15. 测试后端

框架核心不依赖显示器，所以可以在 PC 上用"计数后端"跑单元测试。
`tests/test_hal.c`（本工程自带）就是这么做的，可直接参考：

```c
typedef struct { hal base; int init_calls; int deinit_calls; uint8_t buffer[1024]; } fake_hal;

static hal_ops make_fake_ops(void) {
  hal_ops ops = astra_hal_default_ops;     /* 关键：从完整默认表派生 */
  ops.init = fake_init;
  ops.deinit = fake_deinit;
  ops.get_canvas_buffer = fake_canvas;
  ops.get_buffer_tile_width = fake_tile_w;
  ops.get_buffer_tile_height = fake_tile_h;
  ops.get_font_width = fake_font_width;
  ops.get_font_height = fake_font_height;
  return ops;
}

fake_hal f;
memset(&f, 0, sizeof(f));
static hal_ops s_ops; s_ops = make_fake_ops();
hal_construct(&f.base, &s_ops);
hal_inject(&f.base);

/* 此时可以驱动 menu / selector / launcher，检查状态而不需要屏幕 */
```

`fake_font_width` 返回有意义的宽度（例如 `len * 6`），否则选择框宽度会算成 0，
测试结果失真。

---

## 16. 性能与资源

### 内存

| 项 | 大小 |
|---|---|
| 128×64 帧缓冲 | 1024 B |
| 每帧绘制调用 | 无分配 |
| 后端结构体 | 约 1–2 KB（桌面后端因字形缓存较大） |

### 每帧绘制量

一帧大约是：

| 元素 | 调用次数 |
|---|---|
| 磁贴图标 | 1 `draw_bmp` / 项（30×30 = 900 像素） |
| 列表文字 | 1 `draw_chinese` / 行 |
| 选择框 | 8 `draw_h/v_line` + 2 `draw_pixel`（磁贴）或 1 `draw_rbox`（列表） |
| 进度条 / 箭头 / 虚线 | 约 15 次 |

在 STM32F103（72 MHz）上，纯 GPIO 位操作即可跑到 30+ FPS；
瓶颈通常在 `canvas_update()` 的 I2C 传输（1024 B @ 400 kHz ≈ 23 ms）。

### 优化建议

1. **`canvas_update()` 只发变化页**：记录脏页范围，只传改动的 page。
2. **I2C 用 DMA**：把 23 ms 的阻塞传输变成后台进行。
3. **`draw_box` 按 page 整字节写**：大于 8 像素宽的矩形可整字节填充，
   比逐像素快约 8 倍。
4. **字号只保留实际用到的**：`get_font_width` 每次调用都会遍历字符串，
   列表选择框每帧都会调用它——如果性能敏感，可在菜单里缓存宽度。

---

## 17. 检查清单

实现新后端时逐条确认：

**必须**

- [ ] `hal` 是结构体第一个成员
- [ ] ops 表从 `astra_hal_default_ops` 派生，且存放在静态区
- [ ] `ops` 里没有 NULL（派生方式天然满足）
- [ ] 缓冲区布局为 1bpp page 格式，大小 `tile_w * tile_h * 8`
- [ ] `get_buffer_tile_width/height` 返回**字节数**
- [ ] 绘制函数自行裁剪越界坐标
- [ ] `set_draw_type` 的三种模式都实现（clear / solid / invert）
- [ ] 文字按**左下角**定位
- [ ] `draw_bmp` 遵循 MSB 在左、1 = 点亮
- [ ] `get_font_width` 返回真实像素宽度（不能恒为 0）
- [ ] `delay_ms` 真阻塞；`millis` 单调递增（允许回绕）
- [ ] `deinit` 与 `init` 对称，能处理部分初始化
- [ ] 静态实例用 `hal_construct`，堆实例用 `hal_construct_owned`
- [ ] 没有往 `hal_ops` 里塞按键/输入（输入属于应用层）

**建议**

- [ ] 虚线保持"每 8 像素亮 5"的节奏
- [ ] 圆角用距离场判断，与内置视觉一致
- [ ] `canvas_update` 支持脏页/局部刷新
- [ ] 提供计数型 fake 后端做单元测试
- [ ] `get_random_seed` 接入硬件 RNG（否则每次开机 Logo 星星一样）

**常见故障对照**

| 现象 | 检查 |
|---|---|
| 画面按 8 行重复 | 缓冲区索引用错（必须是 `(y * width + x) / 8`） |
| 全黑 | `hal_inject` 未调用；`screen_weight/height` 为 0 |
| 文字不出现 | `get_font_width` 返回 0，或字体句柄未绑定 |
| 文字上下颠倒／偏一行 | 忘了把左下角转成左上角（`y - font_height`） |
| 图标是实心块 | `draw_bmp` 的 1/0 约定反了 |
| 选择框宽度为 0 | `get_font_width` 未实现 |
| 一调用动画就崩溃 | ops 表里有 NULL（未从默认表派生） |
| 退出时崩溃 | `CloseWindow()` 在 `hal_destroy()` 之前调用 |
| 静态后端退出崩溃 | 用了 `hal_construct_owned` |
