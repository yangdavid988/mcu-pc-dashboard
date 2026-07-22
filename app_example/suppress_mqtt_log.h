/*******************************************************************************
 * suppress_mqtt_log.h - Force-injected via -include to suppress MQTT_DEBUG
 * prints (packet traces, protocol details) without modifying SDK files.
 *
 * Mechanism: GCC keeps the FIRST macro definition when a redefinition occurs.
 * This header is force-included (-include) at compilation start, so our
 * #define mqtt_printf is the OLD definition. When MQTTFreertos.h later
 * redefines it, GCC warns (-Wmacro-redefined suppressed) and keeps ours.
 ******************************************************************************/
#ifndef SUPPRESS_MQTT_LOG_H
#define SUPPRESS_MQTT_LOG_H

 /* Suppress ALL mqtt_printf output (DEBUG packet traces, INFO connection
  * status).  We cannot preserve INFO-level here because RTK_LOGA and the
  * MQTT level enum are not yet defined at -include time. */
#ifdef mqtt_printf
#undef mqtt_printf
#endif
  // #define mqtt_printf(level, fmt, arg...) ((void)(level))
#define mqtt_printf(level, fmt, arg...)     \
	do {\
		if (level >= MQTT_INFO) {\
			{\
				RTK_LOGA("MQTT", "[%d]mqtt:"fmt"\n\r", (int)rtos_time_get_current_system_time_ms(), ##arg);\
			} \
		}\
	}while(0)
#endif /* SUPPRESS_MQTT_LOG_H */
