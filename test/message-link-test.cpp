#include "fixture.h"
#include "libpurple-mock.h"
#include "tdlib-purple.h"
#include "client-utils.h"

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
    return make_object<file>(
        fileId, size, size,
        make_object<localFile>(path, true, true, false, downloaded, 0, downloaded ? size : 0, downloaded ? size : 0),
        make_object<remoteFile>("beh", "bleh", false, true, size)
    );
}

object_ptr<message> MessageLinkTest::makeDocumentMessage(int32_t size, const std::string &path, bool downloaded)
{
    return makeMessage(
        messageId, userIds[0], chatIds[0], false, date,
        make_object<messageDocument>(
            make_object<document>("video.mp4", "video/mp4", nullptr, nullptr, makeFile(size, path, downloaded)),
            make_object<formattedText>("caption", std::vector<object_ptr<textEntity>>())
        )
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
    prpl.discardEvents();

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
    prpl.discardEvents();

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

    tgl.reply(makeFile(655360, "/path/video.mp4", true));
    prpl.verifyEvents(NotifyUriEvent("file:///path/video.mp4"));
}

TEST_F(MessageLinkTest, MessageGone)
{
    loginWithOneContact();

    tgprpl_open_message_link(link().c_str());
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(make_object<error>(404, "Not Found"));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0), "Could not open the file: code 404 (Not Found)",
        PURPLE_MESSAGE_SYSTEM, 0
    ));
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
};

object_ptr<file> VideoSizesTest::makeVideoFile(int32_t id, int64_t size, const std::string &path)
{
    bool downloaded = !path.empty();
    return make_object<file>(
        id, size, size,
        make_object<localFile>(path, true, true, false, downloaded, 0, downloaded ? size : 0, downloaded ? size : 0),
        make_object<remoteFile>("beh", "bleh", false, true, size)
    );
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
    alternatives.push_back(make_object<alternativeVideo>(11, 1280, 720, "h265", nullptr, makeVideoFile(largerId, 12 << 20)));
    alternatives.push_back(make_object<alternativeVideo>(12, 854, 480, "", nullptr, makeVideoFile(smallId, 6 << 20)));
    return makeMessage(messageId, userIds[0], chatIds[0], false, date, makeMessageVideo(
        std::move(video), std::move(alternatives), nullptr, 0,
        make_object<formattedText>("caption", std::vector<object_ptr<textEntity>>()), false, false, false
    ));
}

// Smallest first, each a link of its own
std::string VideoSizesTest::sizeLinks()
{
    return "<a href=\"" + link() + "/a12\">854×480, purple_str_size_to_units</a> · "
           "<a href=\"" + link() + "/a11\">1280×720 (h265), purple_str_size_to_units</a> · "
           "<a href=\"" + link() + "/v\">1920×1080, purple_str_size_to_units</a>";
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
        "<a href=\"" + link() + "\">video.mp4 [video/mp4]</a>\n" + sizeLinks() + "\ncaption",
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
    prpl.discardEvents();
}

TEST_F(VideoSizesTest, LargestOnly)
{
    // Above the limit, so nothing
    purple_account_set_string(account, "prefetch-videos", "largest");
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeVideoMessage()));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.discardEvents();
}

TEST_F(VideoSizesTest, None)
{
    purple_account_set_string(account, "prefetch-videos", "none");
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeVideoMessage()));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.discardEvents();
}

TEST_F(VideoSizesTest, OpenSize)
{
    purple_account_set_string(account, "prefetch-videos", "none");
    loginWithOneContact();
    tgl.update(make_object<updateNewMessage>(makeVideoMessage()));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.discardEvents();

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
}

TEST_F(MessageLinkTest, PhotoSizes)
{
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(makeFile(1000, "/path", true), 320, 240));
    sizes.back()->type_ = "m";
    // Shown is the larger, downloaded here already, so as not to wait for it
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId + 1, 50000, 50000,
            make_object<localFile>("/path", true, true, false, true, 0, 50000, 50000),
            make_object<remoteFile>("beh", "bleh", false, true, 50000)
        ), 1280, 960));
    sizes.back()->type_ = "y";
    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId, userIds[0], chatIds[0], false, date,
        makeMessagePhoto(make_object<photo>(false, nullptr, std::move(sizes)),
                         make_object<formattedText>("caption", std::vector<object_ptr<textEntity>>()),
                         false, false, false)
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
