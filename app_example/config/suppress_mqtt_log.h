#ifndef SUPPRESS_MQTT_LOG_H
#define SUPPRESS_MQTT_LOG_H

#ifdef mqtt_printf
#undef mqtt_printf
#endif
// #define mqtt_printf(level, fmt, arg...) ((void)(level))
#define mqtt_printf(level, fmt, arg...)                                                                        \
    do                                                                                                         \
    {                                                                                                          \
        if (level >= MQTT_INFO)                                                                                \
        {                                                                                                      \
            {                                                                                                  \
                RTK_LOGA("MQTT", "[%d]mqtt:" fmt "\n\r", (int) rtos_time_get_current_system_time_ms(), ##arg); \
            }                                                                                                  \
        }                                                                                                      \
    } while (0)
#endif /* SUPPRESS_MQTT_LOG_H */
