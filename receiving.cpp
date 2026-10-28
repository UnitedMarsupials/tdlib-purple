#include "receiving.h"
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
#include <algorithm>
#include <tuple>
#include <fmt/format.h>

enum {
    HISTORY_MESSAGES_ABSOLUTE_LIMIT = 80
};

// The name is chosen by whoever sent the file and routinely contains spaces and other
// characters a URI cannot carry literally, so the path has to be percent-encoded before
// it is handed to a viewer.
std::string makeFileUri(const std::string &filePath)
{
    gchar *uri = g_filename_to_uri(filePath.c_str(), NULL, NULL);
    if (uri == NULL)
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
    time_t    now = time(NULL);
    struct tm today, sent;
    localtime_r(&now, &today);
    localtime_r(&timestamp, &sent);

    if ((sent.tm_year == today.tm_year) && (sent.tm_yday == today.tm_yday))
        return purple_time_format(&sent);
    else
        return purple_date_format_long(&sent);
}

static std::string getFileKey(const td::td_api::file *file)
{
    if (!file)
        return "";
    // The same file keeps its remote identifier, while its local one may differ between sessions
    if (file->remote_ && !file->remote_->unique_id_.empty())
        return file->remote_->unique_id_;
    return std::to_string(file->id_);
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
                              {formatMessageTime(time(NULL) + shown.expiresIn), link});
    return text;
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
    switch (content.get_id()) {
        case td::td_api::messageText::ID: {
            const auto &text = static_cast<const td::td_api::messageText &>(content);
            if (text.text_)
                result.content = text.text_->text_;
            result.quotable = true;
            break;
        }
        case td::td_api::messagePhoto::ID: {
            const auto &photo = static_cast<const td::td_api::messagePhoto &>(content);
            result.kind = _("photo");
            if (photo.photo_)
                for (const auto &size: photo.photo_->sizes_)
                    if (size && size->photo_) {
                        result.file = getFileKey(size->photo_.get());
                        break;
                    }
            caption = photo.caption_.get();
            break;
        }
        case td::td_api::messageDocument::ID: {
            const auto &document = static_cast<const td::td_api::messageDocument &>(content);
            // TRANSLATOR: What a message is, as in "reacted with 👍 to document from 12:00".
            result.kind = _("document");
            if (document.document_)
                result.file = getFileKey(document.document_->document_.get());
            caption = document.caption_.get();
            break;
        }
        case td::td_api::messageVideo::ID: {
            const auto &video = static_cast<const td::td_api::messageVideo &>(content);
            // TRANSLATOR: What a message is, as in "reacted with 👍 to video from 12:00".
            result.kind = _("video");
            if (video.video_)
                result.file = getFileKey(video.video_->video_.get());
            caption = video.caption_.get();
            break;
        }
        case td::td_api::messageAnimation::ID: {
            const auto &animation = static_cast<const td::td_api::messageAnimation &>(content);
            // TRANSLATOR: What a message is, as in "reacted with 👍 to animation from 12:00".
            result.kind = _("animation");
            if (animation.animation_)
                result.file = getFileKey(animation.animation_->animation_.get());
            caption = animation.caption_.get();
            break;
        }
        case td::td_api::messageAudio::ID: {
            const auto &audio = static_cast<const td::td_api::messageAudio &>(content);
            // TRANSLATOR: What a message is, as in "reacted with 👍 to audio from 12:00".
            result.kind = _("audio");
            if (audio.audio_)
                result.file = getFileKey(audio.audio_->audio_.get());
            caption = audio.caption_.get();
            break;
        }
        case td::td_api::messageVoiceNote::ID: {
            const auto &voiceNote = static_cast<const td::td_api::messageVoiceNote &>(content);
            // TRANSLATOR: What a message is, as in "reacted with 👍 to voice message from 12:00".
            result.kind = _("voice message");
            if (voiceNote.voice_note_)
                result.file = getFileKey(voiceNote.voice_note_->voice_.get());
            caption = voiceNote.caption_.get();
            break;
        }
        case td::td_api::messageVideoNote::ID: {
            const auto &videoNote = static_cast<const td::td_api::messageVideoNote &>(content);
            // TRANSLATOR: What a message is, as in "reacted with 👍 to video message from 12:00".
            result.kind = _("video message");
            if (videoNote.video_note_)
                result.file = getFileKey(videoNote.video_note_->video_.get());
            break;
        }
        case td::td_api::messageSticker::ID: {
            const auto &sticker = static_cast<const td::td_api::messageSticker &>(content);
            result.kind = _("sticker");
            if (sticker.sticker_)
                result.file = getFileKey(sticker.sticker_->sticker_.get());
            break;
        }
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
        result.content  = caption->text_;
        result.quotable = true;
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

std::string getMessageText(const td::td_api::formattedText &text,
                           const std::map<int64_t, std::string> *smileys)
{
    // The stretches of text that custom emoji cover, and what goes there instead
    std::vector<std::tuple<size_t, size_t, std::string>> replaced;
    if (smileys)
        for (const auto &entity: text.entities_)
            if (entity && entity->type_ && (entity->type_->get_id() == td::td_api::textEntityTypeCustomEmoji::ID)) {
                auto it = smileys->find(static_cast<const td::td_api::textEntityTypeCustomEmoji &>(*entity->type_).custom_emoji_id_);
                if (it != smileys->end())
                    replaced.emplace_back(utf16OffsetToBytes(text.text_, entity->offset_),
                                          utf16OffsetToBytes(text.text_, entity->offset_ + entity->length_),
                                          it->second);
            }
    std::sort(replaced.begin(), replaced.end());

    std::string result;
    size_t      position = 0;
    for (const auto &item: replaced) {
        if (std::get<0>(item) < position)
            continue;
        result  += escapeForDisplay(text.text_.substr(position, std::get<0>(item) - position));
        result  += escapeForDisplay(std::get<2>(item));
        position = std::get<1>(item);
    }
    result += escapeForDisplay(text.text_.substr(position));
    return result;
}

unsigned getMessageWaitSeconds(const TgMessageInfo &message)
{
    enum { MESSAGE_WAIT_SECONDS = 1, HISTORY_WAIT_SECONDS = 5 };
    return message.fromHistory ? HISTORY_WAIT_SECONDS : MESSAGE_WAIT_SECONDS;
}

std::string makeInlineImageText(int imgstoreId)
{
    return "\n<img id=\"" + std::to_string(imgstoreId) + "\">";
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
        else if (secretChatId.valid())
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
    PurpleConversation *conv = NULL;

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
        if (conv == NULL)
            conv = getImConversation(account.purpleAccount, purpleUserName);
        purple_conv_im_write(purple_conversation_get_im_data(conv), purpleUserName, notification,
                             getNotificationFlags(flags), timestamp);
    }

    // TODO: sending all pending read receipts for the chat is technically not quite right,
    // because maybe a message is being shown while others are waiting for some asynchronous
    // response before they can be displayed. But who cares.
    if (conv != NULL)
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
                                 message.incomingGroupchatSender.empty() ? "someone" : message.incomingGroupchatSender.c_str(),
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
    PurpleConversation *baseConv = conv ? purple_conv_chat_get_conversation(conv) : NULL;
    if (baseConv != NULL)
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
            if (messageText.text_)
                text = getMessageText(*messageText.text_);
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
static std::string getPrivateChatPurpleName(TdAccountData &account, const td::td_api::user &user)
{
    std::string userName = getPurpleBuddyName(user);

    // If there is no buddy in the buddy list, libpurple won't be able to translate buddy name
    // to alias, so use display name instead of idXXXXXXXXX
    if (!purple_find_buddy(account.purpleAccount, userName.c_str()))
        userName = account.getDisplayName(user);
    return userName;
}

// The chat's conversation, if it is open; none is opened
PurpleConversation *findMessageConversation(TdAccountData &account, const td::td_api::chat &chat)
{
    const td::td_api::user *privateUser = account.getUserByPrivateChat(chat);
    if (privateUser)
        return purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM,
                                                     getPrivateChatPurpleName(account, *privateUser).c_str(),
                                                     account.purpleAccount);
    SecretChatId secretChatId = getSecretChatId(chat);
    if (secretChatId.valid())
        return purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, getSecretChatBuddyName(secretChatId).c_str(),
                                                     account.purpleAccount);
    PurpleConvChat *conv = findChatConversation(account.purpleAccount, chat);
    return conv ? purple_conv_chat_get_conversation(conv) : NULL;
}

PurpleConversation *getMessageConversation(TdAccountData &account, const td::td_api::chat &chat)
{
    const td::td_api::user *privateUser = account.getUserByPrivateChat(chat);
    if (privateUser)
        return getImConversation(account.purpleAccount, getPrivateChatPurpleName(account, *privateUser).c_str());

    SecretChatId secretChatId = getSecretChatId(chat);
    if (secretChatId.valid())
        return getImConversation(account.purpleAccount, getSecretChatBuddyName(secretChatId).c_str());

    if (getBasicGroupId(chat).valid() || getSupergroupId(chat).valid()) {
        PurpleConvChat *conv = getChatConversation(account, chat, account.getPurpleChatId(getId(chat)));
        return conv ? purple_conv_chat_get_conversation(conv) : NULL;
    }
    return NULL;
}

// The menu's link before the message; not while it is being sent, under an id it is yet to change
static std::string getMessagePrefix(TdAccountData &account, const td::td_api::chat &chat,
                                     const TgMessageInfo &message)
{
    return message.sentLocally ? std::string() : makeMenuLink(account.purpleAccount, getId(chat), message.id);
}

void showMessageText(TdAccountData &account, const td::td_api::chat &chat, const TgMessageInfo &message,
                     const char *text, const char *notification, uint32_t extraFlags)
{
    PurpleMessageFlags directionFlag = message.outgoing ? PURPLE_MESSAGE_SEND : PURPLE_MESSAGE_RECV;
    PurpleMessageFlags flags = (PurpleMessageFlags) (extraFlags | directionFlag);
    // Not logged (again): with the conversation's logging off while it is written, as Pidgin shows
    // a message flagged as not to be logged without its time and sender
    PurpleConversation *unloggedConv = (message.unlogged || message.redrawn) ? findMessageConversation(account, chat)
                                                                             : nullptr;
    bool                logging      = unloggedConv && purple_conversation_is_logging(unloggedConv);
    if (logging)
        purple_conversation_set_logging(unloggedConv, FALSE);
    else if ((message.unlogged || message.redrawn) && !unloggedConv)
        flags = (PurpleMessageFlags) (flags | PURPLE_MESSAGE_NO_LOG);
    if (message.outgoing && !message.sentLocally)
        flags = (PurpleMessageFlags) (flags | PURPLE_MESSAGE_REMOTE_SEND);

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
        if (!newText.empty())
            newText += "\n";
        newText += text;
    }
    if (!newText.empty())
        text = newText.c_str();

    // Marks for the message's own text, not for a notice about it, which carries its id
    MessageMarkScope markScope(text ? message.id : MessageId::invalid, getMessagePrefix(account, chat, message));
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

    if (getBasicGroupId(chat).valid() || getSupergroupId(chat).valid())
        showMessageTextChat(account, chat, message, text, notification, flags);

    if (logging && g_list_find(purple_get_conversations(), unloggedConv))
        purple_conversation_set_logging(unloggedConv, TRUE);
}

void showChatNotification(TdAccountData &account, const td::td_api::chat &chat,
                          const char *notification, time_t timestamp, PurpleMessageFlags extraFlags)
{
    TgMessageInfo messageInfo;
    messageInfo.type = TgMessageInfo::Type::Other;
    messageInfo.timestamp = timestamp;
    messageInfo.outgoing = true;
    showMessageText(account, chat, messageInfo, NULL, notification, extraFlags);
}

void showChatNotification(TdAccountData &account, const td::td_api::chat &chat,
                          const char *notification, PurpleMessageFlags extraFlags)
{
    showChatNotification(account, chat, notification,
                         (extraFlags & PURPLE_MESSAGE_NO_LOG) ? 0 : time(NULL), extraFlags);
}

static void showDownloadedImage(const td::td_api::chat &chat, TgMessageInfo &message,
                                const std::string &filePath, const char *caption,
                                TdAccountData &account)
{
    std::string  text;
    std::string  notice;
    gchar       *data   = NULL;
    size_t       len    = 0;

    // A front-end that shows no pictures gets the link to the file
    if (!canShowImages()) {
        // TRANSLATOR: File-type, used to describe what is being downloaded, in sentences like "Downloading photo" or "Ignoring photo download".
        showGenericFileInline(chat, message, filePath, caption, _("photo"), std::string(), account);
        return;
    }

    if (g_file_get_contents (filePath.c_str(), &data, &len, NULL)) {
        int id = purple_imgstore_add_with_id (data, len, NULL);
        text = makeInlineImageText(id);
    } else
        text = "<img src=\"" + escapeForDisplay(makeFileUri(filePath)) + "\">";
    if (!message.mediaVariantLinks.empty())
        text += "\n" + message.mediaVariantLinks;

    if (caption && *caption) {
        if (!text.empty())
            text += "\n";
        text += caption;
    }

    showMessageText(account, chat, message, text.empty() ? NULL : text.c_str(),
                    notice.empty() ? NULL : notice.c_str(), PURPLE_MESSAGE_IMAGES);
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
                                  TdTransceiver &transceiver, TdAccountData &account)
{
    if (isStickerAnimated(filePath)) {
        if (shouldConvertAnimatedSticker(message, account.purpleAccount)) {
            // TRANSLATOR: In-chat status update
            std::string notice = makeNoticeWithSender(chat, message, _("Converting sticker"),
                                                      account.purpleAccount);
            showMessageText(account, chat, message, NULL, notice.c_str());
            StickerConversionThread *thread;
            thread = new StickerConversionThread(account.purpleAccount, filePath, getId(chat),
                                                 std::move(message));
            thread->startThread();
        } else if (thumbnail) {
            // Avoid message like "Downloading sticker thumbnail...
            // Also ignore size limits, but only determined testers and crazy people would notice.
            if (thumbnail->local_ && thumbnail->local_->is_downloading_completed_)
                showDownloadedSticker(chat, message, thumbnail->local_->path_,
                                      fileDescription, nullptr, transceiver, account);
            else
                downloadFileInline(thumbnail->id_, getId(chat), message, fileDescription, nullptr,
                                   transceiver, account);
        } else {
            showGenericFileInline(chat, message, filePath, NULL, fileDescription, std::string(), account);
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
    showMessageText(account, chat, message, text.c_str(), NULL,
                    preview.empty() ? 0 : PURPLE_MESSAGE_IMAGES);
}

void showDownloadedFileInline(ChatId chatId, TgMessageInfo &message,
                              const std::string &filePath, const char *caption,
                              const std::string &fileDescription,
                              td::td_api::object_ptr<td::td_api::file> thumbnail,
                              const std::string &preview,
                              TdTransceiver &transceiver, TdAccountData &account)
{
    const td::td_api::chat *chat = account.getChat(chatId);
    if (!chat) return;

    switch (message.type) {
    case TgMessageInfo::Type::Photo:
        showDownloadedImage(*chat, message, filePath, caption, account);
        break;
    case TgMessageInfo::Type::Sticker:
        showDownloadedSticker(*chat, message, filePath, fileDescription, std::move(thumbnail),
                              transceiver, account);
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
        showMessageText(account, chat, message, displayText.c_str(), NULL);
    }
}

struct InlineDownloadInfo {
    int32_t         fileId;
    ChatId          chatId;
    TgMessageInfo   message;
    std::string     fileDescription;
    TdTransceiver  *transceiver;
    TdAccountData  *account;
};

static void startInlineDownload(void *user_data)
{
    std::unique_ptr<InlineDownloadInfo> info(static_cast<InlineDownloadInfo *>(user_data));
    downloadFileInline(info->fileId, info->chatId, info->message, info->fileDescription,
                       nullptr, *info->transceiver, *info->account);
}

static void ignoreInlineDownload(InlineDownloadInfo *info)
{
    delete info;
}

static void requestInlineDownload(const char *sender, const td::td_api::file &file,
                                  const std::string &fileDesc, const td::td_api::chat &chat,
                                  TgMessageInfo &message, TdTransceiver &transceiver, TdAccountData &account)
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
    info->transceiver = &transceiver;
    info->account = &account;

    // TRANSLATOR: Download dialog, title
    purple_request_action(purple_account_get_connection(account.purpleAccount), _("Download"), question.c_str(),
                          fileInfo.c_str(), 0, account.purpleAccount, NULL, NULL,
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

// The thumbnail fetched for a video, as markup for the conversation window; empty if there is none
static std::string makePreviewImageText(const IncomingMessage &fullMessage)
{
    gchar *data = NULL;
    gsize  len  = 0;

    if (fullMessage.previewPath.empty() ||
        !g_file_get_contents(fullMessage.previewPath.c_str(), &data, &len, NULL))
        return "";

    int id = purple_imgstore_add_with_id(data, len, NULL);
    return "<img id=\"" + std::to_string(id) + "\">";
}

static void showFileInline(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                           const td::td_api::file &file, const char *caption,
                           const std::string &fileDesc,
                           TdTransceiver &transceiver, TdAccountData &account)
{
    std::string notice;
    bool        askDownload  = false;
    bool        autoDownload = false;
    unsigned    fileSize     = getFileSizeKb(file);

    if (caption && (*caption == '\0'))
        caption = NULL;

    // A video's thumbnail goes inside the hyperlink where there is one (see showGenericFileInline);
    // where there is only a notice, it goes above the caption. Either way it precedes the caption.
    std::string preview = makePreviewImageText(fullMessage);

    // Fetched and opened only when clicked, if it is not here by then, so it is no reason to wait
    if (linksToMessage(fullMessage)) {
        showFileLink(chat, fullMessage.messageInfo,
                     makeMessageLink(account.purpleAccount, getId(chat), fullMessage.messageInfo.id),
                     caption, fileDesc, preview, account);
        // Fetched beforehand, within the limit, so that a click opens it at once; of a video in
        // several sizes, the one the account asks for
        const td::td_api::file *prefetch = &file;
        if (fullMessage.message && fullMessage.message->content_ &&
            (fullMessage.message->content_->get_id() == td::td_api::messageVideo::ID))
        {
            std::vector<MediaVariant> variants;
            getMediaVariants(*fullMessage.message->content_, variants);
            prefetch = choosePrefetchVariant(account.purpleAccount, variants, fullMessage.inlineFileSizeLimit);
        } else if (!isSizeWithinLimit(fileSize, fullMessage.inlineFileSizeLimit))
            prefetch = nullptr;
        if (prefetch && !(prefetch->local_ && prefetch->local_->is_downloading_completed_)) {
            purple_debug_misc(config::pluginId, "Fetching %s (file id %d) in the background\n",
                              fileDesc.c_str(), (int)prefetch->id_);
            auto request = td::td_api::make_object<td::td_api::downloadFile>();
            request->file_id_     = prefetch->id_;
            request->priority_    = BACKGROUND_DOWNLOAD_PRIORITY;
            request->offset_      = 0;
            request->limit_       = 0;
            request->synchronous_ = false;
            transceiver.sendQuery(std::move(request), nullptr);
        }
        return;
    }

    if (file.local_ && file.local_->is_downloading_completed_) {
        autoDownload = true;
        notice.clear();
    } else if (isSizeWithinLimit(fileSize, fullMessage.inlineFileSizeLimit)) {
        // Sticker with a caption is not a thing but just in case it was: don't skip "Downloading..."
        // notification for such hypothetical object because it will also include caption
        if ( !((fullMessage.messageInfo.type == TgMessageInfo::Type::Sticker) && !caption) &&
             !fullMessage.inlineDownloadComplete )
        {
            // TRANSLATOR: In-chat notification, appears after a colon (':'). Argument is a file *type*, not a filename.
            notice = formatMessage(_("Downloading {}"), escapeForDisplay(fileDesc));
        }
        autoDownload = true;
    } else if (!ignoreBigDownloads(account.purpleAccount) && !fullMessage.messageInfo.redrawn) {
        // TRANSLATOR: In-chat notification, appears after a colon (':'). Argument is a file *type*, not a filename.
        notice = formatMessage(_("Requesting {} download"), escapeForDisplay(fileDesc));
        askDownload = true;
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

    if (autoDownload || askDownload) {
        if (fullMessage.animatedStickerConverted) {
            if (fullMessage.animatedStickerConvertSuccess) {
                std::string text = makeInlineImageText(fullMessage.animatedStickerImageId);
                showMessageText(account, chat, fullMessage.messageInfo, text.c_str(), NULL, PURPLE_MESSAGE_IMAGES);
            }
        } else if (file.local_ && file.local_->is_downloading_completed_)
            showDownloadedFileInline(getId(chat), fullMessage.messageInfo, file.local_->path_,
                                     caption, fileDesc, std::move(fullMessage.thumbnail), preview,
                                     transceiver, account);
        else if (autoDownload && fullMessage.inlineDownloadComplete)
            showDownloadedFileInline(getId(chat), fullMessage.messageInfo, fullMessage.inlineDownloadedFilePath,
                                     caption, fileDesc, std::move(fullMessage.thumbnail), preview,
                                     transceiver, account);
        else if (autoDownload) {
            // When download takes too long, message will leave PendingMessageQueue and be "shown".
            // However, nothing more should be done at that point except keep waiting for the download.
            if (!fullMessage.inlineDownloadTimeout) {
                purple_debug_misc(config::pluginId, "Downloading %s (file id %d)\n", fileDesc.c_str(),
                                (int)file.id_);
                downloadFileInline(file.id_, getId(chat), fullMessage.messageInfo, fileDesc,
                                std::move(fullMessage.thumbnail), transceiver, account);
            }
        } else if (askDownload) {
            std::string sender = getSenderDisplayName(chat, fullMessage.messageInfo,
                                                      account.purpleAccount);
            requestInlineDownload(sender.c_str(), file, fileDesc, chat, fullMessage.messageInfo,
                                  transceiver, account);
        }
    }

}

static void showPhotoMessage(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                             const td::td_api::file *photoSize, const std::string &caption,
                             TdTransceiver &transceiver, TdAccountData &account)
{
    const char *captionCstr = !caption.empty() ? caption.c_str() : nullptr;

    if (photoSize) {
        // TRANSLATOR: File-type, used to describe what is being downloaded, in sentences like "Downloading photo" or "Ignoring photo download".
        showFileInline(chat, fullMessage, *photoSize, captionCstr, _("photo"),
                       transceiver, account);
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
                            TdTransceiver &transceiver, TdAccountData &account)
{
    const char *captionStr = !caption.empty() ? caption.c_str() : NULL;
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
                           transceiver, account);
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
                showMessageText(account, chat, fullMessage.messageInfo, text.c_str(), NULL,
                                PURPLE_MESSAGE_IMAGES);
            } else if (captionStr)
                showMessageText(account, chat, fullMessage.messageInfo, captionStr, NULL);
            requestStandardDownload(getId(chat), fullMessage.messageInfo, fileName, *file,
                                    transceiver, account);
        }
    }
}

static void showStickerMessage(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                               td::td_api::messageSticker &stickerContent,
                               TdTransceiver &transceiver, TdAccountData &account)
{
    if (!stickerContent.sticker_) return;
    td::td_api::sticker &sticker = *stickerContent.sticker_;

    if (sticker.sticker_)
        // TRANSLATOR: File-type, used to describe what is being downloaded, in sentences like "Downloading photo" or "Ignoring photo download".
        showFileInline(chat, fullMessage, *sticker.sticker_, NULL, _("sticker"),
                       transceiver, account);
}

static void showPendingEdit(TdAccountData &account, ChatId chatId, MessageId messageId);

static void showUnsupportedMessage(const td::td_api::chat &chat, const TgMessageInfo &messageInfo,
                                   const td::td_api::MessageContent &content, TdAccountData &account)
{
    // TRANSLATOR: In-chat error message, argument will be a Telegram type.
    std::string notice = getUnsupportedMessageDescription(content);
    notice = makeNoticeWithSender(chat, messageInfo, notice.c_str(), account.purpleAccount);
    showMessageText(account, chat, messageInfo, NULL, notice.c_str());

    // Show what the message holds anyway, in TDLib's own words. Escaping is what keeps any
    // markup in it inert; the font merely lines up the indentation.
    std::string text = "\n<font face=\"monospace\">" +
                       escapeForDisplay(describeUnsupportedContent(content)) + "</font>";
    showMessageText(account, chat, messageInfo, text.c_str(), NULL);
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
        if (anonymous)
            // TRANSLATOR: In-chat notification in a channel. Arguments are the time (or date and time) the message was sent at, as the conversation window shows it, and the message's new text.
            notice = formatMessage(_("The message from {0} was edited: {1}"), {sentAt, newText});
        else
            // TRANSLATOR: In-chat notification. Arguments are a username, the time (or date and time) the message was sent at, as the conversation window shows it, and the message's new text.
            notice = formatMessage(_("{0} edited the message from {1}: {2}"), {sender, sentAt, newText});
    } else {
        if (anonymous)
            // TRANSLATOR: In-chat notification in a channel, followed by the message as edited. Argument is the time (or date and time) the message was sent at, as the conversation window shows it.
            notice = formatMessage(_("The message from {0} was edited:"), sentAt);
        else
            // TRANSLATOR: In-chat notification, followed by the message as edited. Arguments are a username and the time (or date and time) the message was sent at, as the conversation window shows it.
            notice = formatMessage(_("{0} edited the message from {1}:"), {sender, sentAt});
        complete = false;
    }
    showMessageText(account, chat, messageInfo, NULL, notice.c_str());

    return complete;
}

// The line of a message's reactions, under it in Pidgin's tab: each with its count, ours in bold.
// Custom-emoji ones are left out, as they would need looking up first.
static std::string formatReactions(const td::td_api::messageInteractionInfo *info)
{
    std::string line;
    if (!info || !info->reactions_)
        return line;
    for (const auto &reaction: info->reactions_->reactions_) {
        if (!reaction || !reaction->type_)
            continue;
        std::string emoji;
        if (reaction->type_->get_id() == td::td_api::reactionTypeEmoji::ID)
            emoji = escapeForDisplay(static_cast<const td::td_api::reactionTypeEmoji &>(*reaction->type_).emoji_);
        else if (reaction->type_->get_id() == td::td_api::reactionTypePaid::ID)
            emoji = "\xe2\xad\x90";
        else
            continue;
        std::string item = emoji + "&nbsp;" + std::to_string(reaction->total_count_);
        if (reaction->is_chosen_)
            item = "<b>" + item + "</b>";
        if (!line.empty())
            line += "&nbsp;&nbsp;&nbsp;";
        line += item;
    }
    return line.empty() ? line : "<font size=\"2\">" + line + "</font>";
}

// Shows the message's reactions under it once it has been written
class ReactionsLine {
public:
    ReactionsLine(TdAccountData &account, const td::td_api::chat &chat, MessageId messageId,
                  const td::td_api::messageInteractionInfo *info)
    : m_account(account), m_chat(chat), m_messageId(messageId), m_html(formatReactions(info)) {}
    ~ReactionsLine()
    {
        if (!m_html.empty())
            showMessageReactions(findMessageConversation(m_account, m_chat), m_messageId, m_html);
    }
private:
    TdAccountData           &m_account;
    const td::td_api::chat  &m_chat;
    MessageId                m_messageId;
    std::string              m_html;
};

void showMessage(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                TdTransceiver &transceiver, TdAccountData &account)
{
    if (!fullMessage.message) return;
    td::td_api::message &message = *fullMessage.message;

    if (!message.content_)
        return;
    if (fullMessage.deleted) {
        // Nobody saw it, so nobody needs telling
        purple_debug_misc(config::pluginId, "Not displaying deleted message %" G_GINT64_FORMAT "\n", message.id_);
        return;
    }
    purple_debug_misc(config::pluginId, "Displaying message %" G_GINT64_FORMAT "\n", message.id_);

    TgMessageInfo &messageInfo = fullMessage.messageInfo;
    messageInfo.repliedMessage = std::move(fullMessage.repliedMessage);

    if (fullMessage.editedMessageTime && showEditNotice(chat, fullMessage, account)) {
        // The notice holds all of the edit; the tab gets the message over its earlier version
        // all the same, but not the log
        if (!fullMessage.replacing) {
            showPendingEdit(account, getChatId(message), getId(message));
            return;
        }
        messageInfo.unlogged = true;
    }

    // ttl_ field was removed in newer tdlib versions
    // if (message.ttl_ != 0) {
    //     if (purple_account_get_bool(account.purpleAccount, AccountOptions::ShowSelfDestruct, AccountOptions::ShowSelfDestructDefault)) {
    //         // TRANSLATOR: In-chat warning message
    //         const char *text   = _("Received self-destructing message, displaying anyway");
    //         std::string notice = makeNoticeWithSender(chat, messageInfo, text, account.purpleAccount);
    //         showMessageText(account, chat, messageInfo, NULL, notice.c_str());
    //     } else {
    //         // TRANSLATOR: In-chat warning message
    //         const char *text   = _("Received self-destructing message, not displayed due to lack of support");
    //         std::string notice = makeNoticeWithSender(chat, messageInfo, text, account.purpleAccount);
    //         showMessageText(account, chat, messageInfo, NULL, notice.c_str());
    //         return;
    //     }
    // }

    // Under the message, once all of it is written
    ReactionsLine reactionsLine(account, chat, messageInfo.id, message.interaction_info_.get());
    // All its parts, notices included, are the message, and its menu goes before the first. An edit
    // goes over the earlier version, saying it was edited.
    std::string editNote;
    if (fullMessage.replacing)
        // TRANSLATOR: After the text of a message edited where it stands, in Pidgin's conversation window.
        editNote = formatMessage(" <font size=\"2\"><i>({})</i></font>", std::string(_("edited")));
    MessageMarkScope markScope(messageInfo.id, getMessagePrefix(account, chat, messageInfo),
                               fullMessage.replacing ? editNote.c_str() : nullptr, true,
                               message.content_->get_id() == td::td_api::messageText::ID);
    FileInfo fileInfo;
    getFileFromMessage(fullMessage, fileInfo);
    if (fileInfo.secret) {
        if (purple_account_get_bool(account.purpleAccount, AccountOptions::ShowSelfDestruct, AccountOptions::ShowSelfDestructDefault)) {
            // TRANSLATOR: In-chat warning message
            std::string notice = formatMessage("Received secret file {}, displaying anyway",
                                               escapeForDisplay(fileInfo.description));
            notice = makeNoticeWithSender(chat, messageInfo, notice.c_str(), account.purpleAccount);
            showMessageText(account, chat, messageInfo, !fileInfo.caption.empty() ? fileInfo.caption.c_str() : nullptr,
                            notice.c_str());
        } else {
            // TRANSLATOR: In-chat warning message
            std::string notice = formatMessage("Ignoring secret file ({})",
                                               escapeForDisplay(fileInfo.description));
            notice = makeNoticeWithSender(chat, messageInfo, notice.c_str(), account.purpleAccount);
            showMessageText(account, chat, messageInfo, !fileInfo.caption.empty() ? fileInfo.caption.c_str() : nullptr,
                            notice.c_str());
            return;
        }
    }

    switch (message.content_->get_id()) {
        case td::td_api::messageText::ID:
            showTextMessage(chat, messageInfo, static_cast<const td::td_api::messageText &>(*message.content_),
                            account);
            break;
        case td::td_api::messagePhoto::ID:
            showPhotoMessage(chat, fullMessage, fileInfo.file, fileInfo.caption, transceiver, account);
            break;
        case td::td_api::messageDocument::ID:
        case td::td_api::messageVideo::ID:
        case td::td_api::messageAnimation::ID:
        case td::td_api::messageAudio::ID:
        case td::td_api::messageVoiceNote::ID:
        case td::td_api::messageVideoNote::ID:
            showFileMessage(chat, fullMessage, fileInfo.file, fileInfo.caption, fileInfo.description,
                            fileInfo.name, transceiver, account);
            break;
        case td::td_api::messageSticker::ID:
            showStickerMessage(chat, fullMessage, static_cast<td::td_api::messageSticker &>(*message.content_),
                               transceiver, account);
            break;
        case td::td_api::messageChatChangeTitle::ID: {
            const auto &titleChange = static_cast<const td::td_api::messageChatChangeTitle &>(*message.content_);
            // TRANSLATOR: In-chat status update, arguments are chat names.
            std::string notice = formatMessage(_("{0} changed group name to {1}"),
                                               {escapeForDisplay(getSenderDisplayName(chat, messageInfo,
                                                                                      account.purpleAccount)),
                                                escapeForDisplay(titleChange.title_)});
            showMessageText(account, chat, messageInfo, NULL, notice.c_str());
            break;
        }
        case td::td_api::messageCall::ID:
            showCallMessage(chat, messageInfo, static_cast<td::td_api::messageCall &>(*message.content_), account);
            break;
        case td::td_api::messageAnimatedEmoji::ID: {
            const td::td_api::messageAnimatedEmoji &animatedEmoji =
                static_cast<const td::td_api::messageAnimatedEmoji &>(*message.content_);
            showMessageText(account, chat, messageInfo, animatedEmoji.emoji_.c_str(), NULL);
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
                showMessageText(account, chat, messageInfo, text.c_str(), NULL);
            } else
                // Not supposed to be possible, but just in case
                showUnsupportedMessage(chat, messageInfo, *message.content_, account);
            break;
        }
        default:
            showUnsupportedMessage(chat, messageInfo, *message.content_, account);
    }

    // Put it back - may be needed if long download is in progress
    fullMessage.repliedMessage = std::move(messageInfo.repliedMessage);

    showPendingEdit(account, getChatId(message), getId(message));
}

void showMessages(std::vector<IncomingMessage>& messages, TdAccountData &account)
{
    for (IncomingMessage &readyMessage: messages) {
        if (!readyMessage.message) continue;
        const td::td_api::chat *chat = account.getChat(getChatId(*readyMessage.message));
        if (chat)
            showMessage(*chat, readyMessage, account.transceiver, account);
    }
}

const td::td_api::file *selectPhotoSize(PurpleAccount *account, const td::td_api::messagePhoto &photo)
{
    unsigned                     sizeLimit        = getAutoDownloadLimitKb(account);
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

// The thumbnail the sender attached to a video, if it is one the conversation window can show:
// Telegram also sends MPEG4 thumbnails for some videos and animations, and those are skipped.
// Of a cover the sender chose for a video, the size that does for a preview: the smallest at least as
// wide as the video's thumbnail, which is what the preview would be without a cover, or else the widest
static const td::td_api::file *getCoverPreview(const td::td_api::photo *cover,
                                               const td::td_api::thumbnail *thumbnail)
{
    // Telegram's thumbnails are 320 pixels wide, should one not say
    int32_t previewWidth = (thumbnail && (thumbnail->width_ > 0)) ? thumbnail->width_ : 320;
    const td::td_api::photoSize *chosen = nullptr;
    if (cover)
        for (const auto &size: cover->sizes_) {
            if (!size || !size->photo_)
                continue;
            bool wideEnough = (size->width_ >= previewWidth);
            if (!chosen ||
                (wideEnough && ((chosen->width_ < previewWidth) || (size->width_ < chosen->width_))) ||
                (!wideEnough && (chosen->width_ < previewWidth) && (size->width_ > chosen->width_)))
                chosen = size.get();
        }
    return chosen ? chosen->photo_.get() : nullptr;
}

static const td::td_api::file *getVideoPreview(const td::td_api::MessageContent &content,
                                               PurpleAccount *account)
{
    const td::td_api::thumbnail *thumbnail = nullptr;
    const td::td_api::file      *cover     = nullptr;
    bool                         secret    = false;

    switch (content.get_id()) {
        case td::td_api::messageVideo::ID: {
            const auto &video = static_cast<const td::td_api::messageVideo &>(content);
            // The thumbnail is the first frame, which is often a title card; a cover, where the
            // sender chose one, is what Telegram's own apps show instead
            if (video.video_)
                thumbnail = video.video_->thumbnail_.get();
            cover = getCoverPreview(video.cover_.get(), thumbnail);
            secret = video.is_secret_;
            break;
        }
        case td::td_api::messageAnimation::ID: {
            const auto &animation = static_cast<const td::td_api::messageAnimation &>(content);
            if (animation.animation_)
                thumbnail = animation.animation_->thumbnail_.get();
            secret = animation.is_secret_;
            break;
        }
        case td::td_api::messageVideoNote::ID: {
            const auto &videoNote = static_cast<const td::td_api::messageVideoNote &>(content);
            if (videoNote.video_note_)
                thumbnail = videoNote.video_note_->thumbnail_.get();
            secret = videoNote.is_secret_;
            break;
        }
    }

    // A self-destructing video is only displayed on request, and so is its thumbnail
    if (secret && !purple_account_get_bool(account, AccountOptions::ShowSelfDestruct,
                                           AccountOptions::ShowSelfDestructDefault))
        return nullptr;

    if (cover)
        return cover;
    if (thumbnail && thumbnail->format_ &&
        (thumbnail->format_->get_id() == td::td_api::thumbnailFormatJpeg::ID))
        return thumbnail->file_.get();
    return nullptr;
}

void makeFullMessage(const td::td_api::chat &chat, td::td_api::object_ptr<td::td_api::message> message,
                     IncomingMessage &fullMessage, const TdAccountData &account)
{
    if (!message) {
        fullMessage.message = nullptr;
        return;
    }

    fullMessage.repliedMessage = nullptr;
    fullMessage.selectedPhotoSizeId = 0;
    fullMessage.repliedMessageFetchDoneOrFailed = false;
    fullMessage.previewPath.clear();
    fullMessage.previewFileId = 0;
    fullMessage.previewFetchDone = false;
    fullMessage.inlineDownloadComplete = false;
    fullMessage.inlineDownloadTimeout = false;
    fullMessage.animatedStickerConverted = false;
    fullMessage.animatedStickerConvertSuccess = false;
    fullMessage.animatedStickerImageId = 0;

    const char *option = purple_account_get_string(account.purpleAccount, AccountOptions::DownloadBehaviour,
                                                   AccountOptions::DownloadBehaviourDefault());
    fullMessage.standardDownloadConfigured = (strcmp(option, AccountOptions::DownloadBehaviourHyperlink) != 0);
    fullMessage.inlineFileSizeLimit = getAutoDownloadLimitKb(account.purpleAccount);

    TgMessageInfo &messageInfo = fullMessage.messageInfo;
    messageInfo.id               = getId(*message);
    messageInfo.type             = TgMessageInfo::Type::Other;
    messageInfo.incomingGroupchatSender = getIncomingGroupchatSenderPurpleName(chat, *message, account);
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

        const td::td_api::file *preview = getVideoPreview(*message->content_, account.purpleAccount);
        if (preview) {
            fullMessage.previewFileId = preview->id_;
            if (preview->local_ && preview->local_->is_downloading_completed_) {
                fullMessage.previewPath = preview->local_->path_;
                fullMessage.previewFetchDone = true;
            }
        }
    }

    fullMessage.message = std::move(message);
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

static bool inlineDownloadNeedAutoDl(const IncomingMessage &fullMessage,
                                     const td::td_api::file &file)
{
    unsigned fileSize = getFileSizeKb(file);

    return !((file.local_ && file.local_->is_downloading_completed_)) &&
           isSizeWithinLimit(fileSize, fullMessage.inlineFileSizeLimit);
}

static bool isFileMessageReady(const IncomingMessage &fullMessage, ChatId chatId,
                               const td::td_api::MessageContent &content,
                               const td::td_api::file &file, const TdAccountData &account)
{
    const td::td_api::chat *chat = account.getChat(chatId);

    if (chat && isInlineDownload(fullMessage, content, *chat)) {
        // A link to the message needs nothing but the message
        if (linksToMessage(fullMessage))
            return true;
        // File will be shown inline
        // Animated stickers are not ready until converted
        if (fullMessage.inlineDownloadComplete)
            return !((content.get_id() == td::td_api::messageSticker::ID) &&
                     isStickerAnimated(fullMessage.inlineDownloadedFilePath) &&
                     shouldConvertAnimatedSticker(fullMessage.messageInfo, account.purpleAccount) &&
                     !fullMessage.animatedStickerConverted);
        else if (file.local_ && file.local_->is_downloading_completed_)
            return !((content.get_id() == td::td_api::messageSticker::ID) &&
                     isStickerAnimated(file.local_->path_) &&
                     shouldConvertAnimatedSticker(fullMessage.messageInfo, account.purpleAccount) &&
                     !fullMessage.animatedStickerConverted);
        else
            // Files above limit will either be ignored (in which case, message is ready)
            // or requested (in which case, don't try do display in order)
            return fullMessage.inlineDownloadTimeout || !inlineDownloadNeedAutoDl(fullMessage, file);
    } else
        // Standard libpurple transfer will be used, nothing to postpone
        return true;
}

void getFileFromMessage(const IncomingMessage &fullMessage, FileInfo &result)
{
    result.file = nullptr;
    result.caption = "";
    result.secret = false;
    if (!fullMessage.message || !fullMessage.message->content_)
        return;
    const td::td_api::message &message = *fullMessage.message;

    switch (message.content_->get_id()) {
        case td::td_api::messagePhoto::ID: {
            const td::td_api::messagePhoto &photo = static_cast<const td::td_api::messagePhoto &>(*message.content_);
            result.file = getSelectedPhotoSize(fullMessage, photo);
            result.name = ""; // will not be needed - inline download only
            if (photo.caption_) result.caption = getMessageText(*photo.caption_);
            // TRANSLATOR: File-type, used to describe what is being downloaded, in sentences like "Downloading photo" or "Ignoring photo download".
            result.description = _("photo");
            result.secret = photo.is_secret_;
            break;
        }
        case td::td_api::messageDocument::ID: {
            const td::td_api::messageDocument &document = static_cast<const td::td_api::messageDocument &>(*message.content_);
            result.file = document.document_ ? document.document_->document_.get() : nullptr;
            if (document.caption_) result.caption = getMessageText(*document.caption_);
            result.name = getFileName(document.document_.get());
            result.description = makeDocumentDescription(document.document_.get());
            break;
        }
        case td::td_api::messageVideo::ID: {
            const td::td_api::messageVideo &video = static_cast<const td::td_api::messageVideo &>(*message.content_);
            result.file = video.video_ ? video.video_->video_.get() : nullptr;
            if (video.caption_) result.caption = getMessageText(*video.caption_);
            result.name = getFileName(video.video_.get());
            result.description = makeDocumentDescription(video.video_.get(),
                                                         !fullMessage.messageInfo.mediaVariantLinks.empty());
            result.secret = video.is_secret_;
            break;
        }
        case td::td_api::messageAnimation::ID: {
            const td::td_api::messageAnimation &animation = static_cast<const td::td_api::messageAnimation &>(*message.content_);
            result.file = animation.animation_ ? animation.animation_->animation_.get() : nullptr;
            if (animation.caption_) result.caption = getMessageText(*animation.caption_);
            result.name = getFileName(animation.animation_.get());
            result.description = makeDocumentDescription(animation.animation_.get());
            result.secret = animation.is_secret_;
            break;
        }
        case td::td_api::messageAudio::ID: {
            const td::td_api::messageAudio &audio = static_cast<const td::td_api::messageAudio &>(*message.content_);
            result.file = audio.audio_ ? audio.audio_->audio_.get() : nullptr;
            if (audio.caption_) result.caption = getMessageText(*audio.caption_);
            result.name = getFileName(audio.audio_.get());
            result.description = makeDocumentDescription(audio.audio_.get());
            break;
        }
        case td::td_api::messageVoiceNote::ID: {
            const td::td_api::messageVoiceNote &audio = static_cast<const td::td_api::messageVoiceNote &>(*message.content_);
            result.file = audio.voice_note_ ? audio.voice_note_->voice_.get() : nullptr;
            if (audio.caption_) result.caption = getMessageText(*audio.caption_);
            result.name = getFileName(audio.voice_note_.get());
            result.description = makeDocumentDescription(audio.voice_note_.get());
            break;
        }
        case td::td_api::messageVideoNote::ID: {
            const td::td_api::messageVideoNote &video = static_cast<const td::td_api::messageVideoNote &>(*message.content_);
            result.file = video.video_note_ ? video.video_note_->video_.get() : nullptr;
            result.name = getFileName(video.video_note_.get());
            result.description = makeDocumentDescription(video.video_note_.get());
            result.secret = video.is_secret_;
            break;
        }
        case td::td_api::messageSticker::ID: {
            const td::td_api::messageSticker &sticker = static_cast<const td::td_api::messageSticker &>(*message.content_);
            result.file = sticker.sticker_ ? sticker.sticker_->sticker_.get() : nullptr;
            result.name = ""; // will not be needed - inline download only
            // TRANSLATOR: File-type, used to describe what is being downloaded, in sentences like "Downloading photo" or "Ignoring photo download".
            result.description = _("sticker");
            break;
        }
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

    // Wait for a video's thumbnail, or it would land below messages that arrived after it
    if (fullMessage.previewFileId && !fullMessage.previewFetchDone)
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

    checkMessageReady(pendingMessage, account.transceiver, account);
}

void fetchExtras(IncomingMessage &fullMessage, TdTransceiver &transceiver, TdAccountData &account,
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
        auto getMessageReq = td::td_api::make_object<td::td_api::getMessage>();
        getMessageReq->chat_id_    = chatId.value();
        getMessageReq->message_id_ = replyMessageId.value();
        transceiver.sendQueryWithTimeout(std::move(getMessageReq), onFetchReply,
                                         getMessageWaitSeconds(fullMessage.messageInfo));
    }

    if (fullMessage.previewFileId && !fullMessage.previewFetchDone) {
        purple_debug_misc(config::pluginId, "Fetching video thumbnail for message %" G_GINT64_FORMAT "\n",
                          messageId.value());
        auto downloadReq = td::td_api::make_object<td::td_api::downloadFile>();
        downloadReq->file_id_     = fullMessage.previewFileId;
        downloadReq->priority_    = FILE_DOWNLOAD_PRIORITY;
        downloadReq->offset_      = 0;
        downloadReq->limit_       = 0;
        downloadReq->synchronous_ = true;
        // Past the timeout the video is shown without it; a late thumbnail is then discarded
        transceiver.sendQueryWithTimeout(std::move(downloadReq),
            [&account, chatId, messageId](uint64_t, td::td_api::object_ptr<td::td_api::Object> object) {
                previewDownloadResponse(account, chatId, messageId, std::move(object));
            }, getMessageWaitSeconds(fullMessage.messageInfo));
    }

    FileInfo fileInfo;
    getFileFromMessage(fullMessage, fileInfo);
    if (fileInfo.file && message.content_ && chat && isInlineDownload(fullMessage, *message.content_, *chat)) {
        if (fileInfo.file->local_ && fileInfo.file->local_->is_downloading_completed_ &&
            (message.content_->get_id() == td::td_api::messageSticker::ID) &&
            isStickerAnimated(fileInfo.file->local_->path_))
        {
            if (shouldConvertAnimatedSticker(fullMessage.messageInfo, account.purpleAccount)) {
                StickerConversionThread *thread;
                thread = new StickerConversionThread(account.purpleAccount, fileInfo.file->local_->path_,
                                                     chatId, &fullMessage.messageInfo);
                thread->startThread();
            }
            // TODO: if animated stickers are disabled, fetch thumbnail instead
        } else if (inlineDownloadNeedAutoDl(fullMessage, *fileInfo.file) && !linksToMessage(fullMessage)) {
            // TgMessageInfo on fullMessage has replyMessage=NULL which will be copied onto DownloadRequest.
            // If message leaves PendingMessageQueue while download is still active, there's probably
            // a replyMessage on IncomingMessage by then, and it needs to be moved over to DownloadRequest.
            // Thumbnail also needs moving onto DownloadRequest.
            downloadFileInline(fileInfo.file->id_, chatId, fullMessage.messageInfo, fileInfo.description,
                               nullptr, transceiver, account);
        }
    }
}

void checkMessageReady(const IncomingMessage *message, TdTransceiver &transceiver,
                       TdAccountData &account, std::vector<IncomingMessage> *rvReadyMessages)
{
    if (!message || !message->message) return;

    if (isMessageReady(*message, account)) {
        std::vector<IncomingMessage> readyMessages;
        account.pendingMessages.setMessageReady(getChatId(*message->message), getId(*message->message),
                                               rvReadyMessages ? *rvReadyMessages : readyMessages);
        message = nullptr;
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
        pendingMessage->repliedMessage = td::move_tl_object_as<td::td_api::message>(object);
    else
        purple_debug_misc(config::pluginId, "Failed to fetch reply source for message %" G_GINT64_FORMAT "\n",
                          pendingMessageId.value());

    checkMessageReady(pendingMessage, account.transceiver, account);
}

static void queueMessage(TdAccountData &account, const td::td_api::chat &chat,
                         IncomingMessage &&fullMessage, PendingMessageQueue::MessageAction action)
{
    ChatId chatId = getId(chat);

    // Where only the caption changed, the file is not shown again, and so nothing is awaited
    if (fullMessage.captionOnly || isMessageReady(fullMessage, account)) {
        IncomingMessage readyMessage = account.pendingMessages.addReadyMessage(std::move(fullMessage), action);
        if (readyMessage.message)
            showMessage(chat, readyMessage, account.transceiver, account);
    } else {
        MessageId messageId = getId(*fullMessage.message);
        IncomingMessage &addedMessage = account.pendingMessages.addPendingMessage(std::move(fullMessage), action);
        fetchExtras(addedMessage, account.transceiver, account,
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
    else if (sender.get_id() == td::td_api::messageSenderChat::ID)
        return "c" + std::to_string(static_cast<const td::td_api::messageSenderChat &>(sender).chat_id_);
    return "";
}

static std::string getReactionTypeKey(const td::td_api::ReactionType &type)
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

// Who gave which: the sender's key and the reaction's, in that order, with a space between
static std::string getReactionKey(const std::string &senderKey, const td::td_api::ReactionType &type)
{
    return senderKey + ' ' + getReactionTypeKey(type);
}

// The reactions to a message whose givers are named, our own first so that it is known as such.
// Channels and big groups name nobody, and smaller ones only the latest few; reactions whose
// givers are not all named go into incompleteTypes.
static void getGivenReactions(const td::td_api::messageInteractionInfo *info, std::vector<GivenReaction> &given,
                              std::set<std::string> *incompleteTypes)
{
    // Tags in Saved Messages are reactions too, as far as TDLib is concerned
    if (!info || !info->reactions_ || info->reactions_->are_tags_)
        return;
    for (const auto &reaction: info->reactions_->reactions_) {
        if (!reaction || !reaction->type_)
            continue;
        std::set<std::string> senders;
        auto add = [&](const td::td_api::MessageSender &sender, bool self) {
            std::string senderKey = getSenderKey(sender);
            if (senders.insert(senderKey).second)
                given.push_back({getReactionKey(senderKey, *reaction->type_), reaction->type_.get(), self});
        };
        if (reaction->is_chosen_ && reaction->used_sender_id_)
            add(*reaction->used_sender_id_, true);
        for (const auto &sender: reaction->recent_sender_ids_)
            if (sender)
                add(*sender, false);
        if (incompleteTypes && (reaction->total_count_ > (int32_t)senders.size()))
            incompleteTypes->insert(getReactionTypeKey(*reaction->type_));
    }
}


static std::string getReactionSenderName(TdAccountData &account, const std::string &reactionKey)
{
    std::string senderKey = reactionKey.substr(0, reactionKey.find(' '));
    if ((senderKey.size() > 1) && (senderKey[0] == 'u'))
        return account.getDisplayName(UserId::fromString(senderKey.c_str() + 1));
    if ((senderKey.size() > 1) && (senderKey[0] == 'c')) {
        const td::td_api::chat *chat = account.getChat(ChatId::fromString(senderKey.c_str() + 1));
        if (chat)
            return chat->title_;
    }
    return "";
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
        if (g_utf8_validate(quote.c_str(), quote.size(), NULL) &&
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
    messageInfo.timestamp               = time(NULL);
    messageInfo.outgoing                = self;
    messageInfo.incomingGroupchatSender = getReactionSenderName(account, reactionKey);
    showMessageText(account, chat, messageInfo, text.c_str(), NULL, image ? PURPLE_MESSAGE_IMAGES : 0);
}

// Given the reaction as markup: the emoji, or the picture of a custom one
using CustomEmojiShown = std::function<void(TdAccountData &account, const std::string &reaction, bool image)>;

bool canConvertAnimatedSticker(PurpleAccount *purpleAccount)
{
#ifndef NoLottie
    return canShowImages() && purple_account_get_bool(purpleAccount, AccountOptions::AnimatedStickers,
                                   AccountOptions::AnimatedStickersDefault);
#else
    return false;
#endif
}

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
    // Custom emoji are 100 pixels square
    enum { CUSTOM_EMOJI_SIZE = 100 };
    // Whatever it is shown as, it leads to the file
    std::string linkStart = "<a href=\"" + escapeForDisplay(makeFileLinkTarget(path)) + "\">";
    std::string linkEnd   = "</a>";

    if (format == td::td_api::stickerFormatTgs::ID) {
        if (!canConvertAnimatedSticker(account.purpleAccount)) {
            shown(account, linkStart + fallback + linkEnd, false);
            return;
        }
        ReactionStickerThread *thread = new ReactionStickerThread(account.purpleAccount, path,
            [fallback, shown, linkStart, linkEnd](TdAccountData &account, int imageId) {
                if (imageId)
                    shown(account, linkStart + makeReactionImageText(imageId) + linkEnd, true);
                else
                    shown(account, linkStart + fallback + linkEnd, false);
            });
        thread->setSize(CUSTOM_EMOJI_SIZE);
        thread->startThread();
        return;
    }

    gchar       *data    = NULL;
    gsize        size    = 0;
    ImageSupport support = probeImageFile(path);
    if ((support == ImageSupport::Supported) && g_file_get_contents(path.c_str(), &data, &size, NULL)) {
        // The imgstore takes the data
        int imageId = purple_imgstore_add_with_id(data, size, NULL);
        shown(account, linkStart + makeReactionImageText(imageId) + linkEnd, true);
        return;
    }

    purple_debug_misc(config::pluginId, "Cannot show custom emoji %s\n", path.c_str());
    if (support == ImageSupport::Unrecognized)
        warnMissingImageLoader(path);
    shown(account, linkStart + fallback + linkEnd, false);
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
    if (!sticker || !sticker->sticker_ || !sticker->format_ ||
        purple_account_get_bool(account.purpleAccount, AccountOptions::FoldCustomEmoji,
                                AccountOptions::FoldCustomEmojiDefault))
    {
        shown(account, fallback, false);
        return;
    }

    // Those who would rather not see stickers move, would not like reactions to either
    int32_t format = sticker->format_->get_id();
    if ((format != td::td_api::stickerFormatWebp::ID) &&
        !purple_account_get_bool(account.purpleAccount, AccountOptions::AnimatedStickers,
                                 AccountOptions::AnimatedStickersDefault))
    {
        shown(account, fallback, false);
        return;
    }

    const td::td_api::file &file = *sticker->sticker_;
    if (file.local_ && file.local_->is_downloading_completed_) {
        showCustomEmojiFile(account, file.local_->path_, format, fallback, shown);
        return;
    }

    auto downloadReq = td::td_api::make_object<td::td_api::downloadFile>();
    downloadReq->file_id_     = file.id_;
    downloadReq->priority_    = 1;
    downloadReq->offset_      = 0;
    downloadReq->limit_       = 0;
    downloadReq->synchronous_ = true;
    account.transceiver.sendQuery(std::move(downloadReq),
        [&account, format, fallback, shown](uint64_t, td::td_api::object_ptr<td::td_api::Object> object) {
            const td::td_api::file *file = nullptr;
            if (object && (object->get_id() == td::td_api::file::ID))
                file = static_cast<const td::td_api::file *>(object.get());
            if (file && file->local_ && file->local_->is_downloading_completed_)
                showCustomEmojiFile(account, file->local_->path_, format, fallback, shown);
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
    PendingMessageQueue::MessageAction action, bool redrawn)
{
    if (!message) return;
    ChatId chatId = getId(chat);

    if (isReadReceiptsEnabled(account.purpleAccount))
        account.addPendingReadReceipt(chatId, getId(*message));

    if (message->content_) {
        ShownMessage shownMessage;
        getShownContent(*message->content_, shownMessage);
        shownMessage.timestamp = message->date_;
        // Reactions it came with are no news
        std::vector<GivenReaction> reactions;
        getGivenReactions(message->interaction_info_.get(), reactions, nullptr);
        for (const GivenReaction &reaction: reactions)
            shownMessage.reactions.emplace(reaction.key, reaction.self);
        account.addShownMessage(chatId, getId(*message), std::move(shownMessage));
    }

    IncomingMessage fullMessage;
    makeFullMessage(chat, std::move(message), fullMessage, account);
    fullMessage.messageInfo.fromHistory = (action == PendingMessageQueue::Prepend) || redrawn;
    fullMessage.messageInfo.redrawn     = redrawn;
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
            *shownMessage           = edit.previous;
            shownMessage->reactions = std::move(reactions);
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
    time_t editDate = message->edit_date_ ? message->edit_date_ : time(NULL);
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
    fullMessage.messageInfo.timestamp = editDate;
    queueMessage(account, *chat, std::move(fullMessage), PendingMessageQueue::Append);
}

static void requestEditedMessage(TdAccountData &account, const PendingEdit &edit)
{
    purple_debug_misc(config::pluginId, "Fetching edited message %" G_GINT64_FORMAT "\n",
                      edit.messageId.value());
    auto request = td::td_api::make_object<td::td_api::getMessage>();
    request->chat_id_    = edit.chatId.value();
    request->message_id_ = edit.messageId.value();
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
    if ((editedMessage.file == shownMessage->file) && (editedMessage.content == shownMessage->content)) {
        purple_debug_misc(config::pluginId, "Message %" G_GINT64_FORMAT " changed out of sight\n",
                          messageId.value());
        return;
    }

    PendingEdit edit;
    edit.chatId      = chatId;
    edit.messageId   = messageId;
    edit.messageTime = shownMessage->timestamp;
    edit.captionOnly = !editedMessage.file.empty() && (editedMessage.file == shownMessage->file);
    edit.previous    = *shownMessage;
    editedMessage.timestamp = shownMessage->timestamp;
    // Edits leave reactions be
    editedMessage.reactions = shownMessage->reactions;
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

void deleteMessages(TdAccountData &account, const td::td_api::updateDeleteMessages &update)
{
    // Messages dropped from TDLib's cache are not deleted at all
    if (!update.is_permanent_ || update.from_cache_)
        return;

    ChatId                  chatId = getChatId(update);
    const td::td_api::chat *chat   = account.getChat(chatId);

    for (unsigned i = 0; i < update.message_ids_.size(); i++) {
        MessageId     messageId    = getMessageId(update, i);
        ShownMessage *shownMessage = account.findShownMessage(chatId, messageId);
        if (!shownMessage)
            continue;
        time_t messageTime = shownMessage->timestamp;
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
            if (purple_account_get_bool(account.purpleAccount, AccountOptions::StrikeOutDeleted,
                                        AccountOptions::StrikeOutDeletedDefault))
                strikeOutMessage(conv, messageId);
            else
                removeMessage(conv, messageId);
            // Telegram does not say who deleted it, so the notice cannot either.
            std::string notice = formatMessage(
                // TRANSLATOR: In-chat notification. Argument is the time (or date and time) the message was sent at, as the conversation window shows it.
                _("Message from {} has been deleted"),
                linkToMessage(account.purpleAccount, chatId, messageId, formatMessageTime(messageTime)));
            showChatNotification(account, *chat, notice.c_str());
        }
    }
}

static void fetchHistoryRequest(TdAccountData &account, ChatId chatId, unsigned messagesFetched,
                                MessageId fetchBackFrom, MessageId lastReceivedMessage);

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
            if (stopAt.valid() && (getId(*message) == stopAt)) {
                purple_debug_misc(config::pluginId, "Found message %" G_GINT64_FORMAT ", stopping\n",
                                  stopAt.value());
                break;
            }
            if ((!stopAt.valid() && (messagesFetched >= 100)) ||
                (messagesFetched >= HISTORY_MESSAGES_ABSOLUTE_LIMIT))
            {
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

    if (requestMoreFrom.valid() && messagesFetched < HISTORY_MESSAGES_ABSOLUTE_LIMIT)
        fetchHistoryRequest(account, chatId, messagesFetched, requestMoreFrom, stopAt);
    else {
        purple_debug_misc(config::pluginId, "Done fetching history for chat %" G_GINT64_FORMAT " (%u msgs)\n",
                          chatId.value(), messagesFetched);
        std::vector<IncomingMessage> readyMessages;
        account.pendingMessages.setChatReady(chatId, readyMessages);
        showMessages(readyMessages, account);
    }
}

static void fetchHistoryRequest(TdAccountData &account, ChatId chatId, unsigned messagesFetched,
                                MessageId fetchBackFrom, MessageId stopAt)
{
    auto request = td::td_api::make_object<td::td_api::getChatHistory>();
    request->chat_id_ = chatId.value();
    request->from_message_id_ = fetchBackFrom.valid() ? fetchBackFrom.value() : 0;
    request->limit_ = 30;
    request->offset_ = 0;
    request->only_local_ = false;
    purple_debug_misc(config::pluginId, "Requesting history for chat %" G_GINT64_FORMAT
                      " starting from %" G_GINT64_FORMAT "\n", chatId.value(), fetchBackFrom.value());
    account.transceiver.sendQuery(std::move(request),
        [&account, chatId, messagesFetched, stopAt](uint64_t requestId, td::td_api::object_ptr<td::td_api::Object> response) {
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

// As many messages as Pidgin's tab keeps, at most, it having at least a line for each
static unsigned getRedrawLimit()
{
    static const char SCROLLBACK_PREF[] = "/pidgin/conversations/scrollback_lines";
    enum { NO_SCROLLBACK_LIMIT = 10000 };
    int lines = purple_prefs_exists(SCROLLBACK_PREF) ? purple_prefs_get_int(SCROLLBACK_PREF) : 0;
    return (lines > 0) ? lines : NO_SCROLLBACK_LIMIT;
}

namespace {
struct Redraw {
    ChatId                                                  chatId;
    MessageId                                               earliest; // or
    time_t                                                  since = 0;
    unsigned                                                limit;
    // Newest first, as TDLib gives them
    std::vector<td::td_api::object_ptr<td::td_api::message>> messages;
};
}

static void redrawRequest(TdAccountData &account, std::shared_ptr<Redraw> redraw, MessageId from);

// Messages found too far back for their tab once, which they are no nearer to since
static std::set<std::pair<int64_t, int64_t>> g_redrawTooFar;

// Empties the tab and has the messages shown in it, oldest first, ahead of any that came meanwhile
static void redrawShow(TdAccountData &account, Redraw &redraw)
{
    const td::td_api::chat *chat = account.getChat(redraw.chatId);
    PurpleConversation     *conv = chat ? findMessageConversation(account, *chat) : nullptr;
    if (conv) {
        purple_debug_misc(config::pluginId, "Redrawing chat %" G_GINT64_FORMAT " from message %" G_GINT64_FORMAT
                          " or time %" G_GINT64_FORMAT ": %zu messages\n", redraw.chatId.value(),
                          redraw.earliest.value(), int64_t(redraw.since), redraw.messages.size());
        purple_conversation_clear_message_history(conv);
        for (auto &message: redraw.messages)
            if (message && !account.pendingMessages.findPendingMessage(redraw.chatId, getId(*message)))
                handleIncomingMessage(account, *chat, std::move(message), PendingMessageQueue::Prepend, true);
    }
    std::vector<IncomingMessage> readyMessages;
    account.pendingMessages.setChatReady(redraw.chatId, readyMessages);
    showMessages(readyMessages, account);
}

static void redrawResponse(TdAccountData &account, std::shared_ptr<Redraw> redraw,
                           td::td_api::object_ptr<td::td_api::Object> response)
{
    MessageId more;
    bool      found = false;
    bool      older = false; // than the tab is filled from
    if (response && (response->get_id() == td::td_api::messages::ID)) {
        auto &messages = static_cast<td::td_api::messages &>(*response);
        for (auto &message: messages.messages_) {
            // The message a batch goes back from may come again, starting it
            if (!message || (!redraw->messages.empty() && (getId(*message) == getId(*redraw->messages.back()))))
                continue;
            older = redraw->since && (message->date_ < redraw->since);
            if (older)
                break;
            more = getId(*message);
            found = (more == redraw->earliest);
            redraw->messages.push_back(std::move(message));
            if (found || (redraw->messages.size() >= redraw->limit))
                break;
        }
        bool full = redraw->messages.size() >= redraw->limit;
        // Filling it from a time, all there is since, or as much as fits, is enough
        if (found || (redraw->since && (older || full || messages.messages_.empty()))) {
            if (!redraw->messages.empty()) {
                redrawShow(account, *redraw);
                return;
            }
        } else if (!full && !messages.messages_.empty()) {
            redrawRequest(account, redraw, more);
            return;
        } else {
            purple_debug_misc(config::pluginId, "Message %" G_GINT64_FORMAT " is further back than the tab holds\n",
                              redraw->earliest.value());
            g_redrawTooFar.insert({redraw->chatId.value(), redraw->earliest.value()});
        }
    } else
        purple_debug_warning(config::pluginId, "Cannot fetch chat %" G_GINT64_FORMAT " to redraw it: %s\n",
                             redraw->chatId.value(), getDisplayedError(response).c_str());
    // Left as it is
    std::vector<IncomingMessage> readyMessages;
    account.pendingMessages.setChatReady(redraw->chatId, readyMessages);
    showMessages(readyMessages, account);
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
static bool startRedraw(TdAccountData &account, ChatId chatId, MessageId earliest, time_t since)
{
    const td::td_api::chat *chat = account.getChat(chatId);
    PurpleConversation     *conv = chat ? findMessageConversation(account, *chat) : nullptr;
    if (!conv || !canRedrawConversation(conv))
        return true;
    // Not while history is being fetched, or the tab redrawn already
    if (!account.pendingMessages.isChatReady(chatId)) {
        purple_debug_misc(config::pluginId, "Chat %" G_GINT64_FORMAT " is busy, not redrawn\n", chatId.value());
        return false;
    }
    account.pendingMessages.setChatNotReady(chatId);
    auto redraw = std::make_shared<Redraw>();
    redraw->chatId   = chatId;
    redraw->earliest = earliest;
    redraw->since    = since;
    redraw->limit    = getRedrawLimit();
    redrawRequest(account, redraw, MessageId::invalid);
    return true;
}

void redrawChat(TdAccountData &account, ChatId chatId, MessageId earliest)
{
    if (!g_redrawTooFar.count({chatId.value(), earliest.value()}))
        startRedraw(account, chatId, earliest, 0);
}

bool fillChat(TdAccountData &account, ChatId chatId, time_t since)
{
    return startRedraw(account, chatId, MessageId::invalid, since);
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
    for (const GivenReaction &reaction: reactions) {
        present.insert(reaction.key);
        if (shownMessage->reactions.emplace(reaction.key, reaction.self).second) {
            changedBy.insert(reaction.key.substr(0, reaction.key.find(' ')));
            if (show)
                showReaction(account, *chat, messageId, *shownMessage, reaction.key, reaction.self, reaction.type);
        }
    }

    // Gone, as far as can be told: where everyone who gave that reaction is named, and they are not
    // among them any more. Exchanging one reaction for another is not taking it back.
    for (auto it = shownMessage->reactions.begin(); it != shownMessage->reactions.end(); ) {
        size_t      space   = it->first.find(' ');
        std::string typeKey = it->first.substr(space + 1);
        if (present.count(it->first) || incompleteTypes.count(typeKey)) {
            ++it;
            continue;
        }
        if (show && !changedBy.count(it->first.substr(0, space)))
            showReaction(account, *chat, messageId, *shownMessage, it->first, it->second, nullptr);
        it = shownMessage->reactions.erase(it);
    }
    if (show)
        showMessageReactions(findMessageConversation(account, *chat), messageId, formatReactions(info));
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
    auto request   = td::td_api::make_object<td::td_api::getMessage>();
    request->chat_id_    = chatId.value();
    request->message_id_ = messageId.value();
    account.transceiver.sendQuery(std::move(request),
        [&account, chatId, messageId, reactions](uint64_t, td::td_api::object_ptr<td::td_api::Object> object) {
            unreadReactionsMessageResponse(account, chatId, messageId, *reactions, std::move(object));
        });
}
