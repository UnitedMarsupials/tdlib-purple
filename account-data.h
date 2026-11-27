#ifndef _ACCOUNT_DATA_H
#define _ACCOUNT_DATA_H

#include "buildopt.h"
#include "identifiers.h"
#include "transceiver.h"
#include "clipboard.h"
#include <td/telegram/td_api.h>

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <tuple>
#include <list>
#include <purple.h>

#ifndef NoVoip
#include <VoIPController.h>
#else

namespace tgvoip {
    struct VoIPController {};
}
#endif

bool        isPhoneNumber(const char *s);
const char *getCanonicalPhoneNumber(const char *s);
// A name as the plugin names a user: "id" and the user's id, in digits and nothing else; an id no
// user has, such as 0, too
bool        isUserIdName(const char *s);
UserId      purpleBuddyNameToUserId(const char *s);
SecretChatId purpleBuddyNameToSecretChatId(const char *s);
bool        isPrivateChat(const td::td_api::chat &chat);
// A basic group, or a supergroup: a group or a channel
bool        isGroupChat(const td::td_api::chat &chat);
UserId      getUserIdByPrivateChat(const td::td_api::chat &chat);
class TdAccountData;
bool        isChatInContactList(const TdAccountData &account, const td::td_api::chat &chat,
                                const td::td_api::user *privateChatUser);
bool        canReportAsSpam(const td::td_api::chat &chat);
BasicGroupId getBasicGroupId(const td::td_api::chat &chat);
SupergroupId getSupergroupId(const td::td_api::chat &chat);
SecretChatId getSecretChatId(const td::td_api::chat &chat);
bool        isGroupMember(const td::td_api::object_ptr<td::td_api::ChatMemberStatus> &status);
bool        isSameUser(const td::td_api::MessageSender &member1, const td::td_api::MessageSender &member2);

// Catching up with a chat: as many messages asked for at once, and at most so many in all
enum {
    CHAT_HISTORY_REQUEST_LIMIT  = 30,
    CHAT_HISTORY_RETRIEVE_LIMIT = 100
};

class PendingRequest {
public:
    uint64_t requestId;

    PendingRequest(uint64_t requestId) : requestId(requestId) {}
    virtual ~PendingRequest() {}
};

class GroupInfoRequest: public PendingRequest {
public:
    BasicGroupId groupId;

    GroupInfoRequest(uint64_t requestId, BasicGroupId groupId)
    : PendingRequest(requestId), groupId(groupId) {}
};

class SupergroupInfoRequest: public PendingRequest {
public:
    SupergroupId groupId;

    SupergroupInfoRequest(uint64_t requestId, SupergroupId groupId)
    : PendingRequest(requestId), groupId(groupId) {}
};

class GroupMembersRequestCont: public PendingRequest {
public:
    SupergroupId groupId;
    td::td_api::object_ptr<td::td_api::chatMembers> members;

    GroupMembersRequestCont(uint64_t requestId, SupergroupId groupId,
                            td::td_api::object_ptr<td::td_api::chatMembers> &&members)
    : PendingRequest(requestId), groupId(groupId), members(std::move(members)) {}
};

class ContactRequest: public PendingRequest {
public:
    std::string phoneNumber;
    std::string alias;
    std::string groupName;
    UserId      userId;

    ContactRequest(uint64_t requestId, const std::string &phoneNumber, const std::string &alias,
                   const std::string &groupName, UserId userId)
    : PendingRequest(requestId), phoneNumber(phoneNumber), alias(alias), groupName(groupName),
      userId(userId) {}
};

class GroupJoinRequest: public PendingRequest {
public:
    enum class Type {
        InviteLink,
        Username,
        // Its entry moved out of "Telegram: not joined": no tab opened for it
        Moved,
    };
    std::string joinString;
    Type        type;
    ChatId      chatId;

    GroupJoinRequest(uint64_t requestId, const std::string &joinString, Type type,
                     ChatId chatId = ChatId::invalid)
    : PendingRequest(requestId), joinString(joinString), type(type), chatId(chatId) {}
};

class SendMessageRequest: public PendingRequest {
public:
    ChatId      chatId;
    std::string tempFile;

    SendMessageRequest(uint64_t requestId, ChatId chatId, const char *tempFile)
    : PendingRequest(requestId), chatId(chatId), tempFile(tempFile ? tempFile : "") {}
};

// Checking whether TDLib still needs an image file sent in an earlier session
class TempFileCheckRequest: public PendingRequest {
public:
    ChatId  chatId;
    int64_t messageId;

    TempFileCheckRequest(uint64_t requestId, ChatId chatId, int64_t messageId)
    : PendingRequest(requestId), chatId(chatId), messageId(messageId) {}
};

class UploadRequest: public PendingRequest {
public:
    PurpleXfer    *xfer;
    ChatId         chatId;
    AttachmentKind kind; // what it is uploaded, and to be sent, as

    UploadRequest(uint64_t requestId, PurpleXfer *xfer, ChatId chatId, AttachmentKind kind)
    : PendingRequest(requestId), xfer(xfer), chatId(chatId), kind(kind) {}
};

// The message a message replies to, shared by the copies of the latter's info, as a download's
using RepliedMessage = std::shared_ptr<const td::td_api::message>;

struct TgMessageInfo {
    enum class Type {
        Photo,
        Sticker,
        Other
    };
    MessageId   id;
    Type        type = Type::Other;
    std::string incomingGroupchatSender;
    // The name the sender goes by in the chat, idNNN, telling users of one display name apart, which
    // libpurple shows by the display name; empty where the sender is no user
    std::string incomingGroupchatSenderName;
    time_t      timestamp = 0;
    bool        outgoing = false;
    bool        sentLocally = false; // For outgoing messages, whether sent by this very client
    MessageId   repliedMessageId;
    RepliedMessage repliedMessage;
    std::string forwardedFrom;
    std::string altText; // to show instead of the picture, where it cannot be shown
    bool        anonymousChannelPost = false; // incomingGroupchatSender is "Channel post", naming nobody
    bool        fromHistory = false; // fetched to catch up with a chat, rather than just sent
    std::string mediaVariantLinks; // markup: links to each size a photo or video comes in
    bool        unlogged = false; // shown in the tab only, the log having it in an edit's notice
    bool        redrawn = false; // shown again, as the tab is filled anew: not logged, nothing asked
    bool        logRedrawn = false; // shown so into a tab of a chat with no log yet: logged after all
    bool        picturesComing = false; // shown with stand-ins for pictures fetched: logged once they are

    // Who sent it, as a group chat names them: by idNNN, else by name, else "someone"
    const std::string &getGroupchatSender() const
    {
        static const std::string someone = "someone";
        return !incomingGroupchatSenderName.empty() ? incomingGroupchatSenderName :
               !incomingGroupchatSender.empty()     ? incomingGroupchatSender : someone;
    }

    // A copy kept for later, as by a download, without picturesComing, which is of the showing in
    // progress, not of what is shown later
    void assign(const TgMessageInfo &other)
    {
        *this          = other;
        picturesComing = false;
    }
};

class PurpleTdClient;

// Used for matching completed downloads to chats they belong to, and for starting PurpleXfer for
// time-consuming downloads
class DownloadRequest: public PendingRequest {
public:
    ChatId         chatId;

    // For inline downloads this is a copy of original TgMessageInfo from IncomingMessage.
    TgMessageInfo  message;

    int32_t        fileId;
    int32_t        fileSize;
    int32_t        downloadedSize;
    std::string    fileDescription;
    int            tempFd = -1;
    std::string    tempFileName;
    td::td_api::object_ptr<td::td_api::file> thumbnail;

    // Could not pass object_ptr through variadic funciton :(
    DownloadRequest(uint64_t requestId, ChatId chatId, TgMessageInfo &message,
                    int32_t fileId, int32_t fileSize, const std::string &fileDescription,
                    td::td_api::file *thumbnail)
    : PendingRequest(requestId), chatId(chatId), fileId(fileId),
      fileSize(fileSize), downloadedSize(0), fileDescription(fileDescription),
      thumbnail(thumbnail)
    {
        // The message replied to is shared, if it is here yet; one fetched later is copied in when
        // the message leaves PendingMessageQueue before the download is complete (handleLongInlineDownload)
        this->message.assign(message);
    }
    // An inline download still in progress, as at logout, leaves its temporary file behind otherwise
    ~DownloadRequest();
};

class AvatarDownloadRequest: public PendingRequest {
public:
    UserId userId;
    ChatId chatId;

    AvatarDownloadRequest(uint64_t requestId, const td::td_api::user *user)
    : PendingRequest(requestId), userId(getId(*user)), chatId(ChatId::invalid) {}
    AvatarDownloadRequest(uint64_t requestId, const td::td_api::chat *chat)
    : PendingRequest(requestId), userId(UserId::invalid), chatId(getId(*chat)) {}
};

class NewPrivateChatForMessage: public PendingRequest {
public:
    std::string  username;
    std::string  message;
    PurpleXfer  *fileUpload;

    NewPrivateChatForMessage(uint64_t requestId, const char *username, const char *message)
    : PendingRequest(requestId), username(username), message(message ? message : ""),
      fileUpload(nullptr) {}

    NewPrivateChatForMessage(uint64_t requestId, const char *username, PurpleXfer *upload)
    : PendingRequest(requestId), username(username), fileUpload(upload) {}
};

class ChatActionRequest: public PendingRequest {
public:
    enum class Type: uint8_t {
        Kick,
        Invite,
        GenerateInviteLink
    };
    Type   type;
    ChatId chatId;
    ChatActionRequest(uint64_t requestId, Type type, ChatId chatId)
    : PendingRequest(requestId), type(type), chatId(chatId) {}
};

struct IncomingMessage {
    td::td_api::object_ptr<td::td_api::message> message;
    td::td_api::object_ptr<td::td_api::file>    thumbnail;
    std::string inlineDownloadedFilePath;
    // Thumbnail the sender attached to a video, shown along with the video's own link or notice
    std::string previewPath;

    // This doesn't have to be a separate struct, it exists for historical reasons.
    // Could be refactored.
    TgMessageInfo messageInfo;

    int32_t  selectedPhotoSizeId = 0;
    int32_t  previewFileId = 0; // 0 if there is no thumbnail worth showing
    // Its size, and the low-grade picture the video carries, for its stand-in while it comes
    int32_t     previewWidth  = 0;
    int32_t     previewHeight = 0;
    unsigned    previewSizeKb = 0;
    std::string previewLowGrade;
    unsigned inlineFileSizeLimit = 0;
    bool     standardDownloadConfigured = false;
    bool     repliedMessageFetchDoneOrFailed = false;
    bool     previewFetchDone = false;
    // Whether the video's thumbnail is fetched and waited for, as decided once it is received
    bool     previewWaited = false;
    // Whether the chat's media were fetched unasked when it was received: what is fetched for it, and
    // waited for, goes by that, not by the setting changed meanwhile
    bool     downloadsMedia = true;
    bool     inlineDownloadComplete = false;
    bool     inlineDownloadTimeout = false;

    // For a message shown again because it was edited: when it was first sent, and whether
    // its caption is all that changed, and so all there is to show
    time_t   editedMessageTime = 0;
    bool     captionOnly       = false;
    // Written over its earlier version, where Pidgin's tab still shows it
    bool     replacing         = false;
    // ...not as edited, but as shown again with a picture come late
    bool     refreshing        = false;
    // Its words the same, only their formatting changed
    bool     formattingOnly    = false;
    // Deleted while waiting to be shown, which it then never is
    bool     deleted           = false;

    // Not a message but a line of Pidgin's log, as it is, put among them in the tab when it is filled;
    // with the pictures of the image store it shows, which it holds
    std::string      loggedHtml;
    std::vector<int> loggedImageIds;
    MessageId        loggedId;           // the message the line is, if the log says
};

class PendingMessageQueue {
public:
    enum class MessageAction {
        Append,
        Prepend
    };
    static constexpr MessageAction Append = MessageAction::Append;
    static constexpr MessageAction Prepend = MessageAction::Prepend;

    IncomingMessage &addPendingMessage(IncomingMessage &&message, MessageAction action);
    void             setMessageReady(ChatId chatId, MessageId messageId,
                                     std::vector<IncomingMessage> &readyMessages);
    IncomingMessage  addReadyMessage(IncomingMessage &&message, MessageAction action);
    IncomingMessage *findPendingMessage(ChatId chatId, MessageId messageId);
    void             flush(std::vector<IncomingMessage> &messages);
    void             setChatNotReady(ChatId chatId);
    void             setChatReady(ChatId chatId, std::vector<IncomingMessage> &readyMessages);
    bool             isChatReady(ChatId chatId);
    // Whether any of the chat's messages wait to be shown, as for what goes with them
    bool             hasWaitingMessages(ChatId chatId);
private:
    struct Message {
        IncomingMessage message;
        bool            ready;
    };
    struct ChatQueue {
        ChatId             chatId;
        bool               ready = true;
        std::list<Message> messages;
    };
    std::vector<ChatQueue> m_queues;

    std::vector<ChatQueue>::iterator getChatQueue(ChatId chatId);
    ChatQueue &getOrAddChatQueue(ChatId chatId);
    static std::list<Message>::iterator findMessage(ChatQueue &queue, MessageId messageId);
    Message &addMessage(ChatQueue &queue, MessageAction action);
    void extractReadyMessages(std::vector<ChatQueue>::iterator pQueue,
                              std::vector<IncomingMessage> &readyMessages);
};

struct ReadReceipt {
    ChatId    chatId;
    MessageId messageId;
};

// What the conversation shows of a message received in this session, so that an edit or a
// deletion of it can be reported later
struct ShownMessage {
    std::string file;      // the file the message carries, if any
    std::string content;   // what is shown of it besides: its text, caption or description
    std::string formatting; // of the text or caption, as an edit may change only that
    bool        quotable = false; // content is the text or caption, as written
    std::string kind;      // what the message is, to name it by where there is no text to quote
    std::string sender;    // who sent it, as the conversation names them; empty if not known
    bool        outgoing    = false;
    bool        deletedHere = false; // asked to be, from this client: by us, then
    time_t      timestamp = 0;
    // Who gave which reaction, as far as anyone has been named, and whether it was us
    std::map<std::string, bool> reactions;
    // Those giving each reaction now, by its key: as many as are named, the latest first, all once
    // fetched, and how many in all
    struct Givers {
        std::vector<std::string> senders;
        int32_t                  total    = 0;
        bool                     fetching = false; // or fetched
    };
    std::map<std::string, Givers> givers;

    // As far as an edit changes it
    bool operator==(const ShownMessage &other) const
    {
        return (file == other.file) && (content == other.content) && (formatting == other.formatting) &&
               (timestamp == other.timestamp);
    }
};

// A message of ours as written into its tab while it was being sent, under an id it did not keep:
// for the log once it is sent, under the one it does. So too a message shown with pictures still
// coming, for the log once they are here, as it is shown then, or as it was if it cannot be again
struct UnloggedSent {
    std::string text;
    std::string notification;
    uint32_t    flags     = 0;
    time_t      timestamp = 0;
    // The conversation it was written into, which may be closed by the time it is sent
    int         conversationType = 0;
    std::string conversationName;
    std::string who; // who it is from, other than us
    bool        awaitingPictures = false; // rather than being sent
    std::vector<int> imageIds; // of the image store, held while it waits
};

// An edit to a message that was still waiting to be shown at the time
struct PendingEdit {
    ChatId       chatId;
    MessageId    messageId;
    time_t       messageTime;
    bool         captionOnly;
    bool         formattingOnly = false;
    ShownMessage previous;    // as shown before, to go back to if the edit cannot be fetched...
    ShownMessage installed;   // ...unless a later edit has replaced this one's since
};

class TdAccountData {
public:
    using TdUserPtr           = td::td_api::object_ptr<td::td_api::user>;
    using TdChatPtr           = td::td_api::object_ptr<td::td_api::chat>;
    using TdGroupPtr          = td::td_api::object_ptr<td::td_api::basicGroup>;
    using TdGroupInfoPtr      = td::td_api::object_ptr<td::td_api::basicGroupFullInfo>;
    using TdSupergroupPtr     = td::td_api::object_ptr<td::td_api::supergroup>;
    using TdSupergroupInfoPtr = td::td_api::object_ptr<td::td_api::supergroupFullInfo>;
    using TdChatMembersPtr    = td::td_api::object_ptr<td::td_api::chatMembers>;
    using SecretChatPtr       = td::td_api::object_ptr<td::td_api::secretChat>;

    struct {
        unsigned maxCaptionLength = 0;
        unsigned maxMessageLength = 0;
        int64_t  myUserId         = 0;
    } options;

    // As tabs are filled anew, for this session: messages found too far back for their tab once,
    // which they are no nearer to since; chats whose tabs show their first message; and how many
    // messages a tab was last filled with, for those that would fill it to be reckoned
    std::set<std::pair<int64_t, int64_t>> redrawTooFar;
    std::set<int64_t>                     historyComplete;
    std::map<int64_t, size_t>             redrawnCounts;
    // Chats whose tabs, being opened for a link to a message, are to be filled from that one; and
    // those to be filled with the newest messages once not busy
    std::map<int64_t, MessageId>          linkedMessages;
    std::set<int64_t>                     newestPending;

    PurpleAccount *const  purpleAccount;
    TdTransceiver        &transceiver;
    TdAccountData(PurpleAccount *purpleAccount, TdTransceiver &transceiver)
    : purpleAccount(purpleAccount), transceiver(transceiver) {}
    ~TdAccountData();

    // Gone with this, for work put off to tell whether there is still an account to do it for
    std::shared_ptr<int> lifetime = std::make_shared<int>(0);

    void updateUser(TdUserPtr user);
    void setUserStatus(UserId UserId, td::td_api::object_ptr<td::td_api::UserStatus> status);
    void updateSmallProfilePhoto(UserId userId, td::td_api::object_ptr<td::td_api::file> photo);
    void updateBasicGroup(TdGroupPtr group);
    void setBasicGroupInfoRequested(BasicGroupId groupId);
    bool isBasicGroupInfoRequested(BasicGroupId groupId);
    void updateBasicGroupInfo(BasicGroupId groupId, TdGroupInfoPtr groupInfo);
    void updateSupergroup(TdSupergroupPtr group);
    void setSupergroupInfoRequested(SupergroupId groupId);
    bool isSupergroupInfoRequested(SupergroupId groupId);
    void updateSupergroupInfo(SupergroupId groupId, TdSupergroupInfoPtr groupInfo);
    void updateSupergroupMembers(SupergroupId groupId, TdChatMembersPtr members);

    void addChat(TdChatPtr chat); // Updates existing chat if any
    void updateChatPosition(ChatId chatId, td::td_api::object_ptr<td::td_api::chatPosition> &&position);
    void updateChatTitle(ChatId chatId, const std::string &title);
    void updateChatReadInbox(ChatId chatId, MessageId lastRead, int32_t unreadCount);
    void updateChatActionBar(ChatId chatId, td::td_api::object_ptr<td::td_api::ChatActionBar> actionBar);
    void updateSmallChatPhoto(ChatId chatId, td::td_api::object_ptr<td::td_api::file> photo);
    void setContacts(const td::td_api::users &users);
    void getContactsWithNoChat(std::vector<UserId> &userIds);
    void getChats(std::vector<const td::td_api::chat *> &chats) const;
    void deleteChat(ChatId id);
    void addExpectedChat(ChatId id);
    bool isExpectedChat(ChatId chatId);
    void removeExpectedChat(ChatId id);

    const td::td_api::chat       *getChat(ChatId chatId) const;
    int                           getPurpleChatId(ChatId tdChatId);
    const td::td_api::chat       *getChatByPurpleId(int32_t purpleChatId) const;
    const td::td_api::chat       *getPrivateChatByUserId(UserId userId) const;
    const td::td_api::user       *getUser(UserId userId) const;
    const td::td_api::user       *getUserByPhone(const char *phoneNumber) const;
    const td::td_api::user       *getUserByPrivateChat(const td::td_api::chat &chat) const;
    std::string                   getDisplayName(const td::td_api::user &user) const;
    std::string                   getDisplayName(UserId userId) const;
    void                          getUsersByDisplayName(const char *displayName,
                                                        std::vector<const td::td_api::user*> &users);
    // Resolve a Telegram @username (case-insensitive, optional leading '@') to loaded users, so
    // that a message addressed to a handle rather than an "id<n>" buddy can find its user.
    void                          getUsersByUsername(const char *username,
                                                     std::vector<const td::td_api::user*> &users);

    const td::td_api::basicGroup *getBasicGroup(BasicGroupId groupId) const;
    const td::td_api::basicGroupFullInfo *getBasicGroupInfo(BasicGroupId groupId) const;
    const td::td_api::supergroup *getSupergroup(SupergroupId groupId) const;
    const td::td_api::supergroupFullInfo *getSupergroupInfo(SupergroupId groupId) const;
    const td::td_api::chatMembers*getSupergroupMembers(SupergroupId groupId) const;
    const td::td_api::chat       *getBasicGroupChatByGroup(BasicGroupId groupId) const;
    const td::td_api::chat       *getSupergroupChatByGroup(SupergroupId groupId) const;
    bool                          isGroupChatWithMembership(const td::td_api::chat &chat) const;

    const td::td_api::chat       *getChatBySecretChat(SecretChatId secretChatId);

    template<typename ReqType, typename... ArgsType>
    void addPendingRequest(ArgsType &&... args)
    {
        m_requests.push_back(std::make_unique<ReqType>(std::forward<ArgsType>(args)...));
    }
    template<typename ReqType>
    void addPendingRequest(uint64_t requestId, std::unique_ptr<ReqType> &&request)
    {
        m_requests.push_back(std::move(request));
        m_requests.back()->requestId = requestId;
    }
    template<typename ReqType>
    std::unique_ptr<ReqType> getPendingRequest(uint64_t requestId)
    {
        return std::unique_ptr<ReqType>(dynamic_cast<ReqType *>(getPendingRequestImpl(requestId).release()));
    }
    template<typename ReqType>
    ReqType *findPendingRequest(uint64_t requestId)
    {
        return dynamic_cast<ReqType *>(findPendingRequestImpl(requestId));
    }

    const ContactRequest *     findContactRequest(UserId userId);
    void                       addTempFileUpload(ChatId chatId, int64_t messageId, const std::string &path);
    std::string                extractTempFileUpload(ChatId chatId, int64_t messageId);
    void                       loadTempFileUploads();
    void                       getTempFileUploads(std::vector<std::pair<ChatId, int64_t>> &messages);
    DownloadRequest *          findDownloadRequest(int32_t fileId);
    // Of the file for the message: the same file may come in several messages
    bool                       isDownloadingFor(int32_t fileId, ChatId chatId, MessageId messageId);
    void                       extractFileTransferRequests(std::vector<PurpleXfer *> &transfers);

    // An upload's kind is what it is to be sent as
    void                       addFileTransfer(int32_t fileId, PurpleXfer *xfer, ChatId chatId,
                                               AttachmentKind kind = AttachmentKind::Document);
    bool                       getFileTransfer(int32_t fileId, PurpleXfer *&xfer, ChatId &chatId,
                                               AttachmentKind *kind = nullptr);
    bool                       getFileIdForTransfer(PurpleXfer *xfer, int &fileId);
    void                       removeFileTransfer(int32_t fileId);
    void                       removeAllFileTransfers(std::vector<PurpleXfer *> &transfers);

    void                       addSecretChat(td::td_api::object_ptr<td::td_api::secretChat> secretChat);
    const td::td_api::secretChat *getSecretChat(SecretChatId id);
    void                       deleteSecretChat(SecretChatId id);

    auto                       getBasicGroupsWithMember(UserId userId) ->
                               std::vector<std::pair<BasicGroupId, const td::td_api::basicGroupFullInfo *>>;

    bool                       hasActiveCall();
    void                       setActiveCall(int32_t id);
    int32_t                    getActiveCallId() const { return m_callId; }
    tgvoip::VoIPController    *getCallData();
    void                       removeActiveCall();

    PendingMessageQueue        pendingMessages;
    // Reactions to our messages not seen yet, by chat
    std::map<ChatId, int32_t>  unreadReactionCounts;
    // What /paste and pasting in Pidgin gave placeholders for, kept as long as the account is
    PastedAttachments          pastedAttachments;

    void                       addPendingReadReceipt(ChatId chatId, MessageId messageId);
    void                       extractPendingReadReceipts(ChatId chatId, std::vector<ReadReceipt> &receipts);

    void                       addShownMessage(ChatId chatId, MessageId messageId, ShownMessage &&message);
    ShownMessage *             findShownMessage(ChatId chatId, MessageId messageId);
    void                       removeShownMessage(ChatId chatId, MessageId messageId);
    // A chat deleted here is shown no more, nor logged, until a tab of it is opened anew
    void                       setChatDeleted(ChatId chatId, bool deleted);
    bool                       isChatDeleted(ChatId chatId) const;
    void                       renameShownMessage(ChatId chatId, MessageId oldId, MessageId newId);
    // The id a message sent goes by once it is, where it went by another while being sent, as the
    // links in its showing do
    MessageId                  getSentMessageId(ChatId chatId, MessageId messageId) const;
    void                       addPendingEdit(const PendingEdit &edit);
    bool                       extractPendingEdit(ChatId chatId, MessageId messageId, PendingEdit &edit);
    void                       addUnloggedSent(ChatId chatId, MessageId messageId, UnloggedSent &&sent);
    bool                       extractUnloggedSent(ChatId chatId, MessageId messageId, UnloggedSent &sent);
    bool                       hasUnloggedSent(ChatId chatId, MessageId messageId) const;
    // Those awaiting their pictures, by chat and message
    void                       extractAwaitingPictures(std::vector<std::tuple<ChatId, MessageId, UnloggedSent>> &messages);
    // A picture of the message being fetched, to be shown in it once it is; false if it was already
    bool                       startPictureFetch(ChatId chatId, MessageId messageId, int32_t fileId);
    void                       endPictureFetch(ChatId chatId, MessageId messageId, int32_t fileId);
    bool                       isFetchingPictures(ChatId chatId, MessageId messageId) const;
    // A message to be shown again shortly, with pictures come, by the GLib source that will, which
    // goes with the account
    bool                       isRefreshDue(ChatId chatId, MessageId messageId) const;
    void                       setRefreshDue(ChatId chatId, MessageId messageId, guint sourceId);
    // The pictures of the message shown as their stand-ins, as it was last shown: whatever kept
    // each from being here, it is fetched as the message comes into view where pictures are
    void                       addStandIn(ChatId chatId, MessageId messageId, int32_t fileId);
    void                       forgetStandIns(ChatId chatId, MessageId messageId);
    std::vector<int32_t>       getStandIns(ChatId chatId, MessageId messageId) const;
    // An animated sticker that could not be converted, shown by its thumbnail instead from then on
    void                       setConversionFailed(int32_t fileId);
    bool                       hasConversionFailed(int32_t fileId) const;
    // An animated sticker's animation, converted once, in the image store, by the sticker's file id;
    // and the messages waiting for it while it is: false if it was being converted already
    int                        getConvertedSticker(int32_t fileId) const;
    bool                       startStickerConversion(int32_t fileId, ChatId chatId, MessageId messageId);
    void                       endStickerConversion(int32_t fileId, int imageId,
                                                    std::vector<std::pair<ChatId, MessageId>> &waiting);
private:
    TdAccountData(const TdAccountData &other) = delete;
    TdAccountData &operator=(const TdAccountData &other) = delete;

    struct UserInfo {
        TdUserPtr   user;
        std::string displayName;
    };

    struct ChatInfo {
        int32_t   purpleId;
        TdChatPtr chat;

        ChatInfo() : purpleId(0), chat() {}
    };

    struct GroupInfo {
        TdGroupPtr     group;
        TdGroupInfoPtr fullInfo;
        bool           fullInfoRequested = false;
    };

    struct SupergroupInfo {
        TdSupergroupPtr     group;
        TdSupergroupInfoPtr fullInfo;
        TdChatMembersPtr    members;
        bool                fullInfoRequested = false;
    };

    struct SendMessageInfo {
        ChatId      chatId;
        int64_t     messageId;
        std::string tempFile;
    };

    struct FileTransferInfo {
        int32_t        fileId;
        ChatId         chatId;
        PurpleXfer    *xfer;
        AttachmentKind kind;
    };

    using ChatMap = std::map<ChatId, ChatInfo>;
    using UserMap = std::map<UserId, UserInfo>;
    UserMap                            m_userInfo;
    ChatMap                            m_chatInfo;
    std::map<BasicGroupId, GroupInfo>  m_groups;
    std::map<SupergroupId, SupergroupInfo>  m_supergroups;
    std::map<SecretChatId, SecretChatPtr>   m_secretChats;
    int                                m_lastChatPurpleId = 0;

    // List of contacts for which private chat is not known yet.
    std::vector<UserId>                m_contactUserIdsNoChat;

    // Chats we want to libpurple-join when we get an updateNewChat about them
    std::vector<ChatId>                m_expectedChats;

    std::vector<std::unique_ptr<PendingRequest>> m_requests;

    // Newly sent messages containing inline images, for which a temporary file must be removed when
    // transfer is completed
    std::vector<SendMessageInfo>       m_sentMessages;

    // Currently active file transfers for which PurpleXfer is used
    std::vector<FileTransferInfo>      m_fileTransfers;

    // Voice call data
    std::unique_ptr<tgvoip::VoIPController> m_callData;
    int32_t                                 m_callId = 0;

    std::unique_ptr<PendingRequest> getPendingRequestImpl(uint64_t requestId);
    void                            saveTempFileUploads();
    PendingRequest *                findPendingRequestImpl(uint64_t requestId);

    // Read receipts not sent immediately due to away status (grouped per chat)
    std::map<ChatId, std::vector<ReadReceipt>> m_pendingReadReceipts;

    // Messages received in this session, and the order they came in, oldest first, so that
    // only so many are kept
    using ShownMessageKey = std::pair<ChatId, MessageId>;
    std::map<ShownMessageKey, ShownMessage> m_shownMessages;
    std::set<ChatId>                        m_deletedChats;
    std::map<ShownMessageKey, MessageId>    m_sentMessageIds;
    std::deque<ShownMessageKey>             m_shownMessageOrder;
    std::vector<PendingEdit>                m_pendingEdits;
    std::map<ShownMessageKey, UnloggedSent> m_unloggedSent;
    std::set<std::tuple<ChatId, MessageId, int32_t>> m_pictureFetches;
    std::set<int32_t>                       m_unconvertedStickers;
    std::map<ShownMessageKey, guint>        m_refreshesDue;
    std::map<ShownMessageKey, std::vector<int32_t>> m_standIns;
    std::map<int32_t, int>                  m_convertedStickers;
    std::map<int32_t, std::vector<std::pair<ChatId, MessageId>>> m_stickerConversions;
};

#endif
