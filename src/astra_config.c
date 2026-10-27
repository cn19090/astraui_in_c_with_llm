//
// astra - configuration accessors.
//
// Each supported panel gets its own hand-tuned profile instead of one generic
// layout scaled by a factor: a 32 px tall module and a 240 px square module need
// different margins, row pitches and chrome weights, and rounding a scaled value
// never lands on the sharp numbers a 1bpp panel wants.
//
// The profile is chosen at build time by ASTRA_PANEL (see astra_panel.h).
//
#include "astra_config.h"

#include <math.h>
#include <string.h>

astra_config astra_config_default(void)
{
  astra_config c;
  memset(&c, 0, sizeof(c));

  /* Animation pacing is shared by every panel: it is a function of the frame
     rate, not of the pixel count. */
  c.tile_animation_speed = 80.0f;
  c.list_animation_speed = 95.0f;
  c.selector_y_animation_speed = 95.0f;
  c.selector_x_animation_speed = 95.0f;
  c.selector_width_animation_speed = 95.0f;
  c.selector_height_animation_speed = 95.0f;
  c.window_animation_speed = 90.0f;
  c.side_bar_animation_speed = 60.0f;
  c.fade_animation_speed = 100.0f;
  c.camera_animation_speed = 92.0f;
  c.logo_animation_speed = 95.0f;

  c.tile_unfold = true;
  c.list_unfold = true;
  c.tile_loop = true;
  c.menu_loop = true;
  c.background_blur = true;
  c.light_mode = false;

#if ASTRA_PANEL == ASTRA_PANEL_128x32
  /* ---------------- 128x32: three list rows, no tile page ---------------- */
  c.list_bar_weight = 2.0f;
  c.list_text_height = 8.0f;
  c.list_text_margin = 2.0f;
  c.list_line_height = 10.0f;
  c.selector_radius = 0.0f;
  c.selector_margin = 2.0f;
  c.selector_top_margin = 1.0f;
  c.list_page_turning_mode = 1;

  /* Unused (ASTRA_PANEL_HAS_TILES is 0) but kept consistent for callers that
     inspect the config. */
  c.tile_pic_width = 30.0f;
  c.tile_pic_height = 30.0f;
  c.tile_pic_margin = 6.0f;
  c.tile_pic_top_margin = 1.0f;
  c.tile_arrow_width = 4.0f;
  c.tile_arrow_margin = 2.0f;
  c.tile_dotted_line_bottom_margin = 6.0f;
  c.tile_arrow_bottom_margin = 3.0f;
  c.tile_text_bottom_margin = 2.0f;
  c.tile_bar_height = 1.0f;
  c.tile_select_box_line_length = 3.0f;
  c.tile_select_box_margin = 2.0f;
  c.tile_title_height = 8.0f;
  c.tile_btn_margin = 4.0f;

  c.pop_margin = 2.0f;
  c.pop_radius = 1.0f;
  c.pop_speed = 90.0f;

  c.logo_star_length = 1.0f;
  c.logo_text_height = 10.0f;
  c.logo_copy_right_height = 6.0f;
  c.logo_star_num = 8;

#elif ASTRA_PANEL == ASTRA_PANEL_128x128
  /* -------------- 128x128: five list rows, icons drawn 2x -------------- */
  c.list_bar_weight = 6.0f;
  c.list_text_height = 16.0f;
  c.list_text_margin = 6.0f;
  c.list_line_height = 24.0f;
  c.selector_radius = 1.0f;
  c.selector_margin = 6.0f;
  c.selector_top_margin = 3.0f;
  c.list_page_turning_mode = 1;

  c.tile_pic_width = 60.0f; /* 30x30 asset drawn 2x (ASTRA_PANEL_ICON_SCALE) */
  c.tile_pic_height = 60.0f;
  c.tile_pic_margin = 20.0f;
  c.tile_pic_top_margin = 20.0f;
  c.tile_arrow_width = 10.0f;
  c.tile_arrow_margin = 8.0f;
  c.tile_dotted_line_bottom_margin = 34.0f;
  c.tile_arrow_bottom_margin = 14.0f;
  c.tile_text_bottom_margin = 24.0f;
  c.tile_bar_height = 3.0f;
  c.tile_select_box_line_length = 8.0f;
  c.tile_select_box_margin = 5.0f;
  c.tile_title_height = 16.0f;
  c.tile_btn_margin = 20.0f;

  c.pop_margin = 6.0f;
  c.pop_radius = 3.0f;
  c.pop_speed = 90.0f;

  c.logo_star_length = 3.0f;
  c.logo_text_height = 22.0f;
  c.logo_copy_right_height = 10.0f;
  c.logo_star_num = 24;

#elif ASTRA_PANEL == ASTRA_PANEL_240x240
  /* -------------- 240x240: seven list rows, icons drawn 4x -------------- */
  c.list_bar_weight = 8.0f;
  c.list_text_height = 24.0f;
  c.list_text_margin = 10.0f;
  c.list_line_height = 34.0f;
  c.selector_radius = 2.0f;
  c.selector_margin = 10.0f;
  c.selector_top_margin = 4.0f;
  c.list_page_turning_mode = 1;

  c.tile_pic_width = 120.0f; /* 30x30 asset drawn 4x */
  c.tile_pic_height = 120.0f;
  c.tile_pic_margin = 30.0f;
  c.tile_pic_top_margin = 30.0f;
  c.tile_arrow_width = 18.0f;
  c.tile_arrow_margin = 14.0f;
  c.tile_dotted_line_bottom_margin = 56.0f;
  c.tile_arrow_bottom_margin = 20.0f;
  c.tile_text_bottom_margin = 34.0f;
  c.tile_bar_height = 5.0f;
  c.tile_select_box_line_length = 14.0f;
  c.tile_select_box_margin = 8.0f;
  c.tile_title_height = 24.0f;
  c.tile_btn_margin = 34.0f;

  c.pop_margin = 10.0f;
  c.pop_radius = 5.0f;
  c.pop_speed = 90.0f;

  c.logo_star_length = 5.0f;
  c.logo_text_height = 32.0f;
  c.logo_copy_right_height = 14.0f;
  c.logo_star_num = 32;

#else
  /* ---------------- 128x64: the reference layout ---------------- */
  c.list_bar_weight = 5.0f;
  c.list_text_height = 12.0f;
  c.list_text_margin = 4.0f;
  c.list_line_height = 16.0f;
  c.selector_radius = 0.5f;
  c.selector_margin = 4.0f;
  c.selector_top_margin = 2.0f;
  c.list_page_turning_mode = 1;

  c.tile_pic_width = 30.0f;
  c.tile_pic_height = 30.0f;
  c.tile_pic_margin = 8.0f;
  c.tile_pic_top_margin = 8.0f;
  c.tile_arrow_width = 6.0f;
  c.tile_arrow_margin = 4.0f;
  c.tile_dotted_line_bottom_margin = 18.0f;
  c.tile_arrow_bottom_margin = 8.0f;
  c.tile_text_bottom_margin = 12.0f;
  c.tile_bar_height = 2.0f;
  c.tile_select_box_line_length = 5.0f;
  c.tile_select_box_margin = 3.0f;
  c.tile_title_height = 8.0f;
  c.tile_btn_margin = 16.0f;

  c.pop_margin = 4.0f;
  c.pop_radius = 2.0f;
  c.pop_speed = 90.0f;

  c.logo_star_length = 2.0f;
  c.logo_text_height = 14.0f;
  c.logo_copy_right_height = 8.0f;
  c.logo_star_num = 16;
#endif

  /* Derived: the selection box hugs the icon by a fixed margin. */
  c.tile_select_box_width = c.tile_select_box_margin * 2.0f + c.tile_pic_width;
  c.tile_select_box_height = c.tile_select_box_margin * 2.0f + c.tile_pic_height;

  return c;
}

astra_sys_config astra_sys_config_default(void)
{
  astra_sys_config c;
  c.screen_weight = ASTRA_PANEL_WIDTH;
  c.screen_height = ASTRA_PANEL_HEIGHT;
  c.screen_bright = 255.0f;
  return c;
}

astra_config *astra_ui_config(void)
{
  static astra_config s_config;
  static bool s_ready;

  if (!s_ready)
  {
    s_config = astra_config_default();
    s_ready = true;
  }
  return &s_config;
}

void astra_ui_config_reset(void) { *astra_ui_config() = astra_config_default(); }
