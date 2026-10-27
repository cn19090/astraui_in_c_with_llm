//
// astra - page elements: menu, selector, camera, animation.
//
// Layout convention: element positions are upper-left corners; text is drawn
// from its lower-left corner. Renders happen before the camera advances, so a
// moving camera lags the layout by exactly one frame.
//
#include "item.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* animation helpers                                                         */
/* ------------------------------------------------------------------------ */

void astra_anim_step(float *pos, float trg, float speed) {
  if (*pos == trg) return;

  if (fabsf(*pos - trg) < 0.15f) *pos = trg;
  else *pos += (trg - *pos) / ((100.0f - speed) / 1.0f);
}

void astra_anim_entry(astra_menu *menu) {
  (void) menu; /* reserved for per-page entry effects */
}

/**
 * @brief Fade-out stepper, one step per call.
 */
void astra_anim_exit(astra_menu *menu) {
  (void) menu;

  static uint8_t fade_flag = 1;

  /* Recomputed every call: the canvas size is runtime data (it follows the
     configured panel), so caching it here would overflow the buffer when the
     resolution changes between calls. */
  const uint32_t buffer_len = 8u * hal_get_buffer_tile_height() *
                              hal_get_buffer_tile_width();

  uint8_t *buf = (uint8_t *) hal_get_canvas_buffer();
  if (buf == NULL) return;

  hal_delay((unsigned long) astra_ui_config()->fade_animation_speed);

  const bool light = astra_ui_config()->light_mode;

  if (light) {
    /* light mode extinguishes pixels */
    switch (fade_flag) {
      case 1:
        for (uint32_t i = 0; i < buffer_len; ++i)
          if (i % 2 != 0) buf[i] = (uint8_t) (buf[i] & 0xAA);
        break;
      case 2:
        for (uint32_t i = 0; i < buffer_len; ++i)
          if (i % 2 != 0) buf[i] = (uint8_t) (buf[i] & 0x00);
        break;
      case 3:
        for (uint32_t i = 0; i < buffer_len; ++i)
          if (i % 2 == 0) buf[i] = (uint8_t) (buf[i] & 0x55);
        break;
      case 4:
        for (uint32_t i = 0; i < buffer_len; ++i)
          if (i % 2 == 0) buf[i] = (uint8_t) (buf[i] & 0x00);
        break;
      default:
        fade_flag = 0;
        break;
    }
  } else {
    /* dark mode lights pixels up */
    switch (fade_flag) {
      case 1:
        for (uint32_t i = 0; i < buffer_len; ++i)
          if (i % 2 != 0) buf[i] = (uint8_t) (buf[i] | 0xAA);
        break;
      case 2:
        for (uint32_t i = 0; i < buffer_len; ++i)
          if (i % 2 != 0) buf[i] = (uint8_t) (buf[i] | 0x00);
        break;
      case 3:
        for (uint32_t i = 0; i < buffer_len; ++i)
          if (i % 2 == 0) buf[i] = (uint8_t) (buf[i] | 0x55);
        break;
      case 4:
        for (uint32_t i = 0; i < buffer_len; ++i)
          if (i % 2 == 0) buf[i] = (uint8_t) (buf[i] | 0x00);
        break;
      default:
        fade_flag = 0;
        break;
    }
  }
  fade_flag++;
}

void astra_anim_blur(astra_menu *menu) {
  (void) menu;

  /* Recomputed per call, like astra_anim_exit(): the canvas size follows the
     configured panel. */
  const uint32_t buffer_len = 8u * hal_get_buffer_tile_height() *
                              hal_get_buffer_tile_width();

  uint8_t *buf = (uint8_t *) hal_get_canvas_buffer();
  if (buf == NULL) return;

  for (uint32_t i = 0; i < buffer_len; ++i)
    buf[i] = (uint8_t) (buf[i] & (i % 2 == 0 ? 0x55 : 0xAA));
}

/* ------------------------------------------------------------------------ */
/* menu pool                                                                 */
/* ------------------------------------------------------------------------ */

static astra_menu s_pool[ASTRA_MENU_POOL_SIZE];
static uint16_t s_pool_used;

void astra_menu_pool_reset(void) {
  s_pool_used = 0;
  memset(s_pool, 0, sizeof(s_pool));
}

uint16_t astra_menu_pool_used(void) { return s_pool_used; }

static astra_menu *pool_alloc(void) {
  if (s_pool_used >= ASTRA_MENU_POOL_SIZE) return NULL;

  astra_menu *m = &s_pool[s_pool_used++];
  memset(m, 0, sizeof(*m));
  m->child_type = ASTRA_PAGE_TILE; /* decided by the first add_item() */
  return m;
}

astra_menu *astra_menu_create(const char *title) {
  astra_menu *m = pool_alloc();
  if (m == NULL) return NULL;

  snprintf(m->title, sizeof(m->title), "%s", title == NULL ? "" : title);
  m->self_type = ASTRA_PAGE_LIST;
  m->pic = NULL;

  const float text_margin = astra_ui_config()->list_text_margin;
  m->position.x = text_margin;
  m->position.y = 0.0f;
  m->position.x_trg = text_margin;
  m->position.y_trg = 0.0f;
  return m;
}

astra_menu *astra_menu_create_tile(const char *title, const uint8_t *pic) {
  astra_menu *m = pool_alloc();
  if (m == NULL) return NULL;

  snprintf(m->title, sizeof(m->title), "%s", title == NULL ? "" : title);
  m->self_type = ASTRA_PAGE_TILE;
  m->pic = pic;

  m->position.x = 0.0f;
  m->position.y = 0.0f;
  m->position.x_trg = 0.0f;
  m->position.y_trg = astra_ui_config()->tile_pic_top_margin;
  return m;
}

/* ------------------------------------------------------------------------ */
/* Item::updateConfig                                                        */
/* ------------------------------------------------------------------------ */

void astra_item_update_config(astra_menu *menu) {
  /* Nothing is cached any more: every render and animation step reads
     astra_ui_config() / hal_system_config_or_default() directly, so a config
     change is visible on the next frame with no refresh call. Kept as a no-op
     stub for source compatibility. */
  (void) menu;
}

/* ------------------------------------------------------------------------ */
/* Menu                                                                      */
/* ------------------------------------------------------------------------ */

void astra_menu_init(astra_menu *menu, astra_vec2 camera) {
  if (menu == NULL) return;

  astra_anim_entry(menu);

  const astra_config *cfg = astra_ui_config();
  const astra_sys_config *sys = hal_system_config_or_default();

  if (menu->child_type == ASTRA_PAGE_TILE) {
    if (cfg->tile_unfold) {
      for (uint8_t i = 0; i < menu->child_count; ++i)
        menu->children[i]->position.x = camera.x - cfg->tile_pic_width;
      menu->position_fg.w_bar = 0.0f;
    } else {
      for (uint8_t i = 0; i < menu->child_count; ++i)
        menu->children[i]->position.x = menu->children[i]->position.x_trg;
      menu->position_fg.w_bar = menu->position_fg.w_bar_trg;
    }

    menu->position_fg.y_arrow = (float) sys->screen_height;
    menu->position_fg.y_dotted_line = (float) sys->screen_height;
    menu->position_fg.y_bar = 0.0f - cfg->tile_bar_height;

  } else if (menu->child_type == ASTRA_PAGE_LIST) {
    if (cfg->list_unfold) {
      for (uint8_t i = 0; i < menu->child_count; ++i)
        menu->children[i]->position.y = camera.y - cfg->list_line_height;
      menu->position_fg.h_bar = 0.0f;
    } else {
      for (uint8_t i = 0; i < menu->child_count; ++i)
        menu->children[i]->position.y = menu->children[i]->position.y_trg;
      menu->position_fg.h_bar = menu->position_fg.h_bar_trg;
    }

    menu->position_fg.x_bar = (float) sys->screen_weight;
  }
}

void astra_menu_deinit(astra_menu *menu) {
  if (menu == NULL) return;
  astra_anim_exit(menu);
}

void astra_menu_render(astra_menu *menu, astra_vec2 camera) {
  if (menu == NULL) return;

  if (menu->child_type == ASTRA_PAGE_TILE) {
    const astra_config *cfg = astra_ui_config();
    const astra_sys_config *sys = hal_system_config_or_default();

    hal_set_draw_type(1);

    /* draw icons */
    for (uint8_t i = 0; i < menu->child_count; ++i) {
      astra_menu *child = menu->children[i];
      hal_draw_bmp(child->position.x + camera.x,
                   cfg->tile_pic_top_margin + camera.y, cfg->tile_pic_width,
                   cfg->tile_pic_height, child->pic);
      astra_anim_step(&child->position.x, child->position.x_trg,
                      cfg->tile_animation_speed);
    }

    /* draw progress bar (top edge) */
    const uint8_t count = astra_menu_item_count(menu);
    if (count > 0)
      menu->position_fg.w_bar_trg =
          (float) (menu->select_index + 1) *
          ((float) sys->screen_weight / (float) count);
    hal_draw_box(0.0f, menu->position_fg.y_bar, menu->position_fg.w_bar,
                 cfg->tile_bar_height);

    /* draw left arrow */
    hal_draw_h_line(cfg->tile_arrow_margin, menu->position_fg.y_arrow,
                    cfg->tile_arrow_width);
    hal_draw_pixel(cfg->tile_arrow_margin + 1.0f,
                   menu->position_fg.y_arrow + 1.0f);
    hal_draw_pixel(cfg->tile_arrow_margin + 2.0f,
                   menu->position_fg.y_arrow + 2.0f);
    hal_draw_pixel(cfg->tile_arrow_margin + 1.0f,
                   menu->position_fg.y_arrow - 1.0f);
    hal_draw_pixel(cfg->tile_arrow_margin + 2.0f,
                   menu->position_fg.y_arrow - 2.0f);

    /* draw right arrow */
    hal_draw_h_line((float) sys->screen_weight - cfg->tile_arrow_width -
                        cfg->tile_arrow_margin,
                    menu->position_fg.y_arrow, cfg->tile_arrow_width);
    hal_draw_pixel((float) sys->screen_weight - cfg->tile_arrow_width,
                   menu->position_fg.y_arrow + 1.0f);
    hal_draw_pixel((float) sys->screen_weight - cfg->tile_arrow_width - 1.0f,
                   menu->position_fg.y_arrow + 2.0f);
    hal_draw_pixel((float) sys->screen_weight - cfg->tile_arrow_width,
                   menu->position_fg.y_arrow - 1.0f);
    hal_draw_pixel((float) sys->screen_weight - cfg->tile_arrow_width - 1.0f,
                   menu->position_fg.y_arrow - 2.0f);

    /* draw left button */
    hal_draw_h_line(cfg->tile_btn_margin, menu->position_fg.y_arrow + 2.0f, 9.0f);
    hal_draw_box(cfg->tile_btn_margin + 2.0f,
                 menu->position_fg.y_arrow + 2.0f - 4.0f, 5.0f, 4.0f);

    /* draw right button */
    hal_draw_h_line((float) sys->screen_weight - cfg->tile_btn_margin - 9.0f,
                    menu->position_fg.y_arrow + 2.0f, 9.0f);
    hal_draw_box((float) sys->screen_weight - cfg->tile_btn_margin - 9.0f + 2.0f,
                 menu->position_fg.y_arrow + 2.0f - 4.0f, 5.0f, 4.0f);

    /* draw dotted line */
    hal_draw_h_dotted_line(0.0f, menu->position_fg.y_dotted_line,
                           (float) sys->screen_weight);

    astra_anim_step(&menu->position_fg.y_dotted_line,
                    menu->position_fg.y_dotted_line_trg,
                    cfg->tile_animation_speed);
    astra_anim_step(&menu->position_fg.y_arrow, menu->position_fg.y_arrow_trg,
                    cfg->tile_animation_speed);
    astra_anim_step(&menu->position_fg.w_bar, menu->position_fg.w_bar_trg,
                    cfg->tile_animation_speed);
    astra_anim_step(&menu->position_fg.y_bar, menu->position_fg.y_bar_trg,
                    cfg->tile_animation_speed);

  } else if (menu->child_type == ASTRA_PAGE_LIST) {
    const astra_config *cfg = astra_ui_config();
    const astra_sys_config *sys = hal_system_config_or_default();

    hal_set_draw_type(1);

    /* Vertically centre the text cell inside its row: the selector box spans
       the whole line height, so the baseline goes half a row plus half a cell
       below the row top. list_text_margin is a *side* margin only - using it
       here (as an earlier version did) pushed the text against the bottom of
       the box. */
    const float row_h = cfg->list_line_height;
    const float text_h = (float) hal_get_font_height();
    float baseline_pad = (row_h + text_h) / 2.0f;
    if (baseline_pad < text_h) baseline_pad = text_h; /* short rows: no clipping */

    /* text (coordinates may exceed the screen; clipping happens in the HAL) */
    for (uint8_t i = 0; i < menu->child_count; ++i) {
      astra_menu *child = menu->children[i];
      hal_draw_chinese(child->position.x + camera.x,
                       child->position.y + baseline_pad + camera.y,
                       child->title);
      astra_anim_step(&child->position.y, child->position.y_trg,
                      cfg->list_animation_speed);
    }

    /* draw scroll indicator */
    const uint8_t count = astra_menu_item_count(menu);
    if (count > 0)
      menu->position_fg.h_bar_trg =
          (float) (menu->select_index + 1) *
          ((float) sys->screen_height / (float) count);

    hal_draw_h_line((float) sys->screen_weight - cfg->list_bar_weight, 0.0f,
                    cfg->list_bar_weight);
    hal_draw_h_line((float) sys->screen_weight - cfg->list_bar_weight,
                    (float) sys->screen_height - 1.0f, cfg->list_bar_weight);
    hal_draw_v_line((float) sys->screen_weight -
                        ceilf(cfg->list_bar_weight / 2.0f),
                    0.0f, (float) sys->screen_height);
    hal_draw_box(menu->position_fg.x_bar, 0.0f, cfg->list_bar_weight,
                 menu->position_fg.h_bar);

    /* light mode */
    if (cfg->light_mode) {
      hal_set_draw_type(2);
      hal_draw_box(0.0f, 0.0f, (float) sys->screen_weight,
                   (float) sys->screen_height);
      hal_set_draw_type(1);
    }

    astra_anim_step(&menu->position_fg.h_bar, menu->position_fg.h_bar_trg,
                    cfg->list_animation_speed);
    astra_anim_step(&menu->position_fg.x_bar, menu->position_fg.x_bar_trg,
                    cfg->list_animation_speed);
  }
}

uint8_t astra_menu_item_count(const astra_menu *menu) {
  return menu == NULL ? 0 : menu->child_count;
}

astra_position astra_menu_item_position(const astra_menu *menu, uint8_t index) {
  astra_position empty = {0.0f, 0.0f, 0.0f, 0.0f};
  if (menu == NULL || index >= menu->child_count) return empty;
  return menu->children[index]->position;
}

astra_menu *astra_menu_next(const astra_menu *menu) {
  if (menu == NULL || menu->child_count == 0) return NULL; /* was an OOB read */
  if (menu->select_index >= menu->child_count) return NULL;
  return menu->children[menu->select_index];
}

astra_menu *astra_menu_preview(const astra_menu *menu) {
  return menu == NULL ? NULL : menu->parent;
}

bool astra_menu_add_item(astra_menu *menu, astra_menu *child) {
  if (menu == NULL || child == NULL) return false;

  /* the first child decides the type of every child of this page */
  if (menu->child_count == 0) menu->child_type = child->self_type;

  if (menu->child_type != child->self_type) return false;
  if (menu->child_count >= ASTRA_MENU_MAX_CHILDREN) return false;

  child->parent = menu;
  menu->children[menu->child_count++] = child;

  const astra_config *cfg = astra_ui_config();
  const astra_sys_config *sys = hal_system_config_or_default();

  if (menu->child_type == ASTRA_PAGE_LIST) {
    child->position.x_trg = cfg->list_text_margin;
    child->position.y_trg =
        (float) (menu->child_count - 1) * cfg->list_line_height;
    menu->position_fg.x_bar_trg = (float) sys->screen_weight - cfg->list_bar_weight;
  }

  if (menu->child_type == ASTRA_PAGE_TILE) {
    child->position.x_trg =
        (float) sys->screen_weight / 2.0f - cfg->tile_pic_width / 2.0f +
        (float) (menu->child_count - 1) *
            (cfg->tile_pic_margin + cfg->tile_pic_width);
    child->position.y_trg = cfg->tile_pic_top_margin;

    menu->position_fg.y_bar_trg = 0.0f;
    menu->position_fg.y_arrow_trg =
        (float) sys->screen_height - cfg->tile_arrow_bottom_margin;
    menu->position_fg.y_dotted_line_trg =
        (float) sys->screen_height - cfg->tile_dotted_line_bottom_margin;
  }

  return true;
}

/* ------------------------------------------------------------------------ */
/* Selector                                                                  */
/* ------------------------------------------------------------------------ */

void astra_selector_go(astra_selector *sel, uint8_t index) {
  if (sel == NULL || sel->menu == NULL) return;

  astra_menu *menu = sel->menu;
  if (index >= menu->child_count) return; /* out-of-range index is ignored */

  menu->select_index = index;

  const astra_config *cfg = astra_ui_config();
  const astra_sys_config *sys = hal_system_config_or_default();

  if (menu->child_type == ASTRA_PAGE_TILE) {
    sel->x_trg = menu->children[index]->position.x_trg -
                 cfg->tile_select_box_margin;
    sel->y_trg = menu->children[index]->position.y_trg -
                 cfg->tile_select_box_margin;

    sel->y_text = (float) sys->screen_height;
    sel->y_text_trg = (float) sys->screen_height - cfg->tile_text_bottom_margin;

    sel->w_trg = cfg->tile_select_box_width;
    sel->h_trg = cfg->tile_select_box_height;

  } else if (menu->child_type == ASTRA_PAGE_LIST) {
    sel->x_trg =
        menu->children[index]->position.x_trg - cfg->selector_margin;
    sel->y_trg = menu->children[index]->position.y_trg;

    sel->w_trg = (float) hal_get_font_width(menu->children[index]->title) +
                 cfg->list_text_margin * 2.0f;
    sel->h_trg = cfg->list_line_height;
  }
}

bool astra_selector_inject(astra_selector *sel, astra_menu *menu) {
  if (sel == NULL || menu == NULL) return false;

  sel->menu = menu;
  astra_selector_go(sel, menu->select_index); /* place the box on entry */
  return true;
}

void astra_selector_render(astra_selector *sel, astra_vec2 camera) {
  if (sel == NULL || sel->menu == NULL) return;

  astra_menu *menu = sel->menu;
  const astra_config *cfg = astra_ui_config();
  const astra_sys_config *sys = hal_system_config_or_default();

  astra_anim_step(&sel->x, sel->x_trg, cfg->selector_x_animation_speed);
  astra_anim_step(&sel->y, sel->y_trg, cfg->selector_y_animation_speed);
  astra_anim_step(&sel->h, sel->h_trg, cfg->selector_height_animation_speed);
  astra_anim_step(&sel->w, sel->w_trg, cfg->selector_width_animation_speed);

  if (menu->child_type == ASTRA_PAGE_TILE) {
    if (menu->child_count == 0) return;

    astra_anim_step(&sel->y_text, sel->y_text_trg,
                    cfg->selector_y_animation_speed);

    /* title is not affected by the camera */
    hal_set_draw_type(1);
    hal_draw_chinese(
        ((float) sys->screen_weight -
         (float) hal_get_font_width(menu->children[menu->select_index]->title)) /
            2.0f,
        sel->y_text + cfg->tile_title_height,
        menu->children[menu->select_index]->title);

    /* the box is drawn inverted and follows the camera */
    const float bx = sel->x + camera.x;
    const float by = sel->y + camera.y;
    const float len = cfg->tile_select_box_line_length;

    hal_set_draw_type(2);
    hal_draw_pixel(bx, by);

    /* top-left */
    hal_draw_h_line(bx, by, len + 1.0f);
    hal_draw_v_line(bx, by, len + 1.0f);
    /* bottom-left */
    hal_draw_h_line(bx, by + sel->h - 1.0f, len + 1.0f);
    hal_draw_v_line(bx, by + sel->h - len - 1.0f, len);
    /* top-right */
    hal_draw_h_line(bx + sel->w - len - 1.0f, by, len);
    hal_draw_v_line(bx + sel->w - 1.0f, by, len + 1.0f);
    /* bottom-right */
    hal_draw_h_line(bx + sel->w - len - 1.0f, by + sel->h - 1.0f, len);
    hal_draw_v_line(bx + sel->w - 1.0f, by + sel->h - len - 1.0f, len);

    hal_draw_pixel(bx + sel->w - 1.0f, by + sel->h - 1.0f);

  } else if (menu->child_type == ASTRA_PAGE_LIST) {
    hal_set_draw_type(2);
    hal_draw_rbox(sel->x + camera.x, sel->y + camera.y, sel->w, sel->h - 1.0f,
                  cfg->selector_radius);
    hal_set_draw_type(1);
  }
}

astra_vec2 astra_selector_position(const astra_selector *sel) {
  astra_vec2 p;
  p.x = sel == NULL ? 0.0f : sel->x_trg;
  p.y = sel == NULL ? 0.0f : sel->y_trg;
  return p;
}

/* ------------------------------------------------------------------------ */
/* Camera                                                                    */
/* ------------------------------------------------------------------------ */

void astra_camera_init(astra_camera *cam) {
  if (cam == NULL) return;
  memset(cam, 0, sizeof(*cam));
  cam->x_init = 0.0f;
  cam->y_init = 0.0f;
  cam->x = 0.0f;
  cam->y = 0.0f;
}

void astra_camera_init_at(astra_camera *cam, float x, float y) {
  if (cam == NULL) return;
  memset(cam, 0, sizeof(*cam));
  cam->x_init = 0.0f - x;
  cam->y_init = 0.0f - y;
  cam->x = 0.0f - x;
  cam->y = 0.0f - y;
}

/**
 * @return 0 in view, 1 above/left, 2 below/right
 */
uint8_t astra_camera_out_of_view(astra_camera *cam, float x, float y) {
  if (cam == NULL) return 0;

  const astra_sys_config *sys = hal_system_config_or_default();

  if (x < (0.0f - cam->x) || y < (0.0f - cam->y)) return 1;
  if (x > (0.0f - cam->x) + (float) sys->screen_weight - 1.0f ||
      y > (0.0f - cam->y) + (float) sys->screen_height - 1.0f)
    return 2;
  return 0;
}

astra_vec2 astra_camera_position(const astra_camera *cam) {
  astra_vec2 p;
  p.x = cam == NULL ? 0.0f : cam->x;
  p.y = cam == NULL ? 0.0f : cam->y;
  return p;
}

void astra_camera_go(astra_camera *cam, float x, float y) {
  if (cam == NULL) return;

  const float speed = astra_ui_config()->camera_animation_speed;
  cam->moving = true;
  astra_anim_step(&cam->x, 0.0f - x, speed);
  astra_anim_step(&cam->y, 0.0f - y, speed);
  if (cam->x == 0.0f - x && cam->y == 0.0f - y) cam->moving = false;
}

void astra_camera_go_direct(astra_camera *cam, float x, float y) {
  if (cam == NULL) return;
  cam->x = 0.0f - x;
  cam->y = 0.0f - y;
}

void astra_camera_go_horizontal(astra_camera *cam, float x) {
  if (cam == NULL) return;
  cam->moving = true;
  astra_anim_step(&cam->x, 0.0f - x, astra_ui_config()->camera_animation_speed);
  if (cam->x == 0.0f - x) cam->moving = false;
}

void astra_camera_go_vertical(astra_camera *cam, float y) {
  if (cam == NULL) return;
  cam->moving = true;
  astra_anim_step(&cam->y, 0.0f - y, astra_ui_config()->camera_animation_speed);
  if (cam->y == 0.0f - y) cam->moving = false;
}

void astra_camera_go_to_next_page_item(astra_camera *cam) {
  if (cam == NULL) return;
  const float h = (float) hal_system_config_or_default()->screen_height;
  cam->moving = true;
  astra_anim_step(&cam->y, cam->y - h,
                  astra_ui_config()->camera_animation_speed);
  if (cam->y == cam->y - h) cam->moving = false;
}

void astra_camera_go_to_preview_page_item(astra_camera *cam) {
  if (cam == NULL) return;
  const float h = (float) hal_system_config_or_default()->screen_height;
  cam->moving = true;
  astra_anim_step(&cam->y, cam->y + h,
                  astra_ui_config()->camera_animation_speed);
  if (cam->y == cam->y + h) cam->moving = false;
}

void astra_camera_go_to_list_item_page(astra_camera *cam, uint8_t index) {
  if (cam == NULL) return;

  const astra_config *cfg = astra_ui_config();
  const float h = (float) hal_system_config_or_default()->screen_height;

  /* Rows per screen: derived from the live configuration every call, because
     both the panel size and the row pitch are runtime data. */
  uint8_t max_item_per_screen = (uint8_t) (h / cfg->list_line_height);
  if (max_item_per_screen == 0) max_item_per_screen = 1;

  uint8_t page;
  cam->moving = true;

  if (index == 0) page = 0;
  else if (index % max_item_per_screen == 0)
    page = (uint8_t) (index / max_item_per_screen);
  else page = (uint8_t) floorf((float) index / (float) max_item_per_screen);

  const float target_y = (float) page * h;
  astra_camera_go(cam, 0.0f, target_y);

  if (cam->y == 0.0f - target_y) cam->moving = false;
}

void astra_camera_go_to_list_item_rolling(astra_camera *cam, astra_vec2 pos) {
  if (cam == NULL) return;

  static uint8_t s_direction = 0; /* 0: idle, 1: up, 2: down */

  cam->moving = true;

  if (astra_camera_out_of_view(cam, pos.x, pos.y) == 1) s_direction = 1;
  if (astra_camera_out_of_view(cam, pos.x, pos.y) == 2) s_direction = 2;

  if (s_direction == 1) {
    astra_camera_go(cam, pos.x, pos.y);
    if (cam->x == 0.0f - pos.x && cam->y == 0.0f - pos.y) s_direction = 0;
  }
  if (s_direction == 2) {
    const float target_y =
        pos.y + astra_ui_config()->list_line_height -
        (float) hal_system_config_or_default()->screen_height;
    astra_camera_go(cam, pos.x, target_y);
    if (cam->x == 0.0f - pos.x && cam->y == 0.0f - target_y) s_direction = 0;
  }

  if (astra_camera_out_of_view(cam, pos.x, pos.y) == 0) cam->moving = false;
}

void astra_camera_go_to_tile_item(astra_camera *cam, uint8_t index) {
  if (cam == NULL) return;

  const astra_config *cfg = astra_ui_config();
  const float target =
      (float) index * (cfg->tile_pic_width + cfg->tile_pic_margin);
  cam->moving = true;
  astra_camera_go(cam, target, 0.0f);
  if (cam->x == 0.0f - target) cam->moving = false;
}

bool astra_camera_is_moving(const astra_camera *cam) {
  return cam == NULL ? false : cam->moving;
}

void astra_camera_reset(astra_camera *cam) {
  if (cam == NULL) return;
  const float speed = astra_ui_config()->camera_animation_speed;
  cam->moving = true;
  astra_anim_step(&cam->x, cam->x_init, speed);
  astra_anim_step(&cam->y, cam->y_init, speed);
  if (cam->x == cam->x_init && cam->y == cam->y_init) cam->moving = false;
}

void astra_camera_update(astra_camera *cam, astra_menu *menu,
                         astra_selector *sel) {
  if (cam == NULL || menu == NULL || sel == NULL) return;

  if (menu->child_type == ASTRA_PAGE_LIST) {
    if (astra_ui_config()->list_page_turning_mode == 0)
      astra_camera_go_to_list_item_page(cam, menu->select_index);
    else if (astra_ui_config()->list_page_turning_mode == 1)
      astra_camera_go_to_list_item_rolling(cam, astra_selector_position(sel));
  } else if (menu->child_type == ASTRA_PAGE_TILE) {
    astra_camera_go_to_tile_item(cam, menu->select_index);
  }
}
