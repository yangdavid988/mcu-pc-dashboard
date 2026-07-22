/*******************************************************************************
 * suppress_mqtt_log.h - Suppress MQTT_DEBUG prints (packet traces, protocol
 * details) by redefining mqtt_printf to filter by MQTT_INFO threshold.
 * Included via #include in pc_dashboard.h (after MQTTClient.h).
 ******************************************************************************/
#ifndef SUPPRESS_MQTT_LOG_H
#define SUPPRESS_MQTT_LOG_H

 /* Filter mqtt_printf: suppress DEBUG, keep INFO+. */
#ifdef mqtt_printf
#undef mqtt_printf
#endif
#define mqtt_printf(level, fmt, arg...)     \
	do {\
		if (level >= MQTT_INFO) {\
			{\
				RTK_LOGA("MQTT", "[%d]mqtt:"fmt"\n\r", (int)rtos_time_get_current_system_time_ms(), ##arg);\
			} \
		}\
	}while(0)
#endif /* SUPPRESS_MQTT_LOG_H */
