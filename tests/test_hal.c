//
// astra - framework self-test.
//
// Exercises the parts of the framework that do not need a display: the HAL
// singleton and its ownership rules, the menu pool bounds, camera/selector
// state transitions and NULL-handle safety. Runs headless.
//
// Build: cmake -S Inc/astra -B Inc/astra/build    (standalone)
//        cmake -S . -B build -DASTRA_BUILD_TESTS=ON   (from the repo root)
// Run:   astra_test_hal.exe
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "astra_config.h"
#include "hal.h"
#include "item.h"
#include "launcher.h"

#ifdef _WIN32
#include <crtdbg.h>
#endif

static int s_failures = 0;

#define CHECK(cond)                                            \
  do {                                                         \
    if (!(cond)) {                                             \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
      s_failures++;                                            \
    }                                                          \
  } while (0)

/* ------------------------------------------------------------------------ */
/* a backend that draws nothing, only counts lifecycle calls                */
/* ------------------------------------------------------------------------ */

typedef struct {
  hal base;
  int init_calls;
  int deinit_calls;
  uint8_t buffer[16 * 8 * 8];
} fake_hal;

static void fake_init(hal *self) { ((fake_hal *) self)->init_calls++; }
static void fake_deinit(hal *self) { ((fake_hal *) self)->deinit_calls++; }

static void *fake_canvas(hal *self) { return ((fake_hal *) self)->buffer; }
static uint8_t fake_tile_w(hal *self) {
  (void) self;
  return 16;
}
static uint8_t fake_tile_h(hal *self) {
  (void) self;
  return 8;
}
static void fake_clear(hal *self) {
  memset(((fake_hal *) self)->buffer, 0, sizeof(((fake_hal *) self)->buffer));
}
static uint8_t fake_font_width(hal *self, const char *text) {
  (void) self;
  return (uint8_t) (text == NULL ? 0 : strlen(text) * 6);
}
static uint8_t fake_font_height(hal *self) {
  (void) self;
  return 12;
}

/* Recommended pattern: start from the complete default table and override only
   what the backend actually implements. Every other capability stays a safe
   no-op instead of a NULL pointer. */
static hal_ops make_fake_ops(void) {
  hal_ops ops = astra_hal_default_ops;

  ops.init = fake_init;
  ops.deinit = fake_deinit;
  ops.get_canvas_buffer = fake_canvas;
  ops.get_buffer_tile_width = fake_tile_w;
  ops.get_buffer_tile_height = fake_tile_h;
  ops.canvas_clear = fake_clear;
  ops.get_font_width = fake_font_width;
  ops.get_font_height = fake_font_height;

  return ops;
}

static fake_hal *fake_create_heap(void) {
  static hal_ops s_ops;
  static bool s_ready;

  if (!s_ready) {
    s_ops = make_fake_ops();
    s_ready = true;
  }

  fake_hal *f = (fake_hal *) calloc(1, sizeof(fake_hal));
  if (f == NULL) return NULL;
  hal_construct_owned(&f->base, &s_ops);
  return f;
}

static fake_hal s_static_hal;

/* ------------------------------------------------------------------------ */
/* HAL singleton                                                            */
/* ------------------------------------------------------------------------ */

static void test_destroy_without_inject(void) {
  hal_destroy();
  CHECK(!hal_check());
  printf("ok  hal_destroy() before hal_inject() is a no-op\n");
}

static void test_inject_and_destroy(void) {
  fake_hal *f = fake_create_heap();
  CHECK(f != NULL);

  CHECK(hal_inject(&f->base));
  CHECK(hal_check());
  CHECK(hal_get() == &f->base);
  CHECK(f->init_calls == 1);

  hal_destroy();
  CHECK(!hal_check());
  CHECK(hal_get() == NULL);

  hal_destroy(); /* must not double free */
  CHECK(!hal_check());

  printf("ok  inject -> get -> destroy releases the instance once\n");
}

static void test_repeated_cycles(void) {
  for (int i = 0; i < 200; ++i) {
    fake_hal *f = fake_create_heap();
    CHECK(f != NULL);
    CHECK(hal_inject(&f->base));
    hal_destroy();
  }
  CHECK(!hal_check());
  printf("ok  200 inject/destroy cycles\n");
}

static void test_static_instance_not_freed(void) {
  static hal_ops s_static_ops;
  static bool s_ready;

  if (!s_ready) {
    s_static_ops = make_fake_ops();
    s_ready = true;
  }

  memset(&s_static_hal, 0, sizeof(s_static_hal));
  hal_construct(&s_static_hal.base, &s_static_ops);

  CHECK(hal_inject(&s_static_hal.base));
  hal_destroy();

  /* A free() on static storage would have aborted; the counters proving
     deinit ran and the struct still being intact means ownership was
     respected. */
  CHECK(s_static_hal.init_calls == 1);
  CHECK(s_static_hal.deinit_calls == 1);
  printf("ok  static backend: deinit ran, instance not free()d\n");
}

static void test_inject_rejects_null(void) {
  hal_destroy();
  CHECK(!hal_inject(NULL));
  CHECK(!hal_check());
  printf("ok  hal_inject(NULL) is rejected\n");
}

/* ------------------------------------------------------------------------ */
/* menu pool                                                                */
/* ------------------------------------------------------------------------ */

static void test_pool_bounds(void) {
  astra_menu_pool_reset();

  int created = 0;
  for (int i = 0; i < ASTRA_MENU_POOL_SIZE + 8; ++i) {
    if (astra_menu_create("item") == NULL) break;
    created++;
  }
  CHECK(created == ASTRA_MENU_POOL_SIZE);
  CHECK(astra_menu_pool_used() == ASTRA_MENU_POOL_SIZE);

  astra_menu_pool_reset();
  CHECK(astra_menu_pool_used() == 0);

  printf("ok  menu pool holds %d items, overflow returns NULL\n", created);
}

static void test_child_bounds(void) {
  astra_menu_pool_reset();

  astra_menu *parent = astra_menu_create("parent");
  CHECK(parent != NULL);

  int accepted = 0;
  for (int i = 0; i < ASTRA_MENU_MAX_CHILDREN + 4; ++i) {
    astra_menu *kid = astra_menu_create("kid");
    if (kid == NULL) break;
    if (astra_menu_add_item(parent, kid)) accepted++;
  }
  CHECK(accepted == ASTRA_MENU_MAX_CHILDREN);
  CHECK(astra_menu_item_count(parent) == accepted);

  printf("ok  a page accepts at most %d children\n", accepted);
}

static void test_mixed_child_types_rejected(void) {
  astra_menu_pool_reset();

  static const uint8_t icon[120] = {0};
  astra_menu *list = astra_menu_create("list");
  astra_menu *tile = astra_menu_create_tile("tile", icon);

  CHECK(astra_menu_add_item(list, astra_menu_create("ok")));
  CHECK(!astra_menu_add_item(list, tile)); /* list + tile must not mix */
  CHECK(astra_menu_item_count(list) == 1);

  printf("ok  mixing page types is rejected\n");
}

static void test_title_truncation(void) {
  astra_menu_pool_reset();

  char long_title[128];
  memset(long_title, 'A', sizeof(long_title) - 1);
  long_title[sizeof(long_title) - 1] = '\0';

  astra_menu *m = astra_menu_create(long_title);
  CHECK(m != NULL);
  CHECK(strlen(m->title) == ASTRA_TITLE_MAX - 1); /* always terminated */

  printf("ok  long titles are truncated to %d chars\n", ASTRA_TITLE_MAX - 1);
}

/* ------------------------------------------------------------------------ */
/* navigation state                                                         */
/* ------------------------------------------------------------------------ */

static void test_pool_reset_keeps_null(void) {
  astra_menu_pool_reset();
  astra_menu_pool_reset();
  CHECK(astra_menu_pool_used() == 0);
  printf("ok  pool reset is idempotent\n");
}

static void test_camera_basics(void) {
  astra_camera cam;
  astra_camera_init_at(&cam, 0.0f, 0.0f);

  /* The camera stores the negated scroll offset. */
  astra_camera_go_direct(&cam, 10.0f, 20.0f);
  CHECK(cam.x == -10.0f);
  CHECK(cam.y == -20.0f);

  astra_vec2 pos = astra_camera_position(&cam);
  CHECK(pos.x == -10.0f && pos.y == -20.0f);

  /* (0,0) is inside the 128x64 viewport, (200,0) is outside to the right. */
  CHECK(astra_camera_out_of_view(&cam, 10.0f, 20.0f) == 0);
  CHECK(astra_camera_out_of_view(&cam, 1000.0f, 20.0f) == 2);

  printf("ok  camera offsets and viewport test\n");
}

static void test_selector_follows_items(void) {
  astra_menu_pool_reset();
  hal_destroy();
  {
    fake_hal *f = fake_create_heap();
    hal_inject(&f->base);
  }

  astra_menu *page = astra_menu_create("page");
  CHECK(astra_menu_add_item(page, astra_menu_create("one")));
  CHECK(astra_menu_add_item(page, astra_menu_create("two")));

  astra_selector sel;
  memset(&sel, 0, sizeof(sel));
  sel.system_config = astra_sys_config_default();
  sel.astra_config = astra_config_default();
  CHECK(astra_selector_inject(&sel, page));

  astra_selector_go(&sel, 1);
  CHECK(page->select_index == 1);

  /* an out-of-range index must be ignored, not applied */
  astra_selector_go(&sel, 9);
  CHECK(page->select_index == 1);

  printf("ok  selector drives select_index and ignores bad indices\n");
  hal_destroy();
}

static void test_launcher_navigation(void) {
  astra_menu_pool_reset();
  {
    fake_hal *f = fake_create_heap();
    hal_inject(&f->base);
  }

  astra_menu *root = astra_menu_create("root");
  astra_menu *sub = astra_menu_create("sub");
  astra_menu *leaf = astra_menu_create("leaf");
  CHECK(astra_menu_add_item(sub, leaf));
  CHECK(astra_menu_add_item(root, sub));
  CHECK(astra_menu_add_item(root, astra_menu_create("leaf2")));

  static astra_selector sel;
  static astra_camera cam;
  static astra_launcher l;
  astra_launcher_init(&l, root, &sel, &cam);

  CHECK(astra_launcher_current_page(&l) == root);

  CHECK(astra_launcher_open(&l));  /* enters "sub" */
  CHECK(astra_launcher_current_page(&l) == sub);

  CHECK(astra_launcher_close(&l)); /* back to root */
  CHECK(astra_launcher_current_page(&l) == root);

  /* closing at the root has nowhere to go */
  CHECK(!astra_launcher_close(&l));
  CHECK(astra_launcher_current_page(&l) == root);

  printf("ok  launcher open/close walks the page tree\n");
  hal_destroy();
}

/* ------------------------------------------------------------------------ */
/* robustness                                                              */
/* ------------------------------------------------------------------------ */

static void test_null_handles(void) {
  astra_menu_render(NULL, (astra_vec2) {0.0f, 0.0f});
  astra_menu_init(NULL, (astra_vec2) {0.0f, 0.0f});
  astra_menu_deinit(NULL);
  astra_selector_render(NULL, (astra_vec2) {0.0f, 0.0f});
  astra_camera_update(NULL, NULL, NULL);
  astra_launcher_update(NULL);

  CHECK(astra_menu_item_count(NULL) == 0);
  CHECK(astra_menu_next(NULL) == NULL);
  CHECK(astra_menu_preview(NULL) == NULL);
  CHECK(!astra_menu_add_item(NULL, NULL));
  CHECK(!astra_selector_inject(NULL, NULL));
  CHECK(!astra_launcher_open(NULL));
  CHECK(!astra_launcher_close(NULL));

  printf("ok  NULL handles are ignored instead of crashing\n");
}

static void test_default_config(void) {
  astra_config c = astra_config_default();

  CHECK(c.tile_animation_speed == 80.0f);
  CHECK(c.list_line_height == 16.0f);
  CHECK(c.tile_select_box_width ==
        c.tile_select_box_margin * 2.0f + c.tile_pic_width);
  CHECK(c.tile_unfold == true);

  astra_sys_config s = astra_sys_config_default();
  CHECK(s.screen_weight == 128);
  CHECK(s.screen_height == 64);

  printf("ok  factory defaults are populated (not zeroed)\n");
}

static void test_ui_config_is_shared(void) {
  astra_config *a = astra_ui_config();
  astra_config *b = astra_ui_config();
  CHECK(a == b); /* one instance, not one per translation unit */

  a->light_mode = true;
  CHECK(astra_ui_config()->light_mode == true);

  astra_ui_config_reset();
  CHECK(astra_ui_config()->light_mode == false);

  printf("ok  astra_ui_config() is a single shared instance\n");
}

int main(void) {
#ifdef _WIN32
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
#endif

  test_default_config();
  test_ui_config_is_shared();
  test_destroy_without_inject();
  test_inject_rejects_null();
  test_inject_and_destroy();
  test_repeated_cycles();
  test_pool_bounds();
  test_child_bounds();
  test_mixed_child_types_rejected();
  test_title_truncation();
  test_pool_reset_keeps_null();
  test_camera_basics();
  test_selector_follows_items();
  test_launcher_navigation();
  test_null_handles();
  test_static_instance_not_freed();

  printf("\n%s (%d failure%s)\n", s_failures == 0 ? "PASS" : "FAIL", s_failures,
         s_failures == 1 ? "" : "s");
  return s_failures == 0 ? 0 : 1;
}
