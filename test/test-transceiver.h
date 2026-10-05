#ifndef _TEST_TRANSCEIVER_H
#define _TEST_TRANSCEIVER_H

#include "transceiver.h"
#include <queue>
#include <vector>

class TestTransceiver: public ITransceiverBackend {
public:
    void  send(td::Client::Request &&request) override;
    guint addTimeout(guint interval, GSourceFunc function, gpointer data) override;
    void  cancelTimer(guint id) override;
    void  runTimeouts();

    // Check that given requests, and no others, have been received, and clear the queue
    uint64_t verifyRequest(const td::td_api::Function &request);
    std::vector<uint64_t> verifyRequests(std::initializer_list<td::td_api::object_ptr<td::td_api::Function>> requests);
    void verifyRequests(const std::vector<const td::td_api::Function *> requests);
    void verifyNoRequests();

    void update(td::td_api::object_ptr<td::td_api::Object> object);

    // Replies to the first non-replied request from the last verifyRequest(s) batch, or fails the
    // test case if there is no such request
    void reply(td::td_api::object_ptr<td::td_api::Object> object);
    void reply(uint64_t requestId, td::td_api::object_ptr<td::td_api::Object> object);

    const std::string &getInputPhotoPath(unsigned index) { return m_inputPhotoPaths.at(index); }
private:
    struct TimerInfo {
        guint       id;
        GSourceFunc function;
        gpointer    data;
    };

    std::queue<td::Client::Request> m_requests;
    std::vector<uint64_t>           m_lastRequestIds;
    uint64_t                        expectedRequestId = 1;
    std::vector<std::string>        m_inputPhotoPaths;
    std::vector<TimerInfo>          m_timers;
    guint                           m_nextTimerId = 1;

    void verifyRequestImpl(const td::td_api::Function &request);
};

// Put the following functions in td::td_api namespace so that tdlib types can be used without
// namespace in function prototypes, while avoiding "using namespace..." in this header.
namespace td {
namespace td_api {

object_ptr<user> makeUser(std::int32_t id_, std::string const &first_name_,
                          std::string const &last_name_,
                          std::string const &phone_number_,
                          object_ptr<UserStatus> &&status_);

object_ptr<chat> makeChat(std::int64_t id_,
                          object_ptr<ChatType> &&type_,
                          std::string const &title_,
                          object_ptr<message> &&last_message_,
                          std::int32_t unread_count_,
                          std::int64_t last_read_inbox_message_id_,
                          std::int64_t last_read_outbox_message_id_);

object_ptr<updateChatPosition> makeUpdateChatListMain(int64_t chatId);
object_ptr<updateChatPosition> makeUpdateChatList(int64_t chatId, object_ptr<ChatList> &&chatList);
object_ptr<updateChatPosition> makeUpdateRemoveFromChatList(int64_t chatId, object_ptr<ChatList> &&removeFrom);
object_ptr<loadChats> getChatsRequest();
object_ptr<setTdlibParameters> makeTdlibParameters(const std::string &databaseDirectory,
                                                   bool useSecretChats);
object_ptr<MessageReplyTo> makeReplyTo(std::int64_t message_id_);
object_ptr<photoSize> makePhotoSize(object_ptr<file> &&photo, unsigned width, unsigned height);
object_ptr<sticker> makeSticker(std::int32_t width_, std::int32_t height_,
                                const std::string &emoji_, object_ptr<file> &&sticker_,
                                object_ptr<thumbnail> &&thumbnail_ = nullptr);
object_ptr<supergroup> makeSupergroup(std::int64_t id_, object_ptr<ChatMemberStatus> &&status_,
                                      std::int32_t member_count_);
object_ptr<Object> getChatsNoChatsResponse();

object_ptr<message> makeMessage(std::int64_t id_, std::int32_t sender_user_id_, std::int64_t chat_id_,
                                bool is_outgoing_, std::int32_t date_, object_ptr<MessageContent> &&content_);

object_ptr<messageText> makeTextMessage(const std::string &text);

object_ptr<photo> makePhotoRemote(int32_t fileId, unsigned size, unsigned width, unsigned height);
object_ptr<photo> makePhotoLocal(int32_t fileId, unsigned size, const std::string &path,
                                 unsigned width, unsigned height);
object_ptr<photo> makePhotoUploading(int32_t fileId, unsigned size, unsigned uploaded, const std::string &path,
                                     unsigned width, unsigned height);
object_ptr<chatMember> makeChatMember(int32_t userId, int32_t inviteUserId, time_t joinTime,
                                      object_ptr<ChatMemberStatus> &&memberStatus, const void *);
object_ptr<createChatInviteLink> makeInviteLinkRequest(int64_t chatId);
object_ptr<chatInviteLink> makeChatInviteLink(const std::string &link);

// Requests and replies whose shape depends on the TDLib version (see buildopt.h).
object_ptr<importContacts> makeImportContacts(const std::string &phoneNumber);
object_ptr<addContact> makeAddContact(int64_t userId, const std::string &phoneNumber,
                                      const std::string &firstName, const std::string &lastName);
object_ptr<addProxy> makeAddProxy(const std::string &server, int32_t port,
                                  object_ptr<ProxyType> &&type);
object_ptr<Object> makeAddedProxy(int32_t id, bool isEnabled);
object_ptr<Object> makeAddedProxies(std::initializer_list<std::pair<int32_t, bool>> idsAndEnabled);

// These take what the constructors of TDLib 1.8.50 or so took, in that order, and set fields by
// name: TDLib keeps inserting fields, which breaks every positional constructor call.
object_ptr<messagePhoto> makeMessagePhoto(object_ptr<photo> &&photo_, object_ptr<formattedText> &&caption_,
                                          bool show_caption_above_media_, bool has_spoiler_, bool is_secret_);
object_ptr<messageVideo> makeMessageVideo(object_ptr<video> &&video_,
                                          std::vector<object_ptr<alternativeVideo>> &&alternative_videos_,
                                          object_ptr<photo> &&cover_, std::int32_t start_timestamp_,
                                          object_ptr<formattedText> &&caption_,
                                          bool show_caption_above_media_, bool has_spoiler_, bool is_secret_);
object_ptr<messageCall> makeMessageCall(bool is_video_, object_ptr<CallDiscardReason> &&discard_reason_,
                                        std::int32_t duration_);
object_ptr<chatMemberStatusCreator> makeChatMemberStatusCreator(std::string const &custom_title_,
                                                                bool is_anonymous_, bool is_member_);
// message_thread_id_ is always 0 here, as no test sends into a thread; TDLib has since replaced it.
object_ptr<sendMessage> makeSendMessage(std::int64_t chat_id_, std::int64_t message_thread_id_,
                                        object_ptr<InputMessageReplyTo> &&reply_to_,
                                        object_ptr<messageSendOptions> &&options_,
                                        object_ptr<ReplyMarkup> &&reply_markup_,
                                        object_ptr<InputMessageContent> &&input_message_content_);
object_ptr<inputMessagePhoto> makeInputMessagePhoto(object_ptr<InputFile> &&photo_,
                                                    object_ptr<inputThumbnail> &&thumbnail_,
                                                    std::vector<std::int32_t> &&added_sticker_file_ids_,
                                                    std::int32_t width_, std::int32_t height_,
                                                    object_ptr<formattedText> &&caption_,
                                                    bool show_caption_above_media_,
                                                    object_ptr<MessageSelfDestructType> &&self_destruct_type_,
                                                    bool has_spoiler_);
object_ptr<inputMessageDocument> makeInputMessageDocument(object_ptr<InputFile> &&document_,
                                                          object_ptr<inputThumbnail> &&thumbnail_,
                                                          bool disable_content_type_detection_,
                                                          object_ptr<formattedText> &&caption_);
}
}

#endif
