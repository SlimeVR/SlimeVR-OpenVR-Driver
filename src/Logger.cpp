#include "Logger.hpp"
#include <iostream>

void Logger::Log(const char* format, ...) {
    auto prefixed_format = std::string(format);
    if (!name_.empty()) {
        std::ostringstream ss;
        ss << name_ << ": " << format;
        prefixed_format = ss.str();
    }

    va_list args;
    va_start(args, format);
    va_list args2;
    va_copy(args2, args);
    size_t len = std::vsnprintf(nullptr, 0, prefixed_format.data(), args2);
    va_end(args2);

    std::vector<char> buf(len + 1);
    std::vsnprintf(buf.data(), buf.size(), prefixed_format.data(), args);
    va_end(args);

    std::lock_guard<std::mutex> lock(mutex_);
    LogImpl(buf.data());
}

void ConsoleLogger::LogImpl(const char* message) {
    std::cout << message << '\n' << std::flush;
}

void VRLogger::LogImpl(const char* message) {
    vr::VRDriverLog()->Log(message);
}