#include "fixture.h"
#include "libpurple-mock.h"
#include "tdlib-purple.h"
#include "client-utils.h"
#include "buildopt.h"
#include "td-client.h"

// Files linked to by their messages, which the plugin fetches and opens when clicked, as it does
// where the front-end lets it handle a scheme of its own: in Pidgin
class MessageLinkTest: public CommTest {
protected:
    const int64_t messageId = 1;
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;

    void SetUp() override
    {
        CommTest::SetUp();
        setMessageLinksEnabled(true);
    }
    void TearDown() override
    {
        setMessageLinksEnabled(false);
        CommTest::TearDown();
    }

    object_ptr<message> makeDocumentMessage(int32_t size, const std::string &path, bool downloaded);
    std::string         link() { return "tdlib://%2B" + selfPhoneNumber + "/" + std::to_string(chatIds[0]) +
                                        "/" + std::to_string(messageId); }
    object_ptr<file>    makeFile(int32_t size, const std::string &path, bool downloaded);
};

object_ptr<file> MessageLinkTest::makeFile(int32_t size, const std::string &path, bool downloaded)
{
    return ::makeFile(fileId, size, downloaded ? path : "");
}

object_ptr<message> MessageLinkTest::makeDocumentMessage(int32_t size, const std::string &path, bool downloaded)
{
    return makeMessage(
        messageId, userIds[0], chatIds[0], false, date,
        makeMessageDocument("video.mp4", "video/mp4", makeFile(size, path, downloaded), "caption")
    );
}

TEST_F(MessageLinkTest, ShownAtOnce)
{
    loginWithOneContact();

    // Fetched in the background, but nothing waits for it
    tgl.update(make_object<updateNewMessage>(makeDocumentMessage(10000, "", false)));
    tgl.verifyRequests({
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, messageId), nullptr, true),
        make_object<downloadFile>(fileId, 1, 0, 0, false)
    });
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<a href=\"" + link() + "\">video.mp4 [video/mp4]</a>\ncaption",
        PURPLE_MESSAGE_RECV, date
    ));
}

TEST_F(MessageLinkTest, AboveLimit)
{
    purple_account_set_string(account, "media-size-threshold", "0.5");
    loginWithOneContact();

    // Fetched only when clicked; nobody is asked beforehand
    tgl.update(make_object<updateNewMessage>(makeDocumentMessage(655360, "", false)));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<a href=\"" + link() + "\">video.mp4 [video/mp4]</a>\ncaption",
        PURPLE_MESSAGE_RECV, date
    ));
}

TEST_F(MessageLinkTest, OpenDownloaded)
{
    loginWithOneContact();
    tgl.update(make_object<updateNewMessage>(makeDocumentMessage(10000, "/path/video.mp4", true)));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<a href=\"" + link() + "\">video.mp4 [video/mp4]</a>\ncaption",
        PURPLE_MESSAGE_RECV, date
    ));

    tgprpl_open_message_link(link().c_str());
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeDocumentMessage(10000, "/path/video.mp4", true));
    prpl.verifyEvents(NotifyUriEvent("file:///path/video.mp4"));

    // Clicked again while the player is still starting
    tgprpl_open_message_link(link().c_str());
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();
}

TEST_F(MessageLinkTest, OpenNotDownloaded)
{
    purple_account_set_string(account, "media-size-threshold", "0.5");
    loginWithOneContact();
    tgl.update(make_object<updateNewMessage>(makeDocumentMessage(655360, "", false)));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<a href=\"" + link() + "\">video.mp4 [video/mp4]</a>\ncaption",
        PURPLE_MESSAGE_RECV, date
    ));

    tgprpl_open_message_link(link().c_str());
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeDocumentMessage(655360, "", false));
    tgl.verifyRequest(downloadFile(fileId, 32, 0, 0, true));
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0), "Downloading video.mp4 [video/mp4]",
        PURPLE_MESSAGE_SYSTEM, 0
    ));

    // Clicked again while it downloads
    tgprpl_open_message_link(link().c_str());
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();

    tgl.reply(makeFile(655360, "/path/video.mp4", true));
    prpl.verifyEvents(NotifyUriEvent("file:///path/video.mp4"));
}

TEST_F(MessageLinkTest, MessageGone)
{
    loginWithOneContact();

    tgprpl_open_message_link(link().c_str());
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeError(404, "Not Found"));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0), "Could not open the file: code 404 (Not Found)",
        PURPLE_MESSAGE_SYSTEM, 0
    ));

    // Nothing was opened, so clicking again tries again
    tgprpl_open_message_link(link().c_str());
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
}

TEST_F(MessageLinkTest, NotALink)
{
    loginWithOneContact();
    tgprpl_open_message_link("tdlib://only/two");
    tgprpl_open_message_link("file:///path");
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();
}

// A video Telegram has in three sizes; the largest above the default limit of 32 MB
class VideoSizesTest: public MessageLinkTest {
protected:
    const int32_t mainId   = 1300;
    const int32_t largerId = 1301;
    const int32_t smallId  = 1302;

    object_ptr<file>    makeVideoFile(int32_t id, int64_t size, const std::string &path = "");
    object_ptr<message> makeVideoMessage();
    std::string         sizeLinks();
    void                verifyVideoMessage();
};

object_ptr<file> VideoSizesTest::makeVideoFile(int32_t id, int64_t size, const std::string &path)
{
    return ::makeFile(id, size, path);
}

object_ptr<message> VideoSizesTest::makeVideoMessage()
{
    auto video = make_object<td::td_api::video>();
    video->width_     = 1920;
    video->height_    = 1080;
    video->file_name_ = "video.mp4";
    video->mime_type_ = "video/mp4";
    video->video_     = makeVideoFile(mainId, 45 << 20);
    std::vector<object_ptr<alternativeVideo>> alternatives;
    alternatives.push_back(makeAlternativeVideo(11, 1280, 720, "h265", makeVideoFile(largerId, 12 << 20)));
    alternatives.push_back(makeAlternativeVideo(12, 854, 480, "", makeVideoFile(smallId, 6 << 20)));
    return makeMessage(messageId, userIds[0], chatIds[0], false, date, makeMessageVideo(
        std::move(video), std::move(alternatives), nullptr, 0,
        makeFormattedText("caption"), false, false, false
    ));
}

// Smallest first, each a link of its own; the video as sent has the message's link
std::string VideoSizesTest::sizeLinks()
{
    return "<a href=\"" + link() + "/a12\">854×480, purple_str_size_to_units</a> · "
           "<a href=\"" + link() + "/a11\">1280×720 (h265), purple_str_size_to_units</a>";
}

void VideoSizesTest::verifyVideoMessage()
{
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<a href=\"" + link() + "\">video.mp4 [video/mp4, 1920×1080 (original), purple_str_size_to_units]</a>\n" + sizeLinks() + "\ncaption",
        PURPLE_MESSAGE_RECV, date
    ));
}

TEST_F(VideoSizesTest, LargestWithinLimit)
{
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeVideoMessage()));
    tgl.verifyRequests({
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, messageId), nullptr, true),
        make_object<downloadFile>(largerId, 1, 0, 0, false)
    });
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<a href=\"" + link() + "\">video.mp4 [video/mp4, 1920×1080 (original), purple_str_size_to_units]</a>\n" + sizeLinks() + "\ncaption",
        PURPLE_MESSAGE_RECV, date
    ));
}

TEST_F(VideoSizesTest, UnknownSize)
{
    loginWithOneContact();

    // A size not known yet may be above the limit
    object_ptr<message> msg = makeVideoMessage();
    auto &video = static_cast<messageVideo &>(*msg->content_);
    video.alternative_videos_[0]->video_->size_          = 0;
    video.alternative_videos_[0]->video_->expected_size_ = 0;
    tgl.update(make_object<updateNewMessage>(std::move(msg)));
    tgl.verifyRequests({
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, messageId), nullptr, true),
        make_object<downloadFile>(smallId, 1, 0, 0, false)
    });
    // ...whose link says no size
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<a href=\"" + link() + "\">video.mp4 [video/mp4, 1920×1080 (original), purple_str_size_to_units]</a>\n"
        "<a href=\"" + link() + "/a12\">854×480, purple_str_size_to_units</a> · "
        "<a href=\"" + link() + "/a11\">1280×720 (h265)</a>\ncaption",
        PURPLE_MESSAGE_RECV, date
    ));
}

TEST_F(VideoSizesTest, Smallest)
{
    purple_account_set_string(account, "prefetch-videos", "smallest");
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeVideoMessage()));
    tgl.verifyRequests({
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, messageId), nullptr, true),
        make_object<downloadFile>(smallId, 1, 0, 0, false)
    });
    verifyVideoMessage();
}

TEST_F(VideoSizesTest, SmallestUnknownSize)
{
    purple_account_set_string(account, "prefetch-videos", "smallest");
    loginWithOneContact();

    // The smallest of those known to be within the limit
    object_ptr<message> msg = makeVideoMessage();
    auto &video = static_cast<messageVideo &>(*msg->content_);
    video.alternative_videos_[1]->video_->size_          = 0;
    video.alternative_videos_[1]->video_->expected_size_ = 0;
    tgl.update(make_object<updateNewMessage>(std::move(msg)));
    tgl.verifyRequests({
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, messageId), nullptr, true),
        make_object<downloadFile>(largerId, 1, 0, 0, false)
    });
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<a href=\"" + link() + "\">video.mp4 [video/mp4, 1920×1080 (original), purple_str_size_to_units]</a>\n"
        "<a href=\"" + link() + "/a12\">854×480</a> · "
        "<a href=\"" + link() + "/a11\">1280×720 (h265), purple_str_size_to_units</a>\ncaption",
        PURPLE_MESSAGE_RECV, date
    ));
}

TEST_F(VideoSizesTest, LargestOnly)
{
    // Above the limit, so nothing
    purple_account_set_string(account, "prefetch-videos", "largest");
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeVideoMessage()));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    verifyVideoMessage();
}

TEST_F(VideoSizesTest, None)
{
    purple_account_set_string(account, "prefetch-videos", "none");
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeVideoMessage()));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    verifyVideoMessage();
}

TEST_F(VideoSizesTest, CoverPreview)
{
    const int32_t thumbnailId = 1310;
    const int32_t coverIds[3] = {1311, 1312, 1313};
    purple_account_set_string(account, "prefetch-videos", "none");
    loginWithOneContact();

    // The thumbnail is the first frame, often a title card; the sender's chosen cover is shown instead,
    // in the size nearest a thumbnail's
    object_ptr<message> msg = makeVideoMessage();
    auto &video = static_cast<messageVideo &>(*msg->content_);
    auto thumb = make_object<thumbnail>();
    thumb->format_ = make_object<thumbnailFormatJpeg>();
    thumb->width_  = 320;
    thumb->height_ = 180;
    thumb->file_   = ::makeFile(thumbnailId, 2000, "/thumbnail.jpg");
    video.video_->thumbnail_ = std::move(thumb);
    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(::makeFile(coverIds[0], 1000), 90, 50));
    sizes.push_back(makePhotoSize(::makeFile(coverIds[1], 20000), 320, 180));
    sizes.push_back(makePhotoSize(::makeFile(coverIds[2], 200000), 1280, 720));
    video.cover_ = makePhotoWithSizes(std::move(sizes));
    tgl.update(make_object<updateNewMessage>(std::move(msg)));
    tgl.verifyRequest(downloadFile(coverIds[1], 1, 0, 0, true));
    prpl.verifyNoEvents();

    tgl.reply(::makeFile(coverIds[1], 20000, TEST_SOURCE_DIR "/test.webp"));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "\n<a href=\"" + link() + "\"><img id=\"" + std::to_string(getLastImgstoreId()) + "\">\n"
        "video.mp4 [video/mp4, 1920×1080 (original), purple_str_size_to_units]</a>\n" + sizeLinks() + "\ncaption",
        (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES), date
    ));
    tgl.verifyNoRequests();
}

TEST_F(VideoSizesTest, OpenSize)
{
    purple_account_set_string(account, "prefetch-videos", "none");
    loginWithOneContact();
    tgl.update(make_object<updateNewMessage>(makeVideoMessage()));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    verifyVideoMessage();

    tgprpl_open_message_link((link() + "/a11").c_str());
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeVideoMessage());
    tgl.verifyRequest(downloadFile(largerId, 32, 0, 0, true));
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0), "Downloading 1280×720 (h265), purple_str_size_to_units",
        PURPLE_MESSAGE_SYSTEM, 0
    ));

    tgl.reply(makeVideoFile(largerId, 12 << 20, "/path/video-720.mp4"));
    prpl.verifyEvents(NotifyUriEvent("file:///path/video-720.mp4"));

    // Another size is another file, not a second click on this one
    tgprpl_open_message_link((link() + "/a12").c_str());
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    prpl.verifyNoEvents();
    tgl.reply(makeVideoMessage());
    tgl.verifyRequest(downloadFile(smallId, 32, 0, 0, true));
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0), "Downloading 854×480, purple_str_size_to_units",
        PURPLE_MESSAGE_SYSTEM, 0
    ));
    tgl.reply(makeVideoFile(smallId, 6 << 20, "/path/video-480.mp4"));
    prpl.verifyEvents(NotifyUriEvent("file:///path/video-480.mp4"));
}

TEST_F(MessageLinkTest, PhotoSizes)
{
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(makeFile(1000, "/path", true), 320, 240));
    sizes.back()->type_ = "m";
    // Shown is the larger, downloaded here already, so as not to wait for it
    sizes.push_back(makePhotoSize(::makeFile(fileId + 1, 50000, "/path"), 1280, 960));
    sizes.back()->type_ = "y";
    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId, userIds[0], chatIds[0], false, date,
        makeMessagePhoto(makePhotoWithSizes(std::move(sizes)), makeFormattedText("caption"), false, false, false)
    )));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<img src=\"file:///path\">\n"
        "<a href=\"" + link() + "/pm\">320×240, purple_str_size_to_units</a> · "
        "<a href=\"" + link() + "/py\">1280×960, purple_str_size_to_units</a>\ncaption",
        (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES), date
    ));
}

TEST_F(MessageLinkTest, PhotoSizesIgnored)
{
    purple_account_set_string(account, "media-size-threshold", "0.5");
    purple_account_set_string(account, "media-handling-behavior", "discard");
    loginWithOneContact();

    // Both above the limit, yet a click on either fetches it
    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(makeFile(600000, "", false), 320, 240));
    sizes.back()->type_ = "m";
    sizes.push_back(makePhotoSize(makeFile(900000, "", false), 1280, 960));
    sizes.back()->type_ = "y";
    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId, userIds[0], chatIds[0], false, date,
        makeMessagePhoto(makePhotoWithSizes(std::move(sizes)), makeFormattedText("caption"), false, false, false)
    )));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.verifyEvents(
        ServGotImEvent(connection, purpleUserName(0),
                       "<a href=\"" + link() + "/pm\">320×240, purple_str_size_to_units</a> · "
                       "<a href=\"" + link() + "/py\">1280×960, purple_str_size_to_units</a>\ncaption",
                       PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(purpleUserName(0), purpleUserName(0),
                               userFirstNames[0] + " " + userLastNames[0] +
                               ": Ignoring photo download (purple_str_size_to_units)",
                               PURPLE_MESSAGE_SYSTEM, date)
    );
    tgl.verifyNoRequests();
}

TEST_F(MessageLinkTest, MessageMenu)
{
    loginWithOneContact();
    PurpleTdClient *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    ASSERT_NE(nullptr, client);
    const ChatId    chat    = ChatId::fromString(std::to_string(chatIds[0]).c_str());
    const MessageId message = MessageId::fromString("1");

    // The message for its reactions, then what may be done to it, then whether its sender is
    // blocked, then the reactions it may have; without Pidgin, there is no menu to add them to
    client->showMessageMenu(chat, message, nullptr);
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeDocumentMessage(10000, "", false));
    tgl.verifyRequest(getMessageProperties(chatIds[0], messageId));
    tgl.reply(make_object<messageProperties>());
    tgl.verifyRequest(getUserFullInfo(userIds[0]));
    {
        auto fullInfo = make_object<userFullInfo>();
        fullInfo->block_list_ = make_object<blockListMain>();
        tgl.reply(std::move(fullInfo));
    }
    // Pidgin's list of blocked users follows
    ASSERT_EQ(1u, g_slist_length(account->deny));
    ASSERT_EQ(purpleUserName(0), static_cast<const char *>(account->deny->data));
    tgl.verifyRequest(getMessageAvailableReactions(chatIds[0], messageId, 8));
    tgl.reply(make_object<availableReactions>());
    prpl.verifyNoEvents();

    client->setMessageReaction(chat, message, "\xF0\x9F\x91\x8D", true);
    tgl.verifyRequest(addMessageReaction(chatIds[0], messageId, make_object<reactionTypeEmoji>("\xF0\x9F\x91\x8D"),
                                         false, true));
    tgl.reply(make_object<ok>());
    client->setMessageReaction(chat, message, "\xF0\x9F\x91\x8D", false);
    tgl.verifyRequest(removeMessageReaction(chatIds[0], messageId, make_object<reactionTypeEmoji>("\xF0\x9F\x91\x8D")));
    tgl.reply(make_object<ok>());
    prpl.verifyNoEvents();

    // Editing and deleting, as the menu's dialogs end
    client->editMessage(chat, message, parseFormattedText("<b>new</b> text"), false);
    {
        auto content = make_object<inputMessageText>();
        content->text_ = makeFormattedText("new text");
        content->text_->entities_.push_back(makeTextEntity(0, 3, make_object<textEntityTypeBold>()));
        tgl.verifyRequest(editMessageText(chatIds[0], messageId, nullptr, std::move(content)));
    }
    tgl.reply(makeDocumentMessage(10000, "", false));
    client->editMessage(chat, message, parseFormattedText("new caption"), true);
    tgl.verifyRequest(editMessageCaption(chatIds[0], messageId, nullptr, makeFormattedText("new caption"), false));
    tgl.reply(makeDocumentMessage(10000, "", false));
    client->deleteMessage(chat, message, true);
    tgl.verifyRequest(deleteMessages(chatIds[0], {messageId}, true));
    tgl.reply(make_object<ok>());
    prpl.verifyNoEvents();

    // A failure is told in the conversation
    client->deleteMessage(chat, message, false);
    tgl.verifyRequest(deleteMessages(chatIds[0], {messageId}, false));
    tgl.reply(make_object<error>(400, "MESSAGE_DELETE_FORBIDDEN"));
    prpl.verifyEvents(NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
                      ConversationWriteEvent(purpleUserName(0), purpleUserName(0),
                                             "Cannot delete the message: code 400 (MESSAGE_DELETE_FORBIDDEN)",
                                             PURPLE_MESSAGE_ERROR, 0));
}

TEST_F(MessageLinkTest, BlockUser)
{
    loginWithOneContact();
    PurpleTdClient *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    ASSERT_NE(nullptr, client);
    // As Telegram has it
    ASSERT_EQ(PURPLE_PRIVACY_DENY_USERS, account->perm_deny);

    // Pidgin's list told the server; a failure takes the user off it again
    purple_privacy_deny_add(account, purpleUserName(0).c_str(), TRUE);
    client->setUserBlocked(UserId::fromString(std::to_string(userIds[0]).c_str()), true);
    tgl.verifyRequest(setMessageSenderBlockList(make_object<messageSenderUser>(userIds[0]),
                                                make_object<blockListMain>()));
    tgl.reply(make_object<ok>());
    ASSERT_EQ(1u, g_slist_length(account->deny));
    client->setUserBlocked(UserId::fromString(std::to_string(userIds[0]).c_str()), false);
    tgl.verifyRequest(setMessageSenderBlockList(make_object<messageSenderUser>(userIds[0]), nullptr));
    tgl.reply(make_object<error>(400, "FAILED"));
    ASSERT_EQ(1u, g_slist_length(account->deny));
    prpl.discardEvents();

    // Another client unblocked
    auto fullInfo = make_object<userFullInfo>();
    tgl.update(make_object<updateUserFullInfo>(userIds[0], std::move(fullInfo)));
    ASSERT_EQ(0u, g_slist_length(account->deny));
}

TEST_F(MessageLinkTest, ReportMessage)
{
    loginWithOneContact();
    PurpleTdClient *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    ASSERT_NE(nullptr, client);
    const ChatId    chat    = ChatId::fromString(std::to_string(chatIds[0]).c_str());
    const MessageId message = MessageId::fromString("1");

    // The reason, chosen in a dialog, then details, then the result
    client->reportChat(chat, {message});
    tgl.verifyRequest(reportChat(chatIds[0], "", {messageId}, ""));
    {
        std::vector<object_ptr<reportOption>> options;
        options.push_back(make_object<reportOption>("spam", "Spam"));
        tgl.reply(make_object<reportChatResultOptionRequired>("Why?", std::move(options)));
    }
    client->reportChat(chat, {message}, "spam");
    tgl.verifyRequest(reportChat(chatIds[0], "spam", {messageId}, ""));
    tgl.reply(make_object<reportChatResultTextRequired>("spam2", true));
    prpl.verifyEvents(RequestInputEvent(NULL, account, NULL, NULL));
    prpl.inputEnter("Sells things");
    tgl.verifyRequest(reportChat(chatIds[0], "spam2", {messageId}, "Sells things"));
    tgl.reply(make_object<reportChatResultOk>());
    prpl.verifyEvents(NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
                      ConversationWriteEvent(purpleUserName(0), purpleUserName(0), "Reported to Telegram's moderators.",
                                             PURPLE_MESSAGE_SYSTEM, 0));
}

TEST(JumpLink, Parse)
{
    std::string accountName;
    ChatId      chatId;
    MessageId   messageId;
    ASSERT_TRUE(parseJumpLink("tdlib-message://%2B1234/-100/5", accountName, chatId, messageId));
    ASSERT_EQ(std::string("+1234"), accountName);
    ASSERT_EQ(-100, chatId.value());
    ASSERT_EQ(5, messageId.value());

    // A file's link, or one naming a size, is something else
    ASSERT_FALSE(parseJumpLink("tdlib://%2B1234/-100/5", accountName, chatId, messageId));
    ASSERT_FALSE(parseJumpLink("tdlib-message://%2B1234/-100/5/original", accountName, chatId, messageId));
    ASSERT_FALSE(parseJumpLink("tdlib-message://%2B1234/-100", accountName, chatId, messageId));
}

TEST(JumpLink, OnlyWhereEnabled)
{
    PurpleAccount *account = purple_account_new("+1234", NULL);
    ASSERT_EQ(std::string("12:00"), linkToMessage(account, ChatId::fromString("-100"), MessageId::fromString("5"), "12:00"));
    setJumpLinksEnabled(true);
    ASSERT_EQ(std::string("<a href=\"tdlib-message://%2B1234/-100/5\">12:00</a>"),
              linkToMessage(account, ChatId::fromString("-100"), MessageId::fromString("5"), "12:00"));
    // Nothing to link to
    ASSERT_EQ(std::string("12:00"), linkToMessage(account, ChatId::fromString("-100"), MessageId::invalid, "12:00"));
    setJumpLinksEnabled(false);
    purple_account_destroy(account);
}

TEST(MarkChanges, Words)
{
    auto changes = [](const char *previous, const char *html) {
        return markChanges(previous, html, "red", "green");
    };
    // A word changed, one added, one removed
    ASSERT_EQ(std::string("the <font back=\"red\">quick</font> <font back=\"green\">slow</font> brown fox <font back=\"green\">gladly</font> jumps over the dog"),
              changes("the quick brown fox jumps over the dog", "the slow brown fox gladly jumps over the dog"));
    ASSERT_EQ(std::string("over the <font back=\"red\">lazy</font> dog"), changes("over the lazy\ndog", "over the dog"));
    // Formatting of the new text is kept; entities compare as the characters they stand for
    ASSERT_EQ(std::string("<b>Tom</b> &amp; <font back=\"red\">Spike</font> <i><font back=\"green\">Jerry</font></i>"),
              changes("Tom & Spike", "<b>Tom</b> &amp; <i>Jerry</i>"));
    // Removed at either end, and all of it
    ASSERT_EQ(std::string("<font back=\"red\">Well,</font> yes <font back=\"red\">&lt;indeed&gt;</font>"), changes("Well, yes <indeed>", "yes"));
    ASSERT_EQ(std::string("<font back=\"red\">gone</font> "), changes("gone", ""));
    // Only spacing or formatting changed
    ASSERT_EQ(std::string("a<br><b>b</b>"), changes("a b", "a<br><b>b</b>"));
}
