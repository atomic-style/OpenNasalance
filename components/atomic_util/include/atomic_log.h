#pragma once

#ifdef __cplusplus
extern "C" {
#endif


void atomic_log_print_console(const char *tag, const char *format, ...);
void atomic_log_print_debug(const char *tag, const char *format, ...);
void atomic_log_print_info(const char *tag, const char *format, ...);
void atomic_log_print_notice(const char *tag, const char *format, ...);
void atomic_log_print_alert(const char *tag, const char *format, ...);
void atomic_log_print_warn(const char *tag, const char *format, ...);
void atomic_log_print_err(const char *tag, const char *format, ...);

#define console(tag, format, ...) do { atomic_log_print_console(tag, format, ##__VA_ARGS__); } while(0)
#define debug(tag, format, ...) do { atomic_log_print_debug(tag, format, ##__VA_ARGS__); } while(0)
#define info(tag, format, ...) do { atomic_log_print_info(tag, format, ##__VA_ARGS__); } while(0)
#define notice(tag, format, ...) do { atomic_log_print_notice(tag, format, ##__VA_ARGS__); } while(0)
#define alert(tag, format, ...) do { atomic_log_print_alert(tag, format, ##__VA_ARGS__); } while(0)
#define warn(tag, format, ...) do { atomic_log_print_warn(tag, format, ##__VA_ARGS__); } while(0)
#define err(tag, format, ...) do { atomic_log_print_err(tag, format, ##__VA_ARGS__); } while(0)


#ifdef __cplusplus
}
#endif