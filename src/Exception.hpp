#pragma once
#include "Logging.hpp"
#include <stdexcept>
#include "cpptrace/cpptrace.hpp"

// For future use in engine

class EngineException : public std::exception
{
private:
    std::string mMessage;
    cpptrace::raw_trace mTrace;
public:
    inline explicit EngineException(std::string_view msg) { 
        mMessage = fmt::format("{}", msg); 
        mTrace = cpptrace::generate_raw_trace();
    }

    inline EngineException(EngineException&&) noexcept = default;
    inline EngineException& operator=(EngineException&&) noexcept = default;
    inline EngineException(const EngineException&) = default;
    inline EngineException& operator=(const EngineException&) = default;
    inline ~EngineException() = default;

    inline char const *what() const noexcept override { return mMessage.c_str(); }
    inline cpptrace::raw_trace getTrace() const noexcept { return mTrace; }
};
