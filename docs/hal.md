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
- [2. 面板选择](#2-面板选择)
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

## 2. 面板选择

面板尺寸是**编译期常量**，由 `include/astra_panel.h` 里的 `ASTRA_PANEL` 决定。
框架、内置字体、缓冲区和示例图标都为同一块面板编译，换屏 = 换一次编译参数。

### 2.1 为什么不是运行期

运行期 `width/height` 看起来更灵活，但真正决定观感的是**排版常量**：行高、边距、
选择框留白、图标放大倍数。这些数字没法可靠地"按比例缩放"出来——
`round(16 * 0.5) = 8` 和手工挑的 10 差两像素，在 1bpp 屏上就是"字压线"。
所以每种面板配一套手调数值，代码完全相同，数字不同。

### 2.2 四个内置面板

| 宏 | 分辨率 | 画布 | 内置字体 | 磁贴页 | 图标放大 |
|---|---|---|---|---|---|
| `ASTRA_PANEL_128x64` | 128×64 | 1024 B | 6×12 | 有 | ×1 |
| `ASTRA_PANEL_128x32` | 128×32 | 512 B | 5×8 | 无（太矮） | ×1 |
| `ASTRA_PANEL_128x128` | 128×128 | 2048 B | 8×16 | 有 | ×2 |
| `ASTRA_PANEL_240x240` | 240×240 | 7200 B | 12×24 | 有 | ×4 |

`astra_panel.h` 为每种面板定义这些宏，其它文件只引用宏、不写死数字：

| 宏 | 含义 |
|---|---|
| `ASTRA_PANEL_WIDTH` / `ASTRA_PANEL_HEIGHT` | 面板尺寸 |
| `ASTRA_PANEL_NAME` | 字符串 "128x64" 等，日志用 |
| `ASTRA_PANEL_TILE_W` / `ASTRA_PANEL_TILE_H` | `宽/8`、`高/8`，画布页数 |
| `ASTRA_PANEL_CANVAS_BYTES` | 1bpp 画布字节数 |
| `ASTRA_FONT_TITLE_PX` / `_BODY_PX` / `_SMALL_PX` | 后端 TTF 目标字号 |
| `ASTRA_VBUF_FONT_SUFFIX` | 选用哪张内置位图字体表 |
| `ASTRA_VBUF_FONT_W` / `_H` / `_ASCENT` | 位图字体的字宽、字高、基线到字顶 |
| `ASTRA_PANEL_HAS_TILES` | 面板够不够高放"图标 + 标题" |
| `ASTRA_PANEL_ICON_SCALE` | 示例磁贴图标的整数放大倍数 |

### 2.3 怎么选

CMake：

```bash
cmake -S . -B build -DASTRA_PANEL=ASTRA_PANEL_240x240
```

命令行：

```bash
cl /DASTRA_PANEL=ASTRA_PANEL_128x32 ...
```

不传就用 `ASTRA_PANEL_128x64`。传了未定义的取值会触发 `#error`；
尺寸不是 8 的倍数也会触发 `#error`（1bpp page 格式要求）。

### 2.4 每种面板的排版策略

`astra_config_default()` 按 `ASTRA_PANEL` 分支返回一整套数值。共同的设计约束：

| 顺序（从屏幕顶往下） | 约束 |
|---|---|
| 顶部进度条 `tile_bar_height` | 1~8 px，不能压住选择框 |
| 选择框 `tile_select_box_margin × 2 + tile_pic_height` | 底边必须高于虚线 |
| 虚线 `screen_height - tile_dotted_line_bottom_margin` | 必须低于选择框、高于标题墨迹 |
| 磁贴标题 `screen_height - tile_text_bottom_margin + tile_title_height` | 基线不出屏 |
| 左右箭头 `screen_height - tile_arrow_bottom_margin` | 与标题水平不重叠 |

列表页的约束更简单：`list_line_height >= 字高`，且 `screen_height / list_line_height >= 3`。

四种面板实测的排版骨架（由上到下）：

```
128x64   框 5..41     虚线 46   标题 48..60   箭头 56
128x128  框 15..85    虚线 94   标题 104..120 箭头 114
240x240  框 22..158   虚线 184  标题 206..230 箭头 220
128x32   （无磁贴页）  列表 3 行，行高 10
```

`tests/test_panel.c` 把这些不等式写成断言，改任何数字越界都会让测试失败。

### 2.5 后端侧的字号

raylib 后端的 TTF 字号也是按面板手调的，不再"由高度推导"：

| 面板 | 正文 | 标题 | 版权 |
|---|---|---|---|
| 128x64 | 12 | 14 | 8 |
| 128x32 | 10 | 12 | 8 |
| 128x128 | 16 | 20 | 12 |
| 240x240 | 20 | 28 | 16 |

窗口默认放大倍数同样是按面板挑的（6 / 6 / 4 / 2），让四种面板在桌面上的
物理大小接近。这些只影响 raylib 窗口；`hal_vbuf` 用内置位图字体，与 TTF 无关。

### 2.6 磁贴图标与面板

磁贴图标是固定素材（示例为 30×30）。`draw_bmp` 用 `tile_pic_width/height`
算行宽，所以放大的结果必须真的是一张那么大的位图：示例应用在 `init()` 里
用 `icon_scale_up()` 按 `ASTRA_PANEL_ICON_SCALE` 做最近邻整块放大，
×1 时退化成 `memcpy`。

`ASTRA_PANEL_HAS_TILES == 0` 的面板（128×32）装不下"图标 + 标题"，
示例应用直接把根菜单建成列表页，磁贴相关的 config 字段保留但不使用。

### 2.7 增加一种新面板

1. 在 `astra_panel.h` 里加一个 id，再加一个 `#elif` 分支，填满上面那张表的宏；
2. 在 `astra_config.c` 加对应的 `#if` 分支，按上面 2.4 的约束挑数字；
3. 内置位图字体能复用就复用（`ASTRA_VBUF_FONT_SUFFIX` 指向已有表），
   否则在 `hal_vbuf.c` 里加一张表；
4. raylib 后端加一组 `FONT_*` / `PANEL_PIXEL_SCALE`；
5. `cmake -DASTRA_PANEL=<新宏>` 编译并跑 `astra_test_panel`。

### 2.8 前端框架之外

- 同一份框架**目标代码**只服务一种尺寸；换屏要重新编译。
- `astra_test_hal` 不链接 raylib，也不关心具体尺寸。
- 分辨率不属于框架的"策略"：框架只消费 `hal_system_config()` 报告的几何，
  所以后端仍要在 `create` 时把宽高写进 `hal.config`（用面板宏即可）。

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
  void    (*set_font)(hal *self, astra_font_role role);
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
（见 astra_panel.h），可通过 `hal_system_config()` 读写。

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

128×64 时为 `16 * 8 * 8 = 1024` 字节；其余面板按 `ASTRA_PANEL_CANVAS_BYTES`：
128×32 → 512 B，128×128 → 2048 B，240×240 → 7200 B。

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

文字左下角基准的含义：`y` 传入的是**基线下沿**，字形的墨迹画在 `[y - height, y)` 这个
`height = get_font_height()` 的单元格里。所以绘制一行文字时，调用方会写
`y = row_top + font_height`；要把它垂直居中在行高为 `row_h` 的行里，则写
`y = row_top + (row_h + height) / 2`（框架的列表就是这么算的）。

**墨迹在单元格内应当大致垂直居中**：单元格高度里通常包含一小段上边距（top
bearing）和一段下伸部（descender）空间。若把墨迹贴着单元格顶部或底部画，
列表文字看起来就会一律偏高或偏低——即使框架的居中公式是对的。

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

框架对字体的认知仅限于"选哪个角色 + 宽高查询"：

```c
void    hal_set_font(astra_font_role role);     /* 选择字号角色 */
uint8_t hal_get_font_width(const char *text);   /* 该串在当前字体下的像素宽 */
uint8_t hal_get_font_height(void);              /* 当前字体的行高 */
```

### 6.1 三个角色

```c
typedef enum astra_font_role {
  ASTRA_FONT_TITLE = 0,  /* Logo 主标题，最大 */
  ASTRA_FONT_BODY,       /* 菜单正文与标题    */
  ASTRA_FONT_SMALL,      /* Logo 副标题，最小 */
} astra_font_role;
```

框架只会请求这三个角色之一，**不再传递任何后端私有句柄**。之所以用枚举而不是
不透明指针：句柄曾经要经 `astra_config` 三个字段中转（应用传给 init → 存进配置
→ 转发回后端），整条链路上框架从不解释它们，等于把后端的数据结构塞进框架的
配置里。现在语义在框架侧定义，映射方式在后端侧决定：

```c
/* 桌面后端：角色 → TTF 栅格化字号 */
static void rl_set_font(hal *self, astra_font_role role) {
  hal_raylib *r = (hal_raylib *) self;
  switch (role) {
    case ASTRA_FONT_TITLE: r->font_pixel_size = r->font_pixel_logo_title; break;
    case ASTRA_FONT_SMALL: r->font_pixel_size = r->font_pixel_logo_copyright; break;
    case ASTRA_FONT_BODY:
    default:               r->font_pixel_size = r->font_pixel_main; break;
  }
}

/* MCU 后端：角色 → 不同点阵表 */
static void my_set_font(hal *self, astra_font_role role) {
  my_hal *m = (my_hal *) self;
  switch (role) {
    case ASTRA_FONT_TITLE: m->glyphs = FONT_8X16; break;
    case ASTRA_FONT_SMALL: m->glyphs = FONT_5X8;  break;
    case ASTRA_FONT_BODY:
    default:               m->glyphs = FONT_6X12; break;
  }
}
```

后端可以让三个角色指向同一套字模（`hal_vbuf` 就是这样：它只有 6×12，
`set_font()` 记录请求后忽略）。

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

> `vbuf` 后端的 `print_info` 会用 `printf` 打到 stdout。裸机（`__arm__`）上默认关闭，
> 因为 Arm C 库的 `printf` 走半主机（`BKPT 0xAB`），没有调试器接管时表现为
> "halted due to breakpoint"。需要时用 `-DASTRA_VBUF_USE_PRINTF=1` 打开。


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
以及 `draw_english`（和 `draw_chinese`）。只有一套字模时，`set_font` 直接留空
（默认 no-op）即可；有多档字号时按 `astra_font_role` 切换。

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

字节数由面板决定：128×64 → 1024 B（8 page × 128 B），128×32 → 512 B，
128×128 → 2048 B，240×240 → 7200 B。页内每字节 8 个像素横向排布。

### 14.3 用法

```c
#include "hal_vbuf.h"

/* 堆：实例和画布一起分配，hal_destroy() 一并释放 */
hal_inject(hal_vbuf_create());   /* 几何来自 ASTRA_PANEL，无需参数 */
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
static uint8_t   s_canvas[ASTRA_PANEL_CANVAS_BYTES];   /* 1024 B @128x64 */

hal_vbuf_init_static(&s_vbuf, s_canvas, sizeof(s_canvas));
hal_inject(&s_vbuf.base);

/* ... */

hal_vbuf_deinit(&s_vbuf);   /* 只清内部状态，s_canvas 仍然是你的 */
```

`hal_vbuf_init_static()` 会在数组太小时返回 `NULL`，不会越界。

### 14.6 字体

这个后端带**四套内置 ASCII 点阵**（0x20–0x7E），编译期按面板选一套：

| 面板 | 字模 | 字宽 × 字高 | 基线到字顶 |
|---|---|---|---|
| 128x64 | 6×12 | 6 × 12 | 10 |
| 128x32 | 5×8 | 5 × 8 | 7 |
| 128x128 | 8×16 | 8 × 16 | 13 |
| 240x240 | 12×24 | 12 × 24 | 20 |

三个 `astra_font_role` 全部映射到同一套字模，`set_font()` 只记录请求、不改变输出：

- `hal_get_font_height()` 恒为 `ASTRA_VBUF_FONT_H`（与角色无关）
- `hal_get_font_width()` 为 `字符数 × ASTRA_VBUF_FONT_W`
- 非 ASCII 码点（中文等）绘制为等宽空心框，表示"缺字形"

"基线到字顶"（`ASTRA_VBUF_FONT_ASCENT`）比字高小 2~4 px，作用是把字形墨迹
垂直居中在字符格里——否则字形会贴着格子顶部，视觉上比同一行的选择框高。

所以这里**不需要任何字体句柄**，`astra_demo_init()` 也不收字体参数。
需要多档字号或中文时，请实现自己的后端（框架只把角色与字符串交给后端）。

### 14.7 尺寸规则

尺寸不再有运行时校验：宏在编译期就固定了，`astra_panel.h` 用 `#error` 挡住
非 8 倍数的取值。构造函数只剩一条规则——`hal_vbuf_init_static()` 传入的
数组必须 ≥ `ASTRA_PANEL_CANVAS_BYTES`，否则返回 `NULL`（不会越界写）。

### 14.8 与硬件后端的行为差异

| 项 | `hal_vbuf` | 真实硬件后端 |
|---|---|---|
| `canvas_update` | 调用 flush 回调 | 把缓冲区发到面板 |
| `delay_ms` | 推进虚拟时钟 | 真阻塞 |
| `millis` | 虚拟时钟 | 硬件 tick |
| 按键 | 与应用层无关 | 与应用层无关（两者都不在 HAL 里） |
| 字体 | 内置点阵（按面板选一套） | 由你决定（TTF 栅格化、字库芯片…） |
| 内存 | 画布 0.5 / 1 / 2 / 7 KiB | 通常还要一份面板镜像 |

自测：`tests/test_vbuf.c` 覆盖 12 项，包括面板几何、页布局、三种绘制模式、
裁剪、刷新回调、虚拟时钟、静态存储与销毁顺序。四种面板都跑过。

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

### 16.1 内存预算（实测）

Release `/O2`、MSVC x64 实测，`sizeof` 与 `.data/.bss` 段统计：

| 配置 | 静态 RAM | 说明 |
|---|---|---|
| 默认（池 32 / 子项 8 / 标题 24 / 星星 32 / print 8×48） | **6,925 B** | |
| F103 推荐（池 20 / 星星 16 / print 2×24） | **4,182 B** | |
| 极简（再 + 子项 4 / 标题 16） | **3,382 B** | |

单页面成本从 392 B 降到 184 B（默认）或 144 B（极简）。

| 项 | 默认 | 说明 |
|---|---|---|
| 菜单池 32 × `sizeof(astra_menu)` | 5,888 | `sizeof` = 184 |
| Logo 星星（`LOGO_STARS_MAX`） | 432 | 16 颗时 220 |
| `hal_default_print_info` 缓存 | 384 | 2×24 时 48 |
| `astra_ui_config()` 实例 | 200 | 无法裁剪 |
| 其它（单例、标志） | ~21 | |

**`sizeof(astra_menu)` ≈ 24 + 4×`ASTRA_MENU_MAX_CHILDREN` + `ASTRA_TITLE_MAX`**，
因为指针 4 字节、`float` 4 字节。应用侧的 `astra_selector`(48) 与
`astra_camera`(20) 由调用方提供存储，不在框架的 `.bss` 里。

以上数字**与面板无关**：`astra_config` 是 176 B、`astra_menu` 是 184 B，
换 `ASTRA_PANEL` 不改静态池。会随面板变化的只有画布（`hal_vbuf` 堆内或你的静态数组）：

| 面板 | 画布 | `sizeof(hal_vbuf)` |
|---|---|---|
| 128x64 | 1024 B | 72 B |
| 128x32 | 512 B | 72 B |
| 128x128 | 2048 B | 72 B |
| 240x240 | 7200 B | 72 B |

### 16.2 可裁剪的宏

全部可在构建时覆盖，无需改源码：

```bat
cmake -S Inc/astra -B build ^
      -DASTRA_MENU_POOL_SIZE=20 ^
      -DASTRA_MENU_MAX_CHILDREN=4 ^
      -DASTRA_TITLE_MAX=16 ^
      -DLOGO_STARS_MAX=16 ^
      -DASTRA_PRINT_LINES=2 -DASTRA_PRINT_COLS=24
```

| 宏 | 默认 | 影响 | 注意 |
|---|---|---|---|
| `ASTRA_MENU_POOL_SIZE` | 32 | 池槽数 × `sizeof(astra_menu)` | 不能小于实际建树页面数，否则 `astra_menu_create()` 返回 `NULL` |
| `ASTRA_MENU_MAX_CHILDREN` | 8 | 每页 4 B | 不能小于最宽页面的子项数 |
| `ASTRA_TITLE_MAX` | 24 | 每页 1 B | 超长标题会被 `snprintf` 截断 |
| `LOGO_STARS_MAX` | 32 | 每颗 12 B | `config.logo_star_num` 默认 16，设 16 即可 |
| `ASTRA_PANEL` | `ASTRA_PANEL_128x64` | 整套排版数值 + 内置字体 + 画布大小 | 只影响观感与画布，不改静态池 |
| `ASTRA_PRINT_LINES` / `_COLS` | 8 / 48 | 两者相乘 | 只在用 `hal_print_info()` 时有意义 |
| `hal_vbuf` 画布 | 按面板 | `w*h/8` | 用 `hal_vbuf_init_static()` 可完全静态 |

### 16.3 无堆

框架核心**不分配堆内存**。`hal_vbuf_create()` 是唯一会 `malloc` 的路径
（实例 72 B + 画布）；MCU 上用 `hal_vbuf_init_static()` 换成静态数组，
连 `malloc` 实现都可以不链接。

### 16.4 每帧绘制量

一帧大约是：

| 元素 | 调用次数 |
|---|---|
| 磁贴图标 | 1 `draw_bmp` / 项（30×30 = 900 像素，大面板放大后最多 120×120） |
| 列表文字 | 1 `draw_chinese` / 行 |
| 选择框 | 8 `draw_h/v_line` + 2 `draw_pixel`（磁贴）或 1 `draw_rbox`（列表） |
| 进度条 / 箭头 / 虚线 | 约 15 次 |

在 STM32F103（72 MHz）上，纯 GPIO 位操作即可跑到 30+ FPS；
瓶颈通常在 `canvas_update()` 的 I2C 传输（1024 B @ 400 kHz ≈ 23 ms）。

### 16.5 优化建议

1. **`canvas_update()` 只发变化页**：记录脏页范围，只传改动的 page。
2. **I2C 用 DMA**：把 23 ms 的阻塞传输变成后台进行。
3. **`draw_box` 按 page 整字节写**：大于 8 像素宽的矩形可整字节填充，
   比逐像素快约 8 倍。
4. **字号只保留实际用到的**：`get_font_width` 每次调用都会遍历字符串，
   列表选择框每帧都会调用它——如果性能敏感，可在菜单里缓存宽度。
5. **软浮点**：Cortex-M0/M3 没有 FPU，框架的 `float` 坐标与插值全部走
   `__aeabi_*` 软浮点，约占 1~3 KB Flash。想彻底避免需要把坐标改成定点
   （`int16_t`），属于框架级改动。

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
| 文字不出现 | `get_font_width` 返回 0，或 `set_font` 未实现 |
| 文字上下颠倒／偏一行 | 忘了把左下角转成左上角（`y - font_height`） |
| 图标是实心块 | `draw_bmp` 的 1/0 约定反了 |
| 选择框宽度为 0 | `get_font_width` 未实现 |
| 一调用动画就崩溃 | ops 表里有 NULL（未从默认表派生） |
| 退出时崩溃 | `CloseWindow()` 在 `hal_destroy()` 之前调用 |
| 静态后端退出崩溃 | 用了 `hal_construct_owned` |
