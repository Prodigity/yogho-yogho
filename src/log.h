#ifndef YOGHO_LOG_H
#define YOGHO_LOG_H
/* Porting-gap log: each distinct message is reported once (stderr / browser console). */
void log_gap(const char *fmt, ...);
void log_info(const char *fmt, ...);
#endif
