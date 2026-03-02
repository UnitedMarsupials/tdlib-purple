#include "format.h"
#include <fmt/format.h>

std::string formatMessage(const char *fmt_str, std::initializer_list<std::string> args)
{
    std::string result = fmt_str;
    size_t arg_index = 0;
    
    for (const auto &arg : args) {
        size_t pos = result.find("{}");
        if (pos != std::string::npos) {
            result.replace(pos, 2, arg);
        }
        arg_index++;
    }
    
    return result;
}

std::string formatMessage(const char *fmt, const std::string &s)
{
    return formatMessage(fmt, {s});
}

std::string formatDuration(int32_t seconds)
{
    int32_t hours = seconds / 3600;
    seconds -= hours * 3600;
    int32_t minutes = seconds / 60;
    seconds -= minutes * 60;
    // TRANSLATOR: Time format. For HH:MM:SS use "{:02}:{:02}:{:02}" without the quotes.
    return fmt::format(_("{:02}:{:02}:{:02}"), hours, minutes, seconds);
}

void purpleDebug(const char *fmt, std::initializer_list<std::string> args)
{
    std::string message = formatMessage(fmt, args);
    purple_debug_misc(config::pluginId, "%s\n", message.c_str());
}
