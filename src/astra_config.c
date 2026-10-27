//
// astra - configuration accessors.
//
#include "astra_config.h"

#include <math.h>
#include <string.h>

astra_config astra_config_default(void)
{
  astra_config c;
  memset(&c, 0, sizeof(c));

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

  c.list_bar_weight = 5.0f;
  c.list_text_height = 8.0f;
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
  c.tile_select_box_width = c.tile_select_box_margin * 2.0f + c.tile_pic_width;
  c.tile_select_box_height = c.tile_select_box_margin * 2.0f + c.tile_pic_height;
  c.tile_title_height = 8.0f;
  c.tile_btn_margin = 16.0f;

  c.pop_margin = 4.0f;
  c.pop_radius = 2.0f;
  c.pop_speed = 90.0f;

  c.logo_star_length = 2.0f;
  c.logo_text_height = 14.0f;
  c.logo_copy_right_height = 8.0f;
  c.logo_star_num = 16;

  /* null = "framework default"; the backend maps null to its normal size. */
  c.logo_title_font = NULL;
  c.logo_copy_right_font = NULL;
  c.main_font = NULL;

  return c;
}

astra_sys_config astra_sys_config_default(void)
{
  astra_sys_config c;
  c.screen_weight = 128;
  c.screen_height = 64;
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

void astra_ui_config_reset(void)
{
  *astra_ui_config() = astra_config_default();
}

/* ------------------------------------------------------------------------ */
/* resolution adaptation                                                     */
/* ------------------------------------------------------------------------ */

/* Scale a length and keep a minimum, rounding to whole pixels so boxes stay
   crisp. */
static float scale_len(float value, float factor, float minimum)
{
  float scaled = roundf(value * factor);
  if (scaled < minimum)
    scaled = minimum;
  return scaled;
}

void astra_config_fit_to_screen(astra_config *cfg, uint16_t width,
                                uint16_t height)
{
  if (cfg == NULL || width == 0 || height == 0)
    return;

  const float sx = (float)width / 128.0f;
  const float sy = (float)height / 64.0f;
  float f = sx < sy ? sx : sy;

  if (f < 0.4f)
    f = 0.4f;
  if (f > 3.0f)
    f = 3.0f;
  if (f == 1.0f)
    return; /* reference geometry: nothing to do */

  /* List page. Text height is left to the backend's font size, but the row
     pitch and margins follow the panel. */
  cfg->list_text_margin = scale_len(cfg->list_text_margin, f, 1.0f);
  cfg->list_line_height = scale_len(cfg->list_line_height, f, 8.0f);
  cfg->list_bar_weight = scale_len(cfg->list_bar_weight, f, 2.0f);
  cfg->selector_margin = scale_len(cfg->selector_margin, f, 1.0f);
  cfg->selector_top_margin = scale_len(cfg->selector_top_margin, f, 0.0f);
  cfg->selector_radius = scale_len(cfg->selector_radius, f, 0.0f);
  cfg->list_text_height = scale_len(cfg->list_text_height, f, 4.0f);

  /* Tile page chrome. The icon size is intentionally NOT scaled: tile_pic_* is
     the size of the bitmap asset (icon bitmaps are authored per resolution),
     and shrinking it here would misread the bitmap's row stride. */
  cfg->tile_pic_margin = scale_len(cfg->tile_pic_margin, f, 2.0f);
  cfg->tile_pic_top_margin = scale_len(cfg->tile_pic_top_margin, f, 2.0f);
  cfg->tile_arrow_width = scale_len(cfg->tile_arrow_width, f, 3.0f);
  cfg->tile_arrow_margin = scale_len(cfg->tile_arrow_margin, f, 2.0f);
  cfg->tile_bar_height = scale_len(cfg->tile_bar_height, f, 1.0f);
  cfg->tile_select_box_margin = scale_len(cfg->tile_select_box_margin, f, 1.0f);
  cfg->tile_select_box_line_length =
      scale_len(cfg->tile_select_box_line_length, f, 3.0f);
  cfg->tile_title_height = scale_len(cfg->tile_title_height, f, 6.0f);
  cfg->tile_btn_margin = scale_len(cfg->tile_btn_margin, f, 6.0f);

  /* Keep the vertical chrome inside the panel: the bottom margins are anchored
     to the screen edge, so they must shrink with it. */
  cfg->tile_dotted_line_bottom_margin =
      scale_len(cfg->tile_dotted_line_bottom_margin, f, 4.0f);
  cfg->tile_arrow_bottom_margin =
      scale_len(cfg->tile_arrow_bottom_margin, f, 3.0f);
  cfg->tile_text_bottom_margin =
      scale_len(cfg->tile_text_bottom_margin, f, 4.0f);

  /* Popups and logo. */
  cfg->pop_margin = scale_len(cfg->pop_margin, f, 2.0f);
  cfg->pop_radius = scale_len(cfg->pop_radius, f, 1.0f);
  cfg->logo_star_length = scale_len(cfg->logo_star_length, f, 1.0f);
  cfg->logo_text_height = scale_len(cfg->logo_text_height, f, 8.0f);
  cfg->logo_copy_right_height =
      scale_len(cfg->logo_copy_right_height, f, 6.0f);

  /* Derived field: keep it consistent with the icon size. */
  cfg->tile_select_box_width =
      cfg->tile_select_box_margin * 2.0f + cfg->tile_pic_width;
  cfg->tile_select_box_height =
      cfg->tile_select_box_margin * 2.0f + cfg->tile_pic_height;
}
