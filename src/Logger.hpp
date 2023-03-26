#pragma once
#include <thread>
#include <mutex>
#include <vector>
#include <sstream>
#include <cstdarg>
#include <openvr_driver.h>

class Logger {
public:
    Logger() : name_("") { }
    Logger(const char* name) : name_(name) { }
    void Log(const char* format, ...);
protected:
    virtual void LogImpl(const char* string) = 0;
    std::string name_;
    std::mutex mutex_;
};

class NullLogger: public Logger {
    using Logger::Logger;
protected:
    void LogImpl(const char* message) override {};
};

class ConsoleLogger: public Logger {
    using Logger::Logger;
protected:
    void LogImpl(const char* message) override;
};

class VRLogger: public Logger {
    using Logger::Logger;
protected:
    void LogImpl(const char* message) override;
};