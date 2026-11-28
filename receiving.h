#ifndef _RECEIVING_H
#define _RECEIVING_H

#include "account-data.h"
#include "log-history.h"
#include <purple.h>
#include <map>

std::string makeNoticeWithSender(const td::td_api::chat &chat, const TgMessageInfo &message,
                                 const char *noticeText, PurpleAccount *account);
// As markup; custom emoji given in smileys, by id, are shown by their codes. Sendable, only the
// formatting that Pidgin's markup can send back, for editing it.
std::string getMessageText(const td::td_api::formattedText &text,
                           const std::map<int64_t, std::string> *smileys = nullptr, bool sendable = false);
// Into the image store, which takes the data, under the file's name, for Pidgin to offer when saving it
int addImageFile(gchar *data, size_t length, const std::string &filePath);
// An image of the store, as markup: on a line of its own, unless placed among text
std::string makeInlineImageText(int imgstoreId, bool ownLine = true);
// How long a message may wait for its picture, preview or quoted message before it is shown
// without: longer when catching up with a chat, where keeping messages in order matters more than
// showing each at once, but not so long that a message just sent waits behind them for long
unsigned getMessageWaitSeconds(const TgMessageInfo &message);
// Whether an animated sticker is to be converted into a picture the front-end can show
// The conversation a message in the chat goes to; made, if there is none yet
PurpleConversation *getMessageConversation(TdAccountData &account, const td::td_api::chat &chat);
// For the conversation window, which renders text as HTML
std::string escapeForDisplay(const std::string &text);
// A file:// URI for the path
std::string makeFileUri(const std::string &filePath);
void sendConversationReadReceipts(TdAccountData &account, PurpleConversation *conv);
void showMessageText(TdAccountData &account, const td::td_api::chat &chat, const TgMessageInfo &message,
                     const char *text, const char *notification, uint32_t extraFlags = 0);
void showMessageTextIm(TdAccountData &account, const char *purpleUserName, const char *text,
                       const char *notification, time_t timestamp, PurpleMessageFlags flags);
void showChatNotification(TdAccountData &account, const td::td_api::chat &chat,
                          const char *notification, PurpleMessageFlags extraFlags = (PurpleMessageFlags)0);
void showChatNotification(TdAccountData &account, const td::td_api::chat &chat,
                          const char *notification, time_t timestamp, PurpleMessageFlags extraFlags);
void showGenericFileInline(const td::td_api::chat &chat, const TgMessageInfo &message,
                           const std::string &filePath, const char *caption,
                           const std::string &fileDescription, const std::string &preview,
                           TdAccountData &account);
void showDownloadedFileInline(ChatId chatId, TgMessageInfo &message,
                              const std::string &filePath, const char *caption,
                              const std::string &fileDescription,
                              td::td_api::object_ptr<td::td_api::file> thumbnail,
                              const std::string &preview,
                              TdAccountData &account);
bool isStickerAnimated(const std::string &filePath);
bool shouldConvertAnimatedSticker(const TgMessageInfo &message, const PurpleAccount *purpleAccount);
void showMessage(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                 TdAccountData &account);
void showMessages(std::vector<IncomingMessage>& messages, TdAccountData &account);

struct FileInfo {
    const td::td_api::file *file;
    std::string name;
    std::string caption;
    std::string description;
    bool        secret;
};

const td::td_api::file *selectPhotoSize(PurpleAccount *account, const td::td_api::messagePhoto &photo);
void getFileFromMessage(const IncomingMessage &fullMessage, FileInfo &result);
// What a message's content holds, of the kinds with a file: a photo (its largest size), a document,
// a video, an animation, an audio file, a voice or video note, or a sticker. Pointers into it, null
// where it has none.
struct ContentParts {
    const td::td_api::file          *file     = nullptr;
    const td::td_api::formattedText *caption  = nullptr;
    const std::string               *fileName = nullptr;
    const std::string               *mimeType = nullptr;
    bool                             secret   = false;
};
// Whether the content is of one of those kinds
bool getContentParts(const td::td_api::MessageContent &content, ContentParts &parts);
// Whether a message of the content carries a file, as getFileFromMessage() finds it, and whether
// that is a picture, to be copied as one too: a photo, or a sticker that does not move
void getMessageFileKind(const td::td_api::MessageContent &content, bool &hasFile, bool &picture);

void makeFullMessage(const td::td_api::chat &chat, td::td_api::object_ptr<td::td_api::message> message,
                     IncomingMessage &fullMessage, const TdAccountData &account);
bool isMessageReady(const IncomingMessage &fullMessage, const TdAccountData &account);
void fetchExtras(IncomingMessage &fullMessage, TdAccountData &account,
                 TdTransceiver::ResponseCb2 onFetchReply);
void checkMessageReady(const IncomingMessage *message, TdAccountData &account, std::vector<IncomingMessage> *rvReadyMessages = nullptr);

void handleIncomingMessage(TdAccountData &account, const td::td_api::chat &chat,
                           td::td_api::object_ptr<td::td_api::message> message,
                           PendingMessageQueue::MessageAction action, bool redrawn = false,
                           bool logRedrawn = false);
// Logs our message, sent, as written into its tab while it was being sent, under the id it now has;
// or one shown with pictures still coming, as it was shown, under its own
void logSentMessage(TdAccountData &account, ChatId chatId, MessageId oldId, MessageId newId);
// Messages come into view: those of their pictures left as stand-ins are fetched, if the chat's
// pictures are fetched unasked by now, those that failed tried again
void fetchViewedPictures(TdAccountData &account, PurpleConversation *conv, const std::vector<MessageId> &messageIds);
// An animated sticker converted, into the image store, or not (0): the messages showing it shown again
void stickerConverted(TdAccountData &account, int32_t fileId, int imageId);

// Where the log is read from for messages fetched: the earliest of them, or the tab's last message's
// date, if they follow it
time_t getLoggedLinesSince(const std::vector<td::td_api::object_ptr<td::td_api::message>> &messages,
                           time_t afterDate);
// The log's lines to show with them, oldest first: from there on, past the tab's last message (after)
// if they follow it; those a tab stopping short of the rest would not show left out, messages the log
// has but Telegram no more struck out, if kept
std::vector<LoggedLine> pickLoggedLines(std::vector<LoggedLine> logged,
                                        const std::vector<td::td_api::object_ptr<td::td_api::message>> &messages,
                                        bool stopsShort, bool keepDeleted, MessageId after, time_t afterDate);
// Logs those still awaiting their pictures as they were shown, as the account goes
void logMessagesAwaitingPictures(TdAccountData &account);
// The chat's conversation, if it is open; none is opened
PurpleConversation *findMessageConversation(TdAccountData &account, const td::td_api::chat &chat);
PurpleConversation *findMessageConversation(TdAccountData &account, ChatId chatId);
// Names a message by when it was sent, as the conversation window shows it
std::string formatMessageTime(time_t timestamp);
// Of a picture shown in the conversation, the size that does for it, of those within the limit on
// what is fetched unasked, in kilobytes, if any is (0 for none)
const td::td_api::file *chooseShownPhotoSize(const td::td_api::photo *photo, int32_t fallbackWidth = 320,
                                             unsigned sizeLimitKb = 0);
void fetchHistory(TdAccountData &account, ChatId chatId, MessageId fetchFrom, MessageId stopAt);
// Fills the chat's tab anew, in Pidgin, from the given message to the latest, if the tab can hold
// that many; to show a message it does not, such as one just reacted to
void redrawChat(TdAccountData &account, ChatId chatId, MessageId earliest);
// Fills the tab anew, from as many messages more before the earliest it shows, which stays at its
// top; or, filling a tab that had no more than fits, the window so many times its text's height,
// from about as many more as fill it, the end staying in view
void showEarlierMessages(TdAccountData &account, PurpleConversation *conv, double filling = 0);
// Who gave the reaction to the message, as plain text, a name a line, for its tooltip; asked for
// the first time, those not named yet are fetched, the tooltip asked for again once they are
// A reaction's type by a key, as its tooltip is asked for by, and back: the paid one for any key
// of no emoji, custom or not
std::string getReactionTypeKey(const td::td_api::ReactionType &type);
td::td_api::object_ptr<td::td_api::ReactionType> makeReactionType(const std::string &typeKey);
bool getReactionTip(TdAccountData &account, PurpleConversation *conv, MessageId messageId, const std::string &typeKey,
                    std::string &tip);
// Fills the chat's tab anew, in Pidgin, with its messages from the first unread one, and some read
// before it, as many as it can hold, scrolled to that one; false if the chat is busy, to be tried
// again later; or, if it was opened for a link to a message, from that one
bool fillChat(TdAccountData &account, ChatId chatId);
// Presents the chat's tab, opening it if need be, scrolled to the message, if one is given, or
// filled anew from it if the tab does not show it, as far back as the tab holds; the chat may have
// no tab, or be busy, its tab to be filled later
enum class LinkedMessageShown { Shown, Busy, NoConversation };
LinkedMessageShown showLinkedMessage(TdAccountData &account, const td::td_api::chat &chat, MessageId messageId);
// Where the chat's tab stops short of its newest message, the messages after it, written at its end,
// those at its top taken out as they would not fit
void showLaterMessages(TdAccountData &account, PurpleConversation *conv);
// Where the chat's tab stops short of its newest message, fills it anew with the latest ones, as
// before anything is sent from it
void showNewestMessages(TdAccountData &account, ChatId chatId);
// The chat a conversation is of, if known
const td::td_api::chat *findConversationChat(TdAccountData &account, PurpleConversation *conv);
void updateMessageContent(TdAccountData &account, ChatId chatId, MessageId messageId,
                          td::td_api::object_ptr<td::td_api::MessageContent> content);
void deleteMessages(TdAccountData &account, const td::td_api::updateDeleteMessages &update);
void updateMessageReactions(TdAccountData &account, ChatId chatId, MessageId messageId,
                            const td::td_api::messageInteractionInfo *info);
void updateUnreadReactions(TdAccountData &account, td::td_api::updateMessageUnreadReactions &update);
// A message's buttons, under it, by keys that go to the reactions' handlers: whether the key is a
// button's, and where it is among the message's
bool isButtonKey(const std::string &key);
bool parseButtonKey(const std::string &key, size_t &row, size_t &column, std::string &url);
void updateMessageButtons(TdAccountData &account, ChatId chatId, MessageId messageId,
                          td::td_api::object_ptr<td::td_api::ReplyMarkup> markup);

// A chat with reactions to our messages not seen yet is marked, as the phone app marks it: its
// conversation's title, and its contact in the buddy list, begin with a heart
extern const char UNREAD_REACTIONS_MARK[];
void setUnreadReactionCount(TdAccountData &account, ChatId chatId, int32_t count);
// The chat's messages are read up to the one given, here or on another device: once none is left
// unread, its conversation no longer shows any as unseen
void readChatInbox(TdAccountData &account, ChatId chatId, MessageId lastRead, int32_t unreadCount);
bool hasUnreadReactions(TdAccountData &account, PurpleConversation *conv);
// Marks the title, or takes the mark away, as the chat has reactions unread or not
void markConversationTitle(TdAccountData &account, PurpleConversation *conv);

#endif

