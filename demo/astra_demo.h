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

/* ------------------------------------------------------------------------ */
/* 输入层（由应用自己实现，不属于 HAL）                                       */
/* ------------------------------------------------------------------------ */

/* 只有两个逻辑键：上一个 / 下一个。短按切换高亮项，长按进入或返回。 */
typedef enum astra_demo_key {
  ASTRA_DEMO_KEY_PREV = 0, /* 短按：上移一项；长按：返回上级 */
  ASTRA_DEMO_KEY_NEXT,     /* 短按：下移一项；长按：进入下级 */
  ASTRA_DEMO_KEY_NUM
} astra_demo_key;

/* 建立菜单并播放开机动画。要求 HAL 已注入，且菜单池是干净的。
   返回 false 表示没有 HAL 或对象池已耗尽。 */
bool astra_demo_init(void);

/* 一帧：采样按键电平、更新导航、渲染并送出显存。必须在 init() 之后调用。 */
void astra_demo_update(void);

/* 回收对象池。不碰 HAL：后端由外壳负责销毁。 */
void astra_demo_destroy(void);

/* 注入按键电平——这就是输入层给自己留的缝：外壳每帧把 GPIO/键盘/触摸的
   状态翻译成这两个逻辑键，短按与长按的判定由本层负责。
   电平语义：按下期间保持 true，松开调用 false（边沿在 update() 里结算）。 */
void astra_demo_key_set(astra_demo_key key, bool down);

/* 给拥有独立确认/返回键的外壳用的快捷入口（不经过长按判定）。 */
void astra_demo_activate(void); /* 进入高亮项的下级页面 */
void astra_demo_back(void);     /* 返回上一级页面       */

#endif /* ASTRA_DEMO_H_ */
