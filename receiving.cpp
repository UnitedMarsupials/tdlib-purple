#include "receiving.h"
#include "client-utils.h"
#include "format.h"
#include "purple-info.h"
#include "file-transfer.h"
#include "sticker.h"
#include "config.h"
#include "call.h"
#include <algorithm>
#include <fmt/format.h>

enum {
    HISTORY_MESSAGES_ABSOLUTE_LIMIT = 80
};

// The name is chosen by whoever sent the file and routinely contains spaces and other
// characters a URI cannot carry literally, so the path has to be percent-encoded before
// it is handed to a viewer.
static std::string makeFileUri(const std::string &filePath)
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
static std::string escapeForDisplay(const std::string &text)
{
    gchar *escaped = purple_markup_escape_text(text.c_str(), text.size());
    std::string result(escaped);
    g_free(escaped);
    return result;
}

// Names a message by when it was sent, which is what the conversation window shows of it:
// the time, and the date as well unless that is today
static std::string formatMessageTime(time_t timestamp)
{
    time_t    now = time(NULL);
    struct tm today = *localtime(&now);
    struct tm sent  = *localtime(&timestamp);

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
static std::string makeLocationText(const ShownLocation &shown)
{
    const td::td_api::location &location = *shown.location;
    std::string coordinates = fmt::format("{:.6f}, {:.6f}", location.latitude_, location.longitude_);
    std::string uri         = fmt::format("geo:{:.6f},{:.6f}", location.latitude_, location.longitude_);
    if (location.horizontal_accuracy_ > 0)
        uri += fmt::format(";u={:.0f}", location.horizontal_accuracy_);
    std::string link = "<a href=\"" + escapeForDisplay(makeLinkTarget(uri)) + "\">" + coordinates + "</a>";

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
    switch (content.get_id()) {
        case td::td_api::messageText::ID: {
            const auto &text = static_cast<const td::td_api::messageText &>(content);
            if (text.text_)
                result.content = text.text_->text_;
            break;
        }
        case td::td_api::messagePhoto::ID: {
            const auto &photo = static_cast<const td::td_api::messagePhoto &>(content);
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
            if (document.document_)
                result.file = getFileKey(document.document_->document_.get());
            caption = document.caption_.get();
            break;
        }
        case td::td_api::messageVideo::ID: {
            const auto &video = static_cast<const td::td_api::messageVideo &>(content);
            if (video.video_)
                result.file = getFileKey(video.video_->video_.get());
            caption = video.caption_.get();
            break;
        }
        case td::td_api::messageAnimation::ID: {
            const auto &animation = static_cast<const td::td_api::messageAnimation &>(content);
            if (animation.animation_)
                result.file = getFileKey(animation.animation_->animation_.get());
            caption = animation.caption_.get();
            break;
        }
        case td::td_api::messageAudio::ID: {
            const auto &audio = static_cast<const td::td_api::messageAudio &>(content);
            if (audio.audio_)
                result.file = getFileKey(audio.audio_->audio_.get());
            caption = audio.caption_.get();
            break;
        }
        case td::td_api::messageVoiceNote::ID: {
            const auto &voiceNote = static_cast<const td::td_api::messageVoiceNote &>(content);
            if (voiceNote.voice_note_)
                result.file = getFileKey(voiceNote.voice_note_->voice_.get());
            caption = voiceNote.caption_.get();
            break;
        }
        case td::td_api::messageVideoNote::ID: {
            const auto &videoNote = static_cast<const td::td_api::messageVideoNote &>(content);
            if (videoNote.video_note_)
                result.file = getFileKey(videoNote.video_note_->video_.get());
            break;
        }
        case td::td_api::messageSticker::ID: {
            const auto &sticker = static_cast<const td::td_api::messageSticker &>(content);
            if (sticker.sticker_)
                result.file = getFileKey(sticker.sticker_->sticker_.get());
            break;
        }
        case td::td_api::messageAnimatedEmoji::ID:
            result.content = static_cast<const td::td_api::messageAnimatedEmoji &>(content).emoji_;
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
#if TDLIB_API_LIVE_LOCATION
        case td::td_api::messageLiveLocation::ID:
#endif
        {
            // A live location moves all the time, which is no edit; that it no longer does is shown
            ShownLocation location;
            if (getShownLocation(content, location)) {
                if (location.livePeriod == 0)
                    result.content = makeLocationText(location);
                else if (isStoppedLiveLocation(location))
                    result.content = "stopped";
            }
            break;
        }
        default:
            result.content = describeUnsupportedContent(content);
    }

    if (caption)
        result.content = caption->text_;
}

std::string makeNoticeWithSender(const td::td_api::chat &chat, const TgMessageInfo &message,
                                 const char *noticeText, PurpleAccount *account)
{
    std::string prefix = escapeForDisplay(getSenderDisplayName(chat, message, account));
    if (!prefix.empty())
        prefix += ": ";
    return prefix + noticeText;
}

std::string getMessageText(const td::td_api::formattedText &text)
{
    char *newText = purple_markup_escape_text(text.text_.c_str(), text.text_.size());
    std::string result(newText);
    g_free(newText);
    return result;
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

void sendConversationReadReceipts(TdAccountData &account, PurpleConversation *conv)
{
    if (!conversationHasFocus(conv))
        return;
    ChatId chatId;
    PurpleConversationType convType = purple_conversation_get_type(conv);
    const char            *convName = purple_conversation_get_name(conv);

    if (convType == PURPLE_CONV_TYPE_IM) {
        UserId       privateChatUserId = purpleBuddyNameToUserId(convName);
        SecretChatId secretChatId      = purpleBuddyNameToSecretChatId(convName);
        const td::td_api::chat *tdlibChat = nullptr;

        if (privateChatUserId.valid())
            tdlibChat = account.getPrivateChatByUserId(privateChatUserId);
        else if (secretChatId.valid())
            tdlibChat = account.getChatBySecretChat(secretChatId);

        if (tdlibChat)
            chatId = getId(*tdlibChat);
    } else if (convType == PURPLE_CONV_TYPE_CHAT)
        chatId = getTdlibChatId(convName);

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
    else {
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
                         {escapeForDisplay(originalName), escapeForDisplay(text)});
}

void showMessageText(TdAccountData &account, const td::td_api::chat &chat, const TgMessageInfo &message,
                     const char *text, const char *notification, uint32_t extraFlags)
{
    PurpleMessageFlags directionFlag = message.outgoing ? PURPLE_MESSAGE_SEND : PURPLE_MESSAGE_RECV;
    PurpleMessageFlags flags = (PurpleMessageFlags) (extraFlags | directionFlag);
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

    const td::td_api::user *privateUser = account.getUserByPrivateChat(chat);
    if (privateUser) {
        std::string userName = getPurpleBuddyName(*privateUser);

        // If there is no buddy in the buddy list, libpurple won't be able to translate buddy name
        // to alias, so use display name instead of idXXXXXXXXX
        if (!purple_find_buddy(account.purpleAccount, userName.c_str()))
            userName = account.getDisplayName(*privateUser);
        showMessageTextIm(account, userName.c_str(), text, notification, message.timestamp, flags);
    }

    SecretChatId secretChatId = getSecretChatId(chat);
    if (secretChatId.valid()) {
        std::string userName = getSecretChatBuddyName(secretChatId);
        showMessageTextIm(account, userName.c_str(), text, notification, message.timestamp, flags);
    }

    if (getBasicGroupId(chat).valid() || getSupergroupId(chat).valid())
        showMessageTextChat(account, chat, message, text, notification, flags);
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

    if (g_file_get_contents (filePath.c_str(), &data, &len, NULL)) {
        int id = purple_imgstore_add_with_id (data, len, NULL);
        text = makeInlineImageText(id);
    } else
        text = "<img src=\"" + escapeForDisplay(makeFileUri(filePath)) + "\">";

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
    return !message.outgoing &&
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
        showWebpSticker(chat, message, filePath, fileDescription, account);
    }
}

void showGenericFileInline(const td::td_api::chat &chat, const TgMessageInfo &message,
                           const std::string &filePath, const char *caption,
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
    text += "<a href=\"" + escapeForDisplay(makeFileLinkTarget(filePath)) + "\">";
    if (!preview.empty())
        text += preview + "\n";
    text += escapeForDisplay(fileDescription) + "</a>";
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
        std::string displayText = getMessageText(*text.text_);
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
    } else if (!ignoreBigDownloads(account.purpleAccount)) {
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

    // Notice means file isn't downloaded yet or is ignored. Either way, show caption as well.
    // A thumbnail then starts the message, so it gets a line of its own, as a photo does.
    if (!notice.empty()) {
        if (preview.empty())
            showMessageText(account, chat, fullMessage.messageInfo, caption, notice.c_str());
        else {
            std::string text = "\n" + preview;
            if (caption) {
                text += "\n";
                text += caption;
            }
            showMessageText(account, chat, fullMessage.messageInfo, text.c_str(), notice.c_str(),
                            PURPLE_MESSAGE_IMAGES);
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


// Introduces a message shown again because it was edited. Returns true if that already says
// it all, which is when only its caption changed.
static bool showEditNotice(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                           TdAccountData &account)
{
    const TgMessageInfo &messageInfo = fullMessage.messageInfo;
    std::string          sender      = escapeForDisplay(getSenderDisplayName(chat, messageInfo,
                                                                            account.purpleAccount));
    std::string          sentAt      = formatMessageTime(fullMessage.editedMessageTime);
    FileInfo             fileInfo;
    getFileFromMessage(fullMessage, fileInfo);

    ShownLocation location;
    std::string   notice;
    if (fullMessage.message->content_ && getShownLocation(*fullMessage.message->content_, location) &&
        isStoppedLiveLocation(location))
        // TRANSLATOR: In-chat notification, followed by the last position. Arguments are a username and the time (or date and time) the message was sent at, as the conversation window shows it.
        notice = formatMessage(_("{0} stops sharing the live location from {1}:"), {sender, sentAt});
    else if (fullMessage.captionOnly && fileInfo.caption.empty())
        // TRANSLATOR: In-chat notification. Arguments are a username and the time (or date and time) the message was sent at, as the conversation window shows it.
        notice = formatMessage(_("{0} removes the caption of the message from {1}"), {sender, sentAt});
    else
        // TRANSLATOR: In-chat notification, followed by the message as edited. Arguments are a username and the time (or date and time) the message was sent at, as the conversation window shows it.
        notice = formatMessage(_("{0} edits the message from {1}:"), {sender, sentAt});
    showMessageText(account, chat, messageInfo, NULL, notice.c_str());

    if (!fullMessage.captionOnly)
        return false;
    if (!fileInfo.caption.empty())
        showMessageText(account, chat, messageInfo, fileInfo.caption.c_str(), NULL);
    return true;
}

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
        showPendingEdit(account, getChatId(message), getId(message));
        return;
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
    bool                         selectedFileSize = 0;
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
static const td::td_api::file *getVideoPreview(const td::td_api::MessageContent &content,
                                               PurpleAccount *account)
{
    const td::td_api::thumbnail *thumbnail = nullptr;
    bool                         secret    = false;

    switch (content.get_id()) {
        case td::td_api::messageVideo::ID: {
            const auto &video = static_cast<const td::td_api::messageVideo &>(content);
            if (video.video_)
                thumbnail = video.video_->thumbnail_.get();
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
            result.description = makeDocumentDescription(video.video_.get());
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
        transceiver.sendQueryWithTimeout(std::move(getMessageReq), onFetchReply, 1);
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
            }, 1);
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
        } else if (inlineDownloadNeedAutoDl(fullMessage, *fileInfo.file)) {
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

void handleIncomingMessage(TdAccountData &account, const td::td_api::chat &chat,
    td::td_api::object_ptr<td::td_api::message> message,
    PendingMessageQueue::MessageAction action)
{
    if (!message) return;
    ChatId chatId = getId(chat);

    if (isReadReceiptsEnabled(account.purpleAccount))
        account.addPendingReadReceipt(chatId, getId(*message));

    if (message->content_) {
        ShownMessage shownMessage;
        getShownContent(*message->content_, shownMessage);
        shownMessage.timestamp = message->date_;
        account.addShownMessage(chatId, getId(*message), std::move(shownMessage));
    }

    IncomingMessage fullMessage;
    makeFullMessage(chat, std::move(message), fullMessage, account);
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
        return;
    }

    // The message, or an earlier edit of it, may have gone into the queue meanwhile
    if (account.pendingMessages.findPendingMessage(edit.chatId, edit.messageId)) {
        account.addPendingEdit(edit);
        return;
    }

    td::td_api::object_ptr<td::td_api::message> message = td::move_tl_object_as<td::td_api::message>(object);
    time_t editDate = message->edit_date_ ? message->edit_date_ : time(NULL);
    // Without the reply or forward header, which the original had already
    message->reply_to_     = nullptr;
    message->forward_info_ = nullptr;

    IncomingMessage fullMessage;
    makeFullMessage(*chat, std::move(message), fullMessage, account);
    fullMessage.editedMessageTime     = edit.messageTime;
    fullMessage.captionOnly           = edit.captionOnly;
    fullMessage.messageInfo.timestamp = editDate;
    queueMessage(account, *chat, std::move(fullMessage), PendingMessageQueue::Append);
}

static void requestEditedMessage(TdAccountData &account, const PendingEdit &edit)
{
    purple_debug_misc(config::pluginId, "Fetching edited message %" G_GINT64_FORMAT "\n",
                      edit.messageId.value());
    auto request = td::td_api::make_object<td::td_api::getMessage>(edit.chatId.value(), edit.messageId.value());
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
    editedMessage.timestamp = shownMessage->timestamp;
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
        IncomingMessage *pendingMessage = account.pendingMessages.findPendingMessage(chatId, messageId);
        if (pendingMessage)
            pendingMessage->deleted = true;
        else if (chat) {
            // Telegram does not say who deleted it, so the notice cannot either.
            std::string notice = formatMessage(
                // TRANSLATOR: In-chat notification. Argument is the time (or date and time) the message was sent at, as the conversation window shows it.
                _("Message from {} has been deleted"), formatMessageTime(messageTime));
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
