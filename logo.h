//
// astra - boot logo animation.
//
#ifndef ASTRA_LOGO_H_
#define ASTRA_LOGO_H_

#include <stdint.h>

/* Blocking intro animation. `_time` is measured in frames, not milliseconds. */
void astra_draw_logo(uint16_t time_frames);

#endif /* ASTRA_LOGO_H_ */
