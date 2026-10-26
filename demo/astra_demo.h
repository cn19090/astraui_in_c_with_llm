//
// astra demo: 固定 128x64 的示例应用。
//
// 这一层不依赖任何平台：它只调用 astra 的 HAL，由外壳（main.c、RTOS 任务等）
// 负责注入后端并驱动 init/update/destroy。
//
#ifndef ASTRA_DEMO_H_
#define ASTRA_DEMO_H_

#include <stdbool.h>

#include "hal.h"

/* 在已注入后端的前提下建立菜单并播放开机动画。
   三个字体句柄由后端定义（例如 raylib 后端的 hal_raylib_font_main），
   框架把它们当作不透明 ID 原样传回后端，因此必须传入真实句柄。
   返回 false 表示没有 HAL 或对象池已耗尽。 */
bool astra_demo_init(const uint8_t *main_font, const uint8_t *logo_title_font,
                     const uint8_t *logo_copy_right_font);

/* 一帧：读两个逻辑键、更新导航、渲染并送出显存。必须在 init() 之后调用。 */
void astra_demo_update(void);

/* 回收对象池。不碰 HAL：后端由外壳负责销毁。 */
void astra_demo_destroy(void);

/* 给拥有独立确认/返回键的外壳用的快捷入口。 */
void astra_demo_activate(void); /* 进入高亮项的下级页面 */
void astra_demo_back(void);     /* 返回上一级页面       */

#endif /* ASTRA_DEMO_H_ */
