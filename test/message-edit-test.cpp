#include "fixture.h"
#include "libpurple-mock.h"
#include "client-utils.h"
#include <fmt/format.h>
#include <ctime>

class MessageEditTest: public CommTest {
protected:
    const int64_t messageId = 1;
    const int32_t date      = 10001;
    const int32_t editDate  = 10100;
    const int32_t fileId    = 1234;

    // As the conversation window shows the time of a message: date and time but on its day
    std::string messageTime(time_t timestamp);
    void        receiveText(const std::string &text);
    object_ptr<messagePhoto> makePhoto(const std::string &uniqueId, const std::string &caption);
    void        receivePhoto(const std::string &caption);
};

std::string MessageEditTest::messageTime(time_t timestamp)
{
    char buf[80];
    strftime(buf, sizeof(buf), "%x %X", localtime(&timestamp));
    return buf;
}

void MessageEditTest::receiveText(const std::string &text)
{
    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId, userIds[0], chatIds[0], false, date, makeTextMessage(text)
    )));
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0), text, PURPLE_MESSAGE_RECV, date));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
}

object_ptr<messagePhoto> MessageEditTest::makePhoto(const std::string &uniqueId, const std::string &caption)
{
    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(makeFile(fileId, 10000, "/path", uniqueId), 640, 480));
    return makeMessagePhoto(makePhotoWithSizes(std::move(sizes)), makeFormattedText(caption), false, false, false);
}

void MessageEditTest::receivePhoto(const std::string &caption)
{
    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId, userIds[0], chatIds[0], false, date, makePhoto("bleh", caption)
    )));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0), "<img src=\"file:///path\">\n" + caption,
        (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES), date
    ));
}

TEST_F(MessageEditTest, TextEdited)
{
    loginWithOneContact();
    receiveText("old");

    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, makeTextMessage("new")));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    prpl.verifyNoEvents();

    object_ptr<message> edited = makeMessage(messageId, userIds[0], chatIds[0], false, date,
                                             makeTextMessage("new"));
    edited->edit_date_ = editDate;
    tgl.reply(std::move(edited));
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0),
        userFirstNames[0] + " " + userLastNames[0] + " edited the message from " +
        messageTime(date) + ": new",
        PURPLE_MESSAGE_SYSTEM, editDate
    ));
    tgl.verifyNoRequests();
}

TEST_F(MessageEditTest, EditNotFetched)
{
    loginWithOneContact();
    receiveText("old");

    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, makeTextMessage("new")));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    prpl.verifyNoEvents();
    tgl.reply(makeError(500, "Internal Server Error"));
    prpl.verifyNoEvents();

    tgl.verifyNoRequests();

    // Never shown, so the same edit is fetched again when it comes again
    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, makeTextMessage("new")));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    prpl.verifyNoEvents();
}

TEST_F(MessageEditTest, DeletedWhileFetchingEdit)
{
    loginWithOneContact();
    receiveText("old");

    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, makeTextMessage("new")));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.update(makeUpdateDeleteMessages(chatIds[0], messageId, true, false));
    prpl.verifyEvents(ConversationWriteEvent(purpleUserName(0), purpleUserName(0),
                                             "Message from " + messageTime(date) + " has been deleted",
                                             PURPLE_MESSAGE_SYSTEM, 0));

    // The edit of a message reported deleted is no news
    object_ptr<message> edited = makeMessage(messageId, userIds[0], chatIds[0], false, date,
                                             makeTextMessage("new"));
    edited->edit_date_ = editDate;
    tgl.reply(std::move(edited));
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();
}

TEST_F(MessageEditTest, ChangeOutOfSight)
{
    loginWithOneContact();
    receiveText("text");

    // How a link in it is previewed, which the conversation does not show
    object_ptr<messageText> content = makeTextMessage("text");
    content->link_preview_options_ = make_object<linkPreviewOptions>();
    content->link_preview_options_->is_disabled_ = true;
    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, std::move(content)));
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();

    // Its formatting alone, which it does show
    content = makeTextMessage("text");
    content->text_->entities_.push_back(makeTextEntity(0, 4, make_object<textEntityTypeBold>()));
    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, std::move(content)));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    prpl.verifyNoEvents();
}

static object_ptr<messageDice> makeDice(int32_t value)
{
    auto dice = make_object<messageDice>();
    dice->emoji_ = "\U0001F3B2";
    dice->value_ = value;
    return dice;
}

TEST_F(MessageEditTest, DiceRolled)
{
    loginWithOneContact();
    tgl.update(make_object<updateNewMessage>(makeMessage(messageId, userIds[0], chatIds[0], false, date,
                                                         makeDice(0))));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(purpleUserName(0), purpleUserName(0),
                               userFirstNames[0] + " " + userLastNames[0] + ": Unsupported message type messageDice",
                               PURPLE_MESSAGE_SYSTEM, date),
        // ...followed by what it holds, as TDLib describes it
        ServGotImEvent(connection, purpleUserName(0),
                       "\n<font face=\"monospace\">messageDice {\n  emoji = \"\U0001F3B2\"\n}</font>",
                       PURPLE_MESSAGE_RECV, date)
    );

    // Telegram's own news of a message not shown in full, which nobody edited
    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, makeDice(4)));
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();
}

TEST_F(MessageEditTest, NotShownInThisSession)
{
    loginWithOneContact();

    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, makeTextMessage("new")));
    tgl.update(makeUpdateDeleteMessages(chatIds[0], messageId, true, false));
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();
}

TEST_F(MessageEditTest, CaptionEdited)
{
    loginWithOneContact();
    receivePhoto("old");

    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, makePhoto("bleh", "new")));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    prpl.verifyNoEvents();

    object_ptr<message> edited = makeMessage(messageId, userIds[0], chatIds[0], false, date,
                                             makePhoto("bleh", "new"));
    edited->edit_date_ = editDate;
    tgl.reply(std::move(edited));
    // The picture is the same, so only the caption is shown again, on the same line
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0),
        userFirstNames[0] + " " + userLastNames[0] + " edited the message from " +
        messageTime(date) + ": new",
        PURPLE_MESSAGE_SYSTEM, editDate
    ));
    tgl.verifyNoRequests();
}

TEST_F(MessageEditTest, CaptionRemoved)
{
    loginWithOneContact();
    receivePhoto("old");

    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, makePhoto("bleh", "")));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));

    object_ptr<message> edited = makeMessage(messageId, userIds[0], chatIds[0], false, date,
                                             makePhoto("bleh", ""));
    edited->edit_date_ = editDate;
    tgl.reply(std::move(edited));
    prpl.verifyEvents(
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + " removed the caption of the message from " +
            messageTime(date),
            PURPLE_MESSAGE_SYSTEM, editDate
        )
    );
    tgl.verifyNoRequests();
}

TEST_F(MessageEditTest, PhotoReplaced)
{
    loginWithOneContact();
    receivePhoto("caption");

    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, makePhoto("other", "caption")));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));

    object_ptr<message> edited = makeMessage(messageId, userIds[0], chatIds[0], false, date,
                                             makePhoto("other", "caption"));
    edited->edit_date_ = editDate;
    tgl.reply(std::move(edited));
    // A new picture is shown in full
    prpl.verifyEvents(
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + " edited the message from " +
            messageTime(date) + ":",
            PURPLE_MESSAGE_SYSTEM, editDate
        ),
        ServGotImEvent(
            connection, purpleUserName(0), "<img src=\"file:///path\">\ncaption",
            (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES), editDate
        )
    );
    tgl.verifyNoRequests();
}

TEST_F(MessageEditTest, DeletedWhileEditDownloads)
{
    loginWithOneContact();
    receivePhoto("caption");

    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, makePhoto("other", "caption")));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));

    // The new picture is still to be fetched, so the edit waits for it
    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(makeFile(fileId + 1, 10000, "", "other"), 640, 480));
    object_ptr<message> edited = makeMessage(messageId, userIds[0], chatIds[0], false, date,
        makeMessagePhoto(makePhotoWithSizes(std::move(sizes)), makeFormattedText("caption"), false, false, false));
    edited->edit_date_ = editDate;
    tgl.reply(std::move(edited));
    tgl.verifyRequest(downloadFile(fileId + 1, 1, 0, 0, true));
    prpl.verifyNoEvents();

    // The message shown before is deleted all the same
    tgl.update(makeUpdateDeleteMessages(chatIds[0], messageId, true, false));
    prpl.verifyEvents(ConversationWriteEvent(purpleUserName(0), purpleUserName(0),
                                             "Message from " + messageTime(date) + " has been deleted",
                                             PURPLE_MESSAGE_SYSTEM, 0));

    // ...and its edit is not shown once the picture is here
    tgl.reply(makeFile(fileId + 1, 10000, "/path2", "other"));
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();
}

TEST_F(MessageEditTest, Deleted)
{
    loginWithOneContact();
    receiveText("text");

    // Merely dropped from TDLib's cache
    tgl.update(makeUpdateDeleteMessages(chatIds[0], messageId, false, true));
    prpl.verifyNoEvents();

    tgl.update(makeUpdateDeleteMessages(chatIds[0], messageId, true, false));
    prpl.verifyEvents(
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            "Message from " + messageTime(date) + " has been deleted",
            PURPLE_MESSAGE_SYSTEM, 0
        )
    );

    // Gone, so a second deletion has nothing to report
    tgl.update(makeUpdateDeleteMessages(chatIds[0], messageId, true, false));
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();
}

TEST_F(MessageEditTest, EditedAndDeletedWhilePending)
{
    const int64_t replyId   = 2;
    const int64_t repliedId = 1000;
    loginWithOneContact();

    // Waiting for the message it replies to
    object_ptr<message> reply = makeMessage(replyId, userIds[0], chatIds[0], false, date,
                                            makeTextMessage("old"));
    reply->reply_to_ = makeReplyTo(repliedId);
    tgl.update(make_object<updateNewMessage>(std::move(reply)));
    tgl.verifyRequest(getMessage(chatIds[0], repliedId));

    // Text takes the old one's place quietly
    tgl.update(makeUpdateMessageContent(chatIds[0], replyId, makeTextMessage("new")));
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();

    tgl.update(makeUpdateDeleteMessages(chatIds[0], replyId, true, false));
    prpl.verifyNoEvents();

    // ...and when it could be shown, nobody missed it
    tgl.reply(makeMessage(repliedId, userIds[0], chatIds[0], false, date - 1, makeTextMessage("source")));
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();
}

TEST_F(MessageEditTest, EditedWhilePending)
{
    const int64_t replyId   = 2;
    const int64_t repliedId = 1000;
    loginWithOneContact();

    object_ptr<message> reply = makeMessage(replyId, userIds[0], chatIds[0], false, date,
                                            makeTextMessage("old"));
    reply->reply_to_ = makeReplyTo(repliedId);
    tgl.update(make_object<updateNewMessage>(std::move(reply)));
    tgl.verifyRequest(getMessage(chatIds[0], repliedId));

    tgl.update(makeUpdateMessageContent(chatIds[0], replyId, makeTextMessage("new")));
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();

    tgl.reply(makeMessage(repliedId, userIds[0], chatIds[0], false, date - 1, makeTextMessage("source")));
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        fmt::format(replyPattern, userFirstNames[0] + " " + userLastNames[0], "source", "new"),
        PURPLE_MESSAGE_RECV, date
    ));
    tgl.verifyRequest(viewMessages(chatIds[0], {replyId}, nullptr, true));
    prpl.verifyNoEvents();
}

// In Pidgin, where its tab can be scrolled to the message, the notices link to it
TEST_F(MessageEditTest, NoticesLinkToMessage)
{
    setJumpLinksEnabled(true);
    loginWithOneContact();
    receiveText("old");
    std::string link = "tdlib-message://%2B" + selfPhoneNumber + "/" + std::to_string(chatIds[0]) + "/" +
                       std::to_string(messageId);

    tgl.update(makeUpdateMessageContent(chatIds[0], messageId, makeTextMessage("new")));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    object_ptr<message> edited = makeMessage(messageId, userIds[0], chatIds[0], false, date,
                                             makeTextMessage("new"));
    edited->edit_date_ = editDate;
    tgl.reply(std::move(edited));
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0),
        userFirstNames[0] + " " + userLastNames[0] + " edited the message from <a href=\"" + link + "\">" +
        messageTime(date) + "</a>: new",
        PURPLE_MESSAGE_SYSTEM, editDate
    ));

    tgl.update(makeUpdateDeleteMessages(chatIds[0], messageId, true, false));
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0),
        "Message from <a href=\"" + link + "\">" + messageTime(date) + "</a> has been deleted",
        PURPLE_MESSAGE_SYSTEM, 0
    ));
    setJumpLinksEnabled(false);
}
