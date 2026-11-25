// Host shim of vitashark (perf, b37: tools/hostgame): the compiler's log hook only (the GL shim "compiles" every shader)
#pragma once
typedef enum { SHARK_LOG_INFO = 0, SHARK_LOG_WARNING, SHARK_LOG_ERROR } shark_log_level;
typedef void (*shark_log_cb)(const char *msg, shark_log_level level, int line);
void shark_install_log_cb(shark_log_cb cb);
