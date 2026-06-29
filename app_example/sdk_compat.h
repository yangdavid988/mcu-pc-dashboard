#ifndef _SDK_COMPAT_H_
#define _SDK_COMPAT_H_

#include "ameba_rtos_version.h"
#include "lwip_netconf.h"

/* ========================================================================
 * SDK Version Compatibility Layer
 *
 * Release (<=1.2): LwIP_Check_Connectivity() / LwIP_IP_Address_Request()
 * Master   (>1.2): lwip_check_connectivity()  / lwip_request_ip()
 *
 * Usage: replace SDK LwIP calls with COMPAT_* macros.
 * ======================================================================== */
#if AMEBA_RTOS_VERSION() > AMEBA_RTOS_VERSION_VAL(1, 2, 255)
/* master (v1.3+) — lowercase naming */
#define COMPAT_CHECK_CONNECTIVITY(idx)  lwip_check_connectivity(idx)
#define COMPAT_REQUEST_IP(idx)          lwip_request_ip(idx)
#else
/* release v1.2 and below — uppercase LwIP_ naming */
#define COMPAT_CHECK_CONNECTIVITY(idx)  LwIP_Check_Connectivity(idx)
#define COMPAT_REQUEST_IP(idx)          LwIP_IP_Address_Request(idx)
#endif

#endif /* _SDK_COMPAT_H_ */
