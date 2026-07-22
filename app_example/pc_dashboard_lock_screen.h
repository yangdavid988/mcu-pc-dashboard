#ifndef _PC_DASHBOARD_LOCK_SCREEN_H_
#define _PC_DASHBOARD_LOCK_SCREEN_H_

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lock screen clock UI life-cycle */
void create_lock_screen_clock(void);
void destroy_lock_screen_clock(void);
void update_lock_screen_clock(void);

/* Unlock transition: fade-out clock, then destroy (300 ms ease-out).
 * Call AFTER creating monitor layout (clock fades, revealing monitor behind). */
void start_unlock_transition(void);

#ifdef __cplusplus
}
#endif

#endif /* _PC_DASHBOARD_LOCK_SCREEN_H_ */
