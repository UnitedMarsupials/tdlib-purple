#ifndef _LOG_HISTORY_H
#define _LOG_HISTORY_H

#include "identifiers.h"
#include <purple.h>
#include <string>
#include <vector>

// A line of a conversation's log, as Pidgin's HTML logger writes them
struct LoggedLine {
    time_t           when = 0;
    std::string      html;            // as logged, the time and sender included, the line break not
    bool             message = false; // someone's message, rather than a notice
    MessageId        messageId;       // which message it is, if the log says
    size_t           bodyStart = 0;   // where the message's own text starts in html, past its header
    std::vector<int> imageIds;        // pictures of the image store it shows, to unreference once shown
};

// The lines of a log in Pidgin's HTML, its session begun at logStart: times of day go on from that
// day, into the next ones as they go back past midnight. Markers of the plugin's own are taken out,
// the log-only kind leaving what they enclose.
std::vector<LoggedLine> parseLoggedLines(const std::string &content, time_t logStart);

// The lines of the conversation's logs from since on, oldest first; pictures, which the log keeps
// beside it as files, go into the image store. Only of logs in HTML.
std::vector<LoggedLine> readLoggedLines(PurpleAccount *account, PurpleConversationType type, const char *name,
                                        time_t since);

#endif
