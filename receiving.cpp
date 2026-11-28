#include "receiving.h"
#include "rich-message.h"
#include "client-utils.h"
#include "format.h"
#include "purple-info.h"
#include "file-transfer.h"
#include "sticker.h"
#include "custom-emoji.h"
#include "media-variants.h"
#include "config.h"
#include "call.h"
#include "pidgin-ui.h"
#include "log-history.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <fmt/format.h>

// The name is chosen by whoever sent the file and routinely contains spaces and other
// characters a URI cannot carry literally, so the path has to be percent-encoded before
// it is handed to a viewer.
std::string makeFileUri(const std::string &filePath)
{
    gchar *uri = g_filename_to_uri(filePath.c_str(), nullptr, nullptr);
    if (uri == nullptr)
        // Only for a relative path, which local_->path_ never is.
        return "file://" + filePath;
    std::string result(uri);
    g_free(uri);
    return result;
}

// ...and then, for Pidgin, the scheme has to be written twice, which deserves an explanation.
//
// Pidgin does not treat the target of a "file://" link as a URI at all. Its handler
// strips the scheme and passes the remainder to purple_notify_uri() as if it were a plain
// filesystem path (pidgin/gtkutils.c, open_file()). purple_uri_escape_for_open() then
// percent-encodes that a second time and hands it to xdg-open, which - seeing no scheme -
// runs it through g_filename_to_uri() and escapes the escapes. A name with a space or a
// non-ASCII letter reaches the viewer as %25D0%25A1...%2520... and cannot be opened; this
// happens whether we encode the path or leave it bare, so it cannot be fixed by encoding.
//
// Writing "file://" twice defeats it: Pidgin strips one and is left holding the complete,
// correctly encoded URI. purple_uri_escape_for_open() permits "%" and "/" and passes such
// a URI through untouched, and every opener it may invoke - xdg-open, gnome-open,
// kfmclient - accepts a URI as readily as a path.
//
// The same handler is the only way Pidgin opens a URI of a scheme it does not know, such as
// "geo:": it follows a link only if it has registered the scheme, and does nothing at all
// otherwise. Other front-ends take a link as it is written, and get it so.
static std::string makeLinkTarget(const std::string &uri)
{
    if (!strcasecmp(getUiName(), "pidgin"))
        return "file://" + uri;
    return uri;
}

static std::string makeFileLinkTarget(const std::string &filePath)
{
    return makeLinkTarget(makeFileUri(filePath));
}

// Anything chosen by a remote party - a file name, a contact's display name, a group
// title - reaches the conversation window, which libpurple renders as HTML. Escape it at
// the point it enters that markup, or a value containing markup characters is mangled or
// swallowed outright. Not for text going to purple_request_*, which Pidgin escapes itself.
std::string escapeForDisplay(const std::string &text)
{
    gchar *escaped = purple_markup_escape_text(text.c_str(), text.size());
    std::string result(escaped);
    g_free(escaped);
    return result;
}

// Names a message by when it was sent, which is what the conversation window shows of it:
// the time, and the date as well unless that is today
std::string formatMessageTime(time_t timestamp)
{
    time_t    now = time(nullptr);
    struct tm today, sent;
    localtime_r(&now, &today);
    localtime_r(&timestamp, &sent);

    if ((sent.tm_year == today.tm_year) && (sent.tm_yday == today.tm_yday))
        return purple_time_format(&sent);
    return purple_date_format_long(&sent);
}

// Of a file not uploaded yet, as one we send is until it is
static const char LOCAL_FILE_KEY[] = "local:";

static std::string getFileKey(const td::td_api::file *file)
{
    if (!file)
        return "";
    // The same file keeps its remote identifier, while its local one may differ between sessions
    if (file->remote_ && !file->remote_->unique_id_.empty())
        return file->remote_->unique_id_;
    return LOCAL_FILE_KEY + std::to_string(file->id_);
}

// A location a message holds: where, and in the case of a venue, what is there. A live one has a
// period it was shared for, and the time left of it; TDLib 1.8.65 gave it a type of its own.
struct ShownLocation {
    const td::td_api::location *location   = nullptr;
    const td::td_api::venue    *venue      = nullptr;
    int32_t                     livePeriod = 0; // 0 if not live, 0x7FFFFFFF if without end
    int32_t                     expiresIn  = 0;
};

static bool getShownLocation(const td::td_api::MessageContent &content, ShownLocation &result)
{
    switch (content.get_id()) {
        case td::td_api::messageVenue::ID: {
            const auto &venue = static_cast<const td::td_api::messageVenue &>(content);
            result.venue = venue.venue_.get();
            if (result.venue)
                result.location = result.venue->location_.get();
            break;
        }
        case td::td_api::messageLocation::ID: {
            const auto &location = static_cast<const td::td_api::messageLocation &>(content);
            result.location = location.location_.get();
#if !TDLIB_API_LIVE_LOCATION
            result.livePeriod = location.live_period_;
            result.expiresIn  = location.expires_in_;
#endif
            break;
        }
#if TDLIB_API_LIVE_LOCATION
        case td::td_api::messageLiveLocation::ID: {
            const auto &location = static_cast<const td::td_api::messageLiveLocation &>(content);
            if (location.location_) {
                result.location   = location.location_->location_.get();
                result.livePeriod = location.location_->live_period_;
            }
            result.expiresIn = location.expires_in_;
            break;
        }
#endif
    }

    return (result.location != nullptr);
}

static bool isStoppedLiveLocation(const ShownLocation &location)
{
    return (location.livePeriod != 0) && (location.expiresIn == 0);
}

// A link to the place as a geo: URI (RFC 5870), which leaves the choice of map to whatever opens
// it. The coordinates are written the same in every locale, with a decimal point.
static std::string makeLocationLink(const td::td_api::location &location)
{
    std::string coordinates = fmt::format("{:.6f}, {:.6f}", location.latitude_, location.longitude_);
    std::string uri         = fmt::format("geo:{:.6f},{:.6f}", location.latitude_, location.longitude_);
    if (location.horizontal_accuracy_ > 0)
        uri += fmt::format(";u={:.0f}", location.horizontal_accuracy_);
    return "<a href=\"" + escapeForDisplay(makeLinkTarget(uri)) + "\">" + coordinates + "</a>";
}

static std::string makeLocationText(const ShownLocation &shown)
{
    std::string link = makeLocationLink(*shown.location);
    std::string text;
    if (shown.venue) {
        text = "<b>" + escapeForDisplay(shown.venue->title_) + "</b>\n";
        if (!shown.venue->address_.empty())
            text += escapeForDisplay(shown.venue->address_) + "\n";
    }

    if ((shown.livePeriod == 0) || isStoppedLiveLocation(shown))
        // TRANSLATOR: In-chat message. Argument is a hyperlink, showing map coordinates.
        text += formatMessage(_("Location: {}"), link);
    else if (shown.livePeriod == 0x7FFFFFFF)
        // TRANSLATOR: In-chat message, for a location shared live with no end set. Argument is a hyperlink, showing map coordinates.
        text += formatMessage(_("Live location: {}"), link);
    else
        // TRANSLATOR: In-chat message. Arguments are the time (or date and time) the live location will cease to be shared at, and a hyperlink, showing map coordinates.
        text += formatMessage(_("Live location until {0}: {1}"),
                              {formatMessageTime(time(nullptr) + shown.expiresIn), link});
    return text;
}

// The formatting of a text, to tell whether an edit changed it
static std::string describeFormatting(const td::td_api::formattedText &text)
{
    std::string result;
    for (const auto &entity: text.entities_)
        if (entity)
            result += td::td_api::to_string(entity);
    return result;
}

// What the conversation shows of a message's content, as far as its sender can change it.
// A change to anything else - a poll's tally, a live location's position, a link preview -
// either shows nowhere or is no edit, and is not reported.
static void getShownContent(const td::td_api::MessageContent &content, ShownMessage &result)
{
    const td::td_api::formattedText *caption = nullptr;

    result.file.clear();
    result.content.clear();
    result.quotable = false;
    // TRANSLATOR: What a message is, as in "reacted with 👍 to message from 12:00", where it has no text to quote. Also for "photo", "sticker" and the like.
    result.kind = _("message");
    // A file, and its caption, of the kinds with one; the first of a photo's sizes is its
    ContentParts parts;
    if (getContentParts(content, parts)) {
        result.file = getFileKey(parts.file);
        caption     = parts.caption;
    }
    switch (content.get_id()) {
        case td::td_api::messageText::ID: {
            const auto &text = static_cast<const td::td_api::messageText &>(content);
            if (text.text_) {
                result.content    = text.text_->text_;
                result.formatting = describeFormatting(*text.text_);
            }
            result.quotable = true;
            break;
        }
        case td::td_api::messagePhoto::ID: {
            const auto &photo = static_cast<const td::td_api::messagePhoto &>(content);
            result.kind = _("photo");
            result.file.clear();
            if (photo.photo_)
                for (const auto &size: photo.photo_->sizes_)
                    if (size && size->photo_) {
                        result.file = getFileKey(size->photo_.get());
                        break;
                    }
            break;
        }
        case td::td_api::messageDocument::ID:
            // TRANSLATOR: What a message is, as in "reacted with 👍 to document from 12:00".
            result.kind = _("document");
            break;
        case td::td_api::messageVideo::ID:
            // TRANSLATOR: What a message is, as in "reacted with 👍 to video from 12:00".
            result.kind = _("video");
            break;
        case td::td_api::messageAnimation::ID:
            // TRANSLATOR: What a message is, as in "reacted with 👍 to animation from 12:00".
            result.kind = _("animation");
            break;
        case td::td_api::messageAudio::ID:
            // TRANSLATOR: What a message is, as in "reacted with 👍 to audio from 12:00".
            result.kind = _("audio");
            break;
        case td::td_api::messageVoiceNote::ID:
            // TRANSLATOR: What a message is, as in "reacted with 👍 to voice message from 12:00".
            result.kind = _("voice message");
            break;
        case td::td_api::messageVideoNote::ID:
            // TRANSLATOR: What a message is, as in "reacted with 👍 to video message from 12:00".
            result.kind = _("video message");
            break;
        case td::td_api::messageSticker::ID:
            result.kind = _("sticker");
            break;
        case td::td_api::messageAnimatedEmoji::ID:
            result.content = static_cast<const td::td_api::messageAnimatedEmoji &>(content).emoji_;
            result.quotable = true;
            break;
        case td::td_api::messageChatChangeTitle::ID:
            result.content = static_cast<const td::td_api::messageChatChangeTitle &>(content).title_;
            break;
        case td::td_api::messagePoll::ID: {
            // Its question and options stay as they are; the votes are no edit, but closing it is
            const auto &poll = static_cast<const td::td_api::messagePoll &>(content);
            if (poll.poll_ && poll.poll_->is_closed_)
                result.content = "closed";
            break;
        }
        case td::td_api::messageLocation::ID:
        case td::td_api::messageVenue::ID:
#if TDLIB_API_LIVE_LOCATION
        case td::td_api::messageLiveLocation::ID:
#endif
        {
            // A live location moves all the time, which is no edit; that it no longer does is shown
            ShownLocation location;
            // TRANSLATOR: What a message is, as in "reacted with 👍 to location from 12:00".
            result.kind = _("location");
            if (getShownLocation(content, location)) {
                if (location.livePeriod == 0)
                    result.content = makeLocationText(location);
                else if (isStoppedLiveLocation(location))
                    result.content = "stopped";
            }
            break;
        }
        default:
            // Its type only: Telegram updates some on its own, as a dice once it has been rolled
            result.content = std::to_string(content.get_id());
    }

    if (caption) {
        result.content    = caption->text_;
        result.formatting = describeFormatting(*caption);
        result.quotable   = true;
    }
}

std::string makeNoticeWithSender(const td::td_api::chat &chat, const TgMessageInfo &message,
                                 const char *noticeText, PurpleAccount *account)
{
    // In a channel, "Channel post" says nothing a notice there does not
    if (message.anonymousChannelPost)
        return noticeText;
    std::string prefix = escapeForDisplay(getSenderDisplayName(chat, message, account));
    if (!prefix.empty())
        prefix += ": ";
    return prefix + noticeText;
}

namespace {
// Markup for a stretch of the text: around it, or in place of it
struct TextMarkup {
    size_t      start;
    size_t      end;
    std::string open;
    std::string close;
    bool        replaces = false; // open goes in place of the text
    bool        quote    = false; // each line of it starts with a bar
};
}

// The bar a quoted line starts with, as the phone app draws one beside the quote
static const char QUOTE_BAR[] = "<font color=\"#808080\">\xe2\x96\x8e</font> ";

// Telegram's formatting as the markup Pidgin's conversation window draws; what it has no markup
// for is left plain: mentions, hashtags, addresses (which Pidgin links itself) and the like
// The plain tags of an entity, and whether one can be sent from Pidgin that way, as the rest cannot
namespace {
struct EntityTags {
    int32_t     id;
    const char *open;
    const char *close;
    bool        sendable;
};
}
static const EntityTags EntityTagList[] = {
    {td::td_api::textEntityTypeBold::ID,          "<b>",                    "</b>",    true},
    {td::td_api::textEntityTypeItalic::ID,        "<i>",                    "</i>",    true},
    {td::td_api::textEntityTypeUnderline::ID,     "<u>",                    "</u>",    true},
    {td::td_api::textEntityTypeStrikethrough::ID, "<s>",                    "</s>",    true},
    {td::td_api::textEntityTypeCode::ID,          "<font face=\"monospace\">", "</font>", false},
    {td::td_api::textEntityTypePre::ID,           "<pre>",                  "</pre>",  false},
    {td::td_api::textEntityTypePreCode::ID,       "<pre>",                  "</pre>",  false},
    // Hidden until selected, which shows it in the selection's colors
    {td::td_api::textEntityTypeSpoiler::ID, "<font color=\"#808080\" back=\"#808080\">", "</font>", false},
};

static bool getEntityMarkup(const td::td_api::TextEntityType &type, TextMarkup &markup, bool sendable)
{
    for (const EntityTags &tags: EntityTagList)
        if (tags.id == type.get_id()) {
            if (sendable && !tags.sendable)
                return false;
            markup.open  = tags.open;
            markup.close = tags.close;
            return true;
        }

    switch (type.get_id()) {
    case td::td_api::textEntityTypeTextUrl::ID: {
        // A link to where it may not go is shown as its text alone
        const std::string &url = static_cast<const td::td_api::textEntityTypeTextUrl &>(type).url_;
        if (isLinkSchemeAllowed(url)) {
            markup.open  = "<a href=\"" + escapeForDisplay(url) + "\">";
            markup.close = "</a>";
        }
        return true;
    }
    case td::td_api::textEntityTypeBlockQuote::ID:
    case td::td_api::textEntityTypeExpandableBlockQuote::ID:
        // Not sent from Pidgin
        if (sendable)
            return false;
        markup.open  = QUOTE_BAR;
        markup.quote = true;
        return true;
    default:
        return false;
    }
}

std::string getMessageText(const td::td_api::formattedText &text,
                           const std::map<int64_t, std::string> *smileys, bool sendable)
{
    std::vector<TextMarkup> markups;
    for (const auto &entity: text.entities_) {
        if (!entity || !entity->type_ || (entity->length_ <= 0))
            continue;
        TextMarkup markup;
        markup.start = utf16OffsetToBytes(text.text_, entity->offset_);
        markup.end   = utf16OffsetToBytes(text.text_, entity->offset_ + entity->length_);
        if (entity->type_->get_id() == td::td_api::textEntityTypeCustomEmoji::ID) {
            // The stretch a custom emoji covers is its code instead, if it has one
            auto it = smileys ? smileys->find(static_cast<const td::td_api::textEntityTypeCustomEmoji &>(*entity->type_).custom_emoji_id_)
                              : std::map<int64_t, std::string>::const_iterator();
            if (!smileys || (it == smileys->end()))
                continue;
            markup.open     = escapeForDisplay(it->second);
            markup.replaces = true;
        } else if (!getEntityMarkup(*entity->type_, markup, sendable))
            continue;
        if (markup.start < markup.end)
            markups.push_back(std::move(markup));
    }

    // Where markup opens and closes: closing before opening at the same place, the inner first;
    // opening, the outer first. Telegram nests its entities, never crossing them.
    struct Edge {
        size_t   position;
        bool     opens;
        size_t   index;
    };
    std::vector<Edge> edges;
    for (size_t i = 0; i < markups.size(); i++) {
        edges.push_back({markups[i].start, true, i});
        edges.push_back({markups[i].end, false, i});
    }
    std::sort(edges.begin(), edges.end(), [&markups](const Edge &a, const Edge &b) {
        if (a.position != b.position)
            return a.position < b.position;
        if (a.opens != b.opens)
            return !a.opens;
        const TextMarkup &x = markups[a.index], &y = markups[b.index];
        if (a.opens)
            return (x.end != y.end) ? (x.end > y.end) : (a.index < b.index);
        return (x.start != y.start) ? (x.start > y.start) : (a.index > b.index);
    });

    std::string result;
    size_t      position = 0;
    size_t      skipTo   = 0; // past the text a custom emoji's code replaces
    unsigned    quotes   = 0;
    auto addText = [&](size_t end) {
        size_t from = std::max(position, skipTo);
        if (end > from) {
            std::string escaped = escapeForDisplay(text.text_.substr(from, end - from));
            if (quotes)
                for (size_t newline = escaped.find('\n'); newline != std::string::npos;
                     newline = escaped.find('\n', newline + 1))
                {
                    escaped.insert(newline + 1, QUOTE_BAR);
                    newline += sizeof(QUOTE_BAR) - 1;
                }
            result += escaped;
        }
        position = std::max(position, end);
    };
    for (const Edge &edge: edges) {
        addText(edge.position);
        const TextMarkup &markup = markups[edge.index];
        if (edge.opens) {
            result += markup.open;
            if (markup.replaces)
                skipTo = std::max(skipTo, markup.end);
            if (markup.quote)
                quotes++;
        } else {
            result += markup.close;
            if (markup.quote)
                quotes--;
        }
    }
    addText(text.text_.size());
    return result;
}

unsigned getMessageWaitSeconds(const TgMessageInfo &message)
{
    enum { MESSAGE_WAIT_SECONDS = 1, HISTORY_WAIT_SECONDS = 5 };
    return message.fromHistory ? HISTORY_WAIT_SECONDS : MESSAGE_WAIT_SECONDS;
}

int addImageFile(gchar *data, size_t length, const std::string &filePath)
{
    gchar *name = g_path_get_basename(filePath.c_str());
    int    id   = purple_imgstore_add_with_id(data, length, name);
    g_free(name);
    return id;
}

std::string makeInlineImageText(int imgstoreId, bool ownLine)
{
    return (ownLine ? "\n" : "") + std::string("<img id=\"") + std::to_string(imgstoreId) + "\">";
}

static PurpleMessageFlags getNotificationFlags(PurpleMessageFlags extraFlags)
{
    unsigned flags = (extraFlags & PURPLE_MESSAGE_ERROR) | (extraFlags & PURPLE_MESSAGE_NO_LOG);
    if (flags == 0)
        flags = PURPLE_MESSAGE_SYSTEM;

    return (PurpleMessageFlags)flags;
}

const td::td_api::chat *findConversationChat(TdAccountData &account, PurpleConversation *conv)
{
    PurpleConversationType convType = purple_conversation_get_type(conv);
    const char            *convName = purple_conversation_get_name(conv);

    if (convType == PURPLE_CONV_TYPE_IM) {
        UserId       privateChatUserId = purpleBuddyNameToUserId(convName);
        SecretChatId secretChatId      = purpleBuddyNameToSecretChatId(convName);

        if (privateChatUserId.valid())
            return account.getPrivateChatByUserId(privateChatUserId);
        if (secretChatId.valid())
            return account.getChatBySecretChat(secretChatId);
    } else if (convType == PURPLE_CONV_TYPE_CHAT)
        return account.getChat(getTdlibChatId(convName));
    return nullptr;
}

void sendConversationReadReceipts(TdAccountData &account, PurpleConversation *conv)
{
    if (!conversationHasFocus(conv))
        return;
    ChatId chatId;
    if (purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_CHAT)
        chatId = getTdlibChatId(purple_conversation_get_name(conv));
    else if (const td::td_api::chat *tdlibChat = findConversationChat(account, conv))
        chatId = getId(*tdlibChat);

    std::vector<ReadReceipt> receipts;
    account.extractPendingReadReceipts(chatId, receipts);
    // Read as they are seen on screen, rather than all at once
    if (viewsMessages(conv))
        receipts.clear();

    // Seen now, as the phone app takes them to be once the chat is opened
    if (hasUnreadReactions(account, conv)) {
        purple_debug_misc(config::pluginId, "Reading reactions in chat %" G_GINT64_FORMAT "\n", chatId.value());
        account.transceiver.sendQuery(td::td_api::make_object<td::td_api::readAllChatReactions>(chatId.value()), nullptr);
    }
    // Oldest first, each once: TDLib keeps polling the reactions of only the last few it is told
    std::sort(receipts.begin(), receipts.end(), [](const ReadReceipt &a, const ReadReceipt &b) {
        return a.messageId < b.messageId;
    });
    receipts.erase(std::unique(receipts.begin(), receipts.end(),
                               [](const ReadReceipt &a, const ReadReceipt &b) { return a.messageId == b.messageId; }),
                   receipts.end());
    if (!receipts.empty()) {
        purple_debug_misc(config::pluginId, "Sending %zu read receipts for chat %" G_GINT64_FORMAT "\n",
                          receipts.size(), chatId.value());
        td::td_api::object_ptr<td::td_api::viewMessages> viewMessagesReq = td::td_api::make_object<td::td_api::viewMessages>();
        viewMessagesReq->chat_id_ = chatId.value();
        viewMessagesReq->force_read_ = true; // no idea what "closed chats" are at this point
        viewMessagesReq->message_ids_.resize(receipts.size());
        for (size_t i = 0; i < receipts.size(); i++)
            viewMessagesReq->message_ids_[i] = receipts[i].messageId.value();
        account.transceiver.sendQuery(std::move(viewMessagesReq), nullptr);
    }
}

void showMessageTextIm(TdAccountData &account, const char *purpleUserName, const char *text,
                       const char *notification, time_t timestamp, PurpleMessageFlags flags)
{
    PurpleConversation *conv = nullptr;

    if (text) {
        if (flags & PURPLE_MESSAGE_SEND) {
            // serv_got_im seems to work for messages sent from another client, but not for
            // echoed messages from this client. Therefore, this (code snippet from facebook plugin).
            conv = getImConversation(account.purpleAccount, purpleUserName);
            purple_conv_im_write(purple_conversation_get_im_data(conv),
                                 purple_account_get_name_for_display(account.purpleAccount),
                                 text, flags, timestamp);
        } else {
            serv_got_im(purple_account_get_connection(account.purpleAccount), purpleUserName, text,
                        flags, timestamp);
            conv = getImConversation(account.purpleAccount, purpleUserName);
        }
    }

    if (notification) {
        if (conv == nullptr)
            conv = getImConversation(account.purpleAccount, purpleUserName);
        purple_conv_im_write(purple_conversation_get_im_data(conv), purpleUserName, notification,
                             getNotificationFlags(flags), timestamp);
    }

    // TODO: sending all pending read receipts for the chat is technically not quite right,
    // because maybe a message is being shown while others are waiting for some asynchronous
    // response before they can be displayed. But who cares.
    if (conv != nullptr)
        sendConversationReadReceipts(account, conv);
}

static void showMessageTextChat(TdAccountData &account, const td::td_api::chat &chat,
                                const TgMessageInfo &message, const char *text,
                                const char *notification, PurpleMessageFlags flags)
{
    // Again, doing what facebook plugin does
    int purpleId = account.getPurpleChatId(getId(chat));
    PurpleConvChat *conv = getChatConversation(account, chat, purpleId);

    if (text) {
        if (flags & PURPLE_MESSAGE_SEND) {
            if (conv)
                purple_conv_chat_write(conv, purple_account_get_name_for_display(account.purpleAccount),
                                       text, flags, message.timestamp);
        } else {
            if (purpleId != 0)
                serv_got_chat_in(purple_account_get_connection(account.purpleAccount), purpleId,
                                 message.getGroupchatSender().c_str(),
                                 flags, text, message.timestamp);
        }
    }

    if (notification) {
        if (conv)
            // Protocol plugins mostly use who="" for such messages, but this currently causes problems
            // with Spectrum. Use some non-empty string. Pidgin will ignore the who parameter for
            // notification messages.
            purple_conv_chat_write(conv, " ", notification, getNotificationFlags(flags), message.timestamp);
    }

    // TODO: sending all pending read receipts for the chat is technically not quite right,
    // because maybe a message is being shown while others are waiting for some asynchronous
    // response before they can be displayed. But who cares.
    PurpleConversation *baseConv = conv ? purple_conv_chat_get_conversation(conv) : nullptr;
    if (baseConv != nullptr)
        sendConversationReadReceipts(account, baseConv);
}

static std::string quoteMessage(const td::td_api::message *message, TdAccountData &account)
{
    const td::td_api::user *originalAuthor = nullptr;
    if (message)
        originalAuthor = account.getUser(getSenderUserId(*message));

    std::string originalName;
    if (originalAuthor)
        originalName = account.getDisplayName(*originalAuthor);
    else if (message && message->sender_id_ &&
             (message->sender_id_->get_id() == td::td_api::messageSenderChat::ID))
    {
        // Posted by a channel, or by a group in its own name: signed by its author, or else by the
        // chat itself
        const td::td_api::chat *senderChat = account.getChat(
            getChatId(static_cast<const td::td_api::messageSenderChat &>(*message->sender_id_)));
        if (!message->author_signature_.empty())
            originalName = message->author_signature_;
        else if (senderChat)
            originalName = senderChat->title_;
        else
            originalName = _("Channel post");
    } else {
        // message == NULL means it could not be fetched, or took too long to fetch
        // TRANSLATOR: In-line placeholder if the original author of a quote is unknown. Is at the beginning of the line if and only if you make it so, see "<b>&bt {} wrote:"...
        originalName = _("Unknown user");
    }

    std::string text;
    if (!message || !message->content_) {
        // TRANSLATOR: In-chat placeholder when something unknown is being replied to.
        text = _("[message unavailable]");
    } else switch (message->content_->get_id()) {
        case td::td_api::messageText::ID: {
            const td::td_api::messageText &messageText = static_cast<const td::td_api::messageText &>(*message->content_);
            // Plain, as the quote's one line has no room for formatting, and escaped below
            if (messageText.text_)
                text = messageText.text_->text_;
            else
                text = "";
            break;
        }
        case td::td_api::messagePhoto::ID: {
            const td::td_api::messagePhoto &photo = static_cast<const td::td_api::messagePhoto &>(*message->content_);
            // TRANSLATOR: In-line placeholder when a photo is being replied to.
            text = _("[photo]");
            if (photo.caption_)
                text += " " + photo.caption_->text_;
            break;
        }
        case td::td_api::messageDocument::ID: {
            const td::td_api::messageDocument &document = static_cast<const td::td_api::messageDocument &>(*message->content_);
            if (document.document_) {
                // TRANSLATOR: In-line placeholder when a file is being replied to. Arguments will be the file name and MIME type (e.g. "application/gzip")
                text = formatMessage(_("[file: {0} ({1})]"), {document.document_->file_name_,
                                                           document.document_->mime_type_});
            } else {
                // Not supposed to be possible, but just in case
                text = "[file]";
            }
            if (document.caption_)
                text += " " + document.caption_->text_;
            break;
        }
        case td::td_api::messageVideo::ID: {
            const td::td_api::messageVideo &video = static_cast<const td::td_api::messageVideo &>(*message->content_);
            if (video.video_) {
                // TRANSLATOR: In-line placeholder when a video is being replied to. Argument will be the file name.
                text = formatMessage(_("[video: {}]"), video.video_->file_name_);
            } else {
                // Not supposed to be possible, but just in case
                text = "[video]";
            }
            if (video.caption_)
                text += " " + video.caption_->text_;
            break;
        }
        case td::td_api::messageSticker::ID:
            // TRANSLATOR: In-line placeholder when a sticker is being replied to.
            text = _("[sticker]");
            break;
        case td::td_api::messageAnimatedEmoji::ID: {
            const td::td_api::messageAnimatedEmoji &animatedEmoji =
                static_cast<const td::td_api::messageAnimatedEmoji &>(*message->content_);
            text = animatedEmoji.emoji_;
            break;
        }
        case td::td_api::messageLocation::ID:
#if TDLIB_API_LIVE_LOCATION
        case td::td_api::messageLiveLocation::ID:
#endif
        case td::td_api::messageVenue::ID: {
            ShownLocation location;
            getShownLocation(*message->content_, location);
            // TRANSLATOR: In-line placeholder when a location is being replied to.
            text = _("[location]");
            if (location.venue)
                text += " " + location.venue->title_;
            break;
        }
#if TDLIB_API_RICH_MESSAGE
        case td::td_api::messageRichMessage::ID: {
            const auto &rich = static_cast<const td::td_api::messageRichMessage &>(*message->content_);
            text = rich.message_ ? getRichMessageSummary(*rich.message_) : std::string();
            break;
        }
#endif
        default:
            text = '[' + getUnsupportedMessageDescription(*message->content_) + ']';
            break;
    }

    for (unsigned i = 0; i < text.size(); i++)
        if (text[i] == '\n') text[i] = ' ';

    // TRANSLATOR: In-chat notification of a reply. Arguments will be username and the original text or description thereof. Please preserve the HTML.
    return formatMessage(_("<b>&gt; {0} wrote:</b>\n&gt; {1}"),
                         {escapeForDisplay(originalName),
                          message ? linkToMessage(account.purpleAccount, getChatId(*message), getId(*message),
                                                  escapeForDisplay(text))
                                  : escapeForDisplay(text)});
}

// The name a private chat's conversation goes by
// By id, as any user's buddy goes; the buddy, which shows it by the user's name, added if they are
// not a contact
static std::string getPrivateChatPurpleName(TdAccountData &account, const td::td_api::user &user)
{
    return addTransientBuddy(account, user);
}

// The chat's conversation, if it is open; none is opened
PurpleConversation *findMessageConversation(TdAccountData &account, const td::td_api::chat &chat)
{
    const td::td_api::user *privateUser = account.getUserByPrivateChat(chat);
    if (privateUser)
        return purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, getPurpleBuddyName(*privateUser).c_str(),
                                                     account.purpleAccount);
    SecretChatId secretChatId = getSecretChatId(chat);
    if (secretChatId.valid())
        return purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, getSecretChatBuddyName(secretChatId).c_str(),
                                                     account.purpleAccount);
    PurpleConvChat *conv = findChatConversation(account.purpleAccount, chat);
    return conv ? purple_conv_chat_get_conversation(conv) : nullptr;
}

PurpleConversation *findMessageConversation(TdAccountData &account, ChatId chatId)
{
    const td::td_api::chat *chat = account.getChat(chatId);
    return chat ? findMessageConversation(account, *chat) : nullptr;
}

PurpleConversation *getMessageConversation(TdAccountData &account, const td::td_api::chat &chat)
{
    const td::td_api::user *privateUser = account.getUserByPrivateChat(chat);
    if (privateUser)
        return getImConversation(account.purpleAccount, getPrivateChatPurpleName(account, *privateUser).c_str());

    SecretChatId secretChatId = getSecretChatId(chat);
    if (secretChatId.valid())
        return getImConversation(account.purpleAccount, getSecretChatBuddyName(secretChatId).c_str());

    if (isGroupChat(chat)) {
        PurpleConvChat *conv = getChatConversation(account, chat, account.getPurpleChatId(getId(chat)));
        return conv ? purple_conv_chat_get_conversation(conv) : nullptr;
    }
    return nullptr;
}

static void writeLog(TdAccountData &account, const UnloggedSent &sent, MessageId newId);
static void holdImages(UnloggedSent &waiting);
static void releaseImages(UnloggedSent &waiting);

void showMessageText(TdAccountData &account, const td::td_api::chat &chat, const TgMessageInfo &message,
                     const char *text, const char *notification, uint32_t extraFlags)
{
    if (account.isChatDeleted(getId(chat))) {
        purple_debug_misc(config::pluginId, "Not showing what came of chat %" G_GINT64_FORMAT ", deleted\n",
                          getId(chat).value());
        return;
    }
    PurpleMessageFlags directionFlag = message.outgoing ? PURPLE_MESSAGE_SEND : PURPLE_MESSAGE_RECV;
    PurpleMessageFlags flags = (PurpleMessageFlags) (extraFlags | directionFlag);
    // Not logged (again): with the conversation's logging off while it is written, as Pidgin shows
    // a message flagged as not to be logged without its time and sender
    // Ours, being sent, under an id it will not keep, is logged once sent, under the one it does, if
    // the conversation logs then: its tab, if still open, or Pidgin's preference for a new one; and
    // as ever where there is no tab to write it into unlogged. One sent already, while waiting to be
    // shown, has that id now.
    bool                unlogged     = message.unlogged || (message.redrawn && !message.logRedrawn);
    // One shown with pictures still coming is logged once they are here, as it is shown then, shown
    // again with them, however that is: written into its tab unlogged meanwhile, the tab made first,
    // so that it shows when and from whom it is
    bool                deferred     = text && logMarkersEnabled() && !message.sentLocally && message.id.valid() &&
                                       ((message.picturesComing && !unlogged) ||
                                        account.hasUnloggedSent(getId(chat), message.id));
    if (deferred) {
        unlogged = true;
        getMessageConversation(account, chat);
    }
    MessageId           loggedId     = message.sentLocally ? account.getSentMessageId(getId(chat), message.id)
                                                           : message.id;
    bool                sent         = !message.sentLocally || (loggedId != message.id);
    bool                unsent       = text && !sent && message.id.valid() && !message.unlogged &&
                                       logMarkersEnabled();
    PurpleConversation *unloggedConv = (unlogged || unsent) ? findMessageConversation(account, chat) : nullptr;
    unsent = unsent && unloggedConv && purple_conversation_is_logging(unloggedConv);
    bool                logging      = unloggedConv && purple_conversation_is_logging(unloggedConv);
    if (logging)
        purple_conversation_set_logging(unloggedConv, FALSE);
    else if (unlogged && !unloggedConv)
        flags = (PurpleMessageFlags) (flags | PURPLE_MESSAGE_NO_LOG);
    if (message.outgoing && !message.sentLocally)
        flags = (PurpleMessageFlags) (flags | PURPLE_MESSAGE_REMOTE_SEND);
    // Shown again, or caught up with, rather than coming now: Pidgin makes no sound for it, nor do
    // notifying plugins
    if (message.redrawn || message.fromHistory)
        flags = (PurpleMessageFlags) (flags | PURPLE_MESSAGE_DELAYED);

    std::string newText;
    if (text) {
        if (message.repliedMessageId.valid())
            newText = quoteMessage(message.repliedMessage.get(), account);
        if (!message.forwardedFrom.empty()) {
            if (!newText.empty())
                newText += "\n";
            // TRANSLATOR: In-chat notification of forward. Argument will be a username. Please preserve the HTML.
            newText += formatMessage(_("<b>Forwarded from {}:</b>"),
                                     escapeForDisplay(message.forwardedFrom));
        }
        // A picture begins a line of its own already
        if (!newText.empty() && (*text != '\n'))
            newText += "\n";
        newText += text;
    }
    if (!newText.empty())
        text = newText.c_str();

    std::string forLog = text ? text : "";

    // Which message it is, for the log, once it has its id for good: whatever the front-end now, a
    // tab of Pidgin's may be filled from the log later
    std::string marked;
    if (text && loggedId.valid() && sent && logMarkersEnabled()) {
        marked = text + makeLogMarker(loggedId);
        text   = marked.c_str();
    }

    // Marks for the message's own text, not for a notice about it, which carries its id
    MessageMarkScope markScope(text ? message.id : MessageId::invalid);
    const td::td_api::user *privateUser = account.getUserByPrivateChat(chat);
    if (privateUser) {
        std::string userName = getPrivateChatPurpleName(account, *privateUser);
        showMessageTextIm(account, userName.c_str(), text, notification, message.timestamp, flags);
    }

    SecretChatId secretChatId = getSecretChatId(chat);
    if (secretChatId.valid()) {
        std::string userName = getSecretChatBuddyName(secretChatId);
        showMessageTextIm(account, userName.c_str(), text, notification, message.timestamp, flags);
    }

    if (isGroupChat(chat))
        showMessageTextChat(account, chat, message, text, notification, flags);

    if (logging && g_list_find(purple_get_conversations(), unloggedConv))
        purple_conversation_set_logging(unloggedConv, TRUE);
    // What is to be logged later, of the text given
    auto makeUnlogged = [&](const char *logged) {
        return UnloggedSent{logged, notification ? notification : "", (uint32_t)(flags & ~PURPLE_MESSAGE_DELAYED),
                            message.timestamp, purple_conversation_get_type(unloggedConv),
                            purple_conversation_get_name(unloggedConv)};
    };
    if (unsent)
        account.addUnloggedSent(getId(chat), message.id, makeUnlogged(text));
    if (deferred && unloggedConv) {
        UnloggedSent shown = makeUnlogged(forLog.c_str());
        shown.awaitingPictures = true;
        if (!message.outgoing)
            shown.who = privateUser ? getPrivateChatPurpleName(account, *privateUser) :
                        secretChatId.valid() ? getSecretChatBuddyName(secretChatId) :
                        message.getGroupchatSender();
        UnloggedSent earlier;
        if (account.extractUnloggedSent(getId(chat), message.id, earlier))
            releaseImages(earlier);
        if (message.picturesComing) {
            holdImages(shown);
            account.addUnloggedSent(getId(chat), message.id, std::move(shown));
        } else
            writeLog(account, shown, message.id);
    }
}

// Shown, with its pictures, if they are coming, as stand-ins: logged once they are here
static void showMessageWithPictures(TdAccountData &account, const td::td_api::chat &chat, TgMessageInfo &message,
                                    const char *text, bool images, bool coming)
{
    message.picturesComing = coming;
    showMessageText(account, chat, message, text, nullptr, images ? PURPLE_MESSAGE_IMAGES : 0);
    message.picturesComing = false;
}

// The pictures of a message waiting to be logged stay in the image store until it is
static void holdImages(UnloggedSent &waiting)
{
    static const std::string IMG_ID = "<img id=\"";
    for (size_t pos = waiting.text.find(IMG_ID); pos != std::string::npos;
         pos = waiting.text.find(IMG_ID, pos + 1)) {
        int id = atoi(waiting.text.substr(pos + IMG_ID.size()).c_str());
        if (id && purple_imgstore_find_by_id(id)) {
            purple_imgstore_ref_by_id(id);
            waiting.imageIds.push_back(id);
        }
    }
}

static void releaseImages(UnloggedSent &waiting)
{
    for (int id: waiting.imageIds)
        purple_imgstore_unref_by_id(id);
    waiting.imageIds.clear();
}

void logMessagesAwaitingPictures(TdAccountData &account)
{
    std::vector<std::tuple<ChatId, MessageId, UnloggedSent>> messages;
    account.extractAwaitingPictures(messages);
    for (auto &message: messages) {
        if (!account.isChatDeleted(std::get<0>(message)))
            writeLog(account, std::get<2>(message), std::get<1>(message));
        releaseImages(std::get<2>(message));
    }
}

void logSentMessage(TdAccountData &account, ChatId chatId, MessageId oldId, MessageId newId)
{
    UnloggedSent sent;
    // Nor of a chat deleted since it was sent
    if (!account.extractUnloggedSent(chatId, oldId, sent))
        return;
    if (!account.isChatDeleted(chatId))
        writeLog(account, sent, newId);
    releaseImages(sent);
}

// Into the log alone, under the message's id: of the conversation it was written into, if still open,
// or else a log of its own
static void writeLog(TdAccountData &account, const UnloggedSent &sent, MessageId newId)
{
    std::string             marked = sent.text + makeLogMarker(newId, sent.timestamp);
    PurpleMessageFlags      flags  = (PurpleMessageFlags)sent.flags;
    const char             *self   = !sent.who.empty() ? sent.who.c_str()
                                                           : purple_account_get_name_for_display(account.purpleAccount);
    PurpleConversationType  type   = (PurpleConversationType)sent.conversationType;
    PurpleConversation     *conv   = purple_find_conversation_with_account(type, sent.conversationName.c_str(),
                                                                           account.purpleAccount);
    if (!conv) {
        // Its tab closed meanwhile: into a log of its own, as Pidgin would begin one for it, if a tab
        // opened now would log
        if (!purple_prefs_get_bool((type == PURPLE_CONV_TYPE_CHAT) ? "/purple/logging/log_chats"
                                                                   : "/purple/logging/log_ims"))
            return;
        PurpleLog *log = purple_log_new((type == PURPLE_CONV_TYPE_CHAT) ? PURPLE_LOG_CHAT : PURPLE_LOG_IM,
                                        sent.conversationName.c_str(), account.purpleAccount, nullptr,
                                        time(nullptr), nullptr);
        purple_log_write(log, flags, self, sent.timestamp, marked.c_str());
        if (!sent.notification.empty())
            purple_log_write(log, getNotificationFlags(flags), "", sent.timestamp, sent.notification.c_str());
        purple_log_free(log);
        return;
    }
    // For the log alone, which a tab filled anew takes it from: the tab has it already
    std::string text = makeLogOnly(marked);
    if (type == PURPLE_CONV_TYPE_IM) {
        purple_conv_im_write(purple_conversation_get_im_data(conv), self, text.c_str(), flags, sent.timestamp);
        if (!sent.notification.empty())
            purple_conv_im_write(purple_conversation_get_im_data(conv), purple_conversation_get_name(conv),
                                 makeLogOnly(sent.notification).c_str(), getNotificationFlags(flags),
                                 sent.timestamp);
    } else if (type == PURPLE_CONV_TYPE_CHAT) {
        purple_conv_chat_write(purple_conversation_get_chat_data(conv), self, text.c_str(), flags, sent.timestamp);
        if (!sent.notification.empty())
            purple_conv_chat_write(purple_conversation_get_chat_data(conv), " ",
                                   makeLogOnly(sent.notification).c_str(), getNotificationFlags(flags),
                                   sent.timestamp);
    }
}

void showChatNotification(TdAccountData &account, const td::td_api::chat &chat,
                          const char *notification, time_t timestamp, PurpleMessageFlags extraFlags)
{
    TgMessageInfo messageInfo;
    messageInfo.type = TgMessageInfo::Type::Other;
    messageInfo.timestamp = timestamp;
    messageInfo.outgoing = true;
    showMessageText(account, chat, messageInfo, nullptr, notification, extraFlags);
}

void showChatNotification(TdAccountData &account, const td::td_api::chat &chat,
                          const char *notification, PurpleMessageFlags extraFlags)
{
    showChatNotification(account, chat, notification,
                         (extraFlags & PURPLE_MESSAGE_NO_LOG) ? 0 : time(nullptr), extraFlags);
}

static void showDownloadedImage(const td::td_api::chat &chat, TgMessageInfo &message,
                                const std::string &filePath, const char *caption,
                                TdAccountData &account)
{
    gchar  *data = nullptr;
    size_t  len  = 0;

    // A front-end that shows no pictures gets the link to the file, and so does one whose picture
    // cannot be read: Pidgin shows pictures from its image store only, never from <img src=...>
    if (!canShowImages() || !g_file_get_contents(filePath.c_str(), &data, &len, nullptr)) {
        // TRANSLATOR: File-type, used to describe what is being downloaded, in sentences like "Downloading photo" or "Ignoring photo download".
        showGenericFileInline(chat, message, filePath, caption, _("photo"), std::string(), account);
        return;
    }

    std::string text = makeInlineImageText(addImageFile(data, len, filePath));
    if (!message.mediaVariantLinks.empty())
        text += "\n" + message.mediaVariantLinks;
    if (caption && *caption)
        text += "\n" + std::string(caption);

    showMessageText(account, chat, message, text.c_str(), nullptr, PURPLE_MESSAGE_IMAGES);
}

bool isStickerAnimated(const std::string &filePath)
{
    return (filePath.size() >= 4) && !strcmp(filePath.c_str() + filePath.size() - 4, ".tgs");
}

bool shouldConvertAnimatedSticker(const TgMessageInfo &message, const PurpleAccount *purpleAccount)
{
#ifndef NoLottie
    // Not for a front-end that shows no pictures anyway
    return !message.outgoing && canShowImages() &&
           purple_account_get_bool(purpleAccount, AccountOptions::AnimatedStickers,
                                   AccountOptions::AnimatedStickersDefault);
#else
    return false;
#endif
}

static void showDownloadedSticker(const td::td_api::chat &chat, TgMessageInfo &message,
                                  const std::string &filePath,
                                  const std::string &fileDescription,
                                  td::td_api::object_ptr<td::td_api::file> thumbnail,
                                  TdAccountData &account)
{
    // Where pictures can be shown, a sticker goes by showSticker(); here an animation is not shown
    if (isStickerAnimated(filePath)) {
        if (thumbnail) {
            // Avoid message like "Downloading sticker thumbnail...
            // Also ignore size limits, but only determined testers and crazy people would notice.
            if (thumbnail->local_ && thumbnail->local_->is_downloading_completed_)
                showDownloadedSticker(chat, message, thumbnail->local_->path_,
                                      fileDescription, nullptr, account);
            else if (downloadsChatMedia(account, chat))
                downloadFileInline(thumbnail->id_, getId(chat), message, fileDescription, nullptr,
                                   account);
            else
                showGenericFileInline(chat, message, filePath, nullptr, fileDescription, std::string(), account);
        } else {
            showGenericFileInline(chat, message, filePath, nullptr, fileDescription, std::string(), account);
        }
    } else {
        showStickerFile(chat, message, filePath, fileDescription, account);
    }
}

static void showFileLink(const td::td_api::chat &chat, const TgMessageInfo &message,
                         const std::string &linkTarget, const char *caption,
                         const std::string &fileDescription, const std::string &preview,
                         TdAccountData &account);

void showGenericFileInline(const td::td_api::chat &chat, const TgMessageInfo &message,
                           const std::string &filePath, const char *caption,
                           const std::string &fileDescription, const std::string &preview,
                           TdAccountData &account)
{
    showFileLink(chat, message, makeFileLinkTarget(filePath), caption, fileDescription, preview, account);
}

static void showFileLink(const td::td_api::chat &chat, const TgMessageInfo &message,
                         const std::string &linkTarget, const char *caption,
                         const std::string &fileDescription, const std::string &preview,
                         TdAccountData &account)
{
    // A video's thumbnail leads, so that the link to the video sits directly beneath the picture.
    // The picture is part of the link, although Pidgin 2.14 will only ever activate one from its
    // text: gtk_imhtml_image_add_to() renders an inline image as a widget of its own, and link
    // activation runs through the tag "event" signal, which fires for text. Saying that the
    // picture belongs to the link costs nothing meanwhile, and is what a conversation widget
    // would have to be told in order to make it clickable.
    std::string text = preview.empty() ? "" : "\n";
    text += "<a href=\"" + escapeForDisplay(linkTarget) + "\">";
    if (!preview.empty())
        text += preview + "\n";
    text += escapeForDisplay(fileDescription) + "</a>";
    if (!message.mediaVariantLinks.empty())
        text += "\n" + message.mediaVariantLinks;
    if (caption && *caption) {
        text += "\n";
        text += caption;
    }
    showMessageText(account, chat, message, text.c_str(), nullptr,
                    preview.empty() ? 0 : PURPLE_MESSAGE_IMAGES);
}

void showDownloadedFileInline(ChatId chatId, TgMessageInfo &message,
                              const std::string &filePath, const char *caption,
                              const std::string &fileDescription,
                              td::td_api::object_ptr<td::td_api::file> thumbnail,
                              const std::string &preview,
                              TdAccountData &account)
{
    const td::td_api::chat *chat = account.getChat(chatId);
    if (!chat) return;

    switch (message.type) {
    case TgMessageInfo::Type::Photo:
        showDownloadedImage(*chat, message, filePath, caption, account);
        break;
    case TgMessageInfo::Type::Sticker:
        showDownloadedSticker(*chat, message, filePath, fileDescription, std::move(thumbnail),
                              account);
        break;
    case TgMessageInfo::Type::Other:
        showGenericFileInline(*chat, message, filePath, caption, fileDescription, preview, account);
        break;
    }
}

static void showTextMessage(const td::td_api::chat &chat, const TgMessageInfo &message,
                            const td::td_api::messageText &text, TdAccountData &account)
{
    if (text.text_) {
        // Its custom emoji, as smileys of the conversation, which has to be there for them first
        std::vector<CustomEmoji>       emoji;
        std::map<int64_t, std::string> smileys;
        getCustomEmoji(*text.text_, emoji);
        if (!emoji.empty())
            smileys = addCustomEmojiSmileys(account, getMessageConversation(account, chat), emoji);
        std::string displayText = getMessageText(*text.text_, &smileys);
        showMessageText(account, chat, message, displayText.c_str(), nullptr);
    }
}

struct InlineDownloadInfo {
    int32_t         fileId;
    ChatId          chatId;
    TgMessageInfo   message;
    std::string     fileDescription;
    TdAccountData  *account;
};

static void startInlineDownload(void *user_data)
{
    std::unique_ptr<InlineDownloadInfo> info(static_cast<InlineDownloadInfo *>(user_data));
    downloadFileInline(info->fileId, info->chatId, info->message, info->fileDescription,
                       nullptr, *info->account);
}

static void ignoreInlineDownload(InlineDownloadInfo *info)
{
    delete info;
}

static void requestInlineDownload(const td::td_api::file &file,
                                  const std::string &fileDesc, const td::td_api::chat &chat,
                                  TgMessageInfo &message, TdAccountData &account)
{
    // TRANSLATOR: Download dialog, primary content, argument will be a username.
    std::string question = formatMessage(_("Download file from {}?"),
                                         getSenderDisplayName(chat, message, account.purpleAccount));
    unsigned    size     = getFileSize(file);
    // This dialog is used for files larger than the limit, so size should be non-zero
    char *      sizeStr  = purple_str_size_to_units(size);
    // TRANSLATOR: Download dialog, placeholder chat title, in the sentence "posted in a private chat".
    std::string chatName = isPrivateChat(chat) ? _("a private chat") : chat.title_;
    // TRANSLATOR: Download dialog, secondary content. Arguments will be file description (text), chat name (text), and a file size (text!)
    std::string fileInfo = formatMessage(_("{0} posted in {1}, size: {2}"), {fileDesc,
                                         chatName, std::string(sizeStr)});
    g_free(sizeStr);

    InlineDownloadInfo *info = new InlineDownloadInfo;
    info->fileId = file.id_;
    info->chatId = getId(chat);
    info->message = std::move(message);
    info->fileDescription = fileDesc;
    info->account = &account;

    // TRANSLATOR: Download dialog, title
    purple_request_action(purple_account_get_connection(account.purpleAccount), _("Download"), question.c_str(),
                          fileInfo.c_str(), 0, account.purpleAccount, nullptr, nullptr,
                          // TRANSLATOR: Download dialog, alternative is "_No"
                          info, 2, _("_Yes"), startInlineDownload,
                          // TRANSLATOR: Download dialog, alternative is "_Yes"
                          _("_No"), ignoreInlineDownload);
}

// A file shown as a link to its message, rather than to the file, which the plugin opens when the
// link is clicked; pictures, which are shown as they are, still need the file before they can be
static bool linksToMessage(const IncomingMessage &fullMessage)
{
    return messageLinksEnabled() && (fullMessage.messageInfo.type == TgMessageInfo::Type::Other);
}

static std::string makeStandInText(const std::string &lowGrade, int32_t width, int32_t height, bool clear,
                                   const std::string &words, bool &image);

// A video's thumbnail, as markup for the conversation window: as fetched, or its stand-in while it
// comes, where it is to be written over; empty if there is none
static std::string makePreviewImageText(const IncomingMessage &fullMessage)
{
    gchar *data = nullptr;
    gsize  len  = 0;

    if (fullMessage.previewPath.empty() && fullMessage.previewFileId && canShowImages()) {
        bool image;
        std::string text = makeStandInText(fullMessage.previewLowGrade, fullMessage.previewWidth,
                                           fullMessage.previewHeight, false, std::string(), image);
        return image ? text.substr(1) : std::string();
    }
    if (fullMessage.previewPath.empty() ||
        !g_file_get_contents(fullMessage.previewPath.c_str(), &data, &len, nullptr))
        return "";

    int id = addImageFile(data, len, fullMessage.previewPath);
    return "<img id=\"" + std::to_string(id) + "\">";
}

// A picture not here yet is shown at once, as a stand-in of its size, rather than the message
// waiting for it: where the front-end can show pictures, and so write one over its stand-in
static bool showsPictureComing([[maybe_unused]] const IncomingMessage &fullMessage)
{
#ifndef NoPidgin
    return canShowImages() && ((fullMessage.messageInfo.type == TgMessageInfo::Type::Photo) ||
                               (fullMessage.messageInfo.type == TgMessageInfo::Type::Sticker));
#else
    // Built without Pidgin, nothing can be written over
    return false;
#endif
}

static void refreshShownMessage(TdAccountData &account, ChatId chatId, MessageId messageId);
static void fetchPicture(TdAccountData &account, ChatId chatId, MessageId messageId, int32_t fileId);

// A picture within the limit on what is fetched unasked, shown as its stand-in, until it is here:
// whether it is fetched now, as it is where the chat's media are. Either way, it is fetched again
// as the message comes into view, while it is still a stand-in.
static bool fetchesPicture(TdAccountData &account, const td::td_api::chat &chat, MessageId messageId,
                           int32_t fileId)
{
    account.addStandIn(getId(chat), messageId, fileId);
    return downloadsChatMedia(account, chat);
}

void fetchViewedPictures(TdAccountData &account, PurpleConversation *conv, const std::vector<MessageId> &messageIds)
{
    const td::td_api::chat *chat = findConversationChat(account, conv);
    if (!chat || !downloadsChatMedia(account, *chat))
        return;
    for (MessageId messageId: messageIds)
        for (int32_t fileId: account.getStandIns(getId(*chat), messageId))
            fetchPicture(account, getId(*chat), messageId, fileId);
}

// For a picture shown as its stand-in, not to be fetched unasked: a link that fetches it, where
// there is no other, as to its sizes
static std::string makeFetchLink(TdAccountData &account, ChatId chatId, MessageId messageId,
                                 const td::td_api::file &file, const std::string &fileDesc)
{
    if (!messageLinksEnabled())
        return std::string();
    char       *size = purple_str_size_to_units(getFileSize(file));
    std::string link = "<a href=\"" + makeMessageLink(account.purpleAccount, chatId, messageId) + "\">" +
                       escapeForDisplay(fileDesc) + ", " + size + "</a>";
    g_free(size);
    return link;
}

// The size the picture is shown in, and the low-grade one the message carries, if any
static void getPictureLook(const td::td_api::MessageContent &content, int32_t fileId, int32_t &width,
                           int32_t &height, std::string &lowGrade)
{
    width  = 0;
    height = 0;
    lowGrade.clear();
    if (content.get_id() == td::td_api::messagePhoto::ID) {
        const auto &photo = static_cast<const td::td_api::messagePhoto &>(content);
        if (!photo.photo_)
            return;
        for (const auto &size: photo.photo_->sizes_)
            if (size && size->photo_ && (size->photo_->id_ == fileId)) {
                width  = size->width_;
                height = size->height_;
            }
        if (photo.photo_->minithumbnail_)
            lowGrade = photo.photo_->minithumbnail_->data_;
    }
}

// Pictures come one after another, a rich message's often a dozen together: in Pidgin's tab, the
// message is shown again once for those come within a moment, not once for each
enum { REFRESH_DELAY_MS = 700 };
static void refreshShownMessageSoon(TdAccountData &account, ChatId chatId, MessageId messageId)
{
    if (!pidginUiFound()) {
        refreshShownMessage(account, chatId, messageId);
        return;
    }
    if (account.isRefreshDue(chatId, messageId))
        return;
    auto *refresh = new std::function<void()>([&account, chatId, messageId]() {
        account.setRefreshDue(chatId, messageId, 0);
        refreshShownMessage(account, chatId, messageId);
    });
    // Removed with the account, if it goes first
    guint sourceId = g_timeout_add_full(G_PRIORITY_DEFAULT, REFRESH_DELAY_MS, [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()> *>(data))();
        return FALSE;
    }, refresh, [](gpointer data) {
        delete static_cast<std::function<void()> *>(data);
    });
    account.setRefreshDue(chatId, messageId, sourceId);
}

// Fetched for the message, which is shown again once it is here. One not got is left as its
// stand-in, to be tried again as the message comes into view: the message, unchanged, is not shown
// again, but logged, if it waited for that, once none of its pictures is still coming.
static void fetchPicture(TdAccountData &account, ChatId chatId, MessageId messageId, int32_t fileId)
{
    if (!account.startPictureFetch(chatId, messageId, fileId))
        return;
    purple_debug_misc(config::pluginId, "Fetching picture (file id %d) of message %" G_GINT64_FORMAT "\n",
                      (int)fileId, messageId.value());
    auto request = makeDownloadFile(fileId, FILE_DOWNLOAD_PRIORITY, true);
    account.transceiver.sendQuery(std::move(request),
        [&account, chatId, messageId, fileId](uint64_t, td::td_api::object_ptr<td::td_api::Object> object) {
            account.endPictureFetch(chatId, messageId, fileId);
            if (!getDownloadPath(object).empty())
                refreshShownMessageSoon(account, chatId, messageId);
            else {
                purple_debug_misc(config::pluginId, "Picture (file id %d) of message %" G_GINT64_FORMAT
                                  " not fetched\n", (int)fileId, messageId.value());
                if (!account.isFetchingPictures(chatId, messageId) && !account.isRefreshDue(chatId, messageId))
                    logSentMessage(account, chatId, messageId, messageId);
            }
        });
}

// Stand-ins made for the message being shown, whose own references to them in the image store go
// once it is: the tab holds its own, and the log, if it waits, its own
static std::vector<int> g_shownStandIns;

static int makeShownStandIn(const std::string &lowGrade, int32_t width, int32_t height, bool clear)
{
    int imageId = makeStandInPicture(lowGrade, width, height, clear);
    if (imageId)
        g_shownStandIns.push_back(imageId);
    return imageId;
}

// The stand-in of the size given, as markup, or the words for it where there can be none
static std::string makeStandInText(const std::string &lowGrade, int32_t width, int32_t height, bool clear,
                                   const std::string &words, bool &image)
{
    int imageId = makeShownStandIn(lowGrade, width, height, clear);
    image = (imageId != 0);
    return image ? makeInlineImageText(imageId) : ("[" + escapeForDisplay(words) + "]");
}

// Converted for the message, which is shown again with its animation once it is, as is any other
// message showing the same sticker meanwhile; one that cannot be is shown as if not to be converted
static void convertSticker(TdAccountData &account, const TgMessageInfo &message, ChatId chatId,
                           int32_t fileId, const std::string &path)
{
    if (!account.startStickerConversion(fileId, chatId, message.id))
        return;
    StickerConversionThread *thread = new StickerConversionThread(account.purpleAccount, path, chatId, &message);
    thread->fileId = fileId;
    thread->owner  = account.lifetime;
    thread->startThread();
}

void stickerConverted(TdAccountData &account, int32_t fileId, int imageId)
{
    if (!imageId)
        account.setConversionFailed(fileId);
    std::vector<std::pair<ChatId, MessageId>> waiting;
    account.endStickerConversion(fileId, imageId, waiting);
    for (const auto &message: waiting)
        refreshShownMessageSoon(account, message.first, message.second);
}

// A sticker, here or not: as its picture, or its animation converted, or its thumbnail where
// animations are not converted, or a stand-in for whichever of them is still to come
static void showSticker(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                        const td::td_api::sticker &sticker, const std::string &fileDesc, TdAccountData &account)
{
    const td::td_api::file &file    = *sticker.sticker_;
    TgMessageInfo          &message = fullMessage.messageInfo;
    const std::string      &words   = message.altText.empty() ? fileDesc : message.altText;
    bool                    here    = file.local_ && file.local_->is_downloading_completed_;
    int32_t                 fetched = 0;
    bool                    image;
    std::string             text;

    if (here && !isStickerAnimated(file.local_->path_)) {
        showStickerFile(chat, message, file.local_->path_, fileDesc, account);
        return;
    }
    bool convert = shouldConvertAnimatedSticker(message, account.purpleAccount) &&
                   !(here && account.hasConversionFailed(file.id_));
    if (here && convert) {
        int converted = account.getConvertedSticker(file.id_);
        if (converted) {
            text = makeInlineImageText(converted);
            showMessageText(account, chat, message, text.c_str(), nullptr, PURPLE_MESSAGE_IMAGES);
            return;
        }
        // As big as the animation will be
        text = makeStandInText("", StickerConversionThread::DEFAULT_SIZE, StickerConversionThread::DEFAULT_SIZE,
                               true, words, image);
        showMessageWithPictures(account, chat, message, text.c_str(), image, true);
        convertSticker(account, message, getId(chat), file.id_, file.local_->path_);
        return;
    }
    if (here) {
        // Its thumbnail in place of the animation, taken from it as the message was made
        const td::td_api::file *thumbnail = fullMessage.thumbnail.get();
        if (thumbnail && thumbnail->local_ && thumbnail->local_->is_downloading_completed_) {
            showStickerFile(chat, message, thumbnail->local_->path_, fileDesc, account);
            return;
        }
        if (!thumbnail) {
            showGenericFileInline(chat, message, file.local_->path_, nullptr, words, std::string(), account);
            return;
        }
        if (fetchesPicture(account, chat, message.id, thumbnail->id_))
            fetched = thumbnail->id_;
        else if (!messageLinksEnabled()) {
            // With no link to fetch the thumbnail, the sticker's own file
            showGenericFileInline(chat, message, file.local_->path_, nullptr, words, std::string(), account);
            return;
        }
        text    = makeStandInText("", sticker.thumbnail_ ? sticker.thumbnail_->width_ : 0,
                                  sticker.thumbnail_ ? sticker.thumbnail_->height_ : 0, true, words, image);
    } else {
        if (isSizeWithinLimit(getFileSizeKb(file), fullMessage.inlineFileSizeLimit) &&
            fetchesPicture(account, chat, message.id, file.id_))
            fetched = file.id_;
        bool tgs = sticker.format_ && (sticker.format_->get_id() == td::td_api::stickerFormatTgs::ID);
        // As big as what will be shown: the animation, or else its thumbnail in its place
        if (tgs && convert)
            text = makeStandInText("", StickerConversionThread::DEFAULT_SIZE,
                                   StickerConversionThread::DEFAULT_SIZE, true, words, image);
        else if (tgs && sticker.thumbnail_)
            text = makeStandInText("", sticker.thumbnail_->width_, sticker.thumbnail_->height_, true, words, image);
        else
            text = makeStandInText("", sticker.width_, sticker.height_, true, words, image);
    }
    if (!fetched) {
        std::string link = makeFetchLink(account, getId(chat), message.id, file, fileDesc);
        if (!link.empty())
            text += "\n" + link;
    }
    showMessageWithPictures(account, chat, message, text.c_str(), image, fetched != 0);
    if (fetched)
        fetchPicture(account, getId(chat), message.id, fetched);
}

// Its stand-in, the message logged once the picture is here, if it is fetched unasked
static void showPictureComing(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                              const td::td_api::file &file, const char *caption, const std::string &fileDesc,
                              TdAccountData &account)
{
    int32_t     width, height;
    std::string lowGrade;
    getPictureLook(*fullMessage.message->content_, file.id_, width, height, lowGrade);
    bool        image;
    std::string text = makeStandInText(lowGrade, width, height, false, fileDesc, image);
    TgMessageInfo &message = fullMessage.messageInfo;
    bool wanted = isSizeWithinLimit(getFileSizeKb(file), fullMessage.inlineFileSizeLimit) &&
                  fetchesPicture(account, chat, message.id, file.id_);
    std::string links = message.mediaVariantLinks;
    if (links.empty() && !wanted)
        links = makeFetchLink(account, getId(chat), message.id, file, fileDesc);
    if (!links.empty())
        text += "\n" + links;
    if (caption)
        text += "\n" + std::string(caption);

    showMessageWithPictures(account, chat, message, text.c_str(), image, wanted);
    if (wanted)
        fetchPicture(account, getId(chat), message.id, file.id_);
}

static void showFileInline(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                           const td::td_api::file &file, const char *caption,
                           const std::string &fileDesc,
                           TdAccountData &account)
{
    // Whether it is fetched now, asked about, or let be
    enum class Fetch { Now, Ask, Skip };
    std::string notice;
    Fetch       fetch    = Fetch::Skip;
    unsigned    fileSize = getFileSizeKb(file);
    const td::td_api::MessageContent *content = fullMessage.message ? fullMessage.message->content_.get() : nullptr;

    if (caption && (*caption == '\0'))
        caption = nullptr;

    // A video's thumbnail goes inside the hyperlink where there is one (see showGenericFileInline);
    // where there is only a notice, it goes above the caption. Either way it precedes the caption.
    std::string preview = makePreviewImageText(fullMessage);

    if (content && showsPictureComing(fullMessage)) {
        if (content->get_id() == td::td_api::messageSticker::ID) {
            const auto &sticker = static_cast<const td::td_api::messageSticker &>(*content).sticker_;
            if (sticker && sticker->sticker_) {
                showSticker(chat, fullMessage, *sticker, fileDesc, account);
                return;
            }
        }
        if (!(file.local_ && file.local_->is_downloading_completed_)) {
            showPictureComing(chat, fullMessage, file, caption, fileDesc, account);
            return;
        }
    }

    // Fetched and opened only when clicked, if it is not here by then, so it is no reason to wait
    if (linksToMessage(fullMessage)) {
        showFileLink(chat, fullMessage.messageInfo,
                     makeMessageLink(account.purpleAccount, getId(chat), fullMessage.messageInfo.id),
                     caption, fileDesc, preview, account);
        // Fetched beforehand, within the limit, so that a click opens it at once; of a video in
        // several sizes, the one the account asks for
        const td::td_api::file *prefetch = &file;
        if (content && (content->get_id() == td::td_api::messageVideo::ID)) {
            std::vector<MediaVariant> variants;
            getMediaVariants(*content, variants);
            prefetch = choosePrefetchVariant(account.purpleAccount, variants, fullMessage.inlineFileSizeLimit);
        } else if (!isSizeWithinLimit(fileSize, fullMessage.inlineFileSizeLimit))
            prefetch = nullptr;
        // Not where the chat's media are not fetched unasked: then only once clicked
        if (prefetch && !(prefetch->local_ && prefetch->local_->is_downloading_completed_) &&
            downloadsChatMedia(account, chat))
        {
            purple_debug_misc(config::pluginId, "Fetching %s (file id %d) in the background\n",
                              fileDesc.c_str(), (int)prefetch->id_);
            auto request = makeDownloadFile(prefetch->id_, BACKGROUND_DOWNLOAD_PRIORITY, false);
            account.transceiver.sendQuery(std::move(request), nullptr);
        }
        return;
    }

    // Where the chat's media are not fetched unasked, a file not here is let be, as one too big is
    bool downloads = fullMessage.downloadsMedia;
    if (file.local_ && file.local_->is_downloading_completed_)
        fetch = Fetch::Now;
    else if (downloads && isSizeWithinLimit(fileSize, fullMessage.inlineFileSizeLimit)) {
        // Sticker with a caption is not a thing but just in case it was: don't skip "Downloading..."
        // notification for such hypothetical object because it will also include caption
        bool bareSticker = (fullMessage.messageInfo.type == TgMessageInfo::Type::Sticker) && !caption;
        if (!bareSticker && !fullMessage.inlineDownloadComplete) {
            // TRANSLATOR: In-chat notification, appears after a colon (':'). Argument is a file *type*, not a filename.
            notice = formatMessage(_("Downloading {}"), escapeForDisplay(fileDesc));
        }
        fetch = Fetch::Now;
    } else if (downloads && !ignoreBigDownloads(account.purpleAccount) && !fullMessage.messageInfo.redrawn) {
        // TRANSLATOR: In-chat notification, appears after a colon (':'). Argument is a file *type*, not a filename.
        notice = formatMessage(_("Requesting {} download"), escapeForDisplay(fileDesc));
        fetch  = Fetch::Ask;
    } else {
        char *fileSizeStr = purple_str_size_to_units(fileSize); // File size above limit, so it's non-zero
        // TRANSLATOR: In-chat notification, appears after a colon (':'). Arguments are a file *type*, not a filename; second argument is a file size with unit.
        notice = formatMessage(_("Ignoring {0} download ({1})"), {escapeForDisplay(fileDesc), std::string(fileSizeStr)});
        g_free(fileSizeStr);
    }

    if (!notice.empty())
        notice = makeNoticeWithSender(chat, fullMessage.messageInfo, notice.c_str(),
                                      account.purpleAccount);

    // Notice means file isn't downloaded yet or is ignored. Either way, show caption as well, and
    // the links to other sizes, which may be within reach. A thumbnail then starts the message, so
    // it gets a line of its own, as a photo does.
    if (!notice.empty()) {
        const std::string &links = fullMessage.messageInfo.mediaVariantLinks;
        if (preview.empty() && links.empty())
            showMessageText(account, chat, fullMessage.messageInfo, caption, notice.c_str());
        else {
            std::string text = preview.empty() ? links : ("\n" + preview);
            if (!preview.empty() && !links.empty())
                text += "\n" + links;
            if (caption) {
                text += "\n";
                text += caption;
            }
            showMessageText(account, chat, fullMessage.messageInfo, text.c_str(), notice.c_str(),
                            preview.empty() ? 0 : PURPLE_MESSAGE_IMAGES);
        }
    }

    if (fetch == Fetch::Skip)
        return;
    if (file.local_ && file.local_->is_downloading_completed_)
        showDownloadedFileInline(getId(chat), fullMessage.messageInfo, file.local_->path_,
                                 caption, fileDesc, std::move(fullMessage.thumbnail), preview,
                                 account);
    else if (fetch == Fetch::Ask)
        requestInlineDownload(file, fileDesc, chat, fullMessage.messageInfo, account);
    else if (fullMessage.inlineDownloadComplete)
        showDownloadedFileInline(getId(chat), fullMessage.messageInfo, fullMessage.inlineDownloadedFilePath,
                                 caption, fileDesc, std::move(fullMessage.thumbnail), preview,
                                 account);
    // When download takes too long, message will leave PendingMessageQueue and be "shown".
    // However, nothing more should be done at that point except keep waiting for the download.
    else if (!fullMessage.inlineDownloadTimeout) {
        purple_debug_misc(config::pluginId, "Downloading %s (file id %d)\n", fileDesc.c_str(), (int)file.id_);
        downloadFileInline(file.id_, getId(chat), fullMessage.messageInfo, fileDesc,
                           std::move(fullMessage.thumbnail), account);
    }
}

static void showPhotoMessage(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                             const td::td_api::file *photoSize, const std::string &caption,
                             TdAccountData &account)
{
    const char *captionCstr = !caption.empty() ? caption.c_str() : nullptr;

    if (photoSize) {
        // TRANSLATOR: File-type, used to describe what is being downloaded, in sentences like "Downloading photo" or "Ignoring photo download".
        showFileInline(chat, fullMessage, *photoSize, captionCstr, _("photo"),
                       account);
    } else {
        // Unlikely message not worth translating
        std::string notice = makeNoticeWithSender(chat, fullMessage.messageInfo, "Faulty image",
                                                  account.purpleAccount);
        showMessageText(account, chat, fullMessage.messageInfo, captionCstr, notice.c_str());
    }
}

static void showFileMessage(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                            const td::td_api::file* file,
                            const std::string &caption,
                            const std::string &fileDescription,
                            const std::string &fileName,
                            TdAccountData &account)
{
    const char *captionStr = !caption.empty() ? caption.c_str() : nullptr;
    if (!file) {
        // Unlikely message not worth translating
        std::string notice = formatMessage("Faulty file: {}", escapeForDisplay(fileDescription));
        notice = makeNoticeWithSender(chat, fullMessage.messageInfo, notice.c_str(),
                                      account.purpleAccount);
        showMessageText(account, chat, fullMessage.messageInfo, captionStr, notice.c_str());
    } else {
        if ( !fullMessage.standardDownloadConfigured || !chat.type_ ||
             ((chat.type_->get_id() != td::td_api::chatTypePrivate::ID) &&
              (chat.type_->get_id() != td::td_api::chatTypeSecret::ID)) )
        {
            showFileInline(chat, fullMessage, *file, captionStr, fileDescription,
                           account);
        } else {
            // The transfer itself puts nothing in the conversation, so the caption goes there
            // alone, below the video's thumbnail, if any, which gets a line of its own
            std::string preview = makePreviewImageText(fullMessage);
            if (!preview.empty()) {
                std::string text = "\n" + preview;
                if (captionStr) {
                    text += "\n";
                    text += captionStr;
                }
                showMessageText(account, chat, fullMessage.messageInfo, text.c_str(), nullptr,
                                PURPLE_MESSAGE_IMAGES);
            } else if (captionStr)
                showMessageText(account, chat, fullMessage.messageInfo, captionStr, nullptr);
            // Not asked for again as the message is shown again with its thumbnail
            if (!fullMessage.refreshing)
                requestStandardDownload(getId(chat), fullMessage.messageInfo, fileName, *file,
                                        account);
        }
    }
}

static void showStickerMessage(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                               td::td_api::messageSticker &stickerContent,
                               TdAccountData &account)
{
    if (!stickerContent.sticker_) return;
    td::td_api::sticker &sticker = *stickerContent.sticker_;

    if (sticker.sticker_)
        // TRANSLATOR: File-type, used to describe what is being downloaded, in sentences like "Downloading photo" or "Ignoring photo download".
        showFileInline(chat, fullMessage, *sticker.sticker_, nullptr, _("sticker"),
                       account);
}

static void showPendingEdit(TdAccountData &account, ChatId chatId, MessageId messageId);

static void showUnsupportedMessage(const td::td_api::chat &chat, const TgMessageInfo &messageInfo,
                                   const td::td_api::MessageContent &content, TdAccountData &account)
{
    // TRANSLATOR: In-chat error message, argument will be a Telegram type.
    std::string notice = getUnsupportedMessageDescription(content);
    notice = makeNoticeWithSender(chat, messageInfo, notice.c_str(), account.purpleAccount);
    showMessageText(account, chat, messageInfo, nullptr, notice.c_str());

    // Show what the message holds anyway, in TDLib's own words. Escaping is what keeps any
    // markup in it inert; the font merely lines up the indentation.
    std::string text = "\n<font face=\"monospace\">" +
                       escapeForDisplay(describeUnsupportedContent(content)) + "</font>";
    showMessageText(account, chat, messageInfo, text.c_str(), nullptr);
}


// Shows a message again because it was edited: a line saying so, as of the edit, which names the
// message by the time it was sent and holds the new text, where text is all there is to show.
// Returns false if the message is to follow in full, as it does when it brings a new file.
static bool showEditNotice(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                           TdAccountData &account)
{
    const TgMessageInfo &messageInfo = fullMessage.messageInfo;
    const td::td_api::MessageContent &content = *fullMessage.message->content_;
    std::string          sender      = escapeForDisplay(getSenderDisplayName(chat, messageInfo,
                                                                            account.purpleAccount));
    std::string          sentAt      = linkToMessage(account.purpleAccount, getId(chat), messageInfo.id,
                                                     formatMessageTime(fullMessage.editedMessageTime));
    FileInfo             fileInfo;
    getFileFromMessage(fullMessage, fileInfo);

    // A channel's post signed by nobody has no sender to name, "Channel post" being no one who
    // could edit it, so the notice says what was done, not who did it
    bool anonymous = messageInfo.anonymousChannelPost;

    ShownLocation location;
    std::string   notice;
    bool          complete = true;
    if (getShownLocation(content, location) && isStoppedLiveLocation(location)) {
        std::string link = makeLocationLink(*location.location);
        if (anonymous)
            // TRANSLATOR: In-chat notification in a channel. Arguments are the time (or date and time) the message was sent at, as the conversation window shows it, and a hyperlink, showing the map coordinates last shared.
            notice = formatMessage(_("The live location from {0} is no longer shared: {1}"), {sentAt, link});
        else
            // TRANSLATOR: In-chat notification. Arguments are a username, the time (or date and time) the message was sent at, as the conversation window shows it, and a hyperlink, showing the map coordinates last shared.
            notice = formatMessage(_("{0} stopped sharing the live location from {1}: {2}"),
                                   {sender, sentAt, link});
    } else if (fullMessage.captionOnly && fileInfo.caption.empty()) {
        if (anonymous)
            // TRANSLATOR: In-chat notification in a channel. Argument is the time (or date and time) the message was sent at, as the conversation window shows it.
            notice = formatMessage(_("The caption of the message from {0} was removed"), sentAt);
        else
            // TRANSLATOR: In-chat notification. Arguments are a username and the time (or date and time) the message was sent at, as the conversation window shows it.
            notice = formatMessage(_("{0} removed the caption of the message from {1}"), {sender, sentAt});
    } else if (fullMessage.captionOnly || (content.get_id() == td::td_api::messageText::ID)) {
        const td::td_api::messageText *text = nullptr;
        if (content.get_id() == td::td_api::messageText::ID)
            text = static_cast<const td::td_api::messageText *>(&content);
        std::string newText = fullMessage.captionOnly ? fileInfo.caption :
                              (text && text->text_) ? getMessageText(*text->text_) : std::string();
        if (fullMessage.replacing) {
            // The tab shows the new text where the message is, so the notice there only says it
            // was edited, and leaves the new text to the log
            std::string shown;
            if (anonymous)
                // TRANSLATOR: In-chat notification in a channel, not logged, when the message's new text is shown in its place. Argument is the time (or date and time) the message was sent at, as the conversation window shows it.
                shown = formatMessage(_("The message from {0} was edited"), sentAt);
            else
                // TRANSLATOR: In-chat notification, not logged, when the message's new text is shown in its place. Arguments are a username and the time (or date and time) the message was sent at, as the conversation window shows it.
                shown = formatMessage(_("{0} edited the message from {1}"), {sender, sentAt});
            bool wasUnlogged = messageInfo.unlogged;
            fullMessage.messageInfo.unlogged = true;
            showMessageText(account, chat, messageInfo, nullptr, shown.c_str());
            fullMessage.messageInfo.unlogged = wasUnlogged;
        }
        if (anonymous)
            // TRANSLATOR: In-chat notification in a channel. Arguments are the time (or date and time) the message was sent at, as the conversation window shows it, and the message's new text.
            notice = formatMessage(_("The message from {0} was edited: {1}"), {sentAt, newText});
        else
            // TRANSLATOR: In-chat notification. Arguments are a username, the time (or date and time) the message was sent at, as the conversation window shows it, and the message's new text.
            notice = formatMessage(_("{0} edited the message from {1}: {2}"), {sender, sentAt, newText});
        if (fullMessage.replacing)
            notice = makeLogOnly(notice);
    } else {
        if (anonymous)
            // TRANSLATOR: In-chat notification in a channel, followed by the message as edited. Argument is the time (or date and time) the message was sent at, as the conversation window shows it.
            notice = formatMessage(_("The message from {0} was edited:"), sentAt);
        else
            // TRANSLATOR: In-chat notification, followed by the message as edited. Arguments are a username and the time (or date and time) the message was sent at, as the conversation window shows it.
            notice = formatMessage(_("{0} edited the message from {1}:"), {sender, sentAt});
        complete = false;
    }
    showMessageText(account, chat, messageInfo, nullptr, notice.c_str());

    return complete;
}

// The line of a message's reactions, under it in Pidgin's tab: each with its count, if more than
// one, ours in bold.
// Custom-emoji ones are left out, as they would need looking up first.
namespace {
// A reaction as the line of them shows it: its emoji, or a custom one's id
struct ReactionCount {
    std::string emoji;
    int64_t     customEmojiId = 0;
    int32_t     count         = 0;
    bool        chosen        = false;
    std::string key;
};
}

std::string getReactionTypeKey(const td::td_api::ReactionType &type)
{
    switch (type.get_id()) {
    case td::td_api::reactionTypeEmoji::ID:
        return "e" + static_cast<const td::td_api::reactionTypeEmoji &>(type).emoji_;
    case td::td_api::reactionTypeCustomEmoji::ID:
        return "x" + std::to_string(static_cast<const td::td_api::reactionTypeCustomEmoji &>(type).custom_emoji_id_);
    default:
        return "p";
    }
}

static std::vector<ReactionCount> getReactionCounts(const td::td_api::messageInteractionInfo *info)
{
    std::vector<ReactionCount> counts;
    if (!info || !info->reactions_)
        return counts;
    for (const auto &reaction: info->reactions_->reactions_) {
        if (!reaction || !reaction->type_)
            continue;
        ReactionCount item;
        if (reaction->type_->get_id() == td::td_api::reactionTypeEmoji::ID)
            item.emoji = static_cast<const td::td_api::reactionTypeEmoji &>(*reaction->type_).emoji_;
        else if (reaction->type_->get_id() == td::td_api::reactionTypePaid::ID)
            item.emoji = "\xe2\xad\x90";
        else if (reaction->type_->get_id() == td::td_api::reactionTypeCustomEmoji::ID)
            item.customEmojiId = static_cast<const td::td_api::reactionTypeCustomEmoji &>(*reaction->type_).custom_emoji_id_;
        else
            continue;
        item.count  = reaction->total_count_;
        item.chosen = reaction->is_chosen_;
        item.key    = getReactionTypeKey(*reaction->type_);
        counts.push_back(std::move(item));
    }
    return counts;
}

// Each with its count, if more than one, ours in bold. A custom emoji is the conversation's custom smiley, its picture
// fetched unless already there, where the front-end has them; otherwise left out, its ordinary emoji
// unknown until it is fetched.
static std::vector<ShownReaction> formatReactions(TdAccountData &account, PurpleConversation *conv,
                                                  const std::vector<ReactionCount> &counts)
{
    // Stands for the custom emoji until its picture is in
    static const char CUSTOM_EMOJI_PLACEHOLDER[] = "\xe2\x97\x8c";
    std::vector<CustomEmoji> customEmoji;
    for (const ReactionCount &reaction: counts)
        if (reaction.customEmojiId)
            customEmoji.push_back(CustomEmoji{reaction.customEmojiId, CUSTOM_EMOJI_PLACEHOLDER});
    std::map<int64_t, std::string> smileys;
    if (!customEmoji.empty())
        smileys = addCustomEmojiSmileys(account, conv, customEmoji);

    std::vector<ShownReaction> line;
    for (const ReactionCount &reaction: counts) {
        std::string emoji;
        if (!reaction.customEmojiId)
            emoji = escapeForDisplay(reaction.emoji);
        else {
            auto it = smileys.find(reaction.customEmojiId);
            if (it == smileys.end())
                continue;
            emoji = it->second;
        }
        std::string item = emoji;
        if (reaction.count > 1)
            item += "&nbsp;" + std::to_string(reaction.count);
        if (reaction.chosen)
            item = "<b>" + item + "</b>";
        line.push_back({std::move(item), reaction.key});
    }
    return line;
}

// Shows the message's reactions under it once it has been written
class ReactionsLine {
public:
    ReactionsLine(TdAccountData &account, const td::td_api::chat &chat, MessageId messageId,
                  const td::td_api::messageInteractionInfo *info)
    : m_account(account), m_chat(chat), m_messageId(messageId), m_counts(getReactionCounts(info)) {}
    ~ReactionsLine()
    {
        if (m_counts.empty())
            return;
        // Not where it would not be shown, nor its custom emoji fetched
        PurpleConversation *conv = pidginUiFound() ? findMessageConversation(m_account, m_chat) : nullptr;
        std::vector<ShownReaction> line;
        if (conv)
            line = formatReactions(m_account, conv, m_counts);
        if (!line.empty())
            showMessageReactions(conv, m_messageId, line);
    }
private:
    TdAccountData             &m_account;
    const td::td_api::chat    &m_chat;
    MessageId                  m_messageId;
    std::vector<ReactionCount> m_counts;
};

// A button's key: its row and column, and the address of a link, which its tooltip shows
static const char BUTTON_KEY_PREFIX[] = "button ";

bool isButtonKey(const std::string &key)
{
    return key.compare(0, strlen(BUTTON_KEY_PREFIX), BUTTON_KEY_PREFIX) == 0;
}

bool parseButtonKey(const std::string &key, size_t &row, size_t &column, std::string &url)
{
    if (!isButtonKey(key))
        return false;
    const char *rest = key.c_str() + strlen(BUTTON_KEY_PREFIX);
    char       *end;
    row = strtoul(rest, &end, 10);
    if ((end == rest) || (*end != ' '))
        return false;
    rest   = end + 1;
    column = strtoul(rest, &end, 10);
    if ((end == rest) || (*end && (*end != ' ')))
        return false;
    url = *end ? end + 1 : "";
    return true;
}

// Each row on a line of its own, each button in brackets, coloured as its style is; a link's marked
// with an arrow
static std::vector<ShownReaction> formatButtons(const td::td_api::ReplyMarkup *markup)
{
    std::vector<ShownReaction> lines;
    if (!markup || (markup->get_id() != td::td_api::replyMarkupInlineKeyboard::ID))
        return lines;
    const auto &keyboard = static_cast<const td::td_api::replyMarkupInlineKeyboard &>(*markup);
    for (size_t row = 0; row < keyboard.rows_.size(); row++)
        for (size_t column = 0; column < keyboard.rows_[row].size(); column++) {
            const td::td_api::inlineKeyboardButton *button = keyboard.rows_[row][column].get();
            if (!button || !button->type_)
                continue;
            std::string url;
            if (button->type_->get_id() == td::td_api::inlineKeyboardButtonTypeUrl::ID)
                url = static_cast<const td::td_api::inlineKeyboardButtonTypeUrl &>(*button->type_).url_;
            else if (button->type_->get_id() == td::td_api::inlineKeyboardButtonTypeLoginUrl::ID)
                url = static_cast<const td::td_api::inlineKeyboardButtonTypeLoginUrl &>(*button->type_).url_;
            const char *color = "#3465a4";
#if TDLIB_API_BUTTON_STYLE
            if (button->style_) {
                if (button->style_->get_id() == td::td_api::buttonStyleDanger::ID)
                    color = "#cc0000";
                else if (button->style_->get_id() == td::td_api::buttonStyleSuccess::ID)
                    color = "#4e9a06";
            }
#endif
            std::string html = formatMessage("<font color=\"{}\">[&nbsp;{}{}&nbsp;]</font>",
                                             {std::string(color), escapeForDisplay(button->text_),
                                              std::string(url.empty() ? "" : "&nbsp;\xe2\x86\x97")});
            ShownReaction item;
            item.html    = std::move(html);
            item.key     = BUTTON_KEY_PREFIX + std::to_string(row) + ' ' + std::to_string(column) +
                           (url.empty() ? std::string() : ' ' + url);
            item.newLine = (column == 0);
            lines.push_back(std::move(item));
        }
    return lines;
}

// Shows the message's buttons under it once it has been written, before its reactions
class ButtonsLine {
public:
    ButtonsLine(TdAccountData &account, const td::td_api::chat &chat, MessageId messageId,
                const td::td_api::ReplyMarkup *markup)
    : m_account(account), m_chat(chat), m_messageId(messageId), m_buttons(formatButtons(markup)) {}
    ~ButtonsLine()
    {
        PurpleConversation *conv = (pidginUiFound() && !m_buttons.empty())
                                   ? findMessageConversation(m_account, m_chat) : nullptr;
        if (conv)
            showMessageButtons(conv, m_messageId, m_buttons);
    }
private:
    TdAccountData             &m_account;
    const td::td_api::chat    &m_chat;
    MessageId                  m_messageId;
    std::vector<ShownReaction> m_buttons;
};

void updateMessageButtons(TdAccountData &account, ChatId chatId, MessageId messageId,
                          td::td_api::object_ptr<td::td_api::ReplyMarkup> markup)
{
    const td::td_api::chat *chat = account.getChat(chatId);
    PurpleConversation     *conv = (chat && pidginUiFound()) ? findMessageConversation(account, *chat) : nullptr;
    if (conv && findMessageMarks(conv, messageId).start)
        showMessageButtons(conv, messageId, formatButtons(markup.get()));
    // Not shown yet, as waiting for what it replies to: shown with them as they are by then
    IncomingMessage *pending = account.pendingMessages.findPendingMessage(chatId, messageId);
    if (pending && pending->message)
        pending->message->reply_markup_ = std::move(markup);
}

// A member named in a group event, as markup. TDLib tells of every user it names, ourselves too,
// before the message that names them.
static std::string getMemberName(TdAccountData &account, int64_t userId)
{
    std::string name = account.getDisplayName(UserId::fromString(std::to_string(userId).c_str()));
    // TRANSLATOR: In-chat placeholder for a group member whose name is not known.
    return escapeForDisplay(name.empty() ? std::string(_("Unknown user")) : name);
}

// What a group's own news says: who made it, who came and went, what was pinned. These came in as
// "Unsupported message type" - the first of them opening the chat of a group just made.
static std::string makeGroupEventNotice(const td::td_api::chat &chat, const TgMessageInfo &messageInfo,
                                        const td::td_api::message &message, TdAccountData &account)
{
    std::string sender = messageInfo.anonymousChannelPost ? std::string() :
                         escapeForDisplay(getSenderDisplayName(chat, messageInfo, account.purpleAccount));
    UserId      senderId = getSenderUserId(message);
    const td::td_api::MessageContent &content = *message.content_;

    switch (content.get_id()) {
    case td::td_api::messageBasicGroupChatCreate::ID:
    case td::td_api::messageSupergroupChatCreate::ID: {
        std::string title = escapeForDisplay(
            (content.get_id() == td::td_api::messageBasicGroupChatCreate::ID) ?
            static_cast<const td::td_api::messageBasicGroupChatCreate &>(content).title_ :
            static_cast<const td::td_api::messageSupergroupChatCreate &>(content).title_);
        if (sender.empty())
            // TRANSLATOR: In-chat status update. Argument is a group or channel name.
            return formatMessage(_("{} was created"), title);
        // TRANSLATOR: In-chat status update. Arguments are a user name and a group or channel name.
        return formatMessage(_("{0} created {1}"), {sender, title});
    }
    case td::td_api::messageChatAddMembers::ID: {
        const auto &added = static_cast<const td::td_api::messageChatAddMembers &>(content);
        std::string names;
        for (int64_t userId: added.member_user_ids_) {
            // Who adds themselves has joined
            if (senderId.valid() && (userId == senderId.value()))
                continue;
            if (!names.empty())
                names += ", ";
            names += getMemberName(account, userId);
        }
        if (names.empty())
            // TRANSLATOR: In-chat status update. Argument is a user name.
            return formatMessage(_("{} joined"), sender);
        // TRANSLATOR: In-chat status update. Arguments are a user name and a list of user names.
        return formatMessage(_("{0} added {1}"), {sender, names});
    }
    case td::td_api::messageChatJoinByLink::ID:
        // TRANSLATOR: In-chat status update. Argument is a user name.
        return formatMessage(_("{} joined by an invite link"), sender);
    case td::td_api::messageChatJoinByRequest::ID:
        // TRANSLATOR: In-chat status update. Argument is a user name.
        return formatMessage(_("{} joined"), sender);
    case td::td_api::messageChatDeleteMember::ID: {
        int64_t userId = static_cast<const td::td_api::messageChatDeleteMember &>(content).user_id_;
        if (senderId.valid() && (userId == senderId.value()))
            // TRANSLATOR: In-chat status update. Argument is a user name.
            return formatMessage(_("{} left"), sender);
        // TRANSLATOR: In-chat status update. Arguments are user names.
        return formatMessage(_("{0} removed {1}"), {sender, getMemberName(account, userId)});
    }
    case td::td_api::messagePinMessage::ID:
        if (sender.empty())
            // TRANSLATOR: In-chat status update.
            return _("A message was pinned");
        // TRANSLATOR: In-chat status update. Argument is a user name.
        return formatMessage(_("{} pinned a message"), sender);
    }
    return std::string();
}

#if TDLIB_API_RICH_MESSAGE
static void showRichMessage(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                            const td::td_api::messageRichMessage &rich, TdAccountData &account)
{
    TgMessageInfo &messageInfo = fullMessage.messageInfo;
    // Its media linked to, as a photo's or video's sizes are, to be fetched when clicked
    PurpleAccount *purpleAccount = account.purpleAccount;
    ChatId         chatId        = getId(chat);
    MessageId      messageId     = messageInfo.id;
    RichMediaLink  link;
    if (messageLinksEnabled())
        link = [purpleAccount, chatId, messageId](size_t index) {
            return makeMessageLink(purpleAccount, chatId, messageId, "r" + std::to_string(index));
        };
    // Its pictures, where they can be shown: as they are, or as their stand-ins while those
    // fetched unasked come; elsewhere, placeholders
    bool                 pictures = false;
    std::vector<int32_t> coming;
    RichMediaImage       image;
    if (canShowImages())
        image = [&rich, &fullMessage, &account, &chat, &messageInfo, &pictures, &coming](size_t index) {
            std::string              kind;
            const td::td_api::photo *photo = nullptr;
            const td::td_api::file  *file  = findRichMessageMedia(*rich.message_, index, kind, &photo,
                                                                  fullMessage.inlineFileSizeLimit);
            gchar                   *data  = nullptr;
            gsize                    len   = 0;
            if (!file || !photo)
                return std::string();
            if (file->local_ && file->local_->is_downloading_completed_ &&
                g_file_get_contents(file->local_->path_.c_str(), &data, &len, nullptr))
            {
                pictures = true;
                // Among the text, as a collage's side by side
                return makeInlineImageText(addImageFile(data, len, file->local_->path_), false);
            }
            if (isSizeWithinLimit(getFileSizeKb(*file), fullMessage.inlineFileSizeLimit) &&
                fetchesPicture(account, chat, messageInfo.id, file->id_))
                coming.push_back(file->id_);
            int32_t width = 0, height = 0;
            for (const auto &size: photo->sizes_)
                if (size && (size->photo_.get() == file)) {
                    width  = size->width_;
                    height = size->height_;
                }
            int standIn = makeShownStandIn(photo->minithumbnail_ ? photo->minithumbnail_->data_ : std::string(),
                                           width, height, false);
            pictures = pictures || standIn;
            return standIn ? makeInlineImageText(standIn, false) : std::string();
        };
    std::string html = rich.message_ ? formatRichMessage(*rich.message_, link, image) : std::string();
    if (!html.empty()) {
        showMessageWithPictures(account, chat, messageInfo, html.c_str(), pictures,
                                messageInfo.picturesComing || !coming.empty());
        for (int32_t fileId: coming)
            fetchPicture(account, chatId, messageId, fileId);
    }
    else
        showUnsupportedMessage(chat, messageInfo, rich, account);
}
#endif

void showMessage(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                TdAccountData &account)
{
    if (!fullMessage.message) return;
    td::td_api::message &message = *fullMessage.message;
    if (!fullMessage.loggedHtml.empty()) {
        appendTabLine(findMessageConversation(account, chat), fullMessage.loggedHtml, fullMessage.loggedId);
        for (int id: fullMessage.loggedImageIds)
            purple_imgstore_unref_by_id(id);
        return;
    }

    if (!message.content_)
        return;
    if (fullMessage.deleted) {
        // Nobody saw it, so nobody needs telling
        purple_debug_misc(config::pluginId, "Not displaying deleted message %" G_GINT64_FORMAT "\n", message.id_);
        return;
    }
    // Past the end of what its tab shows, if it stops short of the newest: not shown again, nor yet
    // as it comes, though logged and told of
    PurpleConversation *conv   = findMessageConversation(account, chat);
    MessageId           tabEnd = conv ? getTabEnd(conv) : MessageId();
    if (tabEnd.valid() && (message.id_ > tabEnd.value())) {
        if (fullMessage.messageInfo.redrawn) {
            purple_debug_misc(config::pluginId, "Not displaying message %" G_GINT64_FORMAT " past the tab's end\n",
                              message.id_);
            return;
        }
        hideMessage(conv, getId(message));
    }
    purple_debug_misc(config::pluginId, "Displaying message %" G_GINT64_FORMAT "\n", message.id_);
    // Its stand-ins, as it is shown now
    account.forgetStandIns(getId(chat), getId(message));

    TgMessageInfo &messageInfo = fullMessage.messageInfo;

    if (fullMessage.editedMessageTime && showEditNotice(chat, fullMessage, account)) {
        // The notice holds all of the edit; the tab gets the message over its earlier version
        // all the same, but not the log
        if (!fullMessage.replacing) {
            showPendingEdit(account, getChatId(message), getId(message));
            return;
        }
        messageInfo.unlogged = true;
    }

    // Under the message, once all of it is written: its buttons, then its reactions
    ReactionsLine reactionsLine(account, chat, messageInfo.id, message.interaction_info_.get());
    ButtonsLine   buttonsLine(account, chat, messageInfo.id, message.reply_markup_.get());
    // All its parts, notices included, are the message, and its menu goes before the first. An edit
    // goes over the earlier version, saying it was edited.
    std::string editNote;
    if (fullMessage.replacing && !fullMessage.refreshing)
        editNote = formatMessage(" <font size=\"2\"><i>({})</i></font>",
                                 std::string(fullMessage.formattingOnly ?
                                             // TRANSLATOR: After the text of a message edited where it stands, in Pidgin's conversation window, when only the formatting of its words (bold, italic and the like) changed.
                                             _("formatting edited") :
                                             // TRANSLATOR: After the text of a message edited where it stands, in Pidgin's conversation window.
                                             _("edited")));
    MessageMarkScope markScope(messageInfo.id, fullMessage.replacing ? editNote.c_str() : nullptr, true,
                               message.content_->get_id() == td::td_api::messageText::ID);
    FileInfo fileInfo;
    getFileFromMessage(fullMessage, fileInfo);
    if (fileInfo.secret) {
        if (purple_account_get_bool(account.purpleAccount, AccountOptions::ShowSelfDestruct, AccountOptions::ShowSelfDestructDefault)) {
            // TRANSLATOR: In-chat warning message
            std::string notice = formatMessage(_("Received secret file {}, displaying anyway"),
                                               escapeForDisplay(fileInfo.description));
            notice = makeNoticeWithSender(chat, messageInfo, notice.c_str(), account.purpleAccount);
            showMessageText(account, chat, messageInfo, !fileInfo.caption.empty() ? fileInfo.caption.c_str() : nullptr,
                            notice.c_str());
        } else {
            // TRANSLATOR: In-chat warning message
            std::string notice = formatMessage(_("Ignoring secret file ({})"),
                                               escapeForDisplay(fileInfo.description));
            notice = makeNoticeWithSender(chat, messageInfo, notice.c_str(), account.purpleAccount);
            showMessageText(account, chat, messageInfo, !fileInfo.caption.empty() ? fileInfo.caption.c_str() : nullptr,
                            notice.c_str());
            return;
        }
    }

    // A video's thumbnail still coming: shown as its stand-in, the message logged once it is here
    // ...within the limit on what is fetched unasked, as a cover may not be in any of its sizes
    bool previewComing = fullMessage.previewFileId && !fullMessage.previewFetchDone && canShowImages() &&
                         isSizeWithinLimit(fullMessage.previewSizeKb, fullMessage.inlineFileSizeLimit) &&
                         fetchesPicture(account, chat, messageInfo.id, fullMessage.previewFileId);
    if (previewComing)
        messageInfo.picturesComing = true;

    switch (message.content_->get_id()) {
        case td::td_api::messageText::ID:
            showTextMessage(chat, messageInfo, static_cast<const td::td_api::messageText &>(*message.content_),
                            account);
            break;
        case td::td_api::messagePhoto::ID:
            showPhotoMessage(chat, fullMessage, fileInfo.file, fileInfo.caption, account);
            break;
        case td::td_api::messageDocument::ID:
        case td::td_api::messageVideo::ID:
        case td::td_api::messageAnimation::ID:
        case td::td_api::messageAudio::ID:
        case td::td_api::messageVoiceNote::ID:
        case td::td_api::messageVideoNote::ID:
            showFileMessage(chat, fullMessage, fileInfo.file, fileInfo.caption, fileInfo.description,
                            fileInfo.name, account);
            break;
        case td::td_api::messageSticker::ID:
            showStickerMessage(chat, fullMessage, static_cast<td::td_api::messageSticker &>(*message.content_),
                               account);
            break;
        case td::td_api::messageChatChangeTitle::ID: {
            const auto &titleChange = static_cast<const td::td_api::messageChatChangeTitle &>(*message.content_);
            // TRANSLATOR: In-chat status update, arguments are chat names.
            std::string notice = formatMessage(_("{0} changed group name to {1}"),
                                               {escapeForDisplay(getSenderDisplayName(chat, messageInfo,
                                                                                      account.purpleAccount)),
                                                escapeForDisplay(titleChange.title_)});
            showMessageText(account, chat, messageInfo, nullptr, notice.c_str());
            break;
        }
        case td::td_api::messageContactRegistered::ID: {
            // TRANSLATOR: In-chat status update. Argument is a username.
            std::string notice = formatMessage(_("{} joined Telegram"),
                                               escapeForDisplay(getSenderDisplayName(chat, messageInfo,
                                                                                     account.purpleAccount)));
            showMessageText(account, chat, messageInfo, nullptr, notice.c_str());
            break;
        }
        case td::td_api::messageBasicGroupChatCreate::ID:
        case td::td_api::messageSupergroupChatCreate::ID:
        case td::td_api::messageChatAddMembers::ID:
        case td::td_api::messageChatJoinByLink::ID:
        case td::td_api::messageChatJoinByRequest::ID:
        case td::td_api::messageChatDeleteMember::ID:
        case td::td_api::messagePinMessage::ID: {
            std::string notice = makeGroupEventNotice(chat, messageInfo, message, account);
            showMessageText(account, chat, messageInfo, nullptr, notice.c_str());
            break;
        }
        case td::td_api::messageCall::ID:
            showCallMessage(chat, messageInfo, static_cast<td::td_api::messageCall &>(*message.content_), account);
            break;
        case td::td_api::messageAnimatedEmoji::ID: {
            const td::td_api::messageAnimatedEmoji &animatedEmoji =
                static_cast<const td::td_api::messageAnimatedEmoji &>(*message.content_);
            showMessageText(account, chat, messageInfo, animatedEmoji.emoji_.c_str(), nullptr);
            break;
        }
        case td::td_api::messageLocation::ID:
#if TDLIB_API_LIVE_LOCATION
        case td::td_api::messageLiveLocation::ID:
#endif
        case td::td_api::messageVenue::ID: {
            ShownLocation location;
            if (getShownLocation(*message.content_, location)) {
                std::string text = makeLocationText(location);
                showMessageText(account, chat, messageInfo, text.c_str(), nullptr);
            } else
                // Not supposed to be possible, but just in case
                showUnsupportedMessage(chat, messageInfo, *message.content_, account);
            break;
        }
#if TDLIB_API_RICH_MESSAGE
        case td::td_api::messageRichMessage::ID:
            showRichMessage(chat, fullMessage, static_cast<const td::td_api::messageRichMessage &>(*message.content_),
                            account);
            break;
#endif
        default:
            showUnsupportedMessage(chat, messageInfo, *message.content_, account);
    }

    if (previewComing) {
        messageInfo.picturesComing = false;
        fetchPicture(account, getId(chat), messageInfo.id, fullMessage.previewFileId);
    }
    for (int id: g_shownStandIns)
        purple_imgstore_unref_by_id(id);
    g_shownStandIns.clear();

    showPendingEdit(account, getChatId(message), getId(message));
}

void showMessages(std::vector<IncomingMessage>& messages, TdAccountData &account)
{
    for (IncomingMessage &readyMessage: messages) {
        if (!readyMessage.message) continue;
        const td::td_api::chat *chat = account.getChat(getChatId(*readyMessage.message));
        if (chat)
            showMessage(*chat, readyMessage, account);
    }
}

const td::td_api::file *selectPhotoSize(PurpleAccount *account, const td::td_api::messagePhoto &photo)
{
    unsigned                     sizeLimit        = getAutoDownloadLimitKb(account);
    // In Pidgin, as any picture shown in a conversation
    if (getTextLineHeight() > 0)
        return chooseShownPhotoSize(photo.photo_.get(), 0, sizeLimit);
    // Elsewhere, where it is opened rather than shown, the biggest within the limit
    const td::td_api::photoSize *selectedSize     = nullptr;
    unsigned                     selectedFileSize = 0;
    if (photo.photo_)
        for (const auto &newSize: photo.photo_->sizes_)
            if (newSize && newSize->photo_) {
                unsigned fileSize            = getFileSizeKb(*newSize->photo_);
                bool     isWithinLimit       = isSizeWithinLimit(fileSize, sizeLimit);
                bool     selectedWithinLimit = isSizeWithinLimit(selectedFileSize, sizeLimit);
                if (!selectedSize ||
                    (!selectedWithinLimit && (isWithinLimit || (fileSize < selectedFileSize))) ||
                    (selectedWithinLimit && isWithinLimit && (newSize->width_ > selectedSize->width_)))
                {
                    selectedSize = newSize.get();
                    selectedFileSize = fileSize;
                }
            }

    if (selectedSize)
        purple_debug_misc(config::pluginId, "Selected size %dx%d for photo\n",
                          (int)selectedSize->width_, (int)selectedSize->height_);
    else
        purple_debug_warning(config::pluginId, "No file found for a photo\n");

    return selectedSize ? selectedSize->photo_.get() : nullptr;
}

// Of a picture shown in the conversation, the size that does: in Pidgin, the smallest at least so
// many lines of the conversation's text tall, or else the tallest; elsewhere the smallest at least
// so wide, or else the widest. Only those within the limit on what is fetched unasked, if any is.
enum { PICTURE_LINES = 23 };
const td::td_api::file *chooseShownPhotoSize(const td::td_api::photo *photo, int32_t fallbackWidth,
                                             unsigned sizeLimitKb)
{
    unsigned lineHeight = getTextLineHeight();
    bool     byHeight   = (lineHeight > 0);
    int32_t  wanted     = byHeight ? int32_t(lineHeight * PICTURE_LINES) : fallbackWidth;
    const td::td_api::photoSize *chosen = nullptr;
    for (bool limited: {true, false}) {
        if (photo)
            for (const auto &size: photo->sizes_) {
                if (!size || !size->photo_ ||
                    (limited && !isSizeWithinLimit(getFileSizeKb(*size->photo_), sizeLimitKb)))
                    continue;
                int32_t measure       = byHeight ? size->height_ : size->width_;
                int32_t chosenMeasure = chosen ? (byHeight ? chosen->height_ : chosen->width_) : 0;
                bool    bigEnough     = (measure >= wanted);
                if (!chosen ||
                    (bigEnough && ((chosenMeasure < wanted) || (measure < chosenMeasure))) ||
                    (!bigEnough && (chosenMeasure < wanted) && (measure > chosenMeasure)))
                    chosen = size.get();
            }
        if (chosen)
            break;
    }
    return chosen ? chosen->photo_.get() : nullptr;
}

// The thumbnail the sender attached to a video, if it is one the conversation window can show:
// Telegram also sends MPEG4 thumbnails for some videos and animations, and those are skipped.
// Of a cover the sender chose for a video, the size that does for a preview, as wide, elsewhere
// than Pidgin, as the video's thumbnail, which is what the preview would be without a cover
static const td::td_api::file *getCoverPreview(const td::td_api::photo *cover,
                                               const td::td_api::thumbnail *thumbnail, unsigned sizeLimitKb)
{
    // Telegram's thumbnails are 320 pixels wide, should one not say
    return chooseShownPhotoSize(cover, (thumbnail && (thumbnail->width_ > 0)) ? thumbnail->width_ : 320,
                                sizeLimitKb);
}

// With its size, and the low-grade picture of the video, if asked for
static const td::td_api::file *getVideoPreview(const td::td_api::MessageContent &content,
                                               PurpleAccount *account, int32_t *width = nullptr,
                                               int32_t *height = nullptr, std::string *lowGrade = nullptr)
{
    const td::td_api::thumbnail     *thumbnail = nullptr;
    const td::td_api::file          *cover     = nullptr;
    const td::td_api::photo         *coverPhoto = nullptr;
    const td::td_api::minithumbnail *mini      = nullptr;
    bool                             secret    = false;

    switch (content.get_id()) {
        case td::td_api::messageVideo::ID: {
            const auto &video = static_cast<const td::td_api::messageVideo &>(content);
            // The thumbnail is the first frame, which is often a title card; a cover, where the
            // sender chose one, is what Telegram's own apps show instead
            if (video.video_) {
                thumbnail = video.video_->thumbnail_.get();
                mini      = video.video_->minithumbnail_.get();
            }
            cover = getCoverPreview(video.cover_.get(), thumbnail, getAutoDownloadLimitKb(account));
            coverPhoto = video.cover_.get();
            secret = video.is_secret_;
            break;
        }
        case td::td_api::messageAnimation::ID: {
            const auto &animation = static_cast<const td::td_api::messageAnimation &>(content);
            if (animation.animation_) {
                thumbnail = animation.animation_->thumbnail_.get();
                mini      = animation.animation_->minithumbnail_.get();
            }
            secret = animation.is_secret_;
            break;
        }
        case td::td_api::messageVideoNote::ID: {
            const auto &videoNote = static_cast<const td::td_api::messageVideoNote &>(content);
            if (videoNote.video_note_) {
                thumbnail = videoNote.video_note_->thumbnail_.get();
                mini      = videoNote.video_note_->minithumbnail_.get();
            }
            secret = videoNote.is_secret_;
            break;
        }
    }

    // A self-destructing video is only displayed on request, and so is its thumbnail
    if (secret && !purple_account_get_bool(account, AccountOptions::ShowSelfDestruct,
                                           AccountOptions::ShowSelfDestructDefault))
        return nullptr;

    int32_t chosenWidth = 0, chosenHeight = 0;
    const td::td_api::file *chosen = nullptr;
    if (cover) {
        chosen = cover;
        for (const auto &size: coverPhoto->sizes_)
            if (size && (size->photo_.get() == cover)) {
                chosenWidth  = size->width_;
                chosenHeight = size->height_;
            }
        if (coverPhoto->minithumbnail_)
            mini = coverPhoto->minithumbnail_.get();
    } else if (thumbnail && thumbnail->format_ &&
               (thumbnail->format_->get_id() == td::td_api::thumbnailFormatJpeg::ID)) {
        chosen       = thumbnail->file_.get();
        chosenWidth  = thumbnail->width_;
        chosenHeight = thumbnail->height_;
    }
    if (chosen && width)
        *width = chosenWidth;
    if (chosen && height)
        *height = chosenHeight;
    if (chosen && lowGrade && mini)
        *lowGrade = mini->data_;
    return chosen;
}

static bool waitsForPreview(const IncomingMessage &fullMessage);

void makeFullMessage(const td::td_api::chat &chat, td::td_api::object_ptr<td::td_api::message> message,
                     IncomingMessage &fullMessage, const TdAccountData &account)
{
    if (!message) {
        fullMessage.message = nullptr;
        return;
    }

    fullMessage.messageInfo.repliedMessage = nullptr;
    fullMessage.selectedPhotoSizeId = 0;
    fullMessage.repliedMessageFetchDoneOrFailed = false;
    fullMessage.previewPath.clear();
    fullMessage.previewFileId = 0;
    fullMessage.previewSizeKb = 0;
    fullMessage.previewFetchDone = false;
    fullMessage.previewWaited = false;
    fullMessage.inlineDownloadComplete = false;
    fullMessage.inlineDownloadTimeout = false;

    const char *option = purple_account_get_string(account.purpleAccount, AccountOptions::DownloadBehaviour,
                                                   AccountOptions::DownloadBehaviourDefault());
    fullMessage.standardDownloadConfigured = (strcmp(option, AccountOptions::DownloadBehaviourHyperlink) != 0);
    fullMessage.inlineFileSizeLimit = getAutoDownloadLimitKb(account.purpleAccount);

    TgMessageInfo &messageInfo = fullMessage.messageInfo;
    messageInfo.id               = getId(*message);
    messageInfo.type             = TgMessageInfo::Type::Other;
    messageInfo.incomingGroupchatSender = getIncomingGroupchatSenderPurpleName(chat, *message, account);
    UserId senderId = getSenderUserId(*message);
    if (!message->is_outgoing_ && senderId.valid() && account.getUser(senderId) && isGroupChat(chat))
        messageInfo.incomingGroupchatSenderName = getPurpleBuddyName(*account.getUser(senderId));
    messageInfo.anonymousChannelPost    = isAnonymousChannelPost(chat, *message);
    // In Pidgin, links to each size a photo or video comes in, below it; here already, as a download
    // may start, and finish, before the message is shown
    if (messageLinksEnabled() && message->content_) {
        std::vector<MediaVariant> variants;
        getMediaVariants(*message->content_, variants);
        messageInfo.mediaVariantLinks = makeMediaVariantLinks(account.purpleAccount, getId(chat), getId(*message),
                                                              variants);
    }
    messageInfo.timestamp        = message->date_;
    messageInfo.outgoing         = message->is_outgoing_;
    messageInfo.sentLocally      = (message->sending_state_ != nullptr);
    messageInfo.repliedMessageId = getReplyMessageId(*message);

    if (message->forward_info_)
        messageInfo.forwardedFrom = getForwardSource(*message->forward_info_, account);

    if (message && message->content_) {
        if (message->content_->get_id() == td::td_api::messagePhoto::ID) {
            messageInfo.type = TgMessageInfo::Type::Photo;
            const td::td_api::messagePhoto &photo = static_cast<const td::td_api::messagePhoto &>(*message->content_);
            const td::td_api::file *file = selectPhotoSize(account.purpleAccount, photo);
            if (file)
                fullMessage.selectedPhotoSizeId = file->id_;
        } else if (message->content_->get_id() == td::td_api::messageSticker::ID) {
            messageInfo.type = TgMessageInfo::Type::Sticker;
            td::td_api::messageSticker &sticker = static_cast<td::td_api::messageSticker &>(*message->content_);
            // Shown where the sticker cannot be; set before anything copies the message's info
            if (sticker.sticker_)
                messageInfo.altText = sticker.sticker_->emoji_;
            if (sticker.sticker_ && sticker.sticker_->thumbnail_) {
                fullMessage.thumbnail = std::move(sticker.sticker_->thumbnail_->file_);
            }
        }

        const td::td_api::file *preview = getVideoPreview(*message->content_, account.purpleAccount,
                                                          &fullMessage.previewWidth, &fullMessage.previewHeight,
                                                          &fullMessage.previewLowGrade);
        if (preview) {
            fullMessage.previewFileId = preview->id_;
            fullMessage.previewSizeKb = getFileSizeKb(*preview);
            if (preview->local_ && preview->local_->is_downloading_completed_) {
                fullMessage.previewPath = preview->local_->path_;
                fullMessage.previewFetchDone = true;
            }
        }
    }

    fullMessage.message = std::move(message);
    fullMessage.downloadsMedia = downloadsChatMedia(account, chat);
    fullMessage.previewWaited = waitsForPreview(fullMessage);
}

static const td::td_api::file *getSelectedPhotoSize(const IncomingMessage &fullMessage,
                                                    const td::td_api::messagePhoto &photo)
{
    if (photo.photo_)
        for (const auto &newSize: photo.photo_->sizes_)
            if (newSize && newSize->photo_ && (newSize->photo_->id_ == fullMessage.selectedPhotoSizeId))
                return newSize->photo_.get();

    return nullptr;
}

static bool isInlineDownload(const IncomingMessage &fullMessage,
                             const td::td_api::MessageContent &content,
                             const td::td_api::chat &chat)
{
    return (content.get_id() == td::td_api::messagePhoto::ID) ||
           (content.get_id() == td::td_api::messageSticker::ID) ||
           !fullMessage.standardDownloadConfigured || !chat.type_ ||
           ((chat.type_->get_id() != td::td_api::chatTypePrivate::ID) &&
            (chat.type_->get_id() != td::td_api::chatTypeSecret::ID));
}

// Not where the chat's media are not fetched unasked: the message is shown without the file then
// A video's thumbnail is waited for, or it would land below messages that arrived after it, unless
// it can be written over its stand-in; not where the chat's media are not fetched unasked. Decided
// as the message is received, so that one held back has its thumbnail asked for.
static bool waitsForPreview(const IncomingMessage &fullMessage)
{
    return fullMessage.previewFileId && !fullMessage.previewFetchDone && !canShowImages() &&
           fullMessage.downloadsMedia;
}

static bool inlineDownloadNeedAutoDl(const IncomingMessage &fullMessage, const td::td_api::file &file)
{
    unsigned fileSize = getFileSizeKb(file);

    return !(file.local_ && file.local_->is_downloading_completed_) &&
           isSizeWithinLimit(fileSize, fullMessage.inlineFileSizeLimit) && fullMessage.downloadsMedia;
}

static bool isFileMessageReady(const IncomingMessage &fullMessage, ChatId chatId,
                               const td::td_api::MessageContent &content,
                               const td::td_api::file &file, const TdAccountData &account)
{
    const td::td_api::chat *chat = account.getChat(chatId);

    if (chat && isInlineDownload(fullMessage, content, *chat)) {
        // A link to the message needs nothing but the message; nor does a picture shown as it comes
        if (linksToMessage(fullMessage) || showsPictureComing(fullMessage))
            return true;
        // File will be shown inline
        if (fullMessage.inlineDownloadComplete || (file.local_ && file.local_->is_downloading_completed_))
            return true;
        // Files above limit will either be ignored (in which case, message is ready)
        // or requested (in which case, don't try do display in order)
        return fullMessage.inlineDownloadTimeout || !inlineDownloadNeedAutoDl(fullMessage, file);
    }
    // Standard libpurple transfer will be used, nothing to postpone
    return true;
}

void getFileFromMessage(const IncomingMessage &fullMessage, FileInfo &result)
{
    result.file = nullptr;
    result.caption = "";
    result.secret = false;
    ContentParts parts;
    if (!fullMessage.message || !fullMessage.message->content_ ||
        !getContentParts(*fullMessage.message->content_, parts))
        return;
    const td::td_api::MessageContent &content = *fullMessage.message->content_;
    result.file   = parts.file;
    result.secret = parts.secret;
    if (parts.caption)
        result.caption = getMessageText(*parts.caption);

    // Its name, and what it is called in the notices of its download
    switch (content.get_id()) {
        case td::td_api::messagePhoto::ID:
            result.file = getSelectedPhotoSize(fullMessage, static_cast<const td::td_api::messagePhoto &>(content));
            result.name = ""; // will not be needed - inline download only
            // TRANSLATOR: File-type, used to describe what is being downloaded, in sentences like "Downloading photo" or "Ignoring photo download".
            result.description = _("photo");
            break;
        case td::td_api::messageDocument::ID: {
            const auto *document = static_cast<const td::td_api::messageDocument &>(content).document_.get();
            result.name = getFileName(document);
            result.description = makeDocumentDescription(document);
            break;
        }
        case td::td_api::messageVideo::ID: {
            const auto *video = static_cast<const td::td_api::messageVideo &>(content).video_.get();
            result.name = getFileName(video);
            result.description = makeDocumentDescription(video, !fullMessage.messageInfo.mediaVariantLinks.empty());
            break;
        }
        case td::td_api::messageAnimation::ID: {
            const auto *animation = static_cast<const td::td_api::messageAnimation &>(content).animation_.get();
            result.name = getFileName(animation);
            result.description = makeDocumentDescription(animation);
            break;
        }
        case td::td_api::messageAudio::ID: {
            const auto *audio = static_cast<const td::td_api::messageAudio &>(content).audio_.get();
            result.name = getFileName(audio);
            result.description = makeDocumentDescription(audio);
            break;
        }
        case td::td_api::messageVoiceNote::ID: {
            const auto *voice = static_cast<const td::td_api::messageVoiceNote &>(content).voice_note_.get();
            result.name = getFileName(voice);
            result.description = makeDocumentDescription(voice);
            break;
        }
        case td::td_api::messageVideoNote::ID: {
            const auto *video = static_cast<const td::td_api::messageVideoNote &>(content).video_note_.get();
            result.name = getFileName(video);
            result.description = makeDocumentDescription(video);
            break;
        }
        case td::td_api::messageSticker::ID:
            result.name = ""; // will not be needed - inline download only
            // TRANSLATOR: File-type, used to describe what is being downloaded, in sentences like "Downloading photo" or "Ignoring photo download".
            result.description = _("sticker");
            break;
    }
}

// Of a file and the rest of a kind with them
template<typename Media>
static void setMediaParts(const Media *media, const td::td_api::file *file, ContentParts &parts)
{
    parts.file     = file;
    parts.fileName = media ? &media->file_name_ : nullptr;
    parts.mimeType = media ? &media->mime_type_ : nullptr;
}

bool getContentParts(const td::td_api::MessageContent &content, ContentParts &parts)
{
    parts = ContentParts();
    switch (content.get_id()) {
    case td::td_api::messagePhoto::ID: {
        const auto &photo = static_cast<const td::td_api::messagePhoto &>(content);
        if (photo.photo_ && !photo.photo_->sizes_.empty() && photo.photo_->sizes_.back())
            parts.file = photo.photo_->sizes_.back()->photo_.get();
        parts.caption = photo.caption_.get();
        parts.secret  = photo.is_secret_;
        return true;
    }
    case td::td_api::messageDocument::ID: {
        const auto &document = static_cast<const td::td_api::messageDocument &>(content);
        setMediaParts(document.document_.get(), document.document_ ? document.document_->document_.get() : nullptr,
                      parts);
        parts.caption = document.caption_.get();
        return true;
    }
    case td::td_api::messageVideo::ID: {
        const auto &video = static_cast<const td::td_api::messageVideo &>(content);
        setMediaParts(video.video_.get(), video.video_ ? video.video_->video_.get() : nullptr, parts);
        parts.caption = video.caption_.get();
        parts.secret  = video.is_secret_;
        return true;
    }
    case td::td_api::messageAnimation::ID: {
        const auto &animation = static_cast<const td::td_api::messageAnimation &>(content);
        setMediaParts(animation.animation_.get(), animation.animation_ ? animation.animation_->animation_.get()
                                                                        : nullptr, parts);
        parts.caption = animation.caption_.get();
        parts.secret  = animation.is_secret_;
        return true;
    }
    case td::td_api::messageAudio::ID: {
        const auto &audio = static_cast<const td::td_api::messageAudio &>(content);
        setMediaParts(audio.audio_.get(), audio.audio_ ? audio.audio_->audio_.get() : nullptr, parts);
        parts.caption = audio.caption_.get();
        return true;
    }
    case td::td_api::messageVoiceNote::ID: {
        const auto &voice = static_cast<const td::td_api::messageVoiceNote &>(content);
        parts.file     = voice.voice_note_ ? voice.voice_note_->voice_.get() : nullptr;
        parts.mimeType = voice.voice_note_ ? &voice.voice_note_->mime_type_ : nullptr;
        parts.caption  = voice.caption_.get();
        return true;
    }
    case td::td_api::messageVideoNote::ID: {
        const auto &video = static_cast<const td::td_api::messageVideoNote &>(content);
        parts.file   = video.video_note_ ? video.video_note_->video_.get() : nullptr;
        parts.secret = video.is_secret_;
        return true;
    }
    case td::td_api::messageSticker::ID: {
        const auto &sticker = static_cast<const td::td_api::messageSticker &>(content);
        parts.file = sticker.sticker_ ? sticker.sticker_->sticker_.get() : nullptr;
        return true;
    }
    default:
        return false;
    }
}

void getMessageFileKind(const td::td_api::MessageContent &content, bool &hasFile, bool &picture)
{
    ContentParts parts;
    hasFile = getContentParts(content, parts);
    picture = false;
    if (content.get_id() == td::td_api::messagePhoto::ID)
        picture = true;
    else if (content.get_id() == td::td_api::messageSticker::ID) {
        const auto &sticker = static_cast<const td::td_api::messageSticker &>(content);
        picture = sticker.sticker_ && sticker.sticker_->format_ &&
                  (sticker.sticker_->format_->get_id() == td::td_api::stickerFormatWebp::ID);
    }
}

bool isMessageReady(const IncomingMessage &fullMessage, const TdAccountData &account)
{
    if (!fullMessage.message) return true;
    const td::td_api::message &message = *fullMessage.message;
    ChatId chatId = getChatId(message);

    if (getReplyMessageId(message).valid() && !fullMessage.repliedMessageFetchDoneOrFailed)
    {
        return false;
    }

    if (fullMessage.previewWaited && !fullMessage.previewFetchDone)
        return false;

    if (message.content_)
    {
        FileInfo fileInfo;
        getFileFromMessage(fullMessage, fileInfo);
        // For stickers, will wait for sticker download to display the message, but not for
        // animated sticker conversion
        if (fileInfo.file && !isFileMessageReady(fullMessage, chatId, *message.content_,
            *fileInfo.file, account))
        {
            return false;
        }
    }


    return true;
}

static void queueMessage(TdAccountData &account, const td::td_api::chat &chat,
                         IncomingMessage &&fullMessage, PendingMessageQueue::MessageAction action);

// A message shown already without a picture of its own come since, written again over itself where
// Pidgin's tab shows it, not logged again, not said to be edited
static void refreshShownMessage(TdAccountData &account, ChatId chatId, MessageId messageId)
{
    auto request = makeGetMessage(chatId, messageId);
    account.transceiver.sendQuery(std::move(request),
        [&account, chatId, messageId](uint64_t, td::td_api::object_ptr<td::td_api::Object> object) {
            const td::td_api::chat *chat = account.getChat(chatId);
            // Shown later, as it is by then
            if (account.pendingMessages.findPendingMessage(chatId, messageId))
                return;
            // With nowhere to show it again, it is logged as it was shown, if it waited for that
            if (!chat || !object || (object->get_id() != td::td_api::message::ID) ||
                !canReplaceMessage(findMessageConversation(account, *chat), messageId))
            {
                logSentMessage(account, chatId, messageId, messageId);
                return;
            }
            purple_debug_misc(config::pluginId, "Showing message %" G_GINT64_FORMAT " again, with a picture come late\n",
                              messageId.value());
            IncomingMessage fullMessage;
            makeFullMessage(*chat, td::move_tl_object_as<td::td_api::message>(object), fullMessage, account);
            fullMessage.replacing              = true;
            fullMessage.refreshing             = true;
            fullMessage.messageInfo.unlogged   = true;
            // No news, to make a sound or a notification for
            fullMessage.messageInfo.fromHistory = true;
            queueMessage(account, *chat, std::move(fullMessage), PendingMessageQueue::Append);
        });
}

static void previewDownloadResponse(TdAccountData &account, ChatId chatId, MessageId messageId,
                                    td::td_api::object_ptr<td::td_api::Object> object)
{
    IncomingMessage *pendingMessage = account.pendingMessages.findPendingMessage(chatId, messageId);
    if (!pendingMessage) return;

    pendingMessage->previewFetchDone = true;
    if (object)
        pendingMessage->previewPath = getDownloadPath(object);
    else
        purple_debug_misc(config::pluginId, "Video thumbnail for message %" G_GINT64_FORMAT " timed out\n",
                          messageId.value());

    checkMessageReady(pendingMessage, account);
}

void fetchExtras(IncomingMessage &fullMessage, TdAccountData &account,
                 TdTransceiver::ResponseCb2 onFetchReply)
{
    if (!fullMessage.message) return;
    const td::td_api::message &message = *fullMessage.message;
    MessageId messageId      = getId(message);
    MessageId replyMessageId = getReplyMessageId(message);
    ChatId    chatId         = getChatId(message);
    const td::td_api::chat *chat = account.getChat(chatId);

    if (replyMessageId.valid()) {
        purple_debug_misc(config::pluginId, "Fetching message %" G_GINT64_FORMAT " which message %" G_GINT64_FORMAT " replies to\n",
                        replyMessageId.value(), messageId.value());
        auto getMessageReq = makeGetMessage(chatId, replyMessageId);
        account.transceiver.sendQueryWithTimeout(std::move(getMessageReq), std::move(onFetchReply),
                                         getMessageWaitSeconds(fullMessage.messageInfo));
    }

    if (fullMessage.previewWaited && !fullMessage.previewFetchDone) {
        purple_debug_misc(config::pluginId, "Fetching video thumbnail for message %" G_GINT64_FORMAT "\n",
                          messageId.value());
        auto downloadReq = makeDownloadFile(fullMessage.previewFileId, FILE_DOWNLOAD_PRIORITY, true);
        // Past the timeout the video is shown without it; a late thumbnail is then discarded
        account.transceiver.sendQueryWithTimeout(std::move(downloadReq),
            [&account, chatId, messageId](uint64_t, td::td_api::object_ptr<td::td_api::Object> object) {
                previewDownloadResponse(account, chatId, messageId, std::move(object));
            }, getMessageWaitSeconds(fullMessage.messageInfo));
    }

    FileInfo fileInfo;
    getFileFromMessage(fullMessage, fileInfo);
    // Pictures shown as they come are fetched as they are shown
    if (fileInfo.file && message.content_ && chat && isInlineDownload(fullMessage, *message.content_, *chat) &&
        !showsPictureComing(fullMessage))
    {
        if (inlineDownloadNeedAutoDl(fullMessage, *fileInfo.file) && !linksToMessage(fullMessage)) {
            // TgMessageInfo on fullMessage has replyMessage=NULL which will be copied onto DownloadRequest.
            // If message leaves PendingMessageQueue while download is still active, there's probably
            // a replyMessage on IncomingMessage by then, and it needs to be moved over to DownloadRequest.
            // Thumbnail also needs moving onto DownloadRequest.
            downloadFileInline(fileInfo.file->id_, chatId, fullMessage.messageInfo, fileInfo.description,
                               nullptr, account);
        }
    }
}

void checkMessageReady(const IncomingMessage *message, TdAccountData &account, std::vector<IncomingMessage> *rvReadyMessages)
{
    if (!message || !message->message) return;

    if (isMessageReady(*message, account)) {
        std::vector<IncomingMessage> readyMessages;
        account.pendingMessages.setMessageReady(getChatId(*message->message), getId(*message->message),
                                               rvReadyMessages ? *rvReadyMessages : readyMessages);
        showMessages(rvReadyMessages ? *rvReadyMessages : readyMessages, account);
    }
}

static void findMessageResponse(TdAccountData &account, ChatId chatId, MessageId pendingMessageId,
                                td::td_api::object_ptr<td::td_api::Object> object)
{
    IncomingMessage *pendingMessage = account.pendingMessages.findPendingMessage(chatId, pendingMessageId);
    if (!pendingMessage) return;

    pendingMessage->repliedMessageFetchDoneOrFailed = true;
    if (object && (object->get_id() == td::td_api::message::ID))
        pendingMessage->messageInfo.repliedMessage = RepliedMessage(td::move_tl_object_as<td::td_api::message>(object).release());
    else
        purple_debug_misc(config::pluginId, "Failed to fetch reply source for message %" G_GINT64_FORMAT "\n",
                          pendingMessageId.value());

    checkMessageReady(pendingMessage, account);
}

static void queueMessage(TdAccountData &account, const td::td_api::chat &chat,
                         IncomingMessage &&fullMessage, PendingMessageQueue::MessageAction action)
{
    ChatId chatId = getId(chat);

    // Where only the caption changed, the file is not shown again, and so nothing is awaited
    if (fullMessage.captionOnly || isMessageReady(fullMessage, account)) {
        IncomingMessage readyMessage = account.pendingMessages.addReadyMessage(std::move(fullMessage), action);
        if (readyMessage.message)
            showMessage(chat, readyMessage, account);
    } else {
        MessageId messageId = getId(*fullMessage.message);
        IncomingMessage &addedMessage = account.pendingMessages.addPendingMessage(std::move(fullMessage), action);
        fetchExtras(addedMessage, account,
            [&account, chatId, messageId](uint64_t, td::td_api::object_ptr<td::td_api::Object> object) {
                findMessageResponse(account, chatId, messageId, std::move(object));
            }
        );
    }
}

// A reaction to a message, from someone who is named
struct GivenReaction {
    std::string                     key;  // who gave which, as getReactionKey has it
    const td::td_api::ReactionType *type;
    bool                            self;
};

static std::string getSenderKey(const td::td_api::MessageSender &sender)
{
    if (sender.get_id() == td::td_api::messageSenderUser::ID)
        return "u" + std::to_string(static_cast<const td::td_api::messageSenderUser &>(sender).user_id_);
    if (sender.get_id() == td::td_api::messageSenderChat::ID)
        return "c" + std::to_string(static_cast<const td::td_api::messageSenderChat &>(sender).chat_id_);
    return "";
}

// Who gave which: the sender's key and the reaction's, in that order, with a space between. A
// sender's key, "u" or "c" and an id, has no space in it.
static std::string getReactionKey(const std::string &senderKey, const td::td_api::ReactionType &type)
{
    return senderKey + ' ' + getReactionTypeKey(type);
}

static std::string getReactionKeySender(const std::string &reactionKey)
{
    return reactionKey.substr(0, reactionKey.find(' '));
}

static std::string getReactionKeyType(const std::string &reactionKey)
{
    size_t space = reactionKey.find(' ');
    return (space != std::string::npos) ? reactionKey.substr(space + 1) : std::string();
}

// The user or the chat a sender's key is of; invalid if it is of the other
static UserId getSenderKeyUser(const std::string &senderKey)
{
    return ((senderKey.size() > 1) && (senderKey[0] == 'u')) ? UserId::fromString(senderKey.c_str() + 1) : UserId();
}

static ChatId getSenderKeyChat(const std::string &senderKey)
{
    return ((senderKey.size() > 1) && (senderKey[0] == 'c')) ? ChatId::fromString(senderKey.c_str() + 1) : ChatId();
}

// Each reaction to a message, but tags in Saved Messages, which are reactions too to TDLib
static void forEachReaction(const td::td_api::messageInteractionInfo *info,
                            const std::function<void(const td::td_api::messageReaction &)> &visit)
{
    if (!info || !info->reactions_ || info->reactions_->are_tags_)
        return;
    for (const auto &reaction: info->reactions_->reactions_)
        if (reaction && reaction->type_)
            visit(*reaction);
}

// The reactions to a message whose givers are named, our own first so that it is known as such.
// Channels and big groups name nobody, and smaller ones only the latest few; reactions whose
// givers are not all named go into incompleteTypes.
static void getGivenReactions(const td::td_api::messageInteractionInfo *info, std::vector<GivenReaction> &given,
                              std::set<std::string> *incompleteTypes)
{
    forEachReaction(info, [&given, incompleteTypes](const td::td_api::messageReaction &reaction) {
        std::set<std::string> senders;
        auto add = [&](const td::td_api::MessageSender &sender, bool self) {
            std::string senderKey = getSenderKey(sender);
            if (senders.insert(senderKey).second)
                given.push_back({getReactionKey(senderKey, *reaction.type_), reaction.type_.get(), self});
        };
        if (reaction.is_chosen_ && reaction.used_sender_id_)
            add(*reaction.used_sender_id_, true);
        for (const auto &sender: reaction.recent_sender_ids_)
            if (sender)
                add(*sender, false);
        if (incompleteTypes && (reaction.total_count_ > (int32_t)senders.size()))
            incompleteTypes->insert(getReactionTypeKey(*reaction.type_));
    });
}


// Those giving each reaction now, as far as they are named
static std::map<std::string, ShownMessage::Givers> getReactionGivers(const td::td_api::messageInteractionInfo *info)
{
    std::map<std::string, ShownMessage::Givers> givers;
    forEachReaction(info, [&givers](const td::td_api::messageReaction &reaction) {
        ShownMessage::Givers &item = givers[getReactionTypeKey(*reaction.type_)];
        item.total = reaction.total_count_;
        for (const auto &sender: reaction.recent_sender_ids_)
            if (sender)
                item.senders.push_back(getSenderKey(*sender));
        // Ours is not always among the latest
        if (reaction.is_chosen_ && reaction.used_sender_id_) {
            std::string self = getSenderKey(*reaction.used_sender_id_);
            if (std::find(item.senders.begin(), item.senders.end(), self) == item.senders.end())
                item.senders.insert(item.senders.begin(), self);
        }
    });
    return givers;
}

static std::string getReactionSenderName(TdAccountData &account, const std::string &reactionKey)
{
    std::string senderKey = getReactionKeySender(reactionKey);
    UserId      userId    = getSenderKeyUser(senderKey);
    if (userId.valid())
        return account.getDisplayName(userId);
    const td::td_api::chat *chat = account.getChat(getSenderKeyChat(senderKey));
    return chat ? chat->title_ : std::string();
}

// Shown as a message from whoever reacted, as purple-gowhatsapp has it, which names the message by
// the beginning of its text, or failing that, by what it is and when it was sent. The reaction is
// markup, a picture perhaps; without one, it is one taken back.
static void showReactionText(TdAccountData &account, const td::td_api::chat &chat, MessageId messageId,
                             const ShownMessage &shownMessage, const std::string &reactionKey, bool self,
                             const std::string *reaction, bool image)
{
    enum { MAX_QUOTE_LENGTH = 50 };
    std::string quote;
    if (shownMessage.quotable && !shownMessage.content.empty()) {
        quote = shownMessage.content;
        std::replace(quote.begin(), quote.end(), '\n', ' ');
        if (g_utf8_validate(quote.c_str(), quote.size(), nullptr) &&
            (g_utf8_strlen(quote.c_str(), -1) > MAX_QUOTE_LENGTH))
        {
            quote.erase(g_utf8_offset_to_pointer(quote.c_str(), MAX_QUOTE_LENGTH) - quote.c_str());
            quote += "…";
        }
        quote = linkToMessage(account.purpleAccount, getId(chat), messageId, escapeForDisplay(quote));
    }
    std::string kind   = escapeForDisplay(shownMessage.kind);
    std::string sentAt = linkToMessage(account.purpleAccount, getId(chat), messageId,
                                       formatMessageTime(shownMessage.timestamp));

    std::string text;
    if (reaction) {
        const std::string &emoji = *reaction;
        if (!quote.empty())
            // TRANSLATOR: In-chat message, shown as sent by whoever reacted. Arguments are a reaction (an emoji, mostly) and the beginning of the message's text. Use the quotation marks of your language.
            text = formatMessage(_("reacted with {0} to message „{1}“."), {emoji, quote});
        else
            // TRANSLATOR: In-chat message, shown as sent by whoever reacted. Arguments are a reaction (an emoji, mostly), what the message is ("photo", "sticker" and the like), and the time (or date and time) it was sent at, as the conversation window shows it.
            text = formatMessage(_("reacted with {0} to {1} from {2}."), {emoji, kind, sentAt});
    } else {
        if (!quote.empty())
            // TRANSLATOR: In-chat message, shown as sent by whoever reacted. Argument is the beginning of the message's text. Use the quotation marks of your language.
            text = formatMessage(_("removed their reaction to message „{0}“."), quote);
        else
            // TRANSLATOR: In-chat message, shown as sent by whoever reacted. Arguments are what the message is ("photo", "sticker" and the like), and the time (or date and time) it was sent at, as the conversation window shows it.
            text = formatMessage(_("removed their reaction to {0} from {1}."), {kind, sentAt});
    }

    TgMessageInfo messageInfo;
    messageInfo.type                    = TgMessageInfo::Type::Other;
    messageInfo.timestamp               = time(nullptr);
    messageInfo.outgoing                = self;
    messageInfo.incomingGroupchatSender = getReactionSenderName(account, reactionKey);
    const td::td_api::user *reactor = account.getUser(getSenderKeyUser(getReactionKeySender(reactionKey)));
    if (reactor)
        messageInfo.incomingGroupchatSenderName = getPurpleBuddyName(*reactor);
    showMessageText(account, chat, messageInfo, text.c_str(), nullptr, image ? PURPLE_MESSAGE_IMAGES : 0);
}

// Given the reaction as markup: the emoji, or the picture of a custom one
using CustomEmojiShown = std::function<void(TdAccountData &account, const std::string &reaction, bool image)>;

// In the line, unlike a sticker, which has one of its own
static std::string makeReactionImageText(int imageId)
{
    return "<img id=\"" + std::to_string(imageId) + "\">";
}

// Given to the front-end as it is, if it can show it; TGS, which nothing can, is converted, as it is
// for stickers. Failing that, the emoji the custom one stands for will do.
static void showCustomEmojiFile(TdAccountData &account, const std::string &path, int32_t format,
                                const std::string &fallback, CustomEmojiShown shown)
{
    // Whatever it is shown as, it leads to the file
    std::string linkStart = "<a href=\"" + escapeForDisplay(makeFileLinkTarget(path)) + "\">";
    std::string linkEnd   = "</a>";
    makeCustomEmojiImage(account, path, format, [fallback, shown = std::move(shown), linkStart, linkEnd]
                                                (TdAccountData &account, int imageId) {
        if (imageId)
            shown(account, linkStart + makeReactionImageText(imageId) + linkEnd, true);
        else
            shown(account, linkStart + fallback + linkEnd, false);
    });
}

static void customEmojiResponse(TdAccountData &account, ChatId chatId,
                                td::td_api::object_ptr<td::td_api::Object> object, CustomEmojiShown shown)
{
    const td::td_api::sticker *sticker = nullptr;
    if (object && (object->get_id() == td::td_api::stickers::ID)) {
        const auto &stickers = static_cast<const td::td_api::stickers &>(*object);
        if (!stickers.stickers_.empty())
            sticker = stickers.stickers_.front().get();
    }

    // A smiley of the conversation, where it can be one: shown at once, its picture following
    const td::td_api::chat *chat = account.getChat(chatId);
    if (sticker && chat) {
        std::string code = addCustomEmojiSmiley(account, getMessageConversation(account, *chat), *sticker);
        if (!code.empty()) {
            shown(account, escapeForDisplay(code), false);
            return;
        }
    }

    // The emoji it stands for will do, where the picture will not
    std::string fallback;
    if (sticker && !sticker->emoji_.empty())
        fallback = escapeForDisplay(sticker->emoji_);
    else
        // TRANSLATOR: In-chat reaction, in place of one that is a picture of its own rather than an emoji, where it cannot be shown, as in "reacted with [custom emoji] to message ...".
        fallback = escapeForDisplay(_("[custom emoji]"));
    if (!sticker || !sticker->sticker_ || !sticker->format_ || foldCustomEmoji(account.purpleAccount)) {
        shown(account, fallback, false);
        return;
    }

    // One that may not move is shown as its emoji; one that cannot be converted, as its emoji too,
    // but linked to its file (showCustomEmojiFile)
    int32_t format = sticker->format_->get_id();
    if (!animationsWanted(account.purpleAccount, format)) {
        shown(account, fallback, false);
        return;
    }
    getCustomEmojiFile(account, sticker->sticker_.get(),
                       [format, fallback, shown = std::move(shown)](TdAccountData &account, const std::string &path) {
        if (!path.empty())
            showCustomEmojiFile(account, path, format, fallback, shown);
        else
            shown(account, fallback, false);
    });
}

static void showReaction(TdAccountData &account, const td::td_api::chat &chat, MessageId messageId,
                         const ShownMessage &shownMessage,
                         const std::string &reactionKey, bool self, const td::td_api::ReactionType *reaction)
{
    if (!reaction) {
        showReactionText(account, chat, messageId, shownMessage, reactionKey, self, nullptr, false);
        return;
    }

    switch (reaction->get_id()) {
    case td::td_api::reactionTypeEmoji::ID: {
        std::string emoji = escapeForDisplay(static_cast<const td::td_api::reactionTypeEmoji &>(*reaction).emoji_);
        showReactionText(account, chat, messageId, shownMessage, reactionKey, self, &emoji, false);
        break;
    }
    case td::td_api::reactionTypeCustomEmoji::ID: {
        // A sticker of its own, to be looked up first; the message may be forgotten meanwhile
        int64_t customEmojiId = static_cast<const td::td_api::reactionTypeCustomEmoji &>(*reaction).custom_emoji_id_;
        purple_debug_misc(config::pluginId, "Fetching custom emoji %" G_GINT64_FORMAT "\n", customEmojiId);
        ChatId       chatId = getId(chat);
        ShownMessage message;
        message.content   = shownMessage.content;
        message.quotable  = shownMessage.quotable;
        message.kind      = shownMessage.kind;
        message.timestamp = shownMessage.timestamp;
        CustomEmojiShown shown = [chatId, messageId, message, reactionKey, self](TdAccountData &account,
                                                                     const std::string &reaction, bool image) {
            const td::td_api::chat *chat = account.getChat(chatId);
            if (chat)
                showReactionText(account, *chat, messageId, message, reactionKey, self, &reaction, image);
        };
        auto request = td::td_api::make_object<td::td_api::getCustomEmojiStickers>();
        request->custom_emoji_ids_.push_back(customEmojiId);
        account.transceiver.sendQuery(std::move(request),
            [&account, chatId, shown](uint64_t, td::td_api::object_ptr<td::td_api::Object> object) {
                customEmojiResponse(account, chatId, std::move(object), shown);
            });
        break;
    }
    default: {
        // Paid in Telegram Stars
        std::string star = "\u2B50";
        showReactionText(account, chat, messageId, shownMessage, reactionKey, self, &star, false);
    }
    }
}

void handleIncomingMessage(TdAccountData &account, const td::td_api::chat &chat,
    td::td_api::object_ptr<td::td_api::message> message,
    PendingMessageQueue::MessageAction action, bool redrawn, bool logRedrawn)
{
    if (!message) return;
    ChatId chatId = getId(chat);
    // Fetched while still being sent, as for a tab redrawn just then, and sent since: it goes by
    // its new id, or its marks would name one that is gone
    MessageId sentId = account.getSentMessageId(chatId, getId(*message));
    if (message->sending_state_ && (sentId != getId(*message)))
        message->id_ = sentId.value();

    if (isReadReceiptsEnabled(account.purpleAccount))
        account.addPendingReadReceipt(chatId, getId(*message));

    if (message->content_) {
        ShownMessage shownMessage;
        getShownContent(*message->content_, shownMessage);
        shownMessage.timestamp = message->date_;
        shownMessage.outgoing  = message->is_outgoing_;
        // Reactions it came with are no news
        std::vector<GivenReaction> reactions;
        getGivenReactions(message->interaction_info_.get(), reactions, nullptr);
        for (const GivenReaction &reaction: reactions)
            shownMessage.reactions.emplace(reaction.key, reaction.self);
        shownMessage.givers = getReactionGivers(message->interaction_info_.get());
        account.addShownMessage(chatId, getId(*message), std::move(shownMessage));
    }

    MessageId messageId = getId(*message);
    IncomingMessage fullMessage;
    makeFullMessage(chat, std::move(message), fullMessage, account);
    fullMessage.messageInfo.fromHistory = (action == PendingMessageQueue::Prepend) || redrawn;
    fullMessage.messageInfo.redrawn     = redrawn;
    fullMessage.messageInfo.logRedrawn  = redrawn && logRedrawn;
    // Who sent it, for a notice about it later: several members' messages sent in the same second
    // are otherwise reported deleted in the same words, and in a private chat either of us may have
    // sent it. A channel post signed by nobody has no one to name.
    if (!fullMessage.messageInfo.anonymousChannelPost) {
        ShownMessage *shownMessage = account.findShownMessage(chatId, messageId);
        if (shownMessage)
            shownMessage->sender = getSenderDisplayName(chat, fullMessage.messageInfo, account.purpleAccount);
    }
    queueMessage(account, chat, std::move(fullMessage), action);
}

static void requestEditedMessage(TdAccountData &account, const PendingEdit &edit);

static void editedMessageResponse(TdAccountData &account, const PendingEdit &edit,
                                  td::td_api::object_ptr<td::td_api::Object> object)
{
    const td::td_api::chat *chat = account.getChat(edit.chatId);
    if (!chat || !object || (object->get_id() != td::td_api::message::ID)) {
        purple_debug_warning(config::pluginId, "Failed to fetch edited message %" G_GINT64_FORMAT "\n",
                             edit.messageId.value());
        // Not shown, so the same change coming again is still news, unless a later edit has
        // taken this one's place
        ShownMessage *shownMessage = account.findShownMessage(edit.chatId, edit.messageId);
        if (shownMessage && (*shownMessage == edit.installed)) {
            // Reactions are not the edit's to take back
            std::map<std::string, bool> reactions = std::move(shownMessage->reactions);
            auto                        givers    = std::move(shownMessage->givers);
            *shownMessage           = edit.previous;
            shownMessage->reactions = std::move(reactions);
            shownMessage->givers    = std::move(givers);
        }
        return;
    }

    // Deleted meanwhile, which has been reported already
    if (!account.findShownMessage(edit.chatId, edit.messageId)) {
        purple_debug_misc(config::pluginId, "Edited message %" G_GINT64_FORMAT " is gone\n",
                          edit.messageId.value());
        return;
    }

    // The message, or an earlier edit of it, may have gone into the queue meanwhile
    if (account.pendingMessages.findPendingMessage(edit.chatId, edit.messageId)) {
        account.addPendingEdit(edit);
        return;
    }

    td::td_api::object_ptr<td::td_api::message> message = td::move_tl_object_as<td::td_api::message>(object);
    time_t editDate = message->edit_date_ ? message->edit_date_ : time(nullptr);
    // Written over the original where Pidgin shows it, the reply or forward header included, as
    // the whole of its text goes; otherwise after the notice, without them, the original having
    // them already
    bool replacing = canReplaceMessage(findMessageConversation(account, *chat), edit.messageId);
    if (!replacing) {
        message->reply_to_     = nullptr;
        message->forward_info_ = nullptr;
    }

    IncomingMessage fullMessage;
    makeFullMessage(*chat, std::move(message), fullMessage, account);
    fullMessage.editedMessageTime     = edit.messageTime;
    fullMessage.captionOnly           = edit.captionOnly;
    fullMessage.replacing             = replacing;
    fullMessage.formattingOnly        = edit.formattingOnly;
    fullMessage.messageInfo.timestamp = editDate;
    queueMessage(account, *chat, std::move(fullMessage), PendingMessageQueue::Append);
}

static void requestEditedMessage(TdAccountData &account, const PendingEdit &edit)
{
    purple_debug_misc(config::pluginId, "Fetching edited message %" G_GINT64_FORMAT "\n",
                      edit.messageId.value());
    auto request = makeGetMessage(edit.chatId, edit.messageId);
    account.transceiver.sendQuery(std::move(request),
        [&account, edit](uint64_t, td::td_api::object_ptr<td::td_api::Object> object) {
            editedMessageResponse(account, edit, std::move(object));
        });
}

// Shows an edit that came while the message was waiting to be shown, now that it has been
static void showPendingEdit(TdAccountData &account, ChatId chatId, MessageId messageId)
{
    PendingEdit edit;
    if (account.extractPendingEdit(chatId, messageId, edit))
        requestEditedMessage(account, edit);
}

void updateMessageContent(TdAccountData &account, ChatId chatId, MessageId messageId,
                          td::td_api::object_ptr<td::td_api::MessageContent> content)
{
    // A message not shown in this session is in no conversation window to be corrected
    ShownMessage *shownMessage = account.findShownMessage(chatId, messageId);
    if (!shownMessage || !content)
        return;

    ShownMessage editedMessage;
    getShownContent(*content, editedMessage);
    // A file of ours, uploaded as the message is sent, is the same, only known by its remote
    // identifier from then on
    bool uploaded = g_str_has_prefix(shownMessage->file.c_str(), LOCAL_FILE_KEY) && !editedMessage.file.empty();
    if (((editedMessage.file == shownMessage->file) || uploaded) && (editedMessage.content == shownMessage->content) &&
        (editedMessage.formatting == shownMessage->formatting))
    {
        purple_debug_misc(config::pluginId, "Message %" G_GINT64_FORMAT " changed out of sight\n",
                          messageId.value());
        shownMessage->file = editedMessage.file;
        return;
    }

    PendingEdit edit;
    edit.chatId      = chatId;
    edit.messageId   = messageId;
    edit.messageTime = shownMessage->timestamp;
    edit.captionOnly = !editedMessage.file.empty() && (editedMessage.file == shownMessage->file);
    edit.formattingOnly = (editedMessage.file == shownMessage->file) &&
                          (editedMessage.content == shownMessage->content);
    edit.previous    = *shownMessage;
    editedMessage.timestamp   = shownMessage->timestamp;
    editedMessage.sender      = shownMessage->sender;
    editedMessage.outgoing    = shownMessage->outgoing;
    editedMessage.deletedHere = shownMessage->deletedHere;
    // Edits leave reactions be
    editedMessage.reactions = shownMessage->reactions;
    editedMessage.givers    = shownMessage->givers;
    edit.installed   = editedMessage;
    *shownMessage = std::move(editedMessage);

    IncomingMessage *pendingMessage = account.pendingMessages.findPendingMessage(chatId, messageId);
    if (pendingMessage) {
        td::td_api::message *message = pendingMessage->message.get();
        if (message && message->content_ &&
            (message->content_->get_id() == td::td_api::messageText::ID) &&
            (content->get_id() == td::td_api::messageText::ID))
        {
            // Text needs nothing fetched for it, so the new can simply take the old one's place
            message->content_ = std::move(content);
        } else
            // Requests made for the message are still under way, and would mistake the edit for it
            account.addPendingEdit(edit);
    } else
        requestEditedMessage(account, edit);
}

namespace {
// A deleted message's notice, waiting for who deleted it, where that can be told
struct DeletedMessage {
    MessageId   messageId;
    time_t      timestamp;
    std::string sender; // as the conversation names them, if known
    bool        outgoing    = false;
    bool        deletedHere = false;
};
}

// Says the message is gone: who deleted it, if known, whether they sent it or someone else did
static void showDeletionNotice(TdAccountData &account, ChatId chatId, const DeletedMessage &deleted,
                               const std::string &deleter, bool deletedOwn)
{
    const td::td_api::chat *chat = account.getChat(chatId);
    if (!chat)
        return;
    std::string when = linkToMessage(account.purpleAccount, chatId, deleted.messageId,
                                     formatMessageTime(deleted.timestamp));
    std::string notice;
    if (!deleter.empty() && deletedOwn)
        notice = formatMessage(
            // TRANSLATOR: In-chat notification. Arguments are a username and the time (or date and time) the message was sent at, as the conversation window shows it.
            _("{0} deleted their message sent at {1}"), {escapeForDisplay(deleter), when});
    else if (!deleter.empty() && !deleted.sender.empty())
        notice = formatMessage(
            // TRANSLATOR: In-chat notification. Arguments are the username of who deleted the message, that of who sent it, and the time (or date and time) it was sent at, as the conversation window shows it.
            _("{0} deleted the message {1} sent at {2}"),
            {escapeForDisplay(deleter), escapeForDisplay(deleted.sender), when});
    else if (!deleter.empty())
        notice = formatMessage(
            // TRANSLATOR: In-chat notification. Arguments are a username and the time (or date and time) the message was sent at, as the conversation window shows it.
            _("{0} deleted the message sent at {1}"), {escapeForDisplay(deleter), when});
    else if (!deleted.sender.empty())
        notice = formatMessage(
            // TRANSLATOR: In-chat notification. Arguments are a username and the time (or date and time) the message was sent at, as the conversation window shows it.
            _("The message {0} sent at {1} has been deleted"), {escapeForDisplay(deleted.sender), when});
    else
        notice = formatMessage(
            // TRANSLATOR: In-chat notification. Argument is the time (or date and time) the message was sent at, as the conversation window shows it.
            _("Message from {} has been deleted"), when);
    showChatNotification(account, *chat, notice.c_str());
}

// Telegram does not say who deleted a message, but a supergroup's or channel's log of events,
// which only its administrators can read, does
static bool readsEventLog(TdAccountData &account, const td::td_api::chat &chat)
{
    const td::td_api::supergroup *group = account.getSupergroup(getSupergroupId(chat));
    int32_t status = (group && group->status_) ? group->status_->get_id() : 0;
    return (status == td::td_api::chatMemberStatusAdministrator::ID) ||
           (status == td::td_api::chatMemberStatusCreator::ID);
}

static std::string getEventMemberName(TdAccountData &account, const td::td_api::object_ptr<td::td_api::MessageSender> &member)
{
    if (!member)
        return std::string();
    if (member->get_id() == td::td_api::messageSenderChat::ID) {
        const td::td_api::chat *chat = account.getChat(getChatId(static_cast<const td::td_api::messageSenderChat &>(*member)));
        return chat ? chat->title_ : std::string();
    }
    // Us, as our own messages are named
    UserId userId = getUserId(member);
    if (userId.valid() && (userId.value() == account.options.myUserId))
        return purple_account_get_name_for_display(account.purpleAccount);
    return account.getDisplayName(userId);
}

// The notices, once the log tells who deleted the messages; those it does not name, without
static void eventLogResponse(TdAccountData &account, ChatId chatId, const std::vector<DeletedMessage> &deleted,
                             td::td_api::object_ptr<td::td_api::Object> response)
{
    // Who deleted each message, and whether they sent it: told by who they are, not by their names,
    // which two people may share; a message the chat itself sent, such as a channel's post, is no
    // one's own
    std::map<int64_t, std::pair<std::string, bool>> deleters;
    if (response && (response->get_id() == td::td_api::chatEvents::ID)) {
        for (const auto &event: static_cast<const td::td_api::chatEvents &>(*response).events_)
            if (event && event->action_ &&
                (event->action_->get_id() == td::td_api::chatEventMessageDeleted::ID)) {
                const auto &action = static_cast<const td::td_api::chatEventMessageDeleted &>(*event->action_);
                if (action.message_)
                    deleters.emplace(action.message_->id_, std::make_pair(
                        getEventMemberName(account, event->member_id_),
                        event->member_id_ && action.message_->sender_id_ &&
                        isSameUser(*event->member_id_, *action.message_->sender_id_)));
            }
    } else
        purple_debug_misc(config::pluginId, "No log of events of chat %" G_GINT64_FORMAT ": %s\n",
                          chatId.value(), getDisplayedError(response).c_str());
    for (const DeletedMessage &message: deleted) {
        auto it = deleters.find(message.messageId.value());
        if (it != deleters.end())
            showDeletionNotice(account, chatId, message, it->second.first, it->second.second);
        else
            showDeletionNotice(account, chatId, message, std::string(), false);
    }
}

void deleteMessages(TdAccountData &account, const td::td_api::updateDeleteMessages &update)
{
    // Messages dropped from TDLib's cache are not deleted at all
    if (!update.is_permanent_ || update.from_cache_)
        return;

    ChatId                      chatId = getChatId(update);
    const td::td_api::chat     *chat   = account.getChat(chatId);
    std::vector<DeletedMessage> deleted;

    for (unsigned i = 0; i < update.message_ids_.size(); i++) {
        MessageId     messageId    = getMessageId(update, i);
        ShownMessage *shownMessage = account.findShownMessage(chatId, messageId);
        if (!shownMessage)
            continue;
        DeletedMessage message{messageId, shownMessage->timestamp, shownMessage->sender, shownMessage->outgoing,
                               shownMessage->deletedHere};
        account.removeShownMessage(chatId, messageId);

        PendingEdit edit;
        account.extractPendingEdit(chatId, messageId, edit);
        // A message still waiting to be shown is simply not shown; but one waiting to be shown again,
        // as edited, is in the conversation already
        IncomingMessage *pendingMessage = account.pendingMessages.findPendingMessage(chatId, messageId);
        if (pendingMessage)
            pendingMessage->deleted = true;
        if ((!pendingMessage || pendingMessage->editedMessageTime) && chat) {
            PurpleConversation *conv = findMessageConversation(account, *chat);
            if (keepsDeletedMessages(account.purpleAccount, findChatBlistNode(account, *chat)))
                strikeOutMessage(conv, messageId);
            else
                removeMessage(conv, messageId);
            deleted.push_back(std::move(message));
        }
    }
    // Deleted from here, it was by us
    auto byUs = std::stable_partition(deleted.begin(), deleted.end(),
                                      [](const DeletedMessage &message) { return message.deletedHere; });
    for (auto it = deleted.begin(); it != byUs; ++it)
        showDeletionNotice(account, chatId, *it, purple_account_get_name_for_display(account.purpleAccount),
                           it->outgoing);
    deleted.erase(deleted.begin(), byUs);
    // None are shown, but of a chat known
    if (deleted.empty() || !chat)
        return;

    // In a group the notice names who sent the message, which tells apart those several members
    // sent at once; and who deleted it, where the log of events tells
    if (!readsEventLog(account, *chat)) {
        for (const DeletedMessage &message: deleted)
            showDeletionNotice(account, chatId, message, std::string(), false);
        return;
    }
    auto filters = td::td_api::make_object<td::td_api::chatEventLogFilters>();
    filters->message_deletions_ = true;
    auto request = td::td_api::make_object<td::td_api::getChatEventLog>();
    request->chat_id_       = chatId.value();
    request->from_event_id_ = 0;
    // The latest deletions, these among them, and a few made meanwhile
    enum { EXTRA_EVENTS = 10, MAX_EVENTS = 100, EVENT_LOG_WAIT_SECONDS = 5 };
    request->limit_         = static_cast<int32_t>(std::min<size_t>(deleted.size() + EXTRA_EVENTS, MAX_EVENTS));
    request->filters_       = std::move(filters);
    // Not answered in time, as while offline, the notices are written without it
    account.transceiver.sendQueryWithTimeout(std::move(request),
        [&account, chatId, deleted](uint64_t, td::td_api::object_ptr<td::td_api::Object> response) {
            eventLogResponse(account, chatId, deleted, std::move(response));
        }, EVENT_LOG_WAIT_SECONDS);
}

static void fetchHistoryRequest(TdAccountData &account, ChatId chatId, unsigned messagesFetched,
                                MessageId fetchBackFrom, MessageId lastReceivedMessage);

// Fills the tab with the newest messages, if that was asked for while the chat was busy
static void showPendingNewest(TdAccountData &account, ChatId chatId);

static void fetchHistoryResponse(TdAccountData &account, ChatId chatId, MessageId stopAt,
                                 unsigned messagesFetched,
                                 td::td_api::object_ptr<td::td_api::Object> response)
{
    MessageId requestMoreFrom = MessageId::invalid;
    const td::td_api::chat *chat = account.getChat(chatId);

    if (response && (response->get_id() == td::td_api::messages::ID)) {
        td::td_api::messages &messages = static_cast<td::td_api::messages &>(*response);
        purple_debug_misc(config::pluginId, "Fetched %zu messages for chat %" G_GINT64_FORMAT "\n",
                          messages.messages_.size(), chatId.value());
        auto stop = messages.messages_.begin();
        MessageId lastMessageId = MessageId::invalid;
        for (; stop != messages.messages_.end(); ++stop) {
            td::td_api::object_ptr<td::td_api::message> message = std::move(*stop);
            if (!message) {
                purple_debug_warning(config::pluginId, "Erroneous message in history, stopping\n");
                break;
            }
            // That one may be gone since, deleted, and ids grow with time
            if (stopAt.valid() && (getId(*message).value() <= stopAt.value())) {
                purple_debug_misc(config::pluginId, "Reached message %" G_GINT64_FORMAT ", stopping\n",
                                  stopAt.value());
                break;
            }
            if (!stopAt.valid() && (messagesFetched >= CHAT_HISTORY_RETRIEVE_LIMIT)) {
                purple_debug_misc(config::pluginId, "Reached history limit, stopping\n");
                break;
            }
            messagesFetched++;
            lastMessageId = getId(*message);
            if (chat)
                handleIncomingMessage(account, *chat, std::move(message), PendingMessageQueue::Prepend);
        }

        if (stop == messages.messages_.end())
            requestMoreFrom = lastMessageId;
    } else {
        std::string message = formatMessage(_("Failed to fetch earlier messages: {}"),
                                            getDisplayedError(response));
        purple_debug_warning(config::pluginId, "%s\n", message.c_str());
        if (chat)
            showChatNotification(account, *chat, message.c_str(), PURPLE_MESSAGE_ERROR);
    }

    if (requestMoreFrom.valid())
        fetchHistoryRequest(account, chatId, messagesFetched, requestMoreFrom, stopAt);
    else {
        purple_debug_misc(config::pluginId, "Done fetching history for chat %" G_GINT64_FORMAT " (%u msgs)\n",
                          chatId.value(), messagesFetched);
        std::vector<IncomingMessage> readyMessages;
        account.pendingMessages.setChatReady(chatId, readyMessages);
        showMessages(readyMessages, account);
        showPendingNewest(account, chatId);
    }
}

static void fetchHistoryRequest(TdAccountData &account, ChatId chatId, unsigned messagesFetched,
                                MessageId fetchBackFrom, MessageId stopAt)
{
    auto request = td::td_api::make_object<td::td_api::getChatHistory>();
    request->chat_id_ = chatId.value();
    request->from_message_id_ = fetchBackFrom.valid() ? fetchBackFrom.value() : 0;
    request->limit_ = CHAT_HISTORY_REQUEST_LIMIT;
    request->offset_ = 0;
    request->only_local_ = false;
    purple_debug_misc(config::pluginId, "Requesting history for chat %" G_GINT64_FORMAT
                      " starting from %" G_GINT64_FORMAT "\n", chatId.value(), fetchBackFrom.value());
    account.transceiver.sendQuery(std::move(request),
        [&account, chatId, messagesFetched, stopAt](uint64_t, td::td_api::object_ptr<td::td_api::Object> response) {
            fetchHistoryResponse(account, chatId, stopAt, messagesFetched, std::move(response));
        });
}

void fetchHistory(TdAccountData &account, ChatId chatId, MessageId fetchFrom, MessageId stopAt)
{
    if (!account.pendingMessages.isChatReady(chatId))
        return;

    account.pendingMessages.setChatNotReady(chatId);
    fetchHistoryRequest(account, chatId, 0, fetchFrom, stopAt);
}

// As many messages as Pidgin's tab keeps, at most, it having at least a line for each; no limit
// if it keeps all
static unsigned getRedrawLimit()
{
    static const char SCROLLBACK_PREF[] = "/pidgin/conversations/scrollback_lines";
    int lines = purple_prefs_exists(SCROLLBACK_PREF) ? purple_prefs_get_int(SCROLLBACK_PREF) : 0;
    return (lines > 0) ? lines : std::numeric_limits<unsigned>::max();
}

// Lines a tab that stops short of its chat's newest message is kept to: half of what Pidgin keeps,
// trimming by the line, so that it never takes away what was just shown; none if it keeps all, or
// if the tab's lines cannot be counted
static unsigned getWindowLines(PurpleConversation *conv)
{
    unsigned limit = getRedrawLimit();
    return ((limit != std::numeric_limits<unsigned>::max()) && countTabLines(conv)) ? limit / 2 : 0;
}

// Written into a tab at once, at most, the earliest of the messages found; the tab then stops
// short of the rest, which are shown as it is scrolled down to its end, so many at a time. Pidgin
// takes the longer the more its tab holds, for each message written.
enum { WINDOW_MESSAGES = 200, LATER_BATCH = 50 };

namespace {
// What a tab is filled anew for: from the earliest message, or opening it, from the first unread;
// keeping it at its end, as a tab that fit; going back from its top; for the message a link led to;
// or for its latest messages only
enum class RedrawMode { Plain, Opening, Filling, Back, Linked, Newest };

struct Redraw {
    ChatId                                                  chatId;
    MessageId                                               earliest; // or
    RedrawMode                                              mode = RedrawMode::Plain;
    unsigned                                                unread = 0;  // messages, if opening
    MessageId                                               readUpTo;    // the latest received read
    MessageId                                               firstUnread; // found so far
    unsigned                                                extra = 0; // messages wanted before the earliest
    MessageId                                               latest; // the tab's end, if it stops short
    bool                                                    found  = false; // the earliest, or one read
    unsigned                                                beyond = 0; // found so far
    bool                                                    start  = false; // the chat's first message got
    unsigned                                                limit;
    // Newest first, as TDLib gives them
    std::vector<td::td_api::object_ptr<td::td_api::message>> messages;
};
}

static void redrawRequest(TdAccountData &account, std::shared_ptr<Redraw> redraw, MessageId from);
static bool startRedraw(TdAccountData &account, ChatId chatId, MessageId earliest, RedrawMode mode,
                        unsigned extra = 0);

// Shown before the earliest message scrolled to, or before the first unread on opening a tab, at
// least; a tab is filled with more if they still fit
enum { EARLIER_BATCH = 50 };


static void stopRedrawProgress(TdAccountData &account, ChatId chatId)
{
    const td::td_api::chat *chat = account.getChat(chatId);
    stopProgress(chat ? findMessageConversation(account, *chat) : nullptr);
}

// Whether Pidgin's tab writes TDLib's message as a notice, rather than as someone's message
static bool isShownAsNotice(const td::td_api::message &message)
{
    ContentParts parts;
    return !message.content_ || ((message.content_->get_id() != td::td_api::messageText::ID) &&
                                 !getContentParts(*message.content_, parts));
}

// What of the log goes into the tab with TDLib's messages: a message the log names that TDLib no
// longer has was deleted, and is struck out or left out, as set for the chat; one the log does not
// name, or a notice, is left out if TDLib has a message written the same way at much the same time,
// as it is that one. Other notices, such as of edits and reactions, go in; but not the one closing a
// log, of no message, which is Pidgin's own that the account has disconnected, and which the
// messages shown since make no news.
// From the earliest of the messages; past the tab's last one, if they follow it, as the log's lines
// between the two are shown with none else: of messages deleted, or notices
time_t getLoggedLinesSince(const std::vector<td::td_api::object_ptr<td::td_api::message>> &messages,
                           time_t afterDate)
{
    if (afterDate)
        return afterDate;
    return (!messages.empty() && messages.back()) ? messages.back()->date_ : 0;
}

std::vector<LoggedLine> pickLoggedLines(std::vector<LoggedLine> logged,
                                        const std::vector<td::td_api::object_ptr<td::td_api::message>> &messages,
                                        bool stopsShort, bool keepDeleted, MessageId after, time_t afterDate)
{
    enum { SAME_TIME = 10 };
    std::vector<LoggedLine> lines;
    time_t since = getLoggedLinesSince(messages, afterDate);
    // Nothing past the last of them, in a tab that stops there: by its id, for a message the log names
    bool    cut     = stopsShort && !messages.empty() && messages.front();
    time_t  until   = cut ? messages.front()->date_ : std::numeric_limits<time_t>::max();
    int64_t untilId = cut ? messages.front()->id_ : std::numeric_limits<int64_t>::max();
    std::set<int64_t>           ids;
    std::multimap<time_t, bool> dates; // whether shown as a notice
    for (const auto &message: messages)
        if (message) {
            ids.insert(message->id_);
            dates.emplace(message->date_, isShownAsNotice(*message));
        }
    auto hasNear = [&dates](time_t when, bool notice) {
        for (auto it = dates.lower_bound(when - SAME_TIME); (it != dates.end()) && (it->first <= when + SAME_TIME); ++it)
            if (it->second == notice)
                return true;
        return false;
    };
    for (LoggedLine &line: logged) {
        bool keep;
        // Nor what the tab shows already, before them, which may have been sent in the same second;
        // the notices of the tab's last second were shown with it
        if ((line.when < since) || (line.when > until) ||
            (line.messageId.valid() && (line.messageId.value() > untilId)) ||
            (line.messageId.valid() && after.valid() && (line.messageId.value() <= after.value())) ||
            (!line.messageId.valid() && afterDate && (line.when <= afterDate)))
            keep = false;
        else if (line.messageId.valid()) {
            keep = !ids.count(line.messageId.value()) && keepDeleted;
            if (keep)
                line.html = line.html.substr(0, line.bodyStart) + "<s>" + line.html.substr(line.bodyStart) + "</s>";
        } else
            keep = !hasNear(line.when, !line.message) &&
                   (line.message || !line.closing || (line.html.find(JUMP_LINK_SCHEME) != std::string::npos));
        if (keep)
            lines.push_back(std::move(line));
        else
            for (int id: line.imageIds)
                purple_imgstore_unref_by_id(id);
    }
    return lines;
}

static std::vector<LoggedLine> getLoggedLines(TdAccountData &account, const td::td_api::chat &chat,
                                              PurpleConversation *conv,
                                              const std::vector<td::td_api::object_ptr<td::td_api::message>> &messages,
                                              bool stopsShort, MessageId after = MessageId(), time_t afterDate = 0)
{
    time_t since = getLoggedLinesSince(messages, afterDate);
    if (!since)
        return {};
    bool keepDeleted = keepsDeletedMessages(account.purpleAccount, findChatBlistNode(account, chat));
    return pickLoggedLines(readLoggedLines(account.purpleAccount, purple_conversation_get_type(conv),
                                           purple_conversation_get_name(conv), since),
                           messages, stopsShort, keepDeleted, after, afterDate);
}

// Ready to be shown among the messages, ahead of those already queued
static void queueLoggedLine(TdAccountData &account, ChatId chatId, LoggedLine &line)
{
    IncomingMessage item;
    item.message           = td::td_api::make_object<td::td_api::message>();
    item.message->chat_id_ = chatId.value();
    item.message->date_    = line.when;
    item.loggedHtml        = std::move(line.html);
    item.loggedImageIds    = std::move(line.imageIds);
    item.loggedId          = line.messageId;
    IncomingMessage left = account.pendingMessages.addReadyMessage(std::move(item), PendingMessageQueue::Prepend);
    for (int id: left.loggedImageIds)
        purple_imgstore_unref_by_id(id);
}

// Empties the tab and has the messages shown in it, oldest first, ahead of any that came meanwhile;
// with them, by their times, what of Pidgin's log of the conversation TDLib does not have
static void redrawShow(TdAccountData &account, Redraw &redraw)
{
    const td::td_api::chat *chat = account.getChat(redraw.chatId);
    PurpleConversation     *conv = findMessageConversation(account, redraw.chatId);
    stopProgress(conv);
    unsigned windowLines = conv ? getWindowLines(conv) : 0;
    // The earliest of them only, the rest shown later, if there are too many to write at once: also
    // where Pidgin keeps all lines, as long as a tab's can be counted, as it can then stop short
    bool stopsShort = redraw.latest.valid();
    if (conv && countTabLines(conv) && (redraw.messages.size() > WINDOW_MESSAGES)) {
        redraw.messages.erase(redraw.messages.begin(), redraw.messages.end() - WINDOW_MESSAGES);
        stopsShort = true;
    }
    if (conv) {
        purple_debug_misc(config::pluginId, "Redrawing chat %" G_GINT64_FORMAT " from message %" G_GINT64_FORMAT
                          " or the first unread %" G_GINT64_FORMAT ": %zu messages%s\n", redraw.chatId.value(),
                          redraw.earliest.value(), redraw.firstUnread.value(), redraw.messages.size(),
                          stopsShort ? ", short of the newest" : "");
        purple_conversation_clear_message_history(conv);
        setTabEnd(conv, (stopsShort && redraw.messages.front()) ? getId(*redraw.messages.front()) : MessageId());
        // Its first message is shown only if gone back to it again, below
        account.historyComplete.erase(redraw.chatId.value());
        account.redrawnCounts[redraw.chatId.value()] = redraw.messages.size();
        std::vector<LoggedLine> lines = getLoggedLines(account, *chat, conv, redraw.messages, stopsShort);
        // A chat never logged, as a channel only looked at, is logged as it is shown: else nothing
        // of it would be kept, nor bring it back to the buddy list after a restart
        bool firstLog = lines.empty() && !hasLog(account.purpleAccount, purple_conversation_get_type(conv),
                                                 purple_conversation_get_name(conv));
        // Newest first, as the messages, each going ahead of what is queued
        auto line = lines.rbegin();
        auto queueLinesSince = [&](time_t since) {
            for (; (line != lines.rend()) && (line->when >= since); ++line)
                queueLoggedLine(account, redraw.chatId, *line);
        };
        for (auto &message: redraw.messages)
            if (message && !account.pendingMessages.findPendingMessage(redraw.chatId, getId(*message))) {
                queueLinesSince(message->date_);
                handleIncomingMessage(account, *chat, std::move(message), PendingMessageQueue::Prepend, true,
                                      firstLog);
            }
        queueLinesSince(0);
    }
    std::vector<IncomingMessage> readyMessages;
    account.pendingMessages.setChatReady(redraw.chatId, readyMessages);
    showMessages(readyMessages, account);
    showPendingNewest(account, redraw.chatId);
    // Kept to what Pidgin will not trim: the message scrolled to and what follows, as much as fits,
    // or the latest
    MessageId scrolledTo = ((redraw.mode == RedrawMode::Filling) || (redraw.mode == RedrawMode::Newest))
                           ? MessageId()
                           : (redraw.mode == RedrawMode::Opening) ? redraw.firstUnread : redraw.earliest;
    bool trimmedTop = false;
    if (conv && windowLines && scrolledTo.valid() && trimTab(conv, windowLines, false, scrolledTo))
        setTabEnd(conv, findLastMessage(conv));
    else if (conv && windowLines)
        trimmedTop = trimTab(conv, windowLines, true);
    // Its first message shown, scrolling up looks for none before it, nor refills the tab with less
    if (redraw.start && !trimmedTop)
        account.historyComplete.insert(redraw.chatId.value());
    // Shown again, the messages already read are no news, as Pidgin takes them to be
    if (conv && chat && !chat->unread_count_)
        clearUnseen(conv);
    // Opened: at the first unread, if any. Gone further back: what was at the top stays there,
    // unless all of it was in view, the end then staying in view too, as Pidgin keeps it.
    if (redraw.mode == RedrawMode::Opening) {
        if (conv)
            scrollToMessageLater(conv, redraw.firstUnread);
    } else if (redraw.extra) {
        if (!redraw.beyond)
            account.historyComplete.insert(redraw.chatId.value());
        if (conv)
            scrollToMessageLater(conv, (redraw.mode == RedrawMode::Filling) ? MessageId() : redraw.earliest);
    }
}

static void redrawResponse(TdAccountData &account, std::shared_ptr<Redraw> redraw,
                           td::td_api::object_ptr<td::td_api::Object> response)
{
    MessageId more;
    bool     &found = redraw->found;
    size_t    added = 0;     // none, once the chat's first message is reached
    if (response && (response->get_id() == td::td_api::messages::ID)) {
        auto &messages = static_cast<td::td_api::messages &>(*response);
        for (auto &message: messages.messages_) {
            // The message a batch goes back from may come again, starting it
            if (!message || (!redraw->messages.empty() && (getId(*message) == getId(*redraw->messages.back()))))
                continue;
            more = getId(*message);
            bool read = (redraw->mode == RedrawMode::Opening) &&
                        (!redraw->unread || (more.value() <= redraw->readUpTo.value()));
            if (!found && ((more == redraw->earliest) || read))
                found = true;
            else if (found)
                redraw->beyond++;
            if ((redraw->mode == RedrawMode::Opening) && !read && !message->is_outgoing_)
                redraw->firstUnread = more;
            redraw->messages.push_back(std::move(message));
            added++;
            if ((found && (redraw->beyond >= redraw->extra)) || (redraw->messages.size() >= redraw->limit))
                break;
        }
        bool full = redraw->messages.size() >= redraw->limit;
        if (!added)
            redraw->start = true;
        // Filling it from a message, or from the first unread, and as many before as wanted, or as
        // there are; or as much as fits: either is enough
        bool enough = found && ((redraw->beyond >= redraw->extra) || !added || full);
        if (enough || ((redraw->mode == RedrawMode::Opening) && (full || !added))) {
            if (!redraw->messages.empty()) {
                // Said while they are written, which nothing is drawn during
                PurpleConversation *conv = findMessageConversation(account, redraw->chatId);
                if (conv)
                    // TRANSLATOR: At the end or the top of a conversation window while it is being filled, followed by a turning character. Argument is a number.
                    updateProgress(conv, escapeForDisplay(formatMessage(_("Showing {} messages"),
                                                                        redraw->messages.size())));
                std::weak_ptr<int> alive = account.lifetime;
                runAfterDrawing([&account, alive, redraw]() {
                    if (!alive.expired())
                        redrawShow(account, *redraw);
                });
                return;
            }
        } else if (!full && added) {
            PurpleConversation *conv = findMessageConversation(account, redraw->chatId);
            if (conv)
                updateProgress(conv, escapeForDisplay(formatMessage(
                    // TRANSLATOR: At the top of a conversation window while earlier messages are fetched, followed by a turning character. Argument is a number.
                    (redraw->mode == RedrawMode::Back) ? _("Fetching earlier messages from Telegram: {}")
                                 // TRANSLATOR: At the end of a conversation window while it is being filled, followed by a turning character. Argument is a number.
                                 : _("Fetching messages from Telegram: {}"),
                    redraw->messages.size())));
            redrawRequest(account, redraw, more);
            return;
        } else {
            purple_debug_misc(config::pluginId, "Message %" G_GINT64_FORMAT " is further back than the tab holds\n",
                              redraw->earliest.value());
            account.redrawTooFar.insert({redraw->chatId.value(), redraw->earliest.value()});
        }
    } else
        purple_debug_warning(config::pluginId, "Cannot fetch chat %" G_GINT64_FORMAT " to redraw it: %s\n",
                             redraw->chatId.value(), getDisplayedError(response).c_str());
    bool fetched = response && (response->get_id() == td::td_api::messages::ID);
    // Taken before the messages that came meanwhile are shown, which would not fill it
    const td::td_api::chat *chat  = (redraw->mode == RedrawMode::Linked) ? account.getChat(redraw->chatId) : nullptr;
    PurpleConversation     *conv  = chat ? findMessageConversation(account, *chat) : nullptr;
    bool                    empty = conv && !findFirstMessage(conv).valid();
    // Left as it is
    stopRedrawProgress(account, redraw->chatId);
    std::vector<IncomingMessage> readyMessages;
    account.pendingMessages.setChatReady(redraw->chatId, readyMessages);
    showMessages(readyMessages, account);
    showPendingNewest(account, redraw->chatId);
    // A tab ends at the latest message, and cannot reach so far back: one just opened for the link,
    // still empty, is filled as it would be opened anyway; one showing messages is left as it is
    if (redraw->mode == RedrawMode::Linked) {
        if (!fetched) {
            // Not known to be too far back: why it could not be fetched
            std::string reason = getDisplayedError(response);
            purple_notify_error(purple_account_get_connection(account.purpleAccount), config::pluginName,
                                _("Cannot follow the link"), reason.c_str());
        } else
            // TRANSLATOR: Information dialog, primary content, after a link to a message was followed.
            purple_notify_info(purple_account_get_connection(account.purpleAccount), config::pluginName,
                               _("The message is further back than the conversation holds"),
                               // TRANSLATOR: Information dialog, secondary content.
                               empty ? _("It shows the latest messages instead, from the first unread one.") : nullptr);
        if (empty)
            startRedraw(account, redraw->chatId, MessageId::invalid, RedrawMode::Opening, EARLIER_BATCH);
    }
}

static void redrawRequest(TdAccountData &account, std::shared_ptr<Redraw> redraw, MessageId from)
{
    auto request = td::td_api::make_object<td::td_api::getChatHistory>();
    request->chat_id_         = redraw->chatId.value();
    request->from_message_id_ = from.valid() ? from.value() : 0;
    request->offset_          = 0;
    request->limit_           = 100;
    request->only_local_      = false;
    account.transceiver.sendQuery(std::move(request),
        [&account, redraw](uint64_t, td::td_api::object_ptr<td::td_api::Object> response) {
            redrawResponse(account, redraw, std::move(response));
        });
}

// False if the chat is busy
static bool startRedraw(TdAccountData &account, ChatId chatId, MessageId earliest, RedrawMode mode,
                        unsigned extra)
{
    bool opening = (mode == RedrawMode::Opening);
    bool newest  = (mode == RedrawMode::Newest);
    bool back    = (mode == RedrawMode::Back);
    const td::td_api::chat *chat = account.getChat(chatId);
    PurpleConversation     *conv = findMessageConversation(account, chatId);
    if (!conv || !canRedrawConversation(conv))
        return true;
    // Not while history is being fetched, or the tab redrawn already
    if (!account.pendingMessages.isChatReady(chatId)) {
        purple_debug_misc(config::pluginId, "Chat %" G_GINT64_FORMAT " is busy, not redrawn\n", chatId.value());
        return false;
    }
    account.pendingMessages.setChatNotReady(chatId);
    // Where it is looked for: at the top, going back from there
    // TRANSLATOR: At the top of a conversation window while earlier messages are fetched, followed by a turning character.
    startProgress(conv, escapeForDisplay(back ? _("Fetching earlier messages from Telegram")
                                              // TRANSLATOR: At the end of a conversation window while it is being filled, followed by a turning character.
                                              : _("Fetching messages from Telegram")),
                  back);
    auto redraw = std::make_shared<Redraw>();
    redraw->chatId   = chatId;
    redraw->earliest = earliest;
    redraw->mode     = mode;
    redraw->unread   = std::max(chat->unread_count_, 0);
    redraw->readUpTo = getLastReadMessageId(*chat);
    redraw->extra    = extra;
    redraw->found    = newest;
    redraw->limit    = getRedrawLimit();
    // Down to where the tab ends, if it stops short of the newest and the message is before that
    MessageId end = getTabEnd(conv);
    if (!opening && !newest && end.valid() && (!earliest.valid() || (earliest.value() <= end.value())))
        redraw->latest = end;
    redrawRequest(account, redraw, redraw->latest);
    return true;
}

void redrawChat(TdAccountData &account, ChatId chatId, MessageId earliest)
{
    if (!account.redrawTooFar.count({chatId.value(), earliest.value()}))
        startRedraw(account, chatId, earliest, RedrawMode::Plain);
}

void showEarlierMessages(TdAccountData &account, PurpleConversation *conv, double filling)
{
    const td::td_api::chat *chat = findConversationChat(account, conv);
    if (!chat)
        return;
    MessageId earliest = findFirstMessage(conv);
    if (!earliest.valid() || account.historyComplete.count(getId(*chat).value()) ||
        account.redrawTooFar.count({getId(*chat).value(), earliest.value()}))
        return;
    purple_debug_misc(config::pluginId, "Showing messages before %" G_GINT64_FORMAT " in chat %" G_GINT64_FORMAT "\n",
                      earliest.value(), getId(*chat).value());
    // As many again as it has for each time it would go into the window, and one more
    unsigned extra = EARLIER_BATCH;
    if (filling) {
        auto   it    = account.redrawnCounts.find(getId(*chat).value());
        size_t shown = (it != account.redrawnCounts.end()) ? it->second : 0;
        extra        = unsigned(std::ceil(shown * std::max(filling - 1, 0.0))) + 1;
    }
    // Filling a tab that fits goes on at its end, which is what shows
    startRedraw(account, getId(*chat), earliest, filling ? RedrawMode::Filling : RedrawMode::Back, extra);
}

bool fillChat(TdAccountData &account, ChatId chatId)
{
    auto linked = account.linkedMessages.find(chatId.value());
    if (linked == account.linkedMessages.end())
        return startRedraw(account, chatId, MessageId::invalid, RedrawMode::Opening, EARLIER_BATCH);
    if (!startRedraw(account, chatId, linked->second, RedrawMode::Linked, EARLIER_BATCH))
        return false;
    account.linkedMessages.erase(linked);
    return true;
}

LinkedMessageShown showLinkedMessage(TdAccountData &account, const td::td_api::chat &chat, MessageId messageId)
{
    ChatId              chatId = getId(chat);
    PurpleConversation *conv   = findMessageConversation(account, chat);
    // A group chat's tab kept from an earlier connection is opened again, as if anew
    bool open = conv && ((purple_conversation_get_type(conv) != PURPLE_CONV_TYPE_CHAT) ||
                         !purple_conv_chat_has_left(purple_conversation_get_chat_data(conv)));
    if (!open) {
        // Filled as it is opened, from the message rather than the first unread
        if (messageId.valid())
            account.linkedMessages[chatId.value()] = messageId;
        conv = getMessageConversation(account, chat);
        if (!conv || !canRedrawConversation(conv))
            account.linkedMessages.erase(chatId.value());
        if (!conv)
            return LinkedMessageShown::NoConversation;
    } else if (messageId.valid()) {
        // Filled meanwhile, for a link before, the tab would be scrolled away from the message
        if (!account.pendingMessages.isChatReady(chatId))
            return LinkedMessageShown::Busy;
        if (findMessageMarks(conv, messageId).start)
            scrollToMessageLater(conv, messageId);
        else if (!startRedraw(account, chatId, messageId, RedrawMode::Linked, EARLIER_BATCH))
            return LinkedMessageShown::Busy;
    }
    presentConversation(conv);
    return LinkedMessageShown::Shown;
}

void showNewestMessages(TdAccountData &account, ChatId chatId)
{
    PurpleConversation *conv = findMessageConversation(account, chatId);
    // Busy, as with later messages being fetched: once it is not, or what is sent meanwhile would be
    // past the end it comes to
    if (conv && getTabEnd(conv).valid() &&
        !startRedraw(account, chatId, MessageId::invalid, RedrawMode::Newest, EARLIER_BATCH))
        account.newestPending.insert(chatId.value());
}

static void showPendingNewest(TdAccountData &account, ChatId chatId)
{
    if (account.newestPending.erase(chatId.value()))
        showNewestMessages(account, chatId);
}

using MessagesNewestFirst = std::vector<td::td_api::object_ptr<td::td_api::message>>;

// Written after what the tab shows, oldest first, ahead of any that came meanwhile, then as much
// taken from its top as keeps it short of Pidgin's limit; what was in view stays there
static void showFetchedLater(TdAccountData &account, ChatId chatId, MessageId end, time_t endDate, bool fetched,
                             MessagesNewestFirst later)
{
    const td::td_api::chat *chat = account.getChat(chatId);
    PurpleConversation     *conv = findMessageConversation(account, chatId);
    stopProgress(conv);
    // Unless filled anew meanwhile
    if (fetched && conv && (getTabEnd(conv) == end)) {
        // A chat not joined may have no latest message given: then it is reached once none follow
        MessageId lastId  = chat->last_message_ ? getId(*chat->last_message_) : MessageId();
        bool      reached = later.empty() || (lastId.valid() && (getId(*later.front()).value() >= lastId.value()));
        purple_debug_misc(config::pluginId, "Showing %zu messages after %" G_GINT64_FORMAT " in chat %"
                          G_GINT64_FORMAT "%s\n", later.size(), end.value(), chatId.value(),
                          reached ? ", up to the newest" : "");
        setTabEnd(conv, reached ? MessageId() : getId(*later.front()));
        keepViewLater(conv);
        std::vector<LoggedLine> lines = getLoggedLines(account, *chat, conv, later, !reached, end, endDate);
        auto line = lines.rbegin();
        auto queueLinesSince = [&](time_t since) {
            for (; (line != lines.rend()) && (line->when >= since); ++line)
                queueLoggedLine(account, chatId, *line);
        };
        for (auto &message: later)
            if (!account.pendingMessages.findPendingMessage(chatId, getId(*message))) {
                queueLinesSince(message->date_);
                handleIncomingMessage(account, *chat, std::move(message), PendingMessageQueue::Prepend, true);
            }
        queueLinesSince(0);
    }
    std::vector<IncomingMessage> readyMessages;
    account.pendingMessages.setChatReady(chatId, readyMessages);
    showMessages(readyMessages, account);
    showPendingNewest(account, chatId);
    unsigned windowLines = conv ? getWindowLines(conv) : 0;
    // Its first message taken away, it is to be gone back to again
    if (windowLines && trimTab(conv, windowLines, true, end))
        account.historyComplete.erase(chatId.value());
}

static void laterMessagesResponse(TdAccountData &account, ChatId chatId, MessageId end,
                                  td::td_api::object_ptr<td::td_api::Object> response)
{
    auto   later   = std::make_shared<MessagesNewestFirst>();
    bool   fetched = false;
    time_t endDate = 0;
    if (response && (response->get_id() == td::td_api::messages::ID)) {
        fetched = true;
        // The tab's last message comes too: the log is read past its time
        for (auto &message: static_cast<td::td_api::messages &>(*response).messages_)
            if (message && (getId(*message).value() > end.value()))
                later->push_back(std::move(message));
            else if (message && (getId(*message) == end))
                endDate = message->date_;
    } else
        purple_debug_warning(config::pluginId, "Cannot fetch messages after %" G_GINT64_FORMAT ": %s\n",
                             end.value(), getDisplayedError(response).c_str());
    // Said while they are written, which nothing is drawn during
    PurpleConversation *conv = findMessageConversation(account, chatId);
    if (conv && !later->empty())
        updateProgress(conv, escapeForDisplay(formatMessage(_("Showing {} messages"), later->size())));
    std::weak_ptr<int> alive = account.lifetime;
    runAfterDrawing([&account, alive, chatId, end, endDate, fetched, later]() {
        if (!alive.expired())
            showFetchedLater(account, chatId, end, endDate, fetched, std::move(*later));
    });
}

void showLaterMessages(TdAccountData &account, PurpleConversation *conv)
{
    const td::td_api::chat *chat = findConversationChat(account, conv);
    if (!chat)
        return;
    MessageId end = getTabEnd(conv);
    // Not while history is being fetched, or the tab filled already, nor ahead of messages that wait
    // to be shown still, as later ones would be; once they are, it is told again
    if (!end.valid() || !account.pendingMessages.isChatReady(getId(*chat)) ||
        account.pendingMessages.hasWaitingMessages(getId(*chat)))
        return;
    ChatId chatId = getId(*chat);
    account.pendingMessages.setChatNotReady(chatId);
    // TRANSLATOR: At the end of a conversation window while later messages are fetched, followed by a turning character.
    startProgress(conv, escapeForDisplay(_("Fetching later messages from Telegram")));
    // From the one after it: TDLib's newer ones are asked for by a negative offset, with that one too
    auto request = td::td_api::make_object<td::td_api::getChatHistory>();
    request->chat_id_         = chatId.value();
    request->from_message_id_ = end.value();
    request->offset_          = -LATER_BATCH;
    request->limit_           = LATER_BATCH + 1;
    request->only_local_      = false;
    account.transceiver.sendQuery(std::move(request),
        [&account, chatId, end](uint64_t, td::td_api::object_ptr<td::td_api::Object> response) {
            laterMessagesResponse(account, chatId, end, std::move(response));
        });
}

// While a message waits to be shown, its reactions would come before it, and are part of what it
// is once shown; but one waiting to be shown again, as edited, is in the conversation already
static bool isMessageInConversation(TdAccountData &account, ChatId chatId, MessageId messageId)
{
    const IncomingMessage *pendingMessage = account.pendingMessages.findPendingMessage(chatId, messageId);
    return !pendingMessage || pendingMessage->editedMessageTime;
}

void updateMessageReactions(TdAccountData &account, ChatId chatId, MessageId messageId,
                            const td::td_api::messageInteractionInfo *info)
{
    // Telling of reactions to a message nobody has seen would only puzzle
    ShownMessage *shownMessage = account.findShownMessage(chatId, messageId);
    if (!shownMessage)
        return;
    const td::td_api::chat *chat = account.getChat(chatId);
    bool show = chat && isMessageInConversation(account, chatId, messageId);

    std::vector<GivenReaction> reactions;
    std::set<std::string>      incompleteTypes;
    std::set<std::string>      present;
    std::set<std::string>      changedBy;
    getGivenReactions(info, reactions, &incompleteTypes);
    shownMessage->givers = getReactionGivers(info);
    for (const GivenReaction &reaction: reactions) {
        present.insert(reaction.key);
        if (shownMessage->reactions.emplace(reaction.key, reaction.self).second) {
            changedBy.insert(getReactionKeySender(reaction.key));
            if (show)
                showReaction(account, *chat, messageId, *shownMessage, reaction.key, reaction.self, reaction.type);
        }
    }

    // Gone, as far as can be told: where everyone who gave that reaction is named, and they are not
    // among them any more. Exchanging one reaction for another is not taking it back.
    for (auto it = shownMessage->reactions.begin(); it != shownMessage->reactions.end(); ) {
        if (present.count(it->first) || incompleteTypes.count(getReactionKeyType(it->first))) {
            ++it;
            continue;
        }
        if (show && !changedBy.count(getReactionKeySender(it->first)))
            showReaction(account, *chat, messageId, *shownMessage, it->first, it->second, nullptr);
        it = shownMessage->reactions.erase(it);
    }
    if (show) {
        PurpleConversation *conv = pidginUiFound() ? findMessageConversation(account, *chat) : nullptr;
        if (conv)
            showMessageReactions(conv, messageId, formatReactions(account, conv, getReactionCounts(info)));
    }
}

td::td_api::object_ptr<td::td_api::ReactionType> makeReactionType(const std::string &typeKey)
{
    if ((typeKey.size() > 1) && (typeKey[0] == 'e'))
        return td::td_api::make_object<td::td_api::reactionTypeEmoji>(typeKey.substr(1));
    if ((typeKey.size() > 1) && (typeKey[0] == 'x'))
        return td::td_api::make_object<td::td_api::reactionTypeCustomEmoji>(g_ascii_strtoll(typeKey.c_str() + 1,
                                                                                            nullptr, 10));
    return td::td_api::make_object<td::td_api::reactionTypePaid>();
}

// All who gave the reaction, where Telegram names them, as many as one request returns
static void fetchReactionGivers(TdAccountData &account, ChatId chatId, MessageId messageId, const std::string &typeKey)
{
    enum { MAX_GIVERS = 100 };
    auto request = td::td_api::make_object<td::td_api::getMessageAddedReactions>(
        chatId.value(), messageId.value(), makeReactionType(typeKey), "", MAX_GIVERS);
    account.transceiver.sendQuery(std::move(request),
        [&account, chatId, messageId, typeKey](uint64_t, td::td_api::object_ptr<td::td_api::Object> response) {
            ShownMessage *shownMessage = account.findShownMessage(chatId, messageId);
            if (!shownMessage)
                return;
            // Changed meanwhile, the request made for what was before
            auto it = shownMessage->givers.find(typeKey);
            if ((it == shownMessage->givers.end()) || !it->second.fetching)
                return;
            if (!response || (response->get_id() != td::td_api::addedReactions::ID)) {
                purple_debug_misc(config::pluginId, "Cannot list who gave reaction %s to message %" G_GINT64_FORMAT
                                  ": %s\n", typeKey.c_str(), messageId.value(), getDisplayedError(response).c_str());
                return;
            }
            std::vector<std::string> senders;
            for (const auto &reaction: static_cast<const td::td_api::addedReactions &>(*response).reactions_)
                if (reaction && reaction->sender_id_ &&
                    (std::find(senders.begin(), senders.end(), getSenderKey(*reaction->sender_id_)) == senders.end()))
                    senders.push_back(getSenderKey(*reaction->sender_id_));
            if (senders.size() > it->second.senders.size())
                it->second.senders = std::move(senders);
            PurpleConversation *conv = findMessageConversation(account, chatId);
            if (conv)
                refreshReactionTip(conv);
        });
}

bool getReactionTip(TdAccountData &account, PurpleConversation *conv, MessageId messageId, const std::string &typeKey,
                    std::string &tip)
{
    const td::td_api::chat *chat         = findConversationChat(account, conv);
    ShownMessage           *shownMessage = chat ? account.findShownMessage(getId(*chat), messageId) : nullptr;
    if (!shownMessage)
        return false;
    auto it = shownMessage->givers.find(typeKey);
    if (it == shownMessage->givers.end())
        return false;
    ShownMessage::Givers &givers = it->second;
    // Channels name nobody, and the count is shown already
    if (givers.senders.empty())
        return false;
    if ((givers.total > int32_t(givers.senders.size())) && !givers.fetching) {
        givers.fetching = true;
        fetchReactionGivers(account, getId(*chat), messageId, typeKey);
    }
    tip.clear();
    for (const std::string &sender: givers.senders) {
        std::string name = getReactionSenderName(account, sender);
        if (!name.empty())
            tip += (tip.empty() ? "" : "\n") + name;
    }
    int32_t more = givers.total - int32_t(givers.senders.size());
    if ((more > 0) && !tip.empty())
        // TRANSLATOR: Under the names of those who gave a reaction, in its tooltip, when not all are known. Argument is a number.
        tip += "\n" + formatMessage(_("and {} more"), more);
    return !tip.empty();
}

using UnreadReactions = std::vector<td::td_api::object_ptr<td::td_api::unreadReaction>>;

static void showUnreadReactions(TdAccountData &account, ChatId chatId, MessageId messageId,
                                const UnreadReactions &reactions)
{
    ShownMessage *shownMessage = account.findShownMessage(chatId, messageId);
    const td::td_api::chat *chat = account.getChat(chatId);
    if (!chat)
        return;
    if (!shownMessage) {
        // Shown, reactions and all, by filling its tab anew from it, if it is open
        if (!reactions.empty())
            redrawChat(account, chatId, messageId);
        return;
    }
    bool show = isMessageInConversation(account, chatId, messageId);

    // Unread ones are to our own messages, from others
    for (const auto &reaction: reactions)
        if (reaction && reaction->type_ && reaction->sender_id_) {
            std::string key = getReactionKey(getSenderKey(*reaction->sender_id_), *reaction->type_);
            if (shownMessage->reactions.emplace(key, false).second && show)
                showReaction(account, *chat, messageId, *shownMessage, key, false, reaction->type_.get());
        }
}

static void unreadReactionsMessageResponse(TdAccountData &account, ChatId chatId, MessageId messageId,
                                           const UnreadReactions &reactions,
                                           td::td_api::object_ptr<td::td_api::Object> object)
{
    if (!object || (object->get_id() != td::td_api::message::ID)) {
        purple_debug_warning(config::pluginId, "Failed to fetch message %" G_GINT64_FORMAT " reacted to\n",
                             messageId.value());
        return;
    }

    // Unless shown meanwhile, the message now is, as far as its reactions are concerned
    if (!account.findShownMessage(chatId, messageId)) {
        const td::td_api::message &message = static_cast<const td::td_api::message &>(*object);
        if (!message.content_)
            return;
        ShownMessage shownMessage;
        getShownContent(*message.content_, shownMessage);
        shownMessage.timestamp = message.date_;
        shownMessage.outgoing  = message.is_outgoing_;

        // Reactions other than the unread ones are no news
        std::set<std::string> unreadKeys;
        for (const auto &reaction: reactions)
            if (reaction && reaction->type_ && reaction->sender_id_)
                unreadKeys.insert(getReactionKey(getSenderKey(*reaction->sender_id_), *reaction->type_));
        std::vector<GivenReaction> given;
        getGivenReactions(message.interaction_info_.get(), given, nullptr);
        for (const GivenReaction &reaction: given)
            if (!unreadKeys.count(reaction.key))
                shownMessage.reactions.emplace(reaction.key, reaction.self);
        account.addShownMessage(chatId, messageId, std::move(shownMessage));
    }

    showUnreadReactions(account, chatId, messageId, reactions);
}

void updateUnreadReactions(TdAccountData &account, td::td_api::updateMessageUnreadReactions &update)
{
    ChatId    chatId    = getChatId(update);
    MessageId messageId = getMessageId(update);
    // Empty once they have been read elsewhere
    if (update.unread_reactions_.empty())
        return;

    if (account.findShownMessage(chatId, messageId)) {
        showUnreadReactions(account, chatId, messageId, update.unread_reactions_);
        return;
    }

    // One of our messages from before this session: fetch it, to say which
    purple_debug_misc(config::pluginId, "Fetching message %" G_GINT64_FORMAT " reacted to\n", messageId.value());
    auto reactions = std::make_shared<UnreadReactions>(std::move(update.unread_reactions_));
    auto request = makeGetMessage(chatId, messageId);
    account.transceiver.sendQuery(std::move(request),
        [&account, chatId, messageId, reactions](uint64_t, td::td_api::object_ptr<td::td_api::Object> object) {
            unreadReactionsMessageResponse(account, chatId, messageId, *reactions, std::move(object));
        });
}

const char UNREAD_REACTIONS_MARK[] = "\xe2\x9d\xa4\xef\xb8\x8f ";

static bool hasUnreadReactions(TdAccountData &account, ChatId chatId)
{
    auto it = account.unreadReactionCounts.find(chatId);
    return (it != account.unreadReactionCounts.end()) && (it->second > 0);
}

bool hasUnreadReactions(TdAccountData &account, PurpleConversation *conv)
{
    const td::td_api::chat *chat = conv ? findConversationChat(account, conv) : nullptr;
    return chat && hasUnreadReactions(account, getId(*chat));
}

void markConversationTitle(TdAccountData &account, PurpleConversation *conv)
{
    const char *title  = purple_conversation_get_title(conv);
    bool        marked = title && g_str_has_prefix(title, UNREAD_REACTIONS_MARK);
    // Setting it says the title changed, which comes back here, marked as it is to be by then
    if (hasUnreadReactions(account, conv) && !marked) {
        std::string newTitle = UNREAD_REACTIONS_MARK + std::string(title ? title : purple_conversation_get_name(conv));
        purple_conversation_set_title(conv, newTitle.c_str());
    } else if (!hasUnreadReactions(account, conv) && marked)
        purple_conversation_set_title(conv, title + strlen(UNREAD_REACTIONS_MARK));
}

void readChatInbox(TdAccountData &account, ChatId chatId, MessageId lastRead, int32_t unreadCount)
{
    account.updateChatReadInbox(chatId, lastRead, unreadCount);
    if (unreadCount != 0)
        return;
    PurpleConversation *conv = findMessageConversation(account, chatId);
    if (conv && clearUnseen(conv))
        purple_debug_misc(config::pluginId, "Chat %" G_GINT64_FORMAT " read elsewhere\n", chatId.value());
}

void setUnreadReactionCount(TdAccountData &account, ChatId chatId, int32_t count)
{
    bool wasUnread = hasUnreadReactions(account, chatId);
    account.unreadReactionCounts[chatId] = count;
    if (wasUnread == (count > 0))
        return;
    purple_debug_misc(config::pluginId, "Chat %" G_GINT64_FORMAT ": %d reactions unread\n", chatId.value(), int(count));
    const td::td_api::chat *chat = account.getChat(chatId);
    PurpleConversation     *conv = findMessageConversation(account, chatId);
    if (conv)
        markConversationTitle(account, conv);
    // The buddy list draws its contact anew, asking for the mark
    const td::td_api::user *user  = chat ? account.getUserByPrivateChat(*chat) : nullptr;
    PurpleBuddy            *buddy = user ? purple_find_buddy(account.purpleAccount, getPurpleBuddyName(*user).c_str())
                                         : nullptr;
    PurpleBlistUiOps       *ops   = purple_blist_get_ui_ops();
    if (buddy && ops && ops->update)
        ops->update(purple_get_blist(), PURPLE_BLIST_NODE(buddy));
}
