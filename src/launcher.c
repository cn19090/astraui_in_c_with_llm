//
// astra - launcher: page navigation and toasts.
//
#include "launcher.h"

#include <string.h>

void astra_launcher_init(astra_launcher *l, astra_menu *root,
                         astra_selector *selector, astra_camera *camera) {
  if (l == NULL) return;

  memset(l, 0, sizeof(*l));
  l->current_page = root;
  l->selector = selector;
  l->camera = camera;

  astra_camera_init_at(camera, 0.0f, 0.0f);
  astra_menu_init(root, astra_camera_position(camera));

  memset(selector, 0, sizeof(*selector));
  astra_selector_inject(selector, root);
}

astra_menu *astra_launcher_current_page(const astra_launcher *l) {
  return l == NULL ? NULL : l->current_page;
}

/**
 * @brief Slide-in toast that blocks until it has left the screen.
 *
 * Every value is a local, so consecutive calls animate independently.
 */
void astra_launcher_pop_info(astra_launcher *l, const char *info, uint16_t ms) {
  if (l == NULL || info == NULL) return;

  const astra_config *cfg = astra_ui_config();
  const astra_sys_config *sys = hal_system_config();

  const float w_pop = (float) hal_get_font_width(info) + 2.0f * cfg->pop_margin;
  const float h_pop = (float) hal_get_font_height() + 2.0f * cfg->pop_margin;
  const float x_pop = ((float) sys->screen_weight - w_pop) / 2.0f;

  float y_pop = 0.0f - h_pop - 8.0f;
  const uint64_t begin = l->time;
  bool on_render = true;

  while (on_render) {
    l->time++;

    const float y_trg = (l->time - begin < ms)
                            ? ((float) sys->screen_height - h_pop) / 3.0f
                            : (0.0f - h_pop - 8.0f);

    hal_canvas_clear();

    astra_menu_render(l->current_page, astra_camera_position(l->camera));
    astra_selector_render(l->selector, astra_camera_position(l->camera));
    astra_camera_update(l->camera, l->current_page, l->selector);

    hal_set_draw_type(0);
    hal_draw_rbox(x_pop - 4.0f, y_pop - 4.0f, w_pop + 8.0f, h_pop + 8.0f,
                  cfg->pop_radius + 2.0f);
    hal_set_draw_type(1);
    hal_draw_rframe(x_pop - 1.0f, y_pop - 1.0f, w_pop + 2.0f, h_pop + 2.0f,
                    cfg->pop_radius);
    hal_draw_english(x_pop + cfg->pop_margin,
                     y_pop + cfg->pop_margin + (float) hal_get_font_height(),
                     info);

    hal_canvas_update();

    astra_anim_step(&y_pop, y_trg, cfg->pop_speed);

    if (l->time - begin >= ms && y_pop == 0.0f - h_pop - 8.0f) on_render = false;
  }
}

bool astra_launcher_open(astra_launcher *l) {
  if (l == NULL || l->current_page == NULL) return false;

  astra_menu *next = astra_menu_next(l->current_page);
  if (next == NULL) {
    astra_launcher_pop_info(l, "unreferenced page!", 600);
    return false;
  }
  if (astra_menu_item_count(next) == 0) {
    astra_launcher_pop_info(l, "empty page!", 600);
    return false;
  }

  /* exit animation of the current page first, then move the pointer */
  astra_menu_deinit(l->current_page);

  l->current_page = next;
  astra_menu_init(l->current_page, astra_camera_position(l->camera));

  astra_selector_inject(l->selector, l->current_page);
  return true;
}

bool astra_launcher_close(astra_launcher *l) {
  if (l == NULL || l->current_page == NULL) return false;

  astra_menu *preview = astra_menu_preview(l->current_page);
  if (preview == NULL) {
    astra_launcher_pop_info(l, "unreferenced page!", 600);
    return false;
  }
  if (astra_menu_item_count(preview) == 0) {
    astra_launcher_pop_info(l, "empty page!", 600);
    return false;
  }

  astra_menu_deinit(l->current_page);

  l->current_page = preview;
  astra_menu_init(l->current_page, astra_camera_position(l->camera));

  astra_selector_inject(l->selector, l->current_page);
  return true;
}

void astra_launcher_update(astra_launcher *l) {
  if (l == NULL || l->current_page == NULL) return;

  hal_canvas_clear();

  astra_menu_render(l->current_page, astra_camera_position(l->camera));
  astra_selector_render(l->selector, astra_camera_position(l->camera));
  astra_camera_update(l->camera, l->current_page, l->selector);

  hal_canvas_update();

  l->time++;
}
