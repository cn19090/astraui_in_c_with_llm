# astra API 参考

面向应用开发者的接口说明。头文件位于本工程的 `include/`，全部为 C99。

> **来源**：本框架移植自 [astra UI](https://github.com/AstraThreshold/oled-ui-astra)
> （C++ 单色 OLED 菜单库）。下文所有 API 的名字与语义尽量与上游保持一致：
> `astra::menu` → `astra_menu`、`astra::selector` → `astra_selector`、
> `astra::camera` → `astra_camera`、`astra::launcher` → `astra_launcher`。
> 语言层面的差异（虚函数 → `hal_ops` 函数指针表、动态容器 → 静态池、
> 编译期分辨率 → 运行期配置）见 [../README.md](../README.md#与上游-astra-ui-的关系)。

- [1. 头文件与包含顺序](#1-头文件与包含顺序)
- [2. 数据类型](#2-数据类型)
- [3. 配置](#3-配置)
- [4. 菜单（页面）](#4-菜单页面)
- [5. 选择框](#5-选择框)
- [6. 相机](#6-相机)
- [7. 启动器](#7-启动器)
- [8. 动画基元](#8-动画基元)
- [9. 开机动画](#9-开机动画)
- [10. 示例应用层](#10-示例应用层)
- [11. 生命周期与内存](#11-生命周期与内存)
- [12. 约定与陷阱](#12-约定与陷阱)

---

## 1. 头文件与包含顺序

| 头文件 | 提供 | 依赖 |
|---|---|---|
| `astra_types.h` | 数据类型、容量宏 | — |
| `astra_config.h` | 配置结构与默认值 | `astra_types.h` |
| `hal.h` | HAL 接口（见 [hal.md](hal.md)） | `astra_types.h` |
| `hal_vbuf.h` | 内存后端（见 [hal.md](hal.md) 第 14 章） | `hal.h` |
| `item.h` | 菜单、选择框、相机、动画 | 上述三者 |
| `launcher.h` | 页面调度与弹窗 | `item.h` |
| `logo.h` | 开机动画 | `hal.h` |

以上全部位于本工程的 `include/`，**不包含任何平台头文件**。

框架之外的配套文件（可选使用，不属于框架本体）：

| 头文件 | 位置 | 说明 |
|---|---|---|
| `astra_demo.h` | `demo/` | 示例应用：菜单树 + 输入层，只依赖 HAL |
| `hal_raylib.h` | 外层演示工程的 `port/raylib/` | raylib 后端，唯一引用 raylib 的框架消费者 |

典型包含：

```c
#include "astra_config.h"
#include "hal.h"
#include "item.h"
#include "launcher.h"
```

---

## 2. 数据类型

### 2.1 `astra_vec2`

```c
typedef struct astra_vec2 { float x, y; } astra_vec2;
```

通用二维坐标／偏移。按值返回，可直接写 `astra_vec2 p = {...};`。

### 2.2 `astra_sys_config`

屏幕参数，由 HAL 持有：

```c
typedef struct astra_sys_config {
  uint16_t screen_weight;  /* 宽度像素（历史拼写，勿改） */
  uint16_t screen_height;  /* 高度像素 */
  float    screen_bright;  /* 亮度，默认 255 */
} astra_sys_config;
```

这两个值是**运行期数据**：框架每帧的布局、相机、弹窗都按它们计算，
所以后端换分辨率后无需改框架代码。类型是 `uint16_t`，
因此 128x64、256x64、64x48 等尺寸都只是"一个数值"。

**注意**：改它们必须同时让后端缓冲区匹配——正确做法是通过后端的配置结构
（如 `hal_raylib_config`）在创建时设定，见 [hal.md](hal.md#2-分辨率与配置)。

### 2.3 枚举

```c
typedef enum astra_page_type { ASTRA_PAGE_TILE = 0, ASTRA_PAGE_LIST } astra_page_type;
```

- `ASTRA_PAGE_TILE`：图标横排 + 横向滚动
- `ASTRA_PAGE_LIST`：文字竖排 + 纵向滚动

框架**不定义任何按键枚举**：输入不属于 HAL，由应用自己建模
（示例应用用的是 `astra_demo_key`，见第 10 章）。

### 2.4 容量宏

```c
#define ASTRA_MENU_POOL_SIZE     32   /* 可创建的页面对象上限 */
#define ASTRA_MENU_MAX_CHILDREN   8   /* 单页子项上限         */
#define ASTRA_TITLE_MAX          24   /* 标题字节数（含 '\0'）*/
```

三者都可在构建时用 `-D` 覆盖，是**唯一影响静态内存占用**的宏：

```bat
cmake -S Inc/astra -B build -DASTRA_MENU_POOL_SIZE=20 -DASTRA_MENU_MAX_CHILDREN=4
```

调小之前先算一下页面数：`sizeof(astra_menu)` ≈ `24 + 4×CHILDREN + TITLE_MAX` 字节，
乘以 `POOL_SIZE` 就是池的固定开销。详见 [hal.md](hal.md) 的内存预算章节。

---

## 3. 配置

```c
astra_config      astra_config_default(void);      /* 出厂默认值副本   */
astra_sys_config  astra_sys_config_default(void);  /* 屏幕默认值副本   */
astra_config     *astra_ui_config(void);           /* 全局唯一实例     */
void              astra_ui_config_reset(void);     /* 恢复默认         */
astra_sys_config *hal_system_config(void);         /* HAL 持有的屏幕配置 */

/* 按面板尺寸缩放整套排版参数（见下方"多分辨率"） */
void astra_config_fit_to_screen(astra_config *cfg, uint16_t width,
                                uint16_t height);
```

### 多分辨率

默认排版是按 128x64 调的。换屏时用一行调用即可按比例适配：

```c
astra_config *ui = astra_ui_config();
astra_config_fit_to_screen(ui, hal_system_config()->screen_weight,
                               hal_system_config()->screen_height);
```

规则：

- 缩放因子 `min(width/128, height/64)`，钳制到 `[0.4, 3.0]`
- 128x64 时**不改动任何字段**（参考尺寸像素级不变）
- 缩放行高、边距、选择框、虚线、圆角、Logo 尺寸
- **不缩放 `tile_pic_width/height`**（那是位图素材尺寸，改了会读错位图行宽）
- 必须在 `astra_menu_create*()` **之前**调用：子项目标坐标在 `add_item()` 时算出

字号不归这里管，它由后端决定（`hal_get_font_height()`）。
若后端字号与 `list_text_height` 不一致，请自行同步，否则列表文字会裁切：

```c
ui->list_text_height = (float) hal_get_font_height();
if (ui->list_line_height < ui->list_text_height + ui->list_text_margin)
    ui->list_line_height = ui->list_text_height + ui->list_text_margin;
```

`astra_menu_create*()` 会用**当前**配置初始化页面，所以校准后再建菜单即可生效。


### `astra_config` 字段

#### 动画速度（值越大越快，衰减因子为 `100 - speed`）

| 字段 | 默认 | 作用于 |
|---|---|---|
| `tile_animation_speed` | 80 | 磁贴图标横向入场、进度条、虚线、箭头 |
| `list_animation_speed` | 65 | 列表行纵向入场、滚动条 |
| `selector_x_animation_speed` | 70 | 选择框水平移动 |
| `selector_y_animation_speed` | 52 | 选择框垂直移动、磁贴标题升降 |
| `selector_width_animation_speed` | 70 | 选择框宽度 |
| `selector_height_animation_speed` | 55 | 选择框高度 |
| `camera_animation_speed` | 80 | 相机（滚动）平移 |
| `logo_animation_speed` | 70 | 开机动画 |
| `pop_speed` | 90 | 弹窗滑动 |
| `fade_animation_speed` | 100 | 页面退场淡出延时 |
| `window_animation_speed` / `side_bar_animation_speed` | 25 / 15 | 预留 |

> `logo_animation_speed` 在示例中被设为 88，因为 Logo 以"帧"计时而非毫秒。

#### 开关

| 字段 | 默认 | 说明 |
|---|---|---|
| `tile_unfold` / `list_unfold` | true | 页面进入时子项是否展开动画 |
| `tile_loop` / `menu_loop` | true | 选择是否首尾回绕（由应用决定是否使用） |
| `light_mode` | false | true 时列表页整屏反色 |
| `background_blur` | true | 预留 |

#### 列表布局

| 字段 | 默认 | 说明 |
|---|---|---|
| `list_line_height` | 16 | 行高，决定每屏行数 |
| `list_text_height` | 8 | 文字高度 |
| `list_text_margin` | 4 | 文字左边距 |
| `list_bar_weight` | 5 | 右侧滚动条宽度 |
| `selector_margin` | 4 | 选择框与文字左边距 |
| `selector_radius` | 0.5 | 选择框圆角 |
| `list_page_turning_mode` | 1 | `0` 整页翻页，`1` 逐行滚动 |

#### 磁贴布局

| 字段 | 默认 | 说明 |
|---|---|---|
| `tile_pic_width` / `tile_pic_height` | 30 / 30 | 图标尺寸 |
| `tile_pic_margin` / `tile_pic_top_margin` | 8 / 8 | 图标间距 / 上边距 |
| `tile_bar_height` | 2 | 顶部进度条高度 |
| `tile_arrow_width` / `tile_arrow_margin` | 6 / 4 | 左右箭头 |
| `tile_arrow_bottom_margin` | 8 | 箭头距底 |
| `tile_dotted_line_bottom_margin` | 18 | 虚线距底 |
| `tile_text_bottom_margin` | 12 | 标题距底 |
| `tile_title_height` | 8 | 标题高度 |
| `tile_select_box_margin` | 3 | 选择框外扩 |
| `tile_select_box_line_length` | 5 | 选择框角线长度 |
| `tile_select_box_width/height` | 36 / 36 | **派生**：`margin*2 + pic` |
| `tile_btn_margin` | 16 | 底部按钮图标边距 |

#### 弹窗 / Logo

| 字段 | 默认 |
|---|---|
| `pop_margin` / `pop_radius` | 4 / 2 |
| `logo_star_length` / `logo_star_num` | 2 / 16 |
| `logo_text_height` / `logo_copy_right_height` | 14 / 8 |

字号不在配置里：框架只按 `astra_font_role`（`TITLE` / `BODY` / `SMALL`）请求，
具体像素大小由后端决定。详见 [hal.md](hal.md) 第 7 章。

### 配置的生效时机

框架**不缓存配置**：每次 render / 动画步都直接读 `astra_ui_config()` 与
`hal_system_config_or_default()`，因此：

- 运行时改 `astra_ui_config()` 在**下一帧**生效，无需通知；
- 例外是 `astra_menu_add_item()` 里的坐标：它在调用当时就把目标位置算好写进
  `position*_trg`，改布局后需要重建页面才会重新计算。

---

## 4. 菜单（页面）

一个 `astra_menu` 就是一个页面（也可以是一个叶子项）。

### 4.1 创建

```c
astra_menu *astra_menu_create(const char *title);
astra_menu *astra_menu_create_tile(const char *title, const uint8_t *pic);
```

页面用**当前**的 `astra_ui_config()` / `hal_system_config()` 初始化，
因此在校准配置（如 `astra_config_fit_to_screen`）之后创建就能生效。

| | `astra_menu_create` | `astra_menu_create_tile` |
|---|---|---|
| 类型 | `ASTRA_PAGE_LIST` | `ASTRA_PAGE_TILE` |
| 显示 | 标题文字 | 30×30 图标 |
| 用于 | 子菜单页、可执行项 | 主菜单磁贴 |

- 返回 `NULL` 表示池已耗尽。
- `title` 超过 `ASTRA_TITLE_MAX - 1` 会被截断（`snprintf`，永远以 `'\0'` 结尾）。
- `pic` 必须是 **1bpp、MSB 在左、1 = 点亮、行按字节对齐** 的位图，尺寸对应
  `tile_pic_width × tile_pic_height`（默认 30×30，即每行 4 字节）。
  `pic` 不复制，必须比页面活得久（通常用 `static const`）。

### 4.2 组织

```c
bool astra_menu_add_item(astra_menu *menu, astra_menu *child);
```

- 第一个子项的类型决定整页类型；之后再加入不同类型返回 `false`。
- 超过 `ASTRA_MENU_MAX_CHILDREN` 返回 `false`。
- 成功时自动把 `child->parent` 指向 `menu`。
- **`menu` 和 `child` 都必须来自 `astra_menu_create*()`**，不能是栈上对象。

### 4.3 查询

```c
uint8_t         astra_menu_item_count(const astra_menu *menu);   /* 子项数       */
astra_menu     *astra_menu_next(const astra_menu *menu);         /* 当前选中项   */
astra_menu     *astra_menu_preview(const astra_menu *menu);      /* 上一级页面   */
astra_position  astra_menu_item_position(const astra_menu *menu, uint8_t index);
```

`astra_menu_next()` 在页面为空或索引越界时返回 `NULL`（调用方必须判空）。

### 4.4 渲染

```c
void astra_menu_init(astra_menu *menu, astra_vec2 camera);    /* 进入页面 */
void astra_menu_render(astra_menu *menu, astra_vec2 camera);  /* 每帧     */
void astra_menu_deinit(astra_menu *menu);                     /* 离开页面 */
```

`camera` 传 `astra_camera_position(&cam)`。

- `init` 重置子项坐标到入场起点（受 `tile_unfold` / `list_unfold` 影响）。
- `render` **先绘制当前状态、再做插值**，因此视觉变化在下一帧体现。
  它不清屏也不提交，清屏与 `hal_canvas_update()` 由启动器负责。
- `deinit` 走退场动画（阻塞一帧）。

正常使用时这些由 `astra_launcher_*` 调用，不需要手动管理。

### 4.5 池

```c
void     astra_menu_pool_reset(void);   /* 归还所有页面 */
uint16_t astra_menu_pool_used(void);    /* 已使用槽数   */
```

`astra_menu_pool_reset()` 使所有旧指针失效，只能在所有页面都不再被引用时调用。

### 4.6 结构体字段（只读）

```c
struct astra_menu {
  astra_position   position;        /* 当前/目标坐标    */
  astra_fg_position position_fg;    /* 前景元素坐标     */
  char             title[ASTRA_TITLE_MAX];
  const uint8_t   *pic;
  astra_page_type  self_type;       /* 本页类型         */
  astra_page_type  child_type;      /* 子项类型         */
  astra_menu      *parent;
  astra_menu      *children[ASTRA_MENU_MAX_CHILDREN];
  uint8_t          child_count;
  uint8_t          select_index;    /* 当前选中索引，由选择框维护 */
};
```

页面**不缓存**屏幕几何与 UI 配置——每次 render/动画都直接读
`hal_system_config_or_default()` 与 `astra_ui_config()`。这是为了省 RAM：
早期版本每个页面内嵌一份 `astra_config`（200 B），32 个池槽就是 6.4 KB 的浪费。

副作用是好的：运行时改配置**下一帧就生效**，不需要任何 refresh 调用
（`astra_item_update_config()` 因此保留为空实现，仅为源码兼容）。

应用层通常只读 `select_index`、`child_count` 与应用自定义的判等。

---

## 5. 选择框

绘制高亮：磁贴页是四角 L 形框（反相），列表页是反相圆角矩形。

```c
typedef struct astra_selector {
  astra_menu *menu;
  float x, x_trg, y, y_trg;      /* 位置 */
  float w, w_trg, h, h_trg;      /* 尺寸 */
  float y_text, y_text_trg;      /* 磁贴页标题升降 */
} astra_selector;
```

```c
bool        astra_selector_inject(astra_selector *sel, astra_menu *menu);
void        astra_selector_go(astra_selector *sel, uint8_t index);
void        astra_selector_render(astra_selector *sel, astra_vec2 camera);
astra_vec2  astra_selector_position(const astra_selector *sel);  /* 返回目标位置 */
```

### `astra_selector_inject`

把选择框绑定到页面，并立即定位到 `menu->select_index`。
进入新页面后必须调用一次。

### `astra_selector_go`

移动到第 `index` 项：更新 `menu->select_index`，并设置新位置／尺寸的**目标值**，
由 `render` 逐帧插值。

- `index` 越界时直接返回，不改变任何状态；
- 这是"移动选择"的唯一入口，应用层不要直接写 `select_index`。

### `astra_selector_position`

返回**目标**位置（`x_trg, y_trg`），相机用它判断是否滚动。

`astra_selector` 可以放在栈上或静态区，但必须在 `astra_launcher_init()` 之前
以零初始化，之后由 `inject` 接管。

---

## 6. 相机

相机保存"滚动偏移的负值"：元素绝对坐标 + 相机坐标 = 屏幕坐标。

```c
typedef struct astra_camera {
  float x_init, y_init;   /* reset() 回到的位置 */
  float x, y;             /* 当前偏移（负值）   */
  bool  moving;
} astra_camera;
```

### 创建

```c
void astra_camera_init(astra_camera *cam);                 /* 起点 (0,0) */
void astra_camera_init_at(astra_camera *cam, float x, float y);
```

### 移动

```c
void astra_camera_go(astra_camera *cam, float x, float y);          /* 平滑 */
void astra_camera_go_direct(astra_camera *cam, float x, float y);   /* 瞬移 */
void astra_camera_go_horizontal(astra_camera *cam, float x);
void astra_camera_go_vertical(astra_camera *cam, float y);
void astra_camera_reset(astra_camera *cam);                        /* 回起点 */
```

`go*` 系列每帧调用一次即可；`moving` 在到位后自动置 `false`。

### 按内容自动定位

```c
void astra_camera_go_to_tile_item(astra_camera *cam, uint8_t index);
void astra_camera_go_to_list_item_page(astra_camera *cam, uint8_t index);
void astra_camera_go_to_list_item_rolling(astra_camera *cam, astra_vec2 pos);
void astra_camera_go_to_next_page_item(astra_camera *cam);
void astra_camera_go_to_preview_page_item(astra_camera *cam);
```

- `..._tile_item`：横向对齐到第 `index` 个磁贴
- `..._list_item_page`：整页翻页模式（`list_page_turning_mode = 0`）
- `..._list_item_rolling`：逐行滚动模式（`list_page_turning_mode = 1`，
  传 `astra_selector_position()`）

### 每帧更新

```c
void    astra_camera_update(astra_camera *cam, astra_menu *menu, astra_selector *sel);
bool    astra_camera_is_moving(const astra_camera *cam);
uint8_t astra_camera_out_of_view(astra_camera *cam, float x, float y);
astra_vec2 astra_camera_position(const astra_camera *cam);
```

`astra_camera_update()` 按当前页类型选择上面对应的定位方式，通常在
`astra_menu_render()` 之后、`hal_canvas_update()` 之前调用。

`astra_camera_out_of_view()` 返回 `0` 可见 / `1` 偏左上 / `2` 偏右下。

---

## 7. 启动器

启动器把"当前页面 + 选择框 + 相机"绑在一起，负责页面切换与每帧渲染。

```c
typedef struct astra_launcher {
  astra_menu     *current_page;
  astra_selector *selector;
  astra_camera   *camera;
  uint64_t        time;      /* 帧计数器，弹窗用 */
} astra_launcher;
```

### 初始化

```c
void astra_launcher_init(astra_launcher *l, astra_menu *root,
                         astra_selector *selector, astra_camera *camera);
```

`selector` 与 `camera` 由调用方提供存储（静态或栈，寿命需覆盖启动器）。
函数内部会初始化它们，无需事先手动 init。

### 每帧

```c
void astra_launcher_update(astra_launcher *l);
```

顺序固定：

1. `hal_canvas_clear()`
2. `astra_menu_render(current_page, camera)`
3. `astra_selector_render(selector, camera)`
4. `astra_camera_update(camera, current_page, selector)`
5. `hal_canvas_update()`
6. `time++`

**它不读取按键**——输入策略留给应用层（见 `demo/astra_demo.c`）。

### 页面切换

```c
bool astra_launcher_open(astra_launcher *l);    /* 进入当前选中项 */
bool astra_launcher_close(astra_launcher *l);   /* 返回上一级     */
```

- `open()`：选中项必须是**有子项**的页面，否则返回 `false` 并弹出提示。
- `close()`：根页面无处可返，返回 `false` 并弹出提示。
- 两者都会先跑当前页的退场动画，再移动指针并 `inject` 新页面的选择框。

### 查询

```c
astra_menu *astra_launcher_current_page(const astra_launcher *l);
```

### 弹窗

```c
void astra_launcher_pop_info(astra_launcher *l, const char *info, uint16_t ms);
```

从屏幕上方滑入、停留 `ms` 帧、再滑出。**阻塞**，内部跑自己的帧循环。
因为参数 `ms` 的单位是帧而非毫秒，60 FPS 下 `600` 约等于 10 秒。

---

## 8. 动画基元

```c
void astra_anim_step(float *pos, float trg, float speed);
```

一阶指数逼近：

```
if |pos - trg| < 0.15  ->  pos = trg          （吸附，保证有限时间收敛）
else                    ->  pos += (trg - pos) / (100 - speed)
```

- `speed` 取 `0..99`，越大越快；`>= 100` 会导致除零。
- 框架的所有动画都走这一个函数。
- 自定义控件也可以直接用它，得到和内置元素一致的动效手感。

```c
void astra_anim_entry(astra_menu *menu);   /* 预留，当前为空 */
void astra_anim_exit(astra_menu *menu);    /* 退场淡出，每次调用推进一步 */
void astra_anim_blur(astra_menu *menu);    /* 棋盘格模糊一帧，可多次叠加 */
```

`astra_anim_exit()` / `astra_anim_blur()` 直接操作 HAL 画布缓冲区，
必须在 `hal_canvas_clear()` 之后、`hal_canvas_update()` 之前调用。

---

## 9. 开机动画

```c
#include "logo.h"
void astra_draw_logo(uint16_t time_frames);
```

全屏遮罩自上而下、16 颗随机星点漂移、两行标题分别从上下进入，然后整体退场。

- `time_frames` 的单位是**帧**：60 FPS 下 `110` ≈ 1.8 秒。
- 阻塞，内部跑自己的帧循环。
- 可重复调用（结束后自动复位）。
- 内部依次请求 `ASTRA_FONT_TITLE` / `ASTRA_FONT_SMALL` / `ASTRA_FONT_BODY`，
  由后端把角色映射成实际字号。

---

## 10. 示例应用层（`demo/astra_demo`）

`demo/astra_demo.h` / `.c` 是一个完整但可替换的示例：4 个磁贴页 + 各自列表子页 +
输入层（两个逻辑键 + 短按/长按判定）。它**只依赖 HAL**，不含任何平台代码，
可以直接搬到别的后端上。

```c
bool astra_demo_init(void);                    /* 播动画 + 建菜单树 */
void astra_demo_update(void);                  /* 每帧：采样输入 + 渲染 */
void astra_demo_destroy(void);                 /* 只回收页面池      */
void astra_demo_key_set(astra_demo_key key, bool down);  /* 输入注入  */
void astra_demo_activate(void);                /* 进入选中项（可选）*/
void astra_demo_back(void);                    /* 返回上级（可选）  */
```

`astra_demo_init()` 不收任何参数：字号由后端按 `astra_font_role` 决定，
不需要应用在中间搬运。

### 前置条件

`astra_demo_init()` 要求 **HAL 已经注入**（`hal_inject()` 成功），并且菜单池是干净的。
它不创建也不销毁后端——那是 shell 的职责，因此 `astra_demo_destroy()` 不会
调用 `hal_destroy()`。

`astra_demo_update()` 必须在 `astra_demo_init()` 之后调用（它不重复检查 HAL）。

### 固定 128x64

这个示例按 **128x64** 手工排版，不做分辨率适配，初始化流程固定为：

1. `astra_ui_config_reset()` 回到出厂排版（避免反复 init 累积改动）
2. 设 `logo_animation_speed = 88`
3. 用 `hal_get_font_height()` 校正 `list_text_height`，并把
   `list_line_height` 设为 `字号 + 上边距`
4. 建根页面 + 4 个磁贴页（图标 30x30，128x64 放得下）

换屏时需要自己调 `astra_config` 的间距，或改用通用做法：

```c
astra_config_fit_to_screen(astra_ui_config(), sys->screen_weight, sys->screen_height);
```

该函数在 128x64 上是空操作（缩放系数为 1 时直接返回）。

### 内置输入策略

HAL 不提供输入（见 [hal.md](hal.md#9-输入不属于-hal)），示例应用自带这一层：

```c
typedef enum astra_demo_key {
  ASTRA_DEMO_KEY_PREV = 0,   /* 短按：上一项；长按：返回上级 */
  ASTRA_DEMO_KEY_NEXT,       /* 短按：下一项；长按：进入下级 */
  ASTRA_DEMO_KEY_NUM
} astra_demo_key;

void astra_demo_key_set(astra_demo_key key, bool down);   /* 电平语义 */
```

外壳每帧把平台输入翻译成两个电平，示例应用负责边沿检测与长按判定：

| 事件 | 动作 |
|---|---|
| `ASTRA_DEMO_KEY_PREV` 短按 | 上一项 |
| `ASTRA_DEMO_KEY_NEXT` 短按 | 下一项 |
| `ASTRA_DEMO_KEY_PREV` 长按（0.5 s） | 返回上级 |
| `ASTRA_DEMO_KEY_NEXT` 长按（0.5 s） | 进入选中项 |

长按阈值 `LONG_PRESS_MS = 500`，用 `hal_millis()` 计时，在**松开**时结算。

```c
/* 外壳一侧（raylib） */
astra_demo_key_set(ASTRA_DEMO_KEY_NEXT, IsKeyDown(KEY_RIGHT));
astra_demo_update();
```

`astra_demo_activate()` / `astra_demo_back()` 是给 shell 准备的额外入口，
绕过时长判定，用于有独立确认/返回键的平台
（示例 `main.c` 把它们绑到 `Enter` / `Backspace`）。

**如果你在写自己的应用，不要调用 `astra_demo_*`** —— 直接使用
`astra_menu_*` / `astra_launcher_*`，自己实现输入。
`demo/astra_demo.c` 可直接作为参考实现。

---

## 11. 生命周期与内存
### 分配点

| 资源 | 方式 |
|---|---|
| 页面对象 | 静态池 `ASTRA_MENU_POOL_SIZE` 个槽 |
| 选择框 / 相机 / 启动器 | 调用方提供存储 |
| 配置 | 单个静态实例 |
| 画布（后端内） | 按配置的宽高分配，`hal_destroy()` 时释放 |
| 后端实例 | 由后端 `*_create()` 决定（raylib 后端用 `calloc`） |

框架核心**不分配堆内存**。页面池是整体复用的，所以 `open()` / `close()`
不产生分配或释放。后端的画布与像素缓冲随分辨率变化，由后端自己管理。

### 推荐启动 / 关闭顺序

```c
int main(void) {
    /* 1. 选屏幕尺寸并开窗口（后端无关时换成你的 BSP 初始化） */
    hal_raylib_config dev;
    hal_raylib_config_default(&dev);
    dev.width  = 128;              /* 只改这两行就能换屏 */
    dev.height = 64;

    int ww, wh;
    hal_raylib_window_size(&dev, &ww, &wh);
    InitWindow(ww, wh, "astra");

    /* 2. 注入后端 —— 必须在任何 hal_* 调用之前 */
    hal_inject(hal_raylib_create_with(&dev));

    /* 3. 把排版适配到实际面板（可选；128x64 上是空操作） */
    astra_config_fit_to_screen(astra_ui_config(),
                               hal_system_config()->screen_weight,
                               hal_system_config()->screen_height);

    /* 4. 建菜单并启动 */
    astra_menu_pool_reset();
    astra_menu *root = astra_menu_create("root");
    /* ... add_item ... */
    astra_launcher_init(&launcher, root, &selector, &camera);

    /* 5. 主循环 */
    while (!WindowShouldClose()) {
        read_buttons_and_dispatch();  /* 你自己的输入策略 */
        astra_launcher_update(&launcher);
    }

    /* 6. 反向拆除：先框架，后平台 */
    astra_menu_pool_reset();
    hal_destroy();
    CloseWindow();               /* 必须在 hal_destroy() 之后 */
    return 0;
}
```

`CloseWindow()` 一定要放在 `hal_destroy()` 之后：后端的 `deinit` 需要活着的
图形上下文来释放纹理。

### 销毁顺序

```c
s_root = NULL;                              /* 丢弃池内对象的入口 */
memset(&s_launcher, 0, sizeof(s_launcher)); /* 清掉悬垂指针      */
memset(&s_selector, 0, sizeof(s_selector));
memset(&s_camera,   0, sizeof(s_camera));
astra_menu_pool_reset();                    /* 归还池            */
hal_destroy();                              /* 释放后端          */
```

`hal_destroy()` 是幂等的，重复调用安全。

---

## 12. 约定与陷阱

### 坐标系

| 对象 | 原点 | 说明 |
|---|---|---|
| 元素 `position` | 左上角 | 磁贴图标、列表行 |
| 文字 `draw_*` | **左下角** | 所以绘制时高度要加 `y + font_height` |
| 相机 `x, y` | 负偏移 | `屏幕坐标 = 元素坐标 + 相机坐标` |

### 常见错误

| 现象 | 原因 |
|---|---|
| 屏幕全黑 | 后端没 `hal_inject`，或 `screen_weight/height` 为 0 |
| 文字不显示 | 后端未实现 `get_font_width`，或 `set_font` 里漏了某个角色 |
| 图标是实心方块 | 位图约定反了（必须是 1 = 点亮） |
| 菜单项丢失 | `astra_menu_create()` 或 `add_item()` 返回 `NULL` / `false`，未检查 |
| 动画瞬间到位 | `speed` 设得过大（`>= 100` 会除零） |
| 配置改了没反应 | 布局计算发生在 `add_item()` 时，需重建页面 |
| 页面切换后高亮错位 | 忘记 `astra_selector_inject()` |

### 类型混用

同一父页面的子项**必须同类型**。如果想在一个页面里混合图标和文字，
请把它们拆到两级页面，或自行扩展 `astra_menu_render()`。

### 线程

框架不是线程安全的。所有 `hal_*` / `astra_*` 调用都应在同一个线程中完成。
