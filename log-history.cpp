#include "log-history.h"
#include "client-utils.h"
#include "config.h"
#include <string.h>
#include <time.h>

static const char TIME_START[] = "<span style=\"font-size: smaller\">(";
static const char TIME_END[]   = ")</span>";

// Within the parentheses: the time of day, or, for what was logged long after it was sent, the date
// and time in the locale's way, which is Pidgin's process's too
static bool parseLoggedTime(const std::string &text, time_t day, time_t &when, bool &timeOfDay)
{
    struct tm tm = {};
    const char *end = strptime(text.c_str(), "%x %X", &tm);
    if (end && !*end) {
        tm.tm_isdst = -1;
        when      = mktime(&tm);
        timeOfDay = false;
        return when != time_t(-1);
    }
    tm = {};
    end = strptime(text.c_str(), "%X", &tm);
    if (!end || *end) {
        tm  = {};
        end = strptime(text.c_str(), "%H:%M:%S", &tm);
        if (!end || *end)
            return false;
    }
    struct tm date;
    localtime_r(&day, &date);
    date.tm_hour  = tm.tm_hour;
    date.tm_min   = tm.tm_min;
    date.tm_sec   = tm.tm_sec;
    date.tm_isdst = -1;
    when      = mktime(&date);
    timeOfDay = true;
    return when != time_t(-1);
}

static time_t nextDay(time_t when)
{
    struct tm date;
    localtime_r(&when, &date);
    date.tm_mday++;
    date.tm_isdst = -1;
    return mktime(&date);
}

std::vector<LoggedLine> parseLoggedLines(const std::string &content, time_t logStart)
{
    std::vector<LoggedLine> lines;
    time_t day    = logStart;
    time_t latest = logStart;
    for (size_t start = 0, end; start < content.size(); start = end + 1) {
        end = content.find('\n', start);
        if (end == std::string::npos)
            end = content.size();
        std::string html = content.substr(start, end - start);
        size_t      timeStart = html.find(TIME_START);
        size_t      timeEnd   = (timeStart == std::string::npos) ? std::string::npos : html.find(TIME_END, timeStart);
        // The document's own lines, around those it logs
        if ((timeEnd == std::string::npos) || (timeStart > 32))
            continue;
        if ((html.size() >= 4) && (html.compare(html.size() - 4, 4, "<br>") == 0))
            html.erase(html.size() - 4);

        LoggedLine line;
        timeStart += sizeof(TIME_START) - 1;
        bool timeOfDay;
        if (!parseLoggedTime(html.substr(timeStart, timeEnd - timeStart), day, line.when, timeOfDay)) {
            purple_debug_warning(config::pluginId, "Logged line without a time: %s\n", html.c_str());
            continue;
        }
        // Times of day go on past midnight
        if (timeOfDay) {
            for (unsigned days = 0; (line.when + 60 < latest) && (days < 2); days++) {
                day       = nextDay(day);
                line.when = nextDay(line.when);
            }
            latest = std::max(latest, line.when);
        }

        // Someone's: "(time)</span> <b>name:</b></span> text"; a notice: "(time)</span><b> text</b>"
        size_t afterTime = timeEnd + sizeof(TIME_END) - 1;
        if (html.compare(afterTime, 4, " <b>") == 0) {
            size_t headerEnd = html.find("</b></span> ", afterTime);
            if (headerEnd != std::string::npos) {
                line.message   = true;
                line.bodyStart = headerEnd + strlen("</b></span> ");
            }
        }
        takeLogMarker(html, &line.messageId);
        unmarkLogOnly(html);
        line.html = std::move(html);
        lines.push_back(std::move(line));
    }
    return lines;
}

// "<img src='name'": the file beside the log, put in the image store, by its id
static void loadLoggedImages(LoggedLine &line, const std::string &directory)
{
    static const char IMG_SRC[] = "<img src=";
    for (size_t start = 0; (start = line.html.find(IMG_SRC, start)) != std::string::npos; ) {
        size_t nameStart = start + sizeof(IMG_SRC) - 1;
        char   quote     = (nameStart < line.html.size()) ? line.html[nameStart] : '\0';
        size_t nameEnd   = ((quote == '\'') || (quote == '"')) ? line.html.find(quote, nameStart + 1) : std::string::npos;
        if (nameEnd == std::string::npos)
            break;
        std::string name = line.html.substr(nameStart + 1, nameEnd - nameStart - 1);
        gchar      *path = (name.find('/') == std::string::npos) ? g_build_filename(directory.c_str(), name.c_str(), NULL)
                                                                   : nullptr;
        gchar      *data = nullptr;
        gsize       size = 0;
        std::string replacement;
        if (path && g_file_get_contents(path, &data, &size, NULL)) {
            // The store takes the data
            int id = purple_imgstore_add_with_id(data, size, name.c_str());
            line.imageIds.push_back(id);
            replacement = "<img id=\"" + std::to_string(id) + "\"";
        } else
            replacement = "<img";
        g_free(path);
        line.html.replace(start, nameEnd + 1 - start, replacement);
        start += replacement.size();
    }
}

std::vector<LoggedLine> readLoggedLines(PurpleAccount *account, PurpleConversationType type, const char *name,
                                        time_t since)
{
    std::vector<LoggedLine> lines;
    PurpleLogType logType = (type == PURPLE_CONV_TYPE_CHAT) ? PURPLE_LOG_CHAT : PURPLE_LOG_IM;
    // Newest first: those begun since then, and the one before, which may go on past it
    GList *logs = purple_log_get_logs(logType, name, account);
    std::vector<PurpleLog *> used;
    for (GList *item = logs; item; item = item->next) {
        PurpleLog *log = static_cast<PurpleLog *>(item->data);
        if (log->logger && log->logger->id && !strcmp(log->logger->id, "html"))
            used.push_back(log);
        if (log->time < since)
            break;
    }
    for (auto it = used.rbegin(); it != used.rend(); ++it) {
        PurpleLog                 *log   = *it;
        PurpleLogReadFlags         flags;
        char                      *text  = purple_log_read(log, &flags);
        PurpleLogCommonLoggerData *data  = static_cast<PurpleLogCommonLoggerData *>(log->logger_data);
        gchar                     *directory = (data && data->path) ? g_path_get_dirname(data->path) : nullptr;
        for (LoggedLine &line: parseLoggedLines(text ? text : "", log->time))
            if (line.when >= since) {
                if (directory)
                    loadLoggedImages(line, directory);
                lines.push_back(std::move(line));
            }
        g_free(directory);
        g_free(text);
    }
    for (GList *item = logs; item; item = item->next)
        purple_log_free(static_cast<PurpleLog *>(item->data));
    g_list_free(logs);
    purple_debug_misc(config::pluginId, "%zu logged lines of %s since %" G_GINT64_FORMAT "\n", lines.size(), name,
                      int64_t(since));
    return lines;
}
