//
// astra - page elements: menu, selector, camera, animation.
//
// A page (astra_menu) owns a fixed array of children and their target layout.
// The selector draws the highlight, the camera owns the scroll offset, and
// astra_anim_step() is the shared easing used by all of them.
//
#ifndef ASTRA_ITEM_H_
#define ASTRA_ITEM_H_

#include "astra_config.h"
#include "astra_types.h"
#include "hal.h"

typedef struct astra_position {
  /* Upper-left corner. Text is drawn from its lower-left corner, so callers
     add the font height themselves. */
  float x, x_trg;
  float y, y_trg;
} astra_position;

typedef struct astra_fg_position {
  float h_bar, h_bar_trg;
  float w_bar, w_bar_trg;
  float x_bar, x_bar_trg;
  float y_bar, y_bar_trg;

  float y_arrow, y_arrow_trg;
  float y_dotted_line, y_dotted_line_trg;
} astra_fg_position;

typedef enum astra_page_type {
  ASTRA_PAGE_TILE = 0,
  ASTRA_PAGE_LIST,
} astra_page_type;

typedef struct astra_menu astra_menu;

struct astra_menu {
  /* Effective configuration, refreshed at the start of every render/animation
     step so runtime config changes take effect. */
  astra_sys_config system_config;
  astra_config astra_config;

  /* Layout. */
  astra_position position;
  astra_fg_position position_fg;

  char title[ASTRA_TITLE_MAX];
  const uint8_t *pic;

  astra_page_type self_type;
  astra_page_type child_type; /* decided by the first add_item() */

  astra_menu *parent;
  astra_menu *children[ASTRA_MENU_MAX_CHILDREN];
  uint8_t child_count;
  uint8_t select_index;
};

/* ------------------------------------------------------------------------ */
/* animation helpers                                                        */
/* ------------------------------------------------------------------------ */

void astra_anim_entry(astra_menu *menu);
void astra_anim_exit(astra_menu *menu);
void astra_anim_blur(astra_menu *menu);
/* Easing helper used by every animated field. */
void astra_anim_step(float *pos, float trg, float speed);

/* ------------------------------------------------------------------------ */
/* menu pool - fixed slab, no heap                                          */
/* ------------------------------------------------------------------------ */

/* Returns NULL when the pool is exhausted (checked, never silent). */
astra_menu *astra_menu_create(const char *title);
astra_menu *astra_menu_create_tile(const char *title, const uint8_t *pic);
void astra_menu_pool_reset(void);
uint16_t astra_menu_pool_used(void);

/* ------------------------------------------------------------------------ */
/* Menu                                                                      */
/* ------------------------------------------------------------------------ */

void astra_item_update_config(astra_menu *menu);

void astra_menu_init(astra_menu *menu, astra_vec2 camera);
void astra_menu_deinit(astra_menu *menu);
void astra_menu_render(astra_menu *menu, astra_vec2 camera);

uint8_t astra_menu_item_count(const astra_menu *menu);
astra_position astra_menu_item_position(const astra_menu *menu, uint8_t index);
astra_menu *astra_menu_next(const astra_menu *menu);
astra_menu *astra_menu_preview(const astra_menu *menu);
bool astra_menu_add_item(astra_menu *menu, astra_menu *child);

/* ------------------------------------------------------------------------ */
/* Selector                                                                  */
/* ------------------------------------------------------------------------ */

typedef struct astra_selector {
  astra_sys_config system_config;
  astra_config astra_config;

  astra_menu *menu;

  float x, x_trg;
  float y, y_trg;

  float w, w_trg;
  float h, h_trg;

  float y_text, y_text_trg; /* tile page title rise animation */
} astra_selector;

void astra_selector_go(astra_selector *sel, uint8_t index);
bool astra_selector_inject(astra_selector *sel, astra_menu *menu);
void astra_selector_render(astra_selector *sel, astra_vec2 camera);
astra_vec2 astra_selector_position(const astra_selector *sel);

/* ------------------------------------------------------------------------ */
/* Camera                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct astra_camera {
  astra_sys_config system_config;
  astra_config astra_config;

  float x_init, y_init;
  float x, y;
  bool moving;
} astra_camera;

void astra_camera_init(astra_camera *cam);
void astra_camera_init_at(astra_camera *cam, float x, float y);

uint8_t astra_camera_out_of_view(astra_camera *cam, float x, float y);
astra_vec2 astra_camera_position(const astra_camera *cam);

void astra_camera_go(astra_camera *cam, float x, float y);
void astra_camera_go_direct(astra_camera *cam, float x, float y);
void astra_camera_go_horizontal(astra_camera *cam, float x);
void astra_camera_go_vertical(astra_camera *cam, float y);

void astra_camera_go_to_next_page_item(astra_camera *cam);
void astra_camera_go_to_preview_page_item(astra_camera *cam);
void astra_camera_go_to_list_item_page(astra_camera *cam, uint8_t index);
void astra_camera_go_to_list_item_rolling(astra_camera *cam, astra_vec2 pos);
void astra_camera_go_to_tile_item(astra_camera *cam, uint8_t index);

bool astra_camera_is_moving(const astra_camera *cam);
void astra_camera_reset(astra_camera *cam);
void astra_camera_update(astra_camera *cam, astra_menu *menu,
                         astra_selector *sel);

#endif /* ASTRA_ITEM_H_ */
