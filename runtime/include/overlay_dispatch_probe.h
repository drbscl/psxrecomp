/* Opt-in, guest-read-free dispatch flight recorder. Not overlay ABI state. */
#ifndef OVERLAY_DISPATCH_PROBE_H
#define OVERLAY_DISPATCH_PROBE_H
#include "cpu_state.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OVERLAY_DISPATCH_PROBE_CAP 128
typedef struct {
    uint64_t seq;
    uint32_t addr, pc, a0, s0, ra, owner, crc;
    int candidate, dll, tier, state;
    const char *event; /* internal static string, never user supplied */
} OverlayDispatchProbe;
extern int g_overlay_dispatch_probe_enabled;
/* Inclusive physical range. (0,0) disables. Arming resets ring and count.
 * Invoke/query on the guest/debug safe-point thread, like the CPS probe. */
void overlay_loader_dispatch_probe_set(uint32_t lo, uint32_t hi);
int overlay_loader_dispatch_probe_get(OverlayDispatchProbe *out, int cap, uint64_t *total);
void overlay_loader_dispatch_probe_note(const CPUState *cpu, uint32_t addr, const char *event);
const char *overlay_loader_dispatch_probe_path(int candidate);
#ifdef __cplusplus
}
#endif
#endif
