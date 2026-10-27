#ifndef _RECEIVING_H
#define _RECEIVING_H

#include "account-data.h"
#include <purple.h>
#include <map>

std::string makeNoticeWithSender(const td::td_api::chat &chat, const TgMessageInfo &message,
                                 const char *noticeText, PurpleAccount *account);
// As markup; custom emoji given in smileys, by id, are shown by their codes
std::string getMessageText(const td::td_api::formattedText &text,
                           const std::map<int64_t, std::string> *smileys = nullptr);
std::string makeInlineImageText(int imgstoreId);
// How long a message may wait for its picture, preview or quoted message before it is shown
// without: longer when catching up with a chat, where keeping messages in order matters more than
// showing each at once, but not so long that a message just sent waits behind them for long
unsigned getMessageWaitSeconds(const TgMessageInfo &message);
// Whether an animated sticker is to be converted into a picture the front-end can show
bool canConvertAnimatedSticker(PurpleAccount *purpleAccount);
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
                              TdTransceiver &transceiver, TdAccountData &account);
bool isStickerAnimated(const std::string &filePath);
bool shouldConvertAnimatedSticker(const TgMessageInfo &message, const PurpleAccount *purpleAccount);
void showMessage(const td::td_api::chat &chat, IncomingMessage &fullMessage,
                 TdTransceiver &transceiver, TdAccountData &account);
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

void makeFullMessage(const td::td_api::chat &chat, td::td_api::object_ptr<td::td_api::message> message,
                     IncomingMessage &fullMessage, const TdAccountData &account);
bool isMessageReady(const IncomingMessage &fullMessage, const TdAccountData &account);
void fetchExtras(IncomingMessage &fullMessage, TdTransceiver &transceiver, TdAccountData &account,
                 TdTransceiver::ResponseCb2 onFetchReply);
void checkMessageReady(const IncomingMessage *message, TdTransceiver &transceiver,
                       TdAccountData &account, std::vector<IncomingMessage> *rvReadyMessages = nullptr);

void handleIncomingMessage(TdAccountData &account, const td::td_api::chat &chat,
                           td::td_api::object_ptr<td::td_api::message> message,
                           PendingMessageQueue::MessageAction action, bool redrawn = false);
// The chat's conversation, if it is open; none is opened
PurpleConversation *findMessageConversation(TdAccountData &account, const td::td_api::chat &chat);
// Names a message by when it was sent, as the conversation window shows it
std::string formatMessageTime(time_t timestamp);
void fetchHistory(TdAccountData &account, ChatId chatId, MessageId fetchFrom, MessageId stopAt);
// Fills the chat's tab anew, in Pidgin, from the given message to the latest, if the tab can hold
// that many; to show a message it does not, such as one just reacted to
void redrawChat(TdAccountData &account, ChatId chatId, MessageId earliest);
void updateMessageContent(TdAccountData &account, ChatId chatId, MessageId messageId,
                          td::td_api::object_ptr<td::td_api::MessageContent> content);
void deleteMessages(TdAccountData &account, const td::td_api::updateDeleteMessages &update);
void updateMessageReactions(TdAccountData &account, ChatId chatId, MessageId messageId,
                            const td::td_api::messageInteractionInfo *info);
void updateUnreadReactions(TdAccountData &account, td::td_api::updateMessageUnreadReactions &update);

#endif

