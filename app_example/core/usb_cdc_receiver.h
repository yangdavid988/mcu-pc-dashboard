#ifndef USB_CDC_RECEIVER_H
#define USB_CDC_RECEIVER_H

/* Includes ------------------------------------------------------------------*/

#include <stdint.h>

/* Exported functions ------------------------------------------------------- */

#ifdef CONFIG_USB_CDC_MODE
/**
 * @brief  Start the USB CDC ACM receiver task.
 * @note   Creates a worker thread that initialises the USB device stack as a
 *         CDC ACM virtual serial port, receives JSON lines from the PC host,
 *         and dispatches them to parse_pc_stats_json() or lock-event handlers.
 *         Safe to call once at startup from app_example().
 */
void usb_cdc_receiver_start(void);
#endif /* CONFIG_USB_CDC_MODE */

#endif /* USB_CDC_RECEIVER_H */
