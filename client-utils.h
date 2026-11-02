#ifndef _CLIENT_UTILS_H
#define _CLIENT_UTILS_H

#include "account-data.h"
#include <purple.h>
#include <thread>

const char *errorCodeMessage();

std::string         getUnsupportedMessageDescription(const td::td_api::MessageContent &content);
std::string         describeUnsupportedContent(const td::td_api::MessageContent &content);
std::string         getDisplayedError(const td::td_api::object_ptr<td::td_api::Object> &object);
std::string         proxyTypeToString(PurpleProxyType proxyType);

const char *        getPurpleStatusId(const td::td_api::UserStatus &tdStatus);
std::string         getPurpleBuddyName(const td::td_api::user &user);
std::string         getPurpleBuddyName(UserId userId);
std::string         getSecretChatBuddyName(SecretChatId secretChatId);
// The contact list's entry for a chat, its buddy or its chat entry; none for a chat not there
PurpleBlistNode    *findChatBlistNode(TdAccountData &account, const td::td_api::chat &chat);
// Whether deleted messages stay in the chat's tab, struck out, rather than go: as chosen for its
// entry in the contact list, if it has one and that was chosen, otherwise as the account has it
bool                keepsDeletedMessages(PurpleAccount *account, PurpleBlistNode *node);
void                setKeepsDeletedMessages(PurpleBlistNode *node, bool keep);
std::vector<const td::td_api::user *> getUsersByPurpleName(const char *buddyName, TdAccountData &account,
                                                           const char *action);
PurpleConversation *getImConversation(PurpleAccount *account, const char *username);
PurpleConvChat *    getChatConversation(TdAccountData &account, const td::td_api::chat &chat,
                                        int chatPurpleId);
PurpleConvChat *    findChatConversation(PurpleAccount *account, const td::td_api::chat &chat);
bool                conversationHasFocus(PurpleConversation *conv);

bool                isDeletedUser(const td::td_api::user &user);
PurpleGroup        *getDeletedAccountsGroup(bool create);
void                markBuddyDeleted(TdAccountData &account, const td::td_api::user &user);
void                restoreDeletedBuddy(TdAccountData &account, const td::td_api::user &user);
void                updatePrivateChat(TdAccountData &account, const td::td_api::chat *chat, const td::td_api::user &user);
void                updateBasicGroupChat(TdAccountData &account, BasicGroupId groupId);
void                updateSupergroupChat(TdAccountData &account, SupergroupId groupId);
bool                isInviteLinkActive(const td::td_api::chatInviteLink &linkInfo);
void                removeGroupChat(PurpleAccount *purpleAccount, const td::td_api::chat &chat);
void                removePrivateChat(TdAccountData &account, const td::td_api::chat &chat);
void                saveChatLastMessage(TdAccountData &account, ChatId chatId, MessageId messageId);
MessageId           getChatLastMessage(TdAccountData &account, ChatId chatId);
std::string         makeBasicDisplayName(const td::td_api::user &user);
bool                isAnonymousChannelPost(const td::td_api::chat &chat, const td::td_api::message &message);
std::string         getIncomingGroupchatSenderPurpleName(const td::td_api::chat &chat, const td::td_api::message &message,
                                                         const TdAccountData &account);
std::string         getForwardSource(const td::td_api::messageForwardInfo &forwardInfo,
                                     const TdAccountData &accountData);
void                getNamesFromAlias(const char *alias, std::string &firstName, std::string &lastName);
std::vector<PurpleChat *> findChatsByJoinString(const std::string &inviteLink);
std::vector<PurpleChat *> findChatsByNewGroup(const char *name, int type);

std::string getSenderDisplayName(const td::td_api::chat &chat, const TgMessageInfo &message,
                                 PurpleAccount *account);
// Links to a message's file, which the plugin fetches and opens when one is clicked. Only where the
// front-end lets the plugin handle a scheme of its own, which Pidgin does; elsewhere, files are
// linked to directly.
constexpr const char *MESSAGE_LINK_SCHEME = "tdlib://";
bool        messageLinksEnabled();
void        setMessageLinksEnabled(bool enabled);
// A variant, if given, names one of the sizes a photo or video comes in
std::string makeMessageLink(PurpleAccount *account, ChatId chatId, MessageId messageId,
                            const std::string &variant = std::string());
bool        parseMessageLink(const char *link, std::string &accountName, ChatId &chatId,
                             MessageId &messageId, std::string &variant);

// Links that scroll Pidgin's tab to a message shown in it, as where a reply quotes it or a notice
// tells of its edit. Where they cannot work, the markup is given back as it is.
constexpr const char *JUMP_LINK_SCHEME = "tdlib-message://";
bool        jumpLinksEnabled();
void        setJumpLinksEnabled(bool enabled);
std::string linkToMessage(PurpleAccount *account, ChatId chatId, MessageId messageId,
                          const std::string &markup);
// Put after a message's text, for the log to say which message it is: a comment, which Pidgin's
// log viewer does not show, and which Pidgin's conversation window is not given; Finch shows no
// markup. Not where it would go on to other clients, as from BitlBee; enabled when the plugin loads.
bool        logMarkersEnabled();
void        setLogMarkersEnabled(bool enabled);
std::string makeLogMarker(MessageId messageId);
// Takes the marker out of a message's markup, telling which message it is, if it has one
bool        takeLogMarker(std::string &html, MessageId *messageId = nullptr);
// Markup for the log only, which Pidgin's tab leaves out
std::string makeLogOnly(const std::string &html);
// Takes what is for the log only out of a message's markup; false if there was none
bool        takeLogOnly(std::string &html);
// Takes the markers of what is for the log only out, leaving what they enclose
void        unmarkLogOnly(std::string &html);
// The new version of an edited message's text, its words compared with those of the previous one
// (plain text), as wdiff does: removed words put back where they were, on the one background
// color, and added ones on the other
std::string markChanges(const std::string &previous, const std::string &html,
                        const std::string &removedColor, const std::string &addedColor);
// Whether the link is a jump to a message, and to which
bool        parseJumpLink(const char *link, std::string &accountName, ChatId &chatId,
                          MessageId &messageId);

std::string getDownloadXferPeerName(ChatId chatId,
                                    const TgMessageInfo &message,
                                    TdAccountData &account);

void notifySendFailed(const td::td_api::updateMessageSendFailed &sendFailed, TdAccountData &account);
void updateChatConversation(PurpleConvChat *purpleChat, const td::td_api::basicGroupFullInfo &groupInfo,
                    const TdAccountData &account);
void updateChatConversation(PurpleConvChat *purpleChat, const td::td_api::supergroupFullInfo &groupInfo,
                    const TdAccountData &account);
void updateSupergroupChatMembers(PurpleConvChat *purpleChat, const td::td_api::chatMembers &members,
                                 const TdAccountData &account);

// Pidgin's markup as Telegram's formatted text: bold, italic, underline, strikethrough and links;
// the rest of it dropped
td::td_api::object_ptr<td::td_api::formattedText> parseFormattedText(const std::string &html);

// What a pasted attachment goes as, from the file uploaded, with its caption
td::td_api::object_ptr<td::td_api::InputMessageContent>
makeAttachmentContent(const PlaceholderUse &use, const std::string &uploadPath,
                      td::td_api::object_ptr<td::td_api::formattedText> caption);
// An edit of a message sent, as written: the placeholders in it, and its text without them
struct ParsedEdit {
    std::vector<PlaceholderUse>                       attachments;
    td::td_api::object_ptr<td::td_api::formattedText> text;
};
ParsedEdit parseEdit(const char *message, const TdAccountData &account);
bool isSameFormattedText(const td::td_api::formattedText &a, const td::td_api::formattedText &b);
int  transmitMessage(ChatId chatId, const char *message, TdTransceiver &transceiver,
                     TdAccountData &account, TdTransceiver::ResponseCb response);

void requestRecoveryEmailConfirmation(PurpleConnection *gc, const char *emailInfo);

void updateOption(const td::td_api::updateOption &option, TdAccountData &account);
void populateGroupChatList(PurpleRoomlist *roomlist, const std::vector<const td::td_api::chat *> &chats,
                           const TdAccountData &account);

class AccountThread {
public:
    using Callback = void (PurpleTdClient::*)(AccountThread *thread);
    static void setSingleThread();
    static bool isSingleThread();

    AccountThread(PurpleAccount *purpleAccount);
    virtual ~AccountThread() {}
    void startThread();
private:
    std::thread m_thread;
    std::string m_accountUserName;
    std::string m_accountProtocolId;

    void            threadFunc();
    static gboolean mainThreadCallback(gpointer data);
protected:
    virtual void run() = 0;
    virtual void callback(PurpleTdClient *tdClient) = 0;
};

// A function of the front-end's, or of a library it has loaded and the plugin does not link; null
// where there is none such. GModule looks in every module loaded, on Windows as elsewhere.
void *findFrontEndSymbol(const char *name);

#endif
