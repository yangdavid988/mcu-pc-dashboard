#ifndef _SDK_COMPAT_H_
#define _SDK_COMPAT_H_

#include "ameba_rtos_version.h"
#include "lwip_netconf.h"

/* ========================================================================
 * SDK Version Compatibility Layer
 *
 * Release (<=1.1.x):  LwIP_Check_Connectivity() / LwIP_IP_Address_Request()
 * Release (v1.2+):    lwip_check_connectivity()  / lwip_request_ip()
 *
 * SDK v1.2 migrated all LwIP helper APIs to lowercase naming.
 * Usage: replace SDK LwIP calls with COMPAT_* macros.
 * ======================================================================== */
#if AMEBA_RTOS_VERSION() >= AMEBA_RTOS_VERSION_VAL(1, 2, 0)
 /* v1.2+ — lowercase naming */
#define COMPAT_CHECK_CONNECTIVITY(idx)  lwip_check_connectivity(idx)
#define COMPAT_REQUEST_IP(idx)          lwip_request_ip(idx)
#else
 /* v1.1.x and below — uppercase LwIP_ naming */
#define COMPAT_CHECK_CONNECTIVITY(idx)  LwIP_Check_Connectivity(idx)
#define COMPAT_REQUEST_IP(idx)          LwIP_IP_Address_Request(idx)
#endif

#endif /* _SDK_COMPAT_H_ */
