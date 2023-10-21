#ifndef VVSFS_LOGGING_H
#define VVSFS_LOGGING_H

#define DEBUG 1
#define LOG_FILE_PATH 0

#if defined(LOG_FILE_PATH) && LOG_FILE_PATH == 1
#define FILE_FORMAT_PARAMETER "%s:"
#define FILE_MARKER __FILE__
#else
#define FILE_FORMAT_PARAMETER ""
#define FILE_MARKER
#endif

#define __LOG(prefix, msg, ...)                                                \
    printk(prefix "%s(" FILE_FORMAT_PARAMETER "%d) :: " msg,                   \
           __func__,                                                           \
           FILE_MARKER __LINE__,                                               \
           ##__VA_ARGS__)
#define LOG(msg, ...) __LOG("", msg, ##__VA_ARGS__)

#if defined(DEBUG) && DEBUG == 1
#define DEBUG_LOG(msg, ...) __LOG("[DEBUG] ", msg, ##__VA_ARGS__)
#else
#define DEBUG_LOG(msg, ...) ({})
#endif

#endif // VVSFS_LOGGING_H