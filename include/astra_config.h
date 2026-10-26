//
// astra - configuration accessors.
//
// Defaults live in astra_config_default() / astra_sys_config_default() and are
// copied on first access, so a zero-initialised static never leaks into the
// framework as "screen width 0".
//
#ifndef ASTRA_CONFIG_H_
#define ASTRA_CONFIG_H_

#include "astra_types.h"

/* Template holding every default of astra_config. */
astra_config astra_config_default(void);

/* Template holding every default of astra_sys_config. */
astra_sys_config astra_sys_config_default(void);

/* Process-wide UI config; initialised from the template on first call. */
astra_config *astra_ui_config(void);

/* Restore the UI config to its defaults (useful for tests). */
void astra_ui_config_reset(void);

/* Scale every layout parameter so a menu designed for 128x64 also fits other
   panels. The factor is min(width / 128, height / 64), clamped to a sane
   range; the derived tile selection box is recomputed. Passing 128x64 leaves
   the configuration untouched. Call this before building the menu tree. */
void astra_config_fit_to_screen(astra_config *cfg, uint16_t width,
                                uint16_t height);

#endif /* ASTRA_CONFIG_H_ */
