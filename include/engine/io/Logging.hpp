#pragma once

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/pattern_formatter.h"
#include "spdlog/fmt/ranges.h"
#include "spdlog/fmt/ostr.h"
#include "fmt/color.h"
#include "glm/gtx/io.hpp"

inline std::shared_ptr<spdlog::logger> sLogger;

#define LOG(level, ...) sLogger->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, level, __VA_ARGS__)
#define LOG_TRACE(...) LOG(spdlog::level::trace, __VA_ARGS__)
#define LOG_INFO(...) LOG(spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...) LOG(spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) LOG(spdlog::level::err, __VA_ARGS__)

#define LOG_VAR(x) LOG_TRACE("{} = {}", #x, fmt::streamed(x))

// Leave filename empty to disable file logging
inline void initLogger(std::string_view filename) {
    std::vector<spdlog::sink_ptr> sinks{
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>(),
        std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename.data()),
    };
    sLogger = std::make_shared<spdlog::logger>("sLogger", begin(sinks), end(sinks));
    sLogger->sinks()[0]->set_pattern("%^%v%$");
    sLogger->sinks()[1]->set_pattern("[%c][%l] %^%v%$");

    sLogger->set_level(spdlog::level::trace);
}
