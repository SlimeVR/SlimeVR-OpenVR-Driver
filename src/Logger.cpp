#include "Logger.hpp"

void Log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list args_copy;
    va_copy(args_copy, args);
    size_t len = std::vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    std::vector<char> buf(len + 1);
    std::vsnprintf(buf.data(), buf.size(), format, args);
    va_end(args);

    std::ostringstream ss;
    ss << buf.data() << std::endl;
    vr::VRDriverLog()->Log(ss.str().c_str());
}