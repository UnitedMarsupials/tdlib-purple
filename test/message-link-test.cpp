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
