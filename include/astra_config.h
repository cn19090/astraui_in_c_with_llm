//
// astra - configuration accessors.
//
// Defaults live in astra_config_default() / astra_sys_config_default() and are
// copied on first access, so a zero-initialised static never leaks into the
// framework as "screen width 0".
//
// The layout is a per-panel profile selected at build time by ASTRA_PANEL; see
// astra_panel.h for the list of supported panels. There is deliberately no
// runtime scaling: a 1bpp panel wants integer margins, and rounding a scaled
// value produces off-by-one chrome.
//
#ifndef ASTRA_CONFIG_H_
#define ASTRA_CONFIG_H_

#include "astra_types.h"

/* Template holding every default of astra_config for the selected panel. */
astra_config astra_config_default(void);

/* Template holding every default of astra_sys_config (panel width/height). */
astra_sys_config astra_sys_config_default(void);

/* Process-wide UI config; initialised from the template on first call. */
astra_config *astra_ui_config(void);

/* Restore the UI config to its defaults (useful for tests). */
void astra_ui_config_reset(void);

#endif /* ASTRA_CONFIG_H_ */
