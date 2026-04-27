#pragma once

#include <iostream>
#include <unistd.h>

namespace logx {

enum Level {
    Error = 0,
    Warn  = 1,
    Info  = 2,
    Trace = 3,
};

inline int g_log_level = Info;
inline bool g_use_color = isatty(STDOUT_FILENO) != 0;

} // namespace logx

#define LOG_COLOR_RESET   "\033[0m"
#define LOG_COLOR_RED     "\033[31m"
#define LOG_COLOR_YELLOW  "\033[33m"
#define LOG_COLOR_GREEN   "\033[32m"
#define LOG_COLOR_CYAN    "\033[36m"

#define LOGE(expr) \
    do { \
        if (::logx::g_log_level >= ::logx::Error) { \
            if (::logx::g_use_color) { \
                std::cerr << LOG_COLOR_RED << "[E] " << expr << LOG_COLOR_RESET << std::endl; \
            } else { \
                std::cerr << "[E] " << expr << std::endl; \
            } \
        } \
    } while (0)

#define LOGW(expr) \
    do { \
        if (::logx::g_log_level >= ::logx::Warn) { \
            if (::logx::g_use_color) { \
                std::cout << LOG_COLOR_YELLOW << "[W] " << expr << LOG_COLOR_RESET << std::endl; \
            } else { \
                std::cout << "[W] " << expr << std::endl; \
            } \
        } \
    } while (0)

#define LOGI(expr) \
    do { \
        if (::logx::g_log_level >= ::logx::Info) { \
            if (::logx::g_use_color) { \
                std::cout << LOG_COLOR_GREEN << "[I] " << expr << LOG_COLOR_RESET << std::endl; \
            } else { \
                std::cout << "[I] " << expr << std::endl; \
            } \
        } \
    } while (0)

#define LOGT(expr) \
    do { \
        if (::logx::g_log_level >= ::logx::Trace) { \
            if (::logx::g_use_color) { \
                std::cout << LOG_COLOR_CYAN << "[T] " << expr << LOG_COLOR_RESET << std::endl; \
            } else { \
                std::cout << "[T] " << expr << std::endl; \
            } \
        } \
    } while (0)
