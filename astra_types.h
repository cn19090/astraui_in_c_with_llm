//
// astra - shared data types.
//
// Plain structs and enums with no logic. Every other module depends on these.
//
#ifndef ASTRA_TYPES_H_
#define ASTRA_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

/* Fixed capacities keep every page in static storage. */
#define ASTRA_MENU_MAX_CHILDREN 8
#define ASTRA_TITLE_MAX 24
#define ASTRA_MENU_POOL_SIZE 32

/* A 2D point/offset, returned by value by the camera and selector getters. */
typedef struct astra_vec2 {
  float x;
  float y;
} astra_vec2;

/* Replaces sys::config (hal.h). */
typedef struct astra_sys_config {
  uint16_t screen_weight; /* width in pixels; the name is historical */
  uint16_t screen_height;
  float screen_bright;
} astra_sys_config;

/* Replaces astra::config (config.h). */
typedef struct astra_config {
  float tile_animation_speed;
  float list_animation_speed;
  float selector_y_animation_speed;
  float selector_x_animation_speed;
  float selector_width_animation_speed;
  float selector_height_animation_speed;
  float window_animation_speed;
  float side_bar_animation_speed;
  float fade_animation_speed;
  float camera_animation_speed;
  float logo_animation_speed;

  bool tile_unfold;
  bool list_unfold;
  bool tile_loop;
  bool menu_loop;
  bool background_blur;
  bool light_mode;

  float list_bar_weight;
  float list_text_height;
  float list_text_margin;
  float list_line_height;
  float selector_radius;
  float selector_margin;
  float selector_top_margin;
  uint8_t list_page_turning_mode; /* 0: paged, 1: rolling */

  float tile_pic_width;
  float tile_pic_height;
  float tile_pic_margin;
  float tile_pic_top_margin;
  float tile_arrow_width;
  float tile_arrow_margin;
  float tile_dotted_line_bottom_margin;
  float tile_arrow_bottom_margin;
  float tile_text_bottom_margin;
  float tile_bar_height;
  float tile_select_box_line_length;
  float tile_select_box_margin;
  float tile_select_box_width; /* derived: margin * 2 + pic width  */
  float tile_select_box_height;/* derived: margin * 2 + pic height */
  float tile_title_height;
  float tile_btn_margin;

  float pop_margin;
  float pop_radius;
  float pop_speed;

  float logo_star_length;
  float logo_text_height;
  float logo_copy_right_height;
  uint8_t logo_star_num;

  const uint8_t *logo_title_font;
  const uint8_t *logo_copy_right_font;
  const uint8_t *main_font;
} astra_config;

/* Replaces key::KEY_FILTER / key::KEY_ACTION / key::KEY_INDEX. */
typedef enum astra_key_filter {
  ASTRA_KEY_CHECKING = 0,
  ASTRA_KEY_0_CONFIRM,
  ASTRA_KEY_1_CONFIRM,
  ASTRA_KEY_RELEASED,
} astra_key_filter;

typedef enum astra_key_action {
  ASTRA_KEY_RELEASE = 0,
  ASTRA_KEY_CLICK,
  ASTRA_KEY_PRESS,
} astra_key_action;

typedef enum astra_key_index {
  ASTRA_KEY_0 = 0,
  ASTRA_KEY_1,
  ASTRA_KEY_NUM,
} astra_key_index;

#endif /* ASTRA_TYPES_H_ */
