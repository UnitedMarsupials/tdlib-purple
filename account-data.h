#ifndef _ACCOUNT_DATA_H
#define _ACCOUNT_DATA_H

#include "buildopt.h"
#include "identifiers.h"
#include "transceiver.h"
#include <td/telegram/td_api.h>

#include <deque>
#include <map>
#include <mutex>
#include <set>
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
UserId      purpleBuddyNameToUserId(const char *s);
SecretChatId purpleBuddyNameToSecretChatId(const char *s);
bool        isPrivateChat(const td::td_api::chat &chat);
UserId      getUserIdByPrivateChat(const td::td_api::chat &chat);
bool        isChatInContactList(const td::td_api::chat &chat, const td::td_api::user *privateChatUser);
BasicGroupId getBasicGroupId(const td::td_api::chat &chat);
SupergroupId getSupergroupId(const td::td_api::chat &chat);
SecretChatId getSecretChatId(const td::td_api::chat &chat);
bool        isGroupMember(const td::td_api::object_ptr<td::td_api::ChatMemberStatus> &status);
bool        isSameUser(const td::td_api::MessageSender &member1, const td::td_api::MessageSender &member2);

enum {
    CHAT_HISTORY_REQUEST_LIMIT  = 50,
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

    GroupMembersRequestCont(uint64_t requestId, SupergroupId groupId, td::td_api::chatMembers *members)
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
    PurpleXfer *xfer;
    ChatId      chatId;

    UploadRequest(uint64_t requestId, PurpleXfer *xfer, ChatId chatId)
    : PendingRequest(requestId), xfer(xfer), chatId(chatId) {}
};

struct TgMessageInfo {
    enum class Type {
        Photo,
        Sticker,
        Other
    };
    MessageId   id;
    Type        type = Type::Other;
    std::string incomingGroupchatSender;
    time_t      timestamp = 0;
    bool        outgoing = false;
    bool        sentLocally = false; // For outgoing messages, whether sent by this very client
    MessageId   repliedMessageId;
    td::td_api::object_ptr<td::td_api::message> repliedMessage;
    std::string forwardedFrom;
    std::string altText; // to show instead of the picture, where it cannot be shown
    bool        anonymousChannelPost = false; // incomingGroupchatSender is "Channel post", naming nobody
    bool        fromHistory = false; // fetched to catch up with a chat, rather than just sent
    std::string mediaVariantLinks; // markup: links to each size a photo or video comes in

    void assign(const TgMessageInfo &other)
    {
        id = other.id;
        type = other.type;
        incomingGroupchatSender = other.incomingGroupchatSender;
        timestamp = other.timestamp;
        outgoing = other.outgoing;
        sentLocally = other.sentLocally;
        repliedMessageId = other.repliedMessageId;
        repliedMessage = nullptr;
        forwardedFrom = other.forwardedFrom;
        altText = other.altText;
        anonymousChannelPost = other.anonymousChannelPost;
        fromHistory = other.fromHistory;
        mediaVariantLinks = other.mediaVariantLinks;
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
        // If download is started while the message is in PendingMessageQueue, repliedMessage will
        // be on IncomingMessage, and one here in TgMessageInfo will be NULL. In this case,
        // repliedMessage will be moved onto DownloadRequest if message leaves PendingMessageQueue
        // before download is complete (meaning it took more than 1 second to download).
        this->message.assign(message);
        if (message.repliedMessage)
            this->message.repliedMessage = std::move(message.repliedMessage);
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
    : PendingRequest(requestId), username(username), message(message ? message : nullptr),
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
    td::td_api::object_ptr<td::td_api::message> repliedMessage;
    td::td_api::object_ptr<td::td_api::file>    thumbnail;
    std::string inlineDownloadedFilePath;
    // Thumbnail the sender attached to a video, shown along with the video's own link or notice
    std::string previewPath;

    // This doesn't have to be a separate struct, it exists for historical reasons.
    // Could be refactored.
    TgMessageInfo messageInfo;

    int32_t  selectedPhotoSizeId;
    int32_t  previewFileId; // 0 if there is no thumbnail worth showing
    unsigned inlineFileSizeLimit;
    bool     standardDownloadConfigured;
    bool     repliedMessageFetchDoneOrFailed;
    bool     previewFetchDone;
    bool     inlineDownloadComplete;
    bool     inlineDownloadTimeout;
    bool     animatedStickerConverted;
    bool     animatedStickerConvertSuccess;
    int      animatedStickerImageId;

    // For a message shown again because it was edited: when it was first sent, and whether
    // its caption is all that changed, and so all there is to show
    time_t   editedMessageTime = 0;
    bool     captionOnly       = false;
    // Deleted while waiting to be shown, which it then never is
    bool     deleted           = false;
};

class PendingMessageQueue {
public:
    enum class MessageAction {
        Append,
        Prepend
    };
    static constexpr MessageAction Append = MessageAction::Append;
    static constexpr MessageAction Prepend = MessageAction::Prepend;
    using TdMessagePtr = td::td_api::object_ptr<td::td_api::message>;

    IncomingMessage &addPendingMessage(IncomingMessage &&message, MessageAction action);
    void             setMessageReady(ChatId chatId, MessageId messageId,
                                     std::vector<IncomingMessage> &readyMessages);
    IncomingMessage  addReadyMessage(IncomingMessage &&message, MessageAction action);
    IncomingMessage *findPendingMessage(ChatId chatId, MessageId messageId);
    void             flush(std::vector<IncomingMessage> &messages);
    void             setChatNotReady(ChatId chatId);
    void             setChatReady(ChatId chatId, std::vector<IncomingMessage> &readyMessages);
    bool             isChatReady(ChatId chatId);
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
    bool        quotable = false; // content is the text or caption, as written
    std::string kind;      // what the message is, to name it by where there is no text to quote
    time_t      timestamp;
    // Who gave which reaction, as far as anyone has been named, and whether it was us
    std::map<std::string, bool> reactions;

    // As far as an edit changes it
    bool operator==(const ShownMessage &other) const
    {
        return (file == other.file) && (content == other.content) && (timestamp == other.timestamp);
    }
};

// An edit to a message that was still waiting to be shown at the time
struct PendingEdit {
    ChatId       chatId;
    MessageId    messageId;
    time_t       messageTime;
    bool         captionOnly;
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

    PurpleAccount *const  purpleAccount;
    TdTransceiver        &transceiver;
    TdAccountData(PurpleAccount *purpleAccount, TdTransceiver &transceiver)
    : purpleAccount(purpleAccount), transceiver(transceiver) {}

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
    const td::td_api::user       *getUserByPrivateChat(const td::td_api::chat &chat);
    std::string                   getDisplayName(const td::td_api::user &user) const;
    std::string                   getDisplayName(UserId userId) const;
    void                          getUsersByDisplayName(const char *displayName,
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
    void addPendingRequest(ArgsType... args)
    {
        m_requests.push_back(std::make_unique<ReqType>(args...));
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
    void                       extractFileTransferRequests(std::vector<PurpleXfer *> &transfers);

    void                       addFileTransfer(int32_t fileId, PurpleXfer *xfer, ChatId chatId);
    bool                       getFileTransfer(int32_t fileId, PurpleXfer *&xfer, ChatId &chatId);
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

    void                       addPendingReadReceipt(ChatId chatId, MessageId messageId);
    void                       extractPendingReadReceipts(ChatId chatId, std::vector<ReadReceipt> &receipts);

    void                       addShownMessage(ChatId chatId, MessageId messageId, ShownMessage &&message);
    ShownMessage *             findShownMessage(ChatId chatId, MessageId messageId);
    void                       removeShownMessage(ChatId chatId, MessageId messageId);
    void                       renameShownMessage(ChatId chatId, MessageId oldId, MessageId newId);
    void                       addPendingEdit(const PendingEdit &edit);
    bool                       extractPendingEdit(ChatId chatId, MessageId messageId, PendingEdit &edit);
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
        int32_t     fileId;
        ChatId      chatId;
        PurpleXfer *xfer;
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

    // Used to remember stuff during asynchronous communication when adding contact
    std::vector<ContactRequest>        m_addContactRequests;

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
    int32_t                                 m_callId;

    std::unique_ptr<PendingRequest> getPendingRequestImpl(uint64_t requestId);
    void                            saveTempFileUploads();
    PendingRequest *                findPendingRequestImpl(uint64_t requestId);

    // Read receipts not sent immediately due to away status (grouped per chat)
    std::vector<std::vector<ReadReceipt>> m_pendingReadReceipts;

    // Messages received in this session, and the order they came in, oldest first, so that
    // only so many are kept
    using ShownMessageKey = std::pair<ChatId, MessageId>;
    std::map<ShownMessageKey, ShownMessage> m_shownMessages;
    std::deque<ShownMessageKey>             m_shownMessageOrder;
    std::vector<PendingEdit>                m_pendingEdits;
};

#endif
