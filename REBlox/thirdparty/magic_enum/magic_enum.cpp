#include "magic_enum.hpp"

#include <algorithm>
const char* magic_enum::enum_name_pretty(const char* value)
{
    static std::string buffer;
    buffer = value;
    std::replace(buffer.begin(), buffer.end(), '_', ' ');
    return buffer.c_str();
}
