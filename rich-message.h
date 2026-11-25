#ifndef _RICH_MESSAGE_H
#define _RICH_MESSAGE_H

#include "buildopt.h"
#include <td/telegram/td_api.h>
#include <string>

#if TDLIB_API_RICH_MESSAGE
// A rich message, laid out as an instant view page is, as HTML for the conversation: its text and
// structure, its media as placeholders with their captions
std::string formatRichMessage(const td::td_api::richMessage &message);
// Its first line of text, plain, as for a quote of it
std::string getRichMessageSummary(const td::td_api::richMessage &message);
#endif

#endif
