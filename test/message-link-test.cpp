#include "fixture.h"
#include "libpurple-mock.h"
#include "tdlib-purple.h"
#include "client-utils.h"
#include "buildopt.h"
#include "td-client.h"
#include "config.h"

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

TEST_F(MessageLinkTest, CopyWithoutClipboard)
{
    loginWithOneContact();
    PurpleTdClient *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    ASSERT_NE(nullptr, client);
    const ChatId    chat    = ChatId::fromString(std::to_string(chatIds[0]).c_str());
    const MessageId message = MessageId::fromString("1");

    // Fetched as for opening it, but without Pidgin, there is no clipboard to put it on
    client->openMessageFile(chat, message, std::string(), PurpleTdClient::FileUse::Copy);
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeDocumentMessage(10000, "/path/video.mp4", true));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            "Could not copy video.mp4 [video/mp4]: there is no clipboard to put it on",
            PURPLE_MESSAGE_SYSTEM, 0
        )
    );

    // Opening it is not held up by that
    tgprpl_open_message_link(link().c_str());
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeDocumentMessage(10000, "/path/video.mp4", true));
    prpl.verifyEvents(NotifyUriEvent("file:///path/video.mp4"));
}

TEST_F(MessageLinkTest, Drag)
{
    loginWithOneContact();
    PurpleTdClient *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    ASSERT_NE(nullptr, client);
    const ChatId    chat    = ChatId::fromString(std::to_string(chatIds[0]).c_str());
    const MessageId message = MessageId::fromString("1");
    std::string     path;
    bool            picture = true;
    int             found   = 0;
    auto            fileFound = [&](const std::string &filePath, bool isPicture) {
        path    = filePath;
        picture = isPicture;
        found++;
    };

    // Fetched as for opening it, and told where it is
    client->dragMessageFile(chat, message, std::string(), fileFound);
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeDocumentMessage(10000, "/path/video.mp4", true));
    ASSERT_EQ(1, found);
    ASSERT_EQ("/path/video.mp4", path);
    ASSERT_FALSE(picture);
    prpl.verifyNoEvents();

    // A photo and a still sticker are dropped as pictures too, an animated sticker is not
    auto makeStickerMessage = [&](const std::string &stickerPath, object_ptr<StickerFormat> format) {
        auto stickerObject = makeSticker(320, 200, "", ::makeFile(fileId, 10000, stickerPath));
        stickerObject->format_ = std::move(format);
        return makeMessage(messageId, userIds[0], chatIds[0], false, date,
                           make_object<messageSticker>(std::move(stickerObject), false));
    };
    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(::makeFile(fileId, 10000, "/path/photo.jpg"), 640, 480));
    object_ptr<td::td_api::message> pictures[] = {
        makeMessage(messageId, userIds[0], chatIds[0], false, date,
                    makeMessagePhoto(make_object<photo>(false, nullptr, std::move(sizes)),
                                     make_object<formattedText>("", std::vector<object_ptr<textEntity>>()),
                                     false, false, false)),
        makeStickerMessage("/path/sticker.webp", make_object<stickerFormatWebp>()),
        makeStickerMessage("/path/sticker.tgs", make_object<stickerFormatTgs>())
    };
    const char *paths[]   = {"/path/photo.jpg", "/path/sticker.webp", "/path/sticker.tgs"};
    const bool  isPicture[] = {true, true, false};
    for (unsigned i = 0; i < 3; i++) {
        client->dragMessageFile(chat, message, std::string(), fileFound);
        tgl.verifyRequest(getMessage(chatIds[0], messageId));
        tgl.reply(std::move(pictures[i]));
        ASSERT_EQ(int(2 + i), found) << paths[i];
        ASSERT_EQ(paths[i], path);
        ASSERT_EQ(isPicture[i], picture) << paths[i];
    }
    prpl.verifyNoEvents();

    // Gone, it cannot be had, which the conversation is told
    client->dragMessageFile(chat, message, std::string(), fileFound);
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeError(404, "Not Found"));
    ASSERT_EQ(5, found);
    ASSERT_EQ("", path);
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0), "Could not copy the file: code 404 (Not Found)",
            PURPLE_MESSAGE_SYSTEM, 0
        )
    );
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
        "<a href=\"file://file:///path\">photo</a>\n"
        "<a href=\"" + link() + "/pm\">320×240, purple_str_size_to_units</a> · "
        "<a href=\"" + link() + "/py\">1280×960, purple_str_size_to_units</a>\ncaption",
        PURPLE_MESSAGE_RECV, date
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

TEST_F(MessageLinkTest, SentMessageRenamed)
{
    loginWithOneContact();
    const int64_t sentId = 1000;

    // Shown while being sent, its link names the id it went by then
    tgl.update(makeUpdateMessageSendSucceeded(makeDocumentMessage(10000, "/path/video.mp4", true), sentId));
    tgl.verifyNoRequests();

    std::string sentLink = "tdlib://%2B" + selfPhoneNumber + "/" + std::to_string(chatIds[0]) + "/" +
                           std::to_string(sentId);
    tgprpl_open_message_link(sentLink.c_str());
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeDocumentMessage(10000, "/path/video.mp4", true));
    prpl.verifyEvents(NotifyUriEvent("file:///path/video.mp4"));
}

TEST_F(MessageLinkTest, SentMessageFetchedEarlier)
{
    loginWithOneContact();
    const int64_t sentId = 1000;
    tgl.update(makeUpdateMessageSendSucceeded(makeDocumentMessage(10000, "/path/video.mp4", true), sentId));
    tgl.verifyNoRequests();

    // Its copy fetched before it was sent, as a tab redrawn meanwhile gets it, goes by its new id
    object_ptr<message> early = makeDocumentMessage(10000, "/path/video.mp4", true);
    early->id_            = sentId;
    early->sending_state_ = make_object<messageSendingStatePending>();
    tgl.update(make_object<updateNewMessage>(std::move(early)));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<a href=\"" + link() + "\">video.mp4 [video/mp4]</a>\ncaption",
        PURPLE_MESSAGE_RECV, date
    ));
}

TEST_F(MessageLinkTest, ForwardWithinTelegram)
{
    loginWithOneContact();
    PurpleTdClient *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    ASSERT_NE(nullptr, client);
    const ChatId    chat    = ChatId::fromString(std::to_string(chatIds[0]).c_str());
    const MessageId message = MessageId::fromString("1");

    // To a buddy of this account: Telegram forwards it, and nothing is said unless that fails
    PurpleTdClient::ForwardDestination to;
    to.accountName = "+" + selfPhoneNumber;
    to.protocolId  = config::pluginId;
    to.name        = purpleUserName(0);
    to.label       = "Gottfried Leibniz (+" + selfPhoneNumber + ")";
    client->forwardMessage(chat, message, to);
    tgl.verifyRequest(*makeForwardMessages(chatIds[0], chatIds[0], messageId));
    {
        std::vector<object_ptr<td::td_api::message>> sent;
        sent.push_back(makeMessage(2, selfId, chatIds[0], true, date, makeTextMessage("text")));
        tgl.reply(makeMessages(std::move(sent)));
    }
    prpl.verifyNoEvents();

    // To a group chat, by its name; Telegram's refusal is told in the message's chat, the
    // destination's name as it is
    to.isChat = true;
    to.name   = "chat" + std::to_string(chatIds[1]);
    to.label  = "<Group> (+" + selfPhoneNumber + ")";
    client->forwardMessage(chat, message, to);
    tgl.verifyRequest(*makeForwardMessages(chatIds[1], chatIds[0], messageId));
    tgl.reply(makeError(400, "CHAT_FORWARDS_RESTRICTED"));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            "The message was not forwarded to &lt;Group&gt; (+" + selfPhoneNumber + "): code 400 (CHAT_FORWARDS_RESTRICTED)",
            PURPLE_MESSAGE_ERROR, 0
        )
    );

    // Nor is a message Telegram answers for with none
    to.label = "Group (+" + selfPhoneNumber + ")";
    client->forwardMessage(chat, message, to);
    tgl.verifyRequest(*makeForwardMessages(chatIds[1], chatIds[0], messageId));
    {
        std::vector<object_ptr<td::td_api::message>> sent;
        sent.push_back(nullptr);
        tgl.reply(makeMessages(std::move(sent)));
    }
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0),
        "The message was not forwarded to Group (+" + selfPhoneNumber + "): Telegram did not forward it",
        PURPLE_MESSAGE_ERROR, 0
    ));

    // To a buddy unknown to Telegram
    to.isChat = false;
    to.name   = "id999";
    to.label  = "Nobody (+" + selfPhoneNumber + ")";
    client->forwardMessage(chat, message, to);
    tgl.verifyNoRequests();
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0), "The message was not forwarded to Nobody (+" + selfPhoneNumber + "): no such user",
        PURPLE_MESSAGE_ERROR, 0
    ));
}

TEST_F(MessageLinkTest, ForwardLabelsUnique)
{
    auto destination = [](const char *account, const char *protocol, const char *name, const char *label) {
        PurpleTdClient::ForwardDestination to;
        to.accountName = account;
        to.protocolId  = protocol;
        to.name        = name;
        to.label       = label;
        return to;
    };
    // Two buddies of the same alias are told apart by their names; two accounts of the same name,
    // by their protocols
    std::vector<PurpleTdClient::ForwardDestination> destinations = {
        destination("a", "prpl-a", "id1", "Ann (a)"),
        destination("a", "prpl-a", "id2", "Ann (a)"),
        destination("a", "prpl-a", "id3", "Bob (a)"),
        destination("b", "prpl-b", "x", "Cid (b)"),
        destination("b", "prpl-c", "x", "Cid (b)"),
        // A buddy and a chat of the same name, by their numbers
        destination("c", "prpl-c", "#x", "X (c)"),
        destination("c", "prpl-c", "#x", "X (c)"),
    };
    destinations.back().isChat = true;
    PurpleTdClient::makeForwardLabelsUnique(destinations);
    ASSERT_EQ(std::string("Ann (a) [id1]"), destinations[0].label);
    ASSERT_EQ(std::string("Ann (a) [id2]"), destinations[1].label);
    ASSERT_EQ(std::string("Bob (a)"), destinations[2].label);
    ASSERT_EQ(std::string("Cid (b) [x] [prpl-b]"), destinations[3].label);
    ASSERT_EQ(std::string("Cid (b) [x] [prpl-c]"), destinations[4].label);
    ASSERT_EQ(std::string("X (c) [#x] [prpl-c] [1]"), destinations[5].label);
    ASSERT_EQ(std::string("X (c) [#x] [prpl-c] [2]"), destinations[6].label);
}

TEST_F(MessageLinkTest, ForwardToAnotherAccount)
{
    loginWithOneContact();
    PurpleTdClient *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    ASSERT_NE(nullptr, client);
    const ChatId    chat    = ChatId::fromString(std::to_string(chatIds[0]).c_str());
    const MessageId message = MessageId::fromString("1");

    // A buddy of another, connected, account
    PurpleAccount   *other = purple_account_new("other", "prpl-other");
    PurpleConnection otherConnection;
    otherConnection.state   = PURPLE_CONNECTED;
    otherConnection.account = other;
    other->gc = &otherConnection;
    PurpleTdClient::ForwardDestination to;
    to.accountName = "other";
    to.protocolId  = "prpl-other";
    to.name        = "friend";
    to.label       = "Friend (other)";

    // The message's text is told again, through that account, headed by whom it came from
    client->forwardMessage(chat, message, to);
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeMessage(messageId, userIds[0], chatIds[0], false, date, makeTextMessage("hello")));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, other, "friend"),
        ConversationSendEvent(PURPLE_CONV_TYPE_IM, other, "friend",
                              "<b>Forwarded from Gottfried Leibniz:</b>\nhello")
    );

    // A file cannot go where files cannot be sent: said so, in the message's chat
    client->forwardMessage(chat, message, to);
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeDocumentMessage(10000, "", false));
    prpl.verifyEvents(
        ConversationSendEvent(PURPLE_CONV_TYPE_IM, other, "friend", "<b>Forwarded from Gottfried Leibniz:</b>\ncaption"),
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(purpleUserName(0), purpleUserName(0),
                               "The file of the message was not forwarded to Friend (other): files cannot be sent there.",
                               PURPLE_MESSAGE_ERROR, 0)
    );

    // Nor what has neither text nor a file, such as a location
    client->forwardMessage(chat, message, to);
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    {
        auto location = make_object<messageLocation>();
        location->location_ = make_object<td::td_api::location>();
        tgl.reply(makeMessage(messageId, userIds[0], chatIds[0], false, date, std::move(location)));
    }
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0), "The message was not forwarded to Friend (other): it has neither text nor a file",
        PURPLE_MESSAGE_ERROR, 0
    ));

    // An account of another protocol, of the same name as this one, is another account
    PurpleTdClient::ForwardDestination sameName = to;
    sameName.accountName = "+" + selfPhoneNumber;
    client->forwardMessage(chat, message, sameName);
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeMessage(messageId, userIds[0], chatIds[0], false, date, makeTextMessage("hello")));
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0), "The message was not forwarded to Friend (other): the account is not connected",
        PURPLE_MESSAGE_ERROR, 0
    ));

    // Not while that account is off
    otherConnection.state = PURPLE_DISCONNECTED;
    client->forwardMessage(chat, message, to);
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeMessage(messageId, userIds[0], chatIds[0], false, date, makeTextMessage("hello")));
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0), "The message was not forwarded to Friend (other): the account is not connected",
        PURPLE_MESSAGE_ERROR, 0
    ));
    other->gc = NULL;
}
