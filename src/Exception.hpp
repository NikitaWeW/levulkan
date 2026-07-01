#pragma once
#include "Logging.hpp"
#include <stdexcept>
#include <stacktrace>

// For future use in engine

class EngineException : public std::exception
{
private:
    std::string mMessage;
public:
    inline explicit EngineException(std::string_view msg) { mMessage = fmt::format("{}\nstacktrace:\n{}", mMessage, fmt::streamed(std::stacktrace{}.current())); }

    inline EngineException(EngineException&&) noexcept = default;
    inline EngineException& operator=(EngineException&&) noexcept = default;
    inline EngineException(const EngineException&) = default;
    inline EngineException& operator=(const EngineException&) = default;
    inline ~EngineException() = default;

    inline char const *what() const noexcept override { return mMessage.c_str(); }
};
