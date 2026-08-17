#include "usbd_cdc_acm.h"

/* ---- Build-time guard ---- */
#ifdef CONFIG_USBD_CDC_ACM
#error "usbd_cdc_acm_custom.c compiled but CONFIG_USBD_CDC_ACM is set.  Set CONFIG_USBD_CDC_ACM_MENU=n in prj.conf / menuconfig."
#endif

/* ---- Private VID / PID ---- */
/* Keep Realtek VID (0x0BDA).  Use a custom PID distinct from ROM download
 * mode (0xF851) so the PC Python script only connects to THIS firmware's
 * CDC ACM port, never the download-mode port.                            */
#undef  USBD_CDC_ACM_PID
#define USBD_CDC_ACM_PID  0xF852

/* VID stays the same (Realtek) */
#undef  USBD_CDC_ACM_VID
#define USBD_CDC_ACM_VID  0x0BDA


#include "usbd_cdc_acm.c"
