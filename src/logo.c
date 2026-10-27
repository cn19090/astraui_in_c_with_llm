//
// astra - boot logo animation.
//
// A full-screen wipe with drifting stars and a two-line title. Runs its own
// frame loop and can be replayed.
//
#include "logo.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "astra_config.h"
#include "hal.h"
#include "item.h"

/* Logo star count is the only large static array in logo.c; the default
   drawing count (config->logo_star_num) is 16, so 16 is the natural ceiling on
   a small MCU. */
#ifndef LOGO_STARS_MAX
#define LOGO_STARS_MAX 32
#endif

void astra_draw_logo(uint16_t time_frames) {
  static bool on_render = true;
  static bool is_init = false;
  static uint16_t time = 0;

  static float y_stars[LOGO_STARS_MAX];
  static float y_stars_trg[LOGO_STARS_MAX];
  static float x_stars[LOGO_STARS_MAX];

  static float x_background = 0.0f;
  static float y_background = -1.0f;
  static bool background_ready = false;

  /* Re-arm so the logo can be played more than once, and forget the cached
     geometry: the panel size may differ between plays. */
  if (!on_render) {
    on_render = true;
    is_init = false;
    background_ready = false;
    time = 0;
  }

  while (on_render) {
    time++;

    const astra_config *cfg = astra_ui_config();
    const astra_sys_config *sys = hal_system_config();

    static const char *text = "astra UI";
    static const char *copyright = "powered by";

    hal_set_font(cfg->logo_title_font);
    const float x_title =
        ((float) sys->screen_weight - (float) hal_get_font_width(text)) / 2.0f;
    hal_set_font(cfg->logo_copy_right_font);
    const float x_copyright = ((float) sys->screen_weight -
                               (float) hal_get_font_width(copyright)) /
                              2.0f;
    hal_set_font(cfg->main_font);

    /* all coordinates are upper-left corners */
    float y_title = 0.0f - cfg->logo_text_height - 1.0f;
    float y_copyright = 0.0f - cfg->logo_copy_right_height - 1.0f;
    float y_title_trg = 0.0f;
    float y_copyright_trg = 0.0f;

    static float y_background_trg = 0.0f;

    /* Start the mask just above the panel; recomputed whenever a new play
       begins so a different resolution starts correctly. */
    if (!background_ready) {
      y_background = 0.0f - (float) sys->screen_height - 1.0f;
      background_ready = true;
    }

    const uint8_t star_num =
        cfg->logo_star_num > LOGO_STARS_MAX ? LOGO_STARS_MAX : cfg->logo_star_num;

    if (time < time_frames) {
      y_background_trg = 0.0f;

      if (!is_init) {
        for (uint8_t i = 0; i < star_num; ++i) {
          srand((unsigned int) (hal_get_random_seed() * 7));

          y_stars[i] = 0.0f - cfg->logo_star_length - 1.0f;
          y_stars_trg[i] =
              1.0f + (float) (rand() % (int) ((float) sys->screen_height -
                                              2.0f * cfg->logo_star_length - 2.0f + 1.0f));
          x_stars[i] =
              1.0f + (float) (rand() % (int) ((float) sys->screen_weight -
                                              2.0f * cfg->logo_star_length - 2.0f + 1.0f));
        }
        is_init = true;
      }

      y_title_trg =
          (float) sys->screen_height / 2.0f - cfg->logo_text_height / 2.0f;
      y_copyright_trg = y_title_trg - cfg->logo_copy_right_height - 4.0f;

    } else {
      y_background_trg = 0.0f - (float) sys->screen_height - 1.0f;
      for (uint8_t i = 0; i < star_num; ++i)
        y_stars_trg[i] = 0.0f - cfg->logo_star_length - 1.0f;
      y_title_trg = 0.0f - cfg->logo_text_height - 1.0f;
      y_copyright_trg = 0.0f - cfg->logo_copy_right_height - 1.0f;
    }

    /* mask first, then stars, then text; everything leaves together */
    hal_canvas_clear();

    hal_set_draw_type(0);
    hal_draw_box(x_background, y_background, (float) sys->screen_weight,
                 (float) sys->screen_height);
    astra_anim_step(&y_background, y_background_trg, cfg->logo_animation_speed);
    hal_set_draw_type(1);
    hal_draw_h_line(0.0f, y_background + (float) sys->screen_height,
                    (float) sys->screen_weight);

    for (uint8_t i = 0; i < star_num; ++i) {
      hal_draw_h_line(x_stars[i] - cfg->logo_star_length - 1.0f, y_stars[i],
                      cfg->logo_star_length);
      hal_draw_h_line(x_stars[i] + 2.0f, y_stars[i], cfg->logo_star_length);
      hal_draw_v_line(x_stars[i], y_stars[i] - cfg->logo_star_length - 1.0f,
                      cfg->logo_star_length);
      hal_draw_v_line(x_stars[i], y_stars[i] + 2.0f, cfg->logo_star_length);

      astra_anim_step(&y_stars[i], y_stars_trg[i], cfg->logo_animation_speed);
    }

    hal_set_font(cfg->logo_title_font);
    hal_draw_english(x_title, y_title + cfg->logo_text_height, text);
    hal_set_font(cfg->logo_copy_right_font);
    hal_draw_english(x_copyright, y_copyright + cfg->logo_copy_right_height,
                     copyright);
    astra_anim_step(&y_title, y_title_trg, cfg->logo_animation_speed);
    astra_anim_step(&y_copyright, y_copyright_trg, cfg->logo_animation_speed);

    hal_canvas_update();

    if (time >= time_frames &&
        y_background == 0.0f - (float) sys->screen_height - 1.0f)
      on_render = false;
  }
}
