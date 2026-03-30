#pragma once

#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <vector>
#include <memory>

namespace trader {

struct LoggingConfig;

inline void init_logging(const std::string& level = "info",
                         const std::string& file = "",
                         bool console = true) {
    std::vector<spdlog::sink_ptr> sinks;

    if (console) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    if (!file.empty()) {
        // 10 MB max, 3 rotated files
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            file, 10 * 1024 * 1024, 3));
    }

    auto logger = std::make_shared<spdlog::logger>("trader", sinks.begin(), sinks.end());
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    if (level == "trace")    logger->set_level(spdlog::level::trace);
    else if (level == "debug") logger->set_level(spdlog::level::debug);
    else if (level == "info")  logger->set_level(spdlog::level::info);
    else if (level == "warn")  logger->set_level(spdlog::level::warn);
    else if (level == "error") logger->set_level(spdlog::level::err);
    else                       logger->set_level(spdlog::level::info);

    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(1));
}

} // namespace trader
