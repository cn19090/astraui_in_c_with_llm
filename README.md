# astra

一个用 **C99** 编写的单色 OLED 菜单框架，面向 128×64（以及其它 1bpp 尺寸）的小屏设备。

> **来源**
> 本工程是 [astra UI](https://github.com/AstraThreshold/oled-ui-astra) 的 **C99 移植版本**。
> 上游本体仓库：<https://github.com/AstraThreshold/oled-ui-astra>（C++，作者 dcfsswindy
> 及社区贡献者）。界面形态、页面结构、动画参数与视觉比例均以上游为准；本工程保留
> 同一套 API 语义，把 C++ 的类继承与虚函数改写为 C 的函数指针表（`hal_ops`）与扁平化
> 结构体。面板尺寸用编译期宏 `ASTRA_PANEL` 选定，每种常见尺寸配一套手调排版。

框架本身**不含一行平台代码**：所有绘制与计时都经一张可替换的函数指针表
（HAL）转发。因此同一份框架代码可以跑在 SSD1306、raylib 窗口、SDL 或纯内存
测试桩上，无需改动任何一行。**用户输入不在 HAL 里**——按键、旋钮、触摸由
应用自己实现。

这是一个**独立工程**：自带构建脚本、示例应用、自测和文档，可以整体拷进你的项目，
也可以单独构建验证。

```
┌──────────────────────────────────────────────────────────┐
│  你的应用            menu tree, 输入层, 页面跳转          │
├──────────────────────────────────────────────────────────┤
│  launcher            页面调度 · 弹窗                      │
│  item                menu / selector / camera / 动画      │
├──────────────────────────────────────────────────────────┤
│  hal (ops 表)        绘制 · 计时 · 屏幕开关               │
├──────────────────────────────────────────────────────────┤
│  后端                由你实现：SSD1306 · raylib · 任意平台 │
└──────────────────────────────────────────────────────────┘
```

## 特性

- **纯 C99**，除后端外无外部依赖（只用 `<stdint.h>` `<stdbool.h>` `<string.h>` `<math.h>`）
- **自带两种后端**：`hal_vbuf`（纯内存，零依赖，你负责刷新）与你自己实现的硬件后端
- **面板在编译期选定**：`ASTRA_PANEL` 一个宏同时决定尺寸、字体与整套排版数值
- **零堆分配**：界面对象全部来自固定大小的静态池
- **两种页面**：磁贴页（图标 + 横向滚动）与列表页（文字 + 纵向滚动）
- **内建动画**：页面进出、选择框补间、相机平滑滚动、开机 Logo
- **后端可替换**：实现一张函数指针表即可换平台，框架无需重新编译
- **可测试**：核心逻辑不依赖显示器，自带 headless 自测（31 项，不链接任何图形库）

## 目录结构

```
Inc/astra/                      ← 本工程根目录
├── README.md                   本文件
├── CMakeLists.txt              独立构建脚本
├── include/                    公开头文件（应用只需要包含这些）
│   ├── astra_types.h           数据类型与容量宏
│   ├── astra_config.h          配置结构体与默认值
│   ├── hal.h                   硬件抽象层（ops 表 + 单例 + 内联转发）
│   ├── hal_vbuf.h              内存后端：所有操作只写像素缓冲区
│   ├── item.h                  menu / selector / camera / 动画
│   ├── launcher.h              页面调度与弹窗
│   └── logo.h                  开机动画
├── src/                        实现（不对外暴露）
│   ├── astra_config.c
│   ├── hal.c
│   ├── hal_vbuf.c              内存后端实现 + 内置 6x12 ASCII 字模
│   ├── item.c
│   ├── launcher.c
│   └── logo.c
├── demo/                       示例应用（只依赖 HAL，与平台无关）
│   ├── astra_demo.h
│   └── astra_demo.c            4 个磁贴页 + 列表子页 + 输入层
├── docs/
│   ├── api.md                  应用层 API 参考
│   └── hal.md                  硬件抽象层详解与后端适配指南
└── tests/
    ├── test_hal.c              框架自测（16 项，不需要窗口）
    └── test_vbuf.c             内存后端自测（15 项）
```

依赖方向严格单向，框架位于最底层：

```
  你的应用 / demo ──▶ include/hal.h ──▶ src/*.c
                            ▲
       后端（你实现）────────┘
```

`include/` 里**没有任何**平台头文件；`src/` 只 include 自己的头文件。这就是框架
可以脱离具体显示器件存在的证明。

## 独立构建

不需要任何第三方依赖，只要能编 C99：

```bat
cmake -S Inc/astra -B Inc/astra/build
cmake --build Inc/astra/build --config Debug
Inc\astra\build\Debug\astra_test_hal.exe
```

产出三个目标：

| 目标 | 内容 |
|---|---|
| `astra` | 框架静态库 + 内存后端（零平台依赖） |
| `astra_demo` | 示例应用静态库，用于证明 demo 只依赖框架公开头文件 |
| `astra_test_hal` | 框架自测，16 项，不开窗口、不链接图形库 |
| `astra_test_vbuf` | 内存后端自测，14 项，校验画布布局、绘制、刷新回调、虚拟时钟 |

也可以完全不用 CMake，把 `src/*.c` 加入你的工程、把 `include/` 加进 include 路径即可。

## 接入你的工程

三种接法，按依赖从少到多：

### A. 用内置的内存后端（零依赖，最快上手）

`hal_vbuf` 把全部操作写进一块 1bpp 页缓冲区，不碰任何硬件。你只需要在
`hal_canvas_update()` 之后把缓冲区推给真实面板：

```c
#include "hal_vbuf.h"

static hal_vbuf s_vbuf;                  /* 实例 + 画布都在 .bss */
hal_inject(hal_vbuf_init(&s_vbuf));      /* 不分配任何内存 */
hal_vbuf_set_flush(my_flush, &my_bus);   /* 每帧回调，可选 */

/* ... 运行 UI ... */

/* my_flush() 收到的是 SSD1306 GDDRAM 布局，直接按页发即可 */
```

没有回调也可以主动取：

```c
const uint8_t *buf = hal_vbuf_canvas();  /* (y * width + x) / 8 */
int w = hal_vbuf_width();
int h = hal_vbuf_height();
```

其它要点：

- 按键不经过 HAL：用应用自己的输入层（示例是 `astra_demo_key_set()`）
- 时间用 `hal_vbuf_advance(16)` 每帧推进；`hal_delay()` 不阻塞，只推进虚拟时钟
- **零动态分配**：画布内嵌在实例里，实例由你提供，所以裸机可以不链接堆；
  结束时 `hal_vbuf_deinit()` 只清状态，不释放任何东西
- 内置 ASCII 字模（按面板选 5x8 / 6x12 / 8x16 / 12x24），非 ASCII 码点画成空心框

### B. 自己实现一个后端

后端要做的是：填一张 `hal_ops` 函数指针表（从 `astra_hal_default_ops` 派生，
避免漏填成员导致空指针），然后调用 `hal_construct()` 或 `hal_construct_owned()`
并 `hal_inject()`。

```c
static const hal_ops my_ops = {
    .type = my_type,
    .init = my_init,
    .deinit = my_deinit,
    /* ... 其余全部从 astra_hal_default_ops 拷贝后覆盖 ... */
};
```

**1. 初始化框架并建菜单**

```c
hal_inject(backend);              /* 后端已构造完成 */

astra_menu_pool_reset();
astra_menu *root = astra_menu_create("root");
astra_menu *page = astra_menu_create("Settings");
astra_menu_add_item(page, astra_menu_create("Language"));
astra_menu_add_item(page, astra_menu_create("Storage"));
astra_menu_add_item(root, page);

astra_launcher_init(&launcher, root, &selector, &camera);
```

**2. 每帧调用一次渲染**

```c
astra_launcher_update(&launcher);   /* 清屏 → 画页面 → 画选择器 → 推进相机 → 送出显存 */
```

开机动画是可选的：在 `astra_launcher_init()` 之前调用 `astra_draw_logo(110)`。

### C. 桌面预览

外层仓库的 `port/raylib/` 是一个把画布画进 raylib 窗口的后端，用宏切换：
`-DASTRA_USE_VBUF=ON` 走内存后端，OFF 走 raylib 窗口。

## 示例应用

[`demo/astra_demo.c`](demo/astra_demo.c) 是一个可直接运行的完整示例，也是本框架的
参考用法：

- 4 个磁贴页（System / Display / Network / About），各自带列表子页
- 自带输入层：两个逻辑键，短按切换高亮项，长按进入 / 返回
- 固定 128×64 排版，每个步骤带中文注释

它只 include 框架的公开头文件，不含任何平台代码，因此可以直接搬到你的后端上。

接口：

```c
bool astra_demo_init(void);       /* 播动画 + 建菜单树 */
void astra_demo_update(void);     /* 一帧 */
void astra_demo_destroy(void);    /* 只回收对象池，不碰 HAL */
void astra_demo_key_set(astra_demo_key key, bool down);  /* 输入注入 */
void astra_demo_activate(void);   /* 可选：进入高亮项 */
void astra_demo_back(void);       /* 可选：返回上级   */
```

字号不需要应用参与：框架按 `astra_font_role`（`TITLE` / `BODY` / `SMALL`）
向后端请求，具体像素大小由后端决定。

### 输入由谁负责

HAL 不提供输入，示例应用自带一层最简输入：

```c
typedef enum astra_demo_key {
  ASTRA_DEMO_KEY_PREV = 0,   /* 短按：上一项；长按：返回上级 */
  ASTRA_DEMO_KEY_NEXT,       /* 短按：下一项；长按：进入下级 */
  ASTRA_DEMO_KEY_NUM
} astra_demo_key;
```

外壳每帧把平台输入翻译成两个电平塞进来，边沿检测和长按（500 ms）由示例
应用负责：

```c
astra_demo_key_set(ASTRA_DEMO_KEY_NEXT, IsKeyDown(KEY_RIGHT));   /* raylib */
astra_demo_key_set(ASTRA_DEMO_KEY_PREV, HAL_GPIO_ReadPin(...) == RESET);  /* GPIO */
astra_demo_update();
```

## 文档

| 文档 | 内容 |
|---|---|
| [`docs/api.md`](docs/api.md) | 应用层 API：menu / selector / camera / launcher / 配置项 / 示例应用 |
| [`docs/hal.md`](docs/hal.md) | 硬件抽象层：39 个 ops 成员逐个说明、生命周期、内存归属、内存后端、两个完整硬件后端实现、上板检查清单 |

`docs/hal.md` 里带有 SSD1306（I²C/SPI 位操作）与裸机静态后端的完整代码，可以
直接作为移植起点。

## 面板选择与布局

面板尺寸是**编译期常量**，由 `include/astra_panel.h` 的 `ASTRA_PANEL` 选定：

| 宏 | 分辨率 | 画布 | 内置字体 | 磁贴页 |
|---|---|---|---|---|
| `ASTRA_PANEL_128x64` | 128×64 | 1024 B | 6×12 | 有 |
| `ASTRA_PANEL_128x32` | 128×32 | 512 B | 5×8 | 无（太矮） |
| `ASTRA_PANEL_128x128` | 128×128 | 2048 B | 8×16 | 有 |
| `ASTRA_PANEL_240x240` | 240×240 | 7200 B | 12×24 | 有 |

```bash
cmake -S Inc/astra -B build -DASTRA_PANEL=ASTRA_PANEL_128x32
```

每种面板在 `astra_config_default()` 里带一套手调数值（行高、边距、选择框、
虚线位置、Logo 尺寸、磁贴图标放大倍数），代码完全相同、数字不同。

为什么不做按比例缩放：行距和边距都是整数像素，`round(16 * 0.5) = 8` 与手工挑的
10 差两像素，在 1bpp 屏上就会"字压线"。所以每种面板配一套数字。

细节见 [`docs/hal.md`](docs/hal.md) 第 2 章「面板选择」。

## 容量与内存占用

五个宏决定静态内存（都在构建时可覆盖，无需改源码）：

```c
#define ASTRA_MENU_POOL_SIZE     32   /* 页面对象总数   */
#define ASTRA_MENU_MAX_CHILDREN   8   /* 单页最多子项   */
#define ASTRA_TITLE_MAX          24   /* 标题字节数     */
#define LOGO_STARS_MAX           32   /* 开机星星上限   */
#define ASTRA_PRINT_LINES/COLS  8/48  /* 日志缓存       */
```

实测静态 RAM（含 vbuf 后端，不含画布）：

| 配置 | 静态 RAM | 单页面 |
|---|---|---|
| 默认 | 6,925 B | 184 B |
| F103 推荐（池 20、星星 16、print 2×24） | 4,182 B | 184 B |
| 极简（再 + 子项 4、标题 16） | 3,382 B | 144 B |

```bat
cmake -S Inc/astra -B build -DASTRA_MENU_POOL_SIZE=20 -DLOGO_STARS_MAX=16
```

超出容量时 `astra_menu_create()` 返回 `NULL`、`astra_menu_add_item()` 返回 `false`，
不会静默截断或越界。内存占用在编译期完全确定（池是静态数组）。

页面**不缓存**配置：每次 render 直接读 `astra_ui_config()` 与
`hal_system_config_or_default()`。早期版本每页面内嵌一份 200 B 的
`astra_config`，32 槽就是 6.4 KB 浪费——这是当前版本最大的一处省内存改动。
详见 [`docs/hal.md`](docs/hal.md) 第 16 章「内存预算」。

## 动画节奏

框架**不使用 delta-time**，动画按帧推进。`astra_config` 里有 11 个
`*_animation_speed` 字段，语义是「每帧走完剩余距离的 `1/(100 - speed)`」，
取值必须小于 100：

| speed | 每帧比例 | 收敛帧数（距离 100） |
|---|---|---|
| 52 | 2.08% | 310 |
| 70 | 3.33% | 193 |
| 80 | 5.00% | 128 |
| 90 | 10.0% | 63 |
| 99 | 100% | 1（瞬时） |

因此刷新率决定了动画的绝对速度：同一份配置在 60 FPS 下比 30 FPS 快一倍。

例外：`fade_animation_speed` 的单位是**毫秒**（退场淡出每级的阻塞时长），不是
逼近比例；`astra_draw_logo(time_frames)` 和 `astra_launcher_pop_info(ms)` 的参数
单位是**帧**，不是毫秒。

## 设计要点

| 决策 | 原因 |
|---|---|
| 框架零平台依赖 | 全部绘制/计时经 HAL 转发；自测目标不链接图形库也能编译运行 |
| `ops` 表模拟虚函数 | C 里没有虚函数表，函数指针结构体是等效替代 |
| 后端结构体把 `hal base` 放第一个成员 | 依赖 C 的对象布局保证上下转型安全 |
| 面板在编译期选定 | 排版常量与位图字体都针对一块面板挑数字，避免缩放取整误差 |
| 固定容量池 | 无堆、无碎片、内存占用编译期已知 |
| 位置存"当前值 + 目标值"，每帧插值 | 动画与逻辑解耦，页面代码只描述目标布局 |
| 相机存负偏移 | 元素坐标保持"页面内绝对坐标"，滚动只是平移 |
| 渲染早于相机推进 | 移动中的相机滞后一帧，视觉上更平滑 |
| 只读配置副本 | 每次 render/animation 刷新，运行时改配置即时生效 |
| 单例 HAL | 框架各处都需要访问后端，避免把 `hal *` 逐层透传 |

## 已知约束

- 动画与帧率耦合（见上文「动画节奏」），需要精确控速时请在外层做帧率补偿
- `astra_config` 里 `window_animation_speed` 与 `side_bar_animation_speed`
  目前没有消费者，调它们不起作用
- 磁贴图标是固定尺寸素材，`tile_pic_width/height` 必须等于位图真实尺寸，
  否则 `draw_bmp` 会按错误的行宽解读数据

## 与上游 astra UI 的关系

本工程移植自 <https://github.com/AstraThreshold/oled-ui-astra>（C++）。对应关系：

| 上游（C++） | 本工程（C99） |
|---|---|
| `astra::hal` 抽象基类 + 派生后端 | `hal` 结构体 + `hal_ops` 函数指针表 |
| 虚函数调用 | `h->ops->draw_line(h, ...)` 间接调用 + `hal.h` 里的内联转发 |
| 构造函数里初始化成员 | `hal_construct()` / `hal_construct_owned()` + `astra_config_default()` |
| `std::vector` / 动态容器 | 固定容量静态池（32 个页面 × 8 项） |
| `sys::config` / `astra::config` | `astra_sys_config` / `astra_config` |
| 编译期固定的 128×64 | 运行期由后端报告的 `screen_weight/height` |
| 后端放在框架内 | 后端移出框架，只通过 `hal_ops` 接触框架 |
| 依赖 Arduino / U8g2 | 零平台依赖，只依赖 `<stdint.h>` `<stdbool.h>` `<string.h>` `<math.h>` |

行为与观感保持与上游一致（同样的磁贴/列表布局、同样的动画曲线、同样的开机 Logo
时间轴）；差异集中在语言特性替换、内存策略与分辨率解耦三处。

## 致谢

界面视觉设计（磁贴 / 列表 / 选择框 / 开机动画的动效与比例）、页面结构与交互模型
全部来自 [astra UI](https://github.com/AstraThreshold/oled-ui-astra) 项目，感谢原作者
dcfsswindy 与社区贡献者。本工程是同一套视觉语言的 C99 移植实现；若你要用 C++，
建议直接使用上游本体。

## 许可

MIT。
