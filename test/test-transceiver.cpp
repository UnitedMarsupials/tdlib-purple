#include "test-transceiver.h"
#include "printout.h"
#include "buildopt.h"
#include <gtest/gtest.h>
#include <algorithm>

using namespace td::td_api;

void TestTransceiver::send(td::Client::Request &&request)
{
    ASSERT_EQ(expectedRequestId, request.id);
    expectedRequestId++;
    std::cout << "Received: " << requestToString(*request.function) << std::endl;
    m_requests.push(std::move(request));
}

uint64_t TestTransceiver::verifyRequest(const Function &request)
{
    m_lastRequestIds.clear();
    verifyRequestImpl(request);
    if (!m_requests.empty()) {
        m_lastRequestIds.push_back(m_requests.front().id);
        m_requests.pop();
    }
    verifyNoRequests();
    return m_lastRequestIds.empty() ? 0 : m_lastRequestIds.back();
}

std::vector<uint64_t> TestTransceiver::verifyRequests(std::initializer_list<td::td_api::object_ptr<td::td_api::Function>> requests)
{
    m_lastRequestIds.clear();
    for (auto &pReq: requests) {
        verifyRequestImpl(*pReq);
        if (!m_requests.empty()) {
            m_lastRequestIds.push_back(m_requests.front().id);
            m_requests.pop();
        }
    }

    verifyNoRequests();
    return m_lastRequestIds;
}

void TestTransceiver::verifyRequests(const std::vector<const td::td_api::Function *> requests)
{
    m_lastRequestIds.clear();
    for (auto pReq: requests) {
        verifyRequestImpl(*pReq);
        if (!m_requests.empty()) {
            m_lastRequestIds.push_back(m_requests.front().id);
            m_requests.pop();
        }
    }
    verifyNoRequests();
}

guint TestTransceiver::addTimeout(guint interval, GSourceFunc function, gpointer data)
{
    m_timers.emplace_back();
    m_timers.back().id = m_nextTimerId;
    m_timers.back().function = function;
    m_timers.back().data = data;

    return m_nextTimerId++;
}

void TestTransceiver::cancelTimer(guint id)
{
    m_timers.erase(std::remove_if(m_timers.begin(), m_timers.end(),
                                 [id](const TimerInfo &timer) { return (timer.id == id); }),
                   m_timers.end());
}

void TestTransceiver::runTimeouts()
{
    std::cout << "Waiting for all timeouts\n";
    for (const TimerInfo &timer: m_timers)
        while (timer.function(timer.data)) ;

    m_timers.clear();
}

#define COMPARE(param) ASSERT_EQ(expected.param, actual.param)

// As of TDLib 1.8 the parameters live directly on the request rather than in a nested
// tdlibParameters object, and enable_storage_optimizer_ is gone altogether.
static void compare(const setTdlibParameters &actual, const setTdlibParameters &expected)
{
    COMPARE(database_directory_);
    COMPARE(use_secret_chats_);
}

static void compare(const setAuthenticationPhoneNumber &actual, const setAuthenticationPhoneNumber &expected)
{
    COMPARE(phone_number_);
    COMPARE(settings_ != nullptr);
}

static void compare(const getChats &actual, const getChats &expected)
{
    COMPARE(chat_list_ != nullptr);
    if (expected.chat_list_) {
        COMPARE(chat_list_->get_id());
    }
    COMPARE(limit_);
}

static void compare(const loadChats &actual, const loadChats &expected)
{
    COMPARE(chat_list_->get_id());
    COMPARE(limit_);
}

static void compare(const viewMessages &actual, const viewMessages &expected)
{
    COMPARE(chat_id_);
    COMPARE(message_ids_.size());
    for (size_t i = 0; i < actual.message_ids_.size(); i++)
        COMPARE(message_ids_[i]);
    COMPARE(force_read_);
}

static void compare(const downloadFile &actual, const downloadFile &expected)
{
    COMPARE(file_id_);
    COMPARE(priority_);
    COMPARE(offset_);
    COMPARE(limit_);
    COMPARE(synchronous_);
}

static void compare(const object_ptr<messageSendOptions> &actual, const object_ptr<messageSendOptions> &expected)
{
    ASSERT_EQ(nullptr, actual) << "not supported";
    ASSERT_EQ(nullptr, expected) << "not supported";
}

static void compare(const object_ptr<ReplyMarkup> &actual, const object_ptr<ReplyMarkup> &expected)
{
    ASSERT_EQ(nullptr, actual) << "not supported";
    ASSERT_EQ(nullptr, expected) << "not supported";
}

static void compare(const object_ptr<formattedText> &actual, const object_ptr<formattedText> &expected)
{
    ASSERT_EQ(expected != nullptr, actual != nullptr);
    if (!actual) return;

    ASSERT_EQ(expected->text_, actual->text_);
    ASSERT_TRUE(actual->entities_.empty()) << "not supported";
    ASSERT_TRUE(expected->entities_.empty()) << "not supported";
}

static void compare(const inputMessageText &actual,
                    const inputMessageText &expected)
{
    compare(actual.text_, expected.text_);
    // disable_web_page_preview_ became the linkPreviewOptions object link_preview_options_,
    // which the plugin never sets; assert that it stays that way.
    COMPARE(link_preview_options_ != nullptr);
    COMPARE(clear_draft_);
}

static void compare(const object_ptr<InputFile> &actual, const object_ptr<InputFile> &expected)
{
    ASSERT_EQ(expected != nullptr, actual != nullptr);
    if (!actual) return;
    ASSERT_EQ(expected->get_id(), actual->get_id());

    switch (actual->get_id()) {
        case td::td_api::inputFileLocal::ID:
            ASSERT_EQ(static_cast<const inputFileLocal &>(*expected).path_,
                      static_cast<const inputFileLocal &>(*actual).path_);
            break;
        case td::td_api::inputFileId::ID:
            ASSERT_EQ(static_cast<const inputFileId &>(*expected).id_,
                      static_cast<const inputFileId &>(*actual).id_);
            break;
        default:
            ASSERT_TRUE(false) << "not supported";
    }
}

static void compare(const inputMessageDocument &actual,
                    const inputMessageDocument &expected)
{
    compare(actual.document_, expected.document_);
    ASSERT_EQ(nullptr, expected.thumbnail_) << "not supported";
    ASSERT_EQ(nullptr, actual.thumbnail_) << "not supported";
    compare(actual.caption_, expected.caption_);
}

static void compare(const inputMessagePhoto &actual, const inputMessagePhoto &expected,
                    std::vector<std::string> &m_inputPhotoPaths)
{
    ASSERT_EQ(nullptr, expected.thumbnail_) << "not supported";
    ASSERT_EQ(nullptr, actual.thumbnail_) << "not supported";
    COMPARE(added_sticker_file_ids_.size());
    for (unsigned i = 0; i < actual.added_sticker_file_ids_.size(); i++)
        COMPARE(added_sticker_file_ids_[i]);
    COMPARE(width_);
    COMPARE(height_);
    compare(actual.caption_, expected.caption_);
    // ttl_ became the MessageSelfDestructType object self_destruct_type_
    COMPARE(self_destruct_type_ != nullptr);

    COMPARE(photo_ != nullptr);
    if (actual.photo_) {
        COMPARE(photo_->get_id());
        if (actual.photo_->get_id() == inputFileLocal::ID)
            m_inputPhotoPaths.push_back(static_cast<const inputFileLocal &>(*actual.photo_).path_);
    }
}

static void compare(const object_ptr<InputMessageContent> &actual,
                    const object_ptr<InputMessageContent> &expected,
                    std::vector<std::string> &m_inputPhotoPaths)
{
    ASSERT_EQ(expected != nullptr, actual != nullptr);
    if (!actual) return;

    ASSERT_EQ(expected->get_id(), actual->get_id());
    switch (actual->get_id()) {
        case inputMessageText::ID:
            compare(static_cast<const inputMessageText &>(*actual), static_cast<const inputMessageText &>(*expected));
            break;
        case inputMessagePhoto::ID:
            compare(static_cast<const inputMessagePhoto &>(*actual), static_cast<const inputMessagePhoto &>(*expected),
                    m_inputPhotoPaths);
            break;
        case inputMessageDocument::ID:
            compare(static_cast<const inputMessageDocument &>(*actual), static_cast<const inputMessageDocument &>(*expected));
            break;
        default:
            ASSERT_TRUE(false) << "Unsupported input message content";
    }
}

static void compare(const sendMessage &actual, const sendMessage &expected,
                    std::vector<std::string> &m_inputPhotoPaths)
{
    COMPARE(chat_id_);
    // reply_to_message_id_ became the InputMessageReplyTo object reply_to_, which the plugin
    // does not set when sending
    COMPARE(reply_to_ != nullptr);

    compare(actual.options_,               expected.options_);
    compare(actual.reply_markup_,          expected.reply_markup_);
    compare(actual.input_message_content_, expected.input_message_content_, m_inputPhotoPaths);
}

static void compare(const getBasicGroupFullInfo &actual, const getBasicGroupFullInfo &expected)
{
    COMPARE(basic_group_id_);
}

static void compare(const joinChatByInviteLink &actual, const joinChatByInviteLink &expected)
{
    COMPARE(invite_link_);
}

static void compare(const contact &actual, const contact &expected)
{
    COMPARE(phone_number_);
    COMPARE(first_name_);
    COMPARE(last_name_);
    COMPARE(vcard_);
    COMPARE(user_id_);
}

static void compare(const importContacts &actual, const importContacts &expected)
{
    COMPARE(contacts_.size());

    for (size_t i = 0; i < actual.contacts_.size(); i++)
        compare(*actual.contacts_[i], *expected.contacts_[i]);
}

static void compare(const addContact &actual, const addContact &expected)
{
    compare(*actual.contact_, *expected.contact_);
    COMPARE(share_phone_number_);
}

static void compare(const createPrivateChat &actual, const createPrivateChat &expected)
{
    COMPARE(user_id_);
    COMPARE(force_);
}

static void compare(const checkAuthenticationCode &actual, const checkAuthenticationCode &expected)
{
    COMPARE(code_);
}

static void compare(const registerUser &actual, const registerUser &expected)
{
    COMPARE(first_name_);
    COMPARE(last_name_);
}

static void compare(const getMessage &actual, const getMessage &expected)
{
    COMPARE(chat_id_);
    COMPARE(message_id_);
}

static void compare(const sendChatAction &actual, const sendChatAction &expected)
{
    COMPARE(chat_id_);
    COMPARE(action_ != nullptr);
    if (actual.action_) {
        COMPARE(action_->get_id());
    }
}

static void compare(const proxyTypeHttp &actual, const proxyTypeHttp &expected)
{
    COMPARE(username_);
    COMPARE(password_);
    COMPARE(http_only_);
}

static void compare(const proxyTypeSocks5 &actual, const proxyTypeSocks5 &expected)
{
    COMPARE(username_);
    COMPARE(password_);
}

static void compare(const addProxy &actual, const addProxy &expected)
{
    COMPARE(server_);
    COMPARE(port_);
    COMPARE(enable_);
    COMPARE(type_ != nullptr);
    if (actual.type_ != nullptr) {
        COMPARE(type_->get_id());
        switch (actual.type_->get_id()) {
            case proxyTypeHttp::ID:
                compare(static_cast<const proxyTypeHttp &>(*actual.type_),
                        static_cast<const proxyTypeHttp &>(*expected.type_));
                break;
            case proxyTypeSocks5::ID:
                compare(static_cast<const proxyTypeSocks5 &>(*actual.type_),
                        static_cast<const proxyTypeSocks5 &>(*expected.type_));
                break;
            default:
                ASSERT_TRUE(false) << "Unsupported proxy type";
        }
    }
}

static void compare(const removeProxy &actual, const removeProxy &expected)
{
    COMPARE(proxy_id_);
}

static void compare(const deleteChatHistory &actual, const deleteChatHistory &expected)
{
    COMPARE(chat_id_);
    COMPARE(remove_from_chat_list_);
    COMPARE(revoke_);
}

static void compare(const removeContacts &actual, const removeContacts &expected)
{
    COMPARE(user_ids_.size());
    for (unsigned i = 0; i < actual.user_ids_.size(); i++)
        COMPARE(user_ids_[i]);
}

static void compare(const leaveChat &actual, const leaveChat &expected)
{
    COMPARE(chat_id_);
}

static void compare(const deleteChat &actual, const deleteChat &expected)
{
    COMPARE(chat_id_);
}

static void compare(const checkAuthenticationPassword &actual, const checkAuthenticationPassword &expected)
{
    COMPARE(password_);
}

static void compare(const preliminaryUploadFile &actual, const preliminaryUploadFile &expected)
{
    compare(actual.file_, expected.file_);

    COMPARE(file_type_ != nullptr);
    if (actual.file_type_) {
        COMPARE(file_type_->get_id());
    }

    COMPARE(priority_);
}

static void compare(const closeSecretChat &actual, const closeSecretChat &expected)
{
    COMPARE(secret_chat_id_);
}

static void compare(const getSupergroupFullInfo &actual, const getSupergroupFullInfo &expected)
{
    COMPARE(supergroup_id_);
}

static void compare(const cancelDownloadFile &actual, const cancelDownloadFile &expected)
{
    COMPARE(file_id_);
    COMPARE(only_if_pending_);
}

static void compare(const messageSenderUser &actual, const messageSenderUser &expected)
{
    COMPARE(user_id_);
}

static void compare(const setChatMemberStatus &actual, const setChatMemberStatus &expected)
{
    COMPARE(chat_id_);
    COMPARE(member_id_->get_id());
    switch (actual.member_id_->get_id()) {
        case messageSenderUser::ID:
            compare(static_cast<const messageSenderUser &>(*actual.member_id_),
                    static_cast<const messageSenderUser &>(*expected.member_id_));
            break;
    }
    COMPARE(status_ != nullptr);
    if (actual.status_) {
        COMPARE(status_->get_id());
    }
}

static void compare(const addChatMember &actual, const addChatMember &expected)
{
    COMPARE(chat_id_);
    COMPARE(user_id_);
    COMPARE(forward_limit_);
}

static void compare(const createChatInviteLink &actual, const createChatInviteLink &expected)
{
    COMPARE(chat_id_);
}

static void compare(const getSupergroupMembers &actual, const getSupergroupMembers &expected)
{
    COMPARE(supergroup_id_);
    COMPARE(filter_ != nullptr);
    if (actual.filter_) {
        COMPARE(filter_->get_id());
    }
    COMPARE(offset_);
    COMPARE(limit_);
}

static void compare(const searchPublicChat &actual, const searchPublicChat &expected)
{
    COMPARE(username_);
}

static void compare(const joinChat &actual, const joinChat &expected)
{
    COMPARE(chat_id_);
}

static void compare(const createNewSecretChat &actual, const createNewSecretChat &expected)
{
    COMPARE(user_id_);
}

static void compare(const getChatHistory &actual, const getChatHistory &expected)
{
    COMPARE(chat_id_);
    COMPARE(from_message_id_);
    COMPARE(offset_);
    COMPARE(limit_);
    COMPARE(only_local_);
}

static void compareRequests(const Function &actual, const Function &expected,
                            std::vector<std::string> &m_inputPhotoPaths)
{
    ASSERT_EQ(expected.get_id(), actual.get_id()) << "Wrong request type: got " <<
        requestToString(actual) << " expected " << requestToString(expected);

#define C(class) case class::ID: \
    compare(static_cast<const class &>(actual), static_cast<const class &>(expected)); \
    break;

    switch (actual.get_id()) {
        C(setTdlibParameters)
        C(setAuthenticationPhoneNumber)
        case getContacts::ID: break;
        C(getChats)
        C(loadChats)
        C(viewMessages)
        C(downloadFile)
        case sendMessage::ID:
            compare(static_cast<const sendMessage &>(actual), static_cast<const sendMessage &>(expected),
                    m_inputPhotoPaths);
            break;
        C(getBasicGroupFullInfo)
        C(joinChatByInviteLink)
        C(importContacts)
        C(addContact)
        C(createPrivateChat)
        C(checkAuthenticationCode)
        C(registerUser)
        C(getMessage)
        C(sendChatAction)
        C(addProxy)
        case disableProxy::ID: break; // no data fields
        case getProxies::ID: break; // no data fields
        C(removeProxy)
        C(deleteChatHistory)
        C(removeContacts)
        C(leaveChat)
        C(deleteChat)
        C(checkAuthenticationPassword)
        C(preliminaryUploadFile)
        C(closeSecretChat)
        C(getSupergroupFullInfo)
        C(cancelDownloadFile)
        C(setChatMemberStatus)
        C(addChatMember)
        C(createChatInviteLink)
        C(getSupergroupMembers)
        C(searchPublicChat)
        C(joinChat)
        C(createNewSecretChat)
        C(getChatHistory)
        default: ASSERT_TRUE(false) << "Unsupported request " << requestToString(actual);
    }
}

void TestTransceiver::verifyRequestImpl(const Function &request)
{
    ASSERT_FALSE(m_requests.empty()) << "Missing request: expected " << requestToString(request);

    std::cout << "Received request " << m_requests.front().id << ": " << requestToString(*m_requests.front().function) << "\n";
    compareRequests(*m_requests.front().function, request, m_inputPhotoPaths);
}

void TestTransceiver::verifyNoRequests()
{
    ASSERT_TRUE(m_requests.empty()) << "Unexpected request: " << requestToString(*m_requests.front().function);
}

void TestTransceiver::update(object_ptr<Object> object)
{
    std::cout << "Sending update: " << responseToString(*object) << "\n";
    receive({0, std::move(object)});
}

void TestTransceiver::reply(object_ptr<Object> object)
{
    ASSERT_FALSE(m_lastRequestIds.empty()) << "No requests to reply to";
    reply(m_lastRequestIds.front(), std::move(object));
    m_lastRequestIds.erase(m_lastRequestIds.begin());
}

void TestTransceiver::reply(uint64_t requestId, td::td_api::object_ptr<td::td_api::Object> object)
{
    std::cout << "Replying to request " << requestId << ": " << responseToString(*object) << "\n";
    receive({requestId, std::move(object)});
}

namespace td {
namespace td_api {

// These used to pass every field of the object positionally, and so had to be rewritten -- and
// grown #if TDLIB_VERSION_NUMBER arms -- each time TDLib inserted a field. TDLib inserts fields
// constantly: `user` and `chat` have each gained some ten since this suite was written. Setting
// only the fields a test actually looks at, and letting the rest default-initialise, is what
// keeps these compiling as the API grows.
object_ptr<user> makeUser(std::int32_t id_, std::string const &first_name_,
                          std::string const &last_name_,
                          std::string const &phone_number_,
                          object_ptr<UserStatus> &&status_)
{
    auto result = make_object<user>();

    result->id_           = id_;
    result->first_name_   = first_name_;
    result->last_name_    = last_name_;
    result->phone_number_ = phone_number_;
    result->status_       = std::move(status_);
    result->have_access_  = true;
    result->type_         = make_object<userTypeRegular>();

    return result;
}

object_ptr<chat> makeChat(std::int64_t id_,
                          object_ptr<ChatType> &&type_,
                          std::string const &title_,
                          object_ptr<message> &&last_message_,
                          std::int32_t unread_count_,
                          std::int64_t last_read_inbox_message_id_,
                          std::int64_t last_read_outbox_message_id_)
{
    auto result = make_object<chat>();

    result->id_                         = id_;
    result->type_                       = std::move(type_);
    result->title_                      = title_;
    result->permissions_                = make_object<chatPermissions>();
    result->last_message_               = std::move(last_message_);
    result->is_marked_as_unread_        = (unread_count_ > 0);
    result->unread_count_               = unread_count_;
    result->last_read_inbox_message_id_ = last_read_inbox_message_id_;
    result->last_read_outbox_message_id_= last_read_outbox_message_id_;
    result->notification_settings_      = make_object<chatNotificationSettings>();

    return result;
}

object_ptr<updateChatPosition> makeUpdateChatListMain(int64_t chatId)
{
    return makeUpdateChatList(chatId, make_object<chatListMain>());
}

object_ptr<updateChatPosition> makeUpdateChatList(int64_t chatId, object_ptr<ChatList> &&chatList)
{
    return make_object<updateChatPosition>(
        chatId,
        make_object<chatPosition>(std::move(chatList), 1, false, nullptr)
    );
}

object_ptr<updateChatPosition> makeUpdateRemoveFromChatList(int64_t chatId, object_ptr<ChatList> &&removeFrom)
{
    return make_object<updateChatPosition>(
        chatId,
        make_object<chatPosition>(std::move(removeFrom), 0, false, nullptr)
    );
}

object_ptr<loadChats> getChatsRequest()
{
    return make_object<loadChats>(make_object<chatListMain>(), 200);
}

// message.reply_to_message_id_ became reply_to_, a MessageReplyTo object; getReplyMessageId()
// in identifiers.cpp reads the message_id_ out of its messageReplyToMessage form.
object_ptr<MessageReplyTo> makeReplyTo(std::int64_t message_id_)
{
    auto result = make_object<messageReplyToMessage>();

    result->message_id_ = message_id_;

    return result;
}

object_ptr<sticker> makeSticker(std::int32_t width_, std::int32_t height_,
                                const std::string &emoji_, object_ptr<file> &&sticker_,
                                object_ptr<thumbnail> &&thumbnail_)
{
    auto result = make_object<sticker>();

    result->width_   = width_;
    result->height_  = height_;
    result->emoji_   = emoji_;
    result->sticker_   = std::move(sticker_);
    result->thumbnail_ = std::move(thumbnail_);

    return result;
}

// supergroup's username became a usernames object, and it has gained boost_level_ and a dozen
// flags besides; only what the tests assert on is set here.
object_ptr<supergroup> makeSupergroup(std::int64_t id_, object_ptr<ChatMemberStatus> &&status_,
                                      std::int32_t member_count_)
{
    auto result = make_object<supergroup>();

    result->id_           = id_;
    result->status_       = std::move(status_);
    result->member_count_ = member_count_;

    return result;
}

// Only the fields compare(setTdlibParameters) looks at; see the note on makeUser above.
object_ptr<setTdlibParameters> makeTdlibParameters(const std::string &databaseDirectory,
                                                   bool useSecretChats)
{
    auto result = make_object<setTdlibParameters>();

    result->database_directory_ = databaseDirectory;
    result->use_secret_chats_   = useSecretChats;

    return result;
}

object_ptr<Object> getChatsNoChatsResponse()
{
    return make_object<error>(404, "No more chats");
}

object_ptr<message> makeMessage(std::int64_t id_, std::int32_t sender_user_id_, std::int64_t chat_id_,
                                bool is_outgoing_, std::int32_t date_, object_ptr<MessageContent> &&content_)
{
    auto result = make_object<message>();

    result->id_            = id_;
    // The sender is a MessageSender object now, not a bare user id
    result->sender_id_     = make_object<messageSenderUser>(sender_user_id_);
    result->chat_id_       = chat_id_;
    result->sending_state_ = is_outgoing_ ? make_object<messageSendingStatePending>() : nullptr;
    result->is_outgoing_   = is_outgoing_;
    result->can_be_saved_  = true;
    result->date_          = date_;
    result->content_       = std::move(content_);

    return result;
}

// photoSize has gained progressive_sizes_, and is built identically by each of the helpers below
object_ptr<photoSize> makePhotoSize(object_ptr<file> &&photo, unsigned width, unsigned height)
{
    auto result = make_object<photoSize>();

    result->type_   = "whatever";
    result->photo_  = std::move(photo);
    result->width_  = width;
    result->height_ = height;

    return result;
}

object_ptr<messageText> makeTextMessage(const std::string &text)
{
    auto result = make_object<messageText>();

    result->text_ = make_object<formattedText>(text, std::vector<object_ptr<textEntity>>());

    return result;
}

object_ptr<photo> makePhotoRemote(int32_t fileId, unsigned size, unsigned width, unsigned height)
{
    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, size, size,
            make_object<localFile>("", true, true, false, false, 0, 0, 0),
            make_object<remoteFile>("beh", "bleh", false, true, size)
        ), width, height));
    return make_object<photo>(false, nullptr, std::move(sizes));
}

object_ptr<photo> makePhotoLocal(int32_t fileId, unsigned size, const std::string &path,
                                 unsigned width, unsigned height)
{
    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, size, size,
            make_object<localFile>(path, true, true, false, true, 0, size, size),
            make_object<remoteFile>("beh", "bleh", false, true, size)
        ), width, height));
    return make_object<photo>(false, nullptr, std::move(sizes));
}

object_ptr<photo> makePhotoUploading(int32_t fileId, unsigned size, unsigned uploaded, const std::string &path,
                                     unsigned width, unsigned height)
{
    EXPECT_TRUE(uploaded < size);

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, size, size,
            make_object<localFile>(path, true, true, false, true, 0, size, size),
            make_object<remoteFile>("beh", "bleh", false, false, uploaded)
        ), width, height));
    return make_object<photo>(false, nullptr, std::move(sizes));
}

object_ptr<chatMember> makeChatMember(int32_t userId, int32_t inviteUserId, time_t joinTime,
                                      object_ptr<ChatMemberStatus> &&memberStatus, const void *)
{
    return make_object<chatMember>(make_object<messageSenderUser>(userId),
                                   inviteUserId, joinTime, std::move(memberStatus));
}

object_ptr<createChatInviteLink> makeInviteLinkRequest(int64_t chatId)
{
    auto result = make_object<createChatInviteLink>();
    result->chat_id_ = chatId;
    return result;
}

object_ptr<chatInviteLink> makeChatInviteLink(const std::string &link)
{
    auto result = make_object<chatInviteLink>();
    result->invite_link_ = link;
    return result;
}

}
}
