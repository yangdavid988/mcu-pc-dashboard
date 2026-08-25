#ifndef TOUCH_GESTURE_H
#define TOUCH_GESTURE_H

/** @brief Initialise touch gesture handling.
 *
 *  Registers LV_EVENT_GESTURE handler on the current screen object
 *  to translate swipe/double-tap into layout/theme/brightness actions.
 *
 *  Must be called after lv_init() and lv_scr_act() are available
 *  (i.e. inside or after create_dashboard_ui()).
 */
void touch_gesture_init(void);

#endif /* TOUCH_GESTURE_H */
