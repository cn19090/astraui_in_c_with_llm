//
// astra - launcher: page navigation and toasts.
//
#ifndef ASTRA_LAUNCHER_H_
#define ASTRA_LAUNCHER_H_

#include "item.h"

typedef struct astra_launcher {
  astra_menu *current_page;
  astra_selector *selector;
  astra_camera *camera;
  uint64_t time;
} astra_launcher;

void astra_launcher_init(astra_launcher *l, astra_menu *root,
                         astra_selector *selector, astra_camera *camera);

/* One frame: clear, render page + selector, advance camera, present. */
void astra_launcher_update(astra_launcher *l);

bool astra_launcher_open(astra_launcher *l);
bool astra_launcher_close(astra_launcher *l);

/* Modal toast. Blocks and runs its own frame loop. */
void astra_launcher_pop_info(astra_launcher *l, const char *info, uint16_t ms);

astra_menu *astra_launcher_current_page(const astra_launcher *l);

#endif /* ASTRA_LAUNCHER_H_ */
