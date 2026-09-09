/* Optional title-owned enhancements. Null callbacks leave the runtime unchanged.
 * Callbacks run synchronously on the guest thread. Title implementations must
 * validate guest memory spans before any title-owned rendering mutations. */
#ifndef PSX_TITLE_UI_H
#define PSX_TITLE_UI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t (*title_ui_filter_load_word)(uint32_t pc, uint32_t addr,
                                            uint32_t value, const uint32_t *gpr);
extern void (*title_ui_reset)(void);
#ifdef __cplusplus
}
#endif
#endif
