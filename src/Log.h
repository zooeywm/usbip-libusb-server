#pragma once

#include <iostream>
#include <unistd.h>

enum LogLevel {
    LOG_ERROR = 0,
    LOG_WARN  = 1,
    LOG_INFO  = 2,
    LOG_TRACE = 3
};

inline int g_log_level = LOG_INFO;
inline bool g_use_color = isatty(STDOUT_FILENO);

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_CYAN    "\033[36m"

#define LOGE(expr) \
    do { \
        if (g_log_level >= LOG_ERROR) { \
            if (g_use_color) { \
                std::cerr << COLOR_RED << "[E] " << expr << COLOR_RESET << std::endl; \
            } else { \
                std::cerr << "[E] " << expr << std::endl; \
            } \
        } \
    } while (0)

#define LOGW(expr) \
    do { \
        if (g_log_level >= LOG_WARN) { \
            if (g_use_color) { \
                std::cout << COLOR_YELLOW << "[W] " << expr << COLOR_RESET << std::endl; \
            } else { \
                std::cout << "[W] " << expr << std::endl; \
            } \
        } \
    } while (0)

#define LOGI(expr) \
    do { \
        if (g_log_level >= LOG_INFO) { \
            if (g_use_color) { \
                std::cout << COLOR_GREEN << "[I] " << expr << COLOR_RESET << std::endl; \
            } else { \
                std::cout << "[I] " << expr << std::endl; \
            } \
        } \
    } while (0)

#define LOGT(expr) \
    do { \
        if (g_log_level >= LOG_TRACE) { \
            if (g_use_color) { \
                std::cout << COLOR_CYAN << "[T] " << expr << COLOR_RESET << std::endl; \
            } else { \
                std::cout << "[T] " << expr << std::endl; \
            } \
        } \
    } while (0)
