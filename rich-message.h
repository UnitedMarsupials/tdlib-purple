#ifndef _RICH_MESSAGE_H
#define _RICH_MESSAGE_H

#include "buildopt.h"
#include <td/telegram/td_api.h>
#include <functional>
#include <string>

#if TDLIB_API_RICH_MESSAGE
// The link to a rich message's medium, by its number in the order they are shown; empty for none
using RichMediaLink = std::function<std::string(size_t index)>;
// A rich message, laid out as an instant view page is, as HTML for the conversation: its text and
// structure, its media as placeholders with their captions, linked to where they can be
std::string formatRichMessage(const td::td_api::richMessage &message, const RichMediaLink &link = RichMediaLink());
// The file of a rich message's medium, by its number, a picture in the size shown; null if none
const td::td_api::file *findRichMessageMedia(const td::td_api::richMessage &message, size_t index,
                                             std::string &description);
// Its first line of text, plain, as for a quote of it
std::string getRichMessageSummary(const td::td_api::richMessage &message);
#endif

#endif
