#ifndef _PC_DASHBOARD_UI_H_
#define _PC_DASHBOARD_UI_H_

#include "pc_dashboard.h"

/* ========================================================================
 * UI widget creation and update
 * ======================================================================== */

/* Create dashboard UI (first call) */
void create_dashboard_ui(void);

/* Refresh dashboard data (called periodically by timer — migrated to V3 update_current_layout) */

/* UI update timer callback */
void dashboard_timer_cb(lv_timer_t *timer);

/* Extern timer handle reference */
extern lv_timer_t *g_dashboard_timer;

/* MQTT connection status label (assigned by each layout's create function, updated by ui layer) */
extern lv_obj_t *g_mqtt_status_label;

/* Reset MQTT status tracking after layout switch, force refresh on next tick */
void reset_mqtt_status_tracking(void);

#endif /* _PC_DASHBOARD_UI_H_ */
