# astra

一个用 **C99** 编写的单色 OLED 菜单框架，面向 128×64（以及其它 1bpp 尺寸）的小屏设备。

框架本身**不含一行平台代码**：所有绘制、计时、按键都经一张可替换的函数指针表
（HAL）转发。因此同一份框架代码可以跑在 SSD1306、raylib 窗口、SDL 或纯内存
测试桩上，无需改动任何一行。

这是一个**独立工程**：自带构建脚本、示例应用、自测和文档，可以整体拷进你的项目，
也可以单独构建验证。

```
┌──────────────────────────────────────────────────────────┐
│  你的应用            menu tree, 按键策略, 页面跳转        │
├──────────────────────────────────────────────────────────┤
│  launcher            页面调度 · 弹窗                      │
│  item                menu / selector / camera / 动画      │
├──────────────────────────────────────────────────────────┤
│  hal (ops 表)        绘制 · 计时 · 按键 · 屏幕开关         │
├──────────────────────────────────────────────────────────┤
│  后端                由你实现：SSD1306 · raylib · 任意平台 │
└──────────────────────────────────────────────────────────┘
```

## 特性

- **纯 C99**，除后端外无外部依赖（只用 `<stdint.h>` `<stdbool.h>` `<string.h>` `<math.h>`）
- **分辨率是运行期数据**：面板尺寸由后端在初始化时报告，框架据此自适应布局
- **零堆分配**：界面对象全部来自固定大小的静态池
- **两种页面**：磁贴页（图标 + 横向滚动）与列表页（文字 + 纵向滚动）
- **内建动画**：页面进出、选择框补间、相机平滑滚动、开机 Logo
- **后端可替换**：实现一张函数指针表即可换平台，框架无需重新编译
- **可测试**：核心逻辑不依赖显示器，自带 headless 自测（16 项，不链接任何图形库）

## 目录结构

```
Inc/astra/                      ← 本工程根目录
├── README.md                   本文件
├── CMakeLists.txt              独立构建脚本
├── include/                    公开头文件（应用只需要包含这些）
│   ├── astra_types.h           数据类型与容量宏
│   ├── astra_config.h          配置结构体与默认值
│   ├── hal.h                   硬件抽象层（ops 表 + 单例 + 内联转发）
│   ├── item.h                  menu / selector / camera / 动画
│   ├── launcher.h              页面调度与弹窗
│   └── logo.h                  开机动画
├── src/                        实现（不对外暴露）
│   ├── astra_config.c
│   ├── hal.c
│   ├── item.c
│   ├── launcher.c
│   └── logo.c
├── demo/                       示例应用（只依赖 HAL，与平台无关）
│   ├── astra_demo.h
│   └── astra_demo.c            4 个磁贴页 + 列表子页 + 按键策略
├── docs/
│   ├── api.md                  应用层 API 参考
│   └── hal.md                  硬件抽象层详解与后端适配指南
└── tests/
    └── test_hal.c              框架自测（16 项，不需要窗口）
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
| `astra` | 框架静态库（零平台依赖） |
| `astra_demo` | 示例应用静态库，用于证明 demo 只依赖框架公开头文件 |
| `astra_test_hal` | 框架自测，16 项，不开窗口、不链接图形库 |

也可以完全不用 CMake，把 `src/*.c` 加入你的工程、把 `include/` 加进 include 路径即可。

## 接入你的工程

三步：

**1. 实现一个后端**（见 [`docs/hal.md`](docs/hal.md)）

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

**2. 初始化框架并建菜单**

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

**3. 每帧调用一次渲染**

```c
astra_launcher_update(&launcher);   /* 清屏 → 画页面 → 画选择器 → 推进相机 → 送出显存 */
```

开机动画是可选的：在 `astra_launcher_init()` 之前调用 `astra_draw_logo(110)`。

## 示例应用

[`demo/astra_demo.c`](demo/astra_demo.c) 是一个可直接运行的完整示例，也是本框架的
参考用法：

- 4 个磁贴页（System / Display / Network / About），各自带列表子页
- 两个逻辑键：短按切换高亮项，长按进入 / 返回
- 固定 128×64 排版，每个步骤带中文注释

它只 include 框架的公开头文件，不含任何平台代码，因此可以直接搬到你的后端上。

接口：

```c
bool astra_demo_init(const uint8_t *main_font,
                     const uint8_t *logo_title_font,
                     const uint8_t *logo_copy_right_font);
void astra_demo_update(void);     /* 一帧 */
void astra_demo_destroy(void);    /* 只回收对象池，不碰 HAL */
void astra_demo_activate(void);   /* 可选：进入高亮项 */
void astra_demo_back(void);       /* 可选：返回上级   */
```

三个字体句柄由后端定义，框架只当作不透明 ID 原样传回后端。

## 文档

| 文档 | 内容 |
|---|---|
| [`docs/api.md`](docs/api.md) | 应用层 API：menu / selector / camera / launcher / 配置项 / 示例应用 |
| [`docs/hal.md`](docs/hal.md) | 硬件抽象层：39 个 ops 成员逐个说明、生命周期、内存归属、两个完整后端实现、上板检查清单 |

`docs/hal.md` 里带有 SSD1306（I²C/SPI 位操作）与裸机静态后端的完整代码，可以
直接作为移植起点。

## 分辨率与布局

框架从 `hal_system_config()` 读取后端报告的 `screen_weight` / `screen_height`，
所有布局参数集中在 `astra_ui_config()` 返回的 `astra_config` 里。

换一块不同尺寸的屏：

```c
/* 按面板尺寸等比缩放整套排版参数 */
astra_config_fit_to_screen(astra_ui_config(), sys->screen_weight, sys->screen_height);
```

该函数在 128×64 上是空操作（缩放系数为 1 时直接返回），因此可以无条件调用。
细节见 [`docs/hal.md`](docs/hal.md) 第 2 章「分辨率与配置」。

## 容量与裁剪

全部在 [`include/astra_types.h`](include/astra_types.h)：

```c
#define ASTRA_MENU_POOL_SIZE     32   /* 页面对象总数   */
#define ASTRA_MENU_MAX_CHILDREN   8   /* 单页最多子项   */
#define ASTRA_TITLE_MAX          24   /* 标题字节数     */
```

超出时 `astra_menu_create()` 返回 `NULL`、`astra_menu_add_item()` 返回 `false`，
不会静默截断或越界。内存占用在编译期完全确定（池是静态数组）。

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
| 框架零平台依赖 | 全部绘制/计时/按键经 HAL 转发；自测目标不链接图形库也能编译运行 |
| `ops` 表模拟虚函数 | C 里没有虚函数表，函数指针结构体是等效替代 |
| 后端结构体把 `hal base` 放第一个成员 | 依赖 C 的对象布局保证上下转型安全 |
| 分辨率是运行期数据 | 后端画布按配置分配；框架读 `hal_system_config()` 自适应布局 |
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

## 致谢

界面视觉设计（磁贴 / 列表 / 选择框 / 开机动画的动效与比例）来自
[astra UI](https://github.com/AstraThreshold/oled-ui-astra) 项目，感谢原作者
dcfsswindy 与社区贡献者。本工程是同一套视觉语言的 C99 实现。

## 许可

MIT。
