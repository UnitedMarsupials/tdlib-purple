#include "fixture.h"
#include "libpurple-mock.h"
#include "buildopt.h"
#include <clocale>
#include <ctime>

class LocationTest: public CommTest {
protected:
    const int64_t messageId = 1;
    const int32_t date      = 10001;
    const int32_t editDate  = 10100;

    void receive(object_ptr<MessageContent> content, const std::string &expectedText);
};

static object_ptr<MessageContent> makeLocation(double latitude, double longitude, double accuracy,
                                               int32_t livePeriod = 0, int32_t expiresIn = 0)
{
    return makeMessageLocation(latitude, longitude, accuracy, livePeriod, expiresIn);
}

void LocationTest::receive(object_ptr<MessageContent> content, const std::string &expectedText)
{
    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId, userIds[0], chatIds[0], false, date, std::move(content)
    )));
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0), expectedText, PURPLE_MESSAGE_RECV, date));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
}

TEST_F(LocationTest, Location)
{
    loginWithOneContact();

    // A decimal comma, as some locales would have it, would break the URI
    std::string oldLocale = setlocale(LC_NUMERIC, NULL);
    if (!setlocale(LC_NUMERIC, "uk_UA.UTF-8"))
        GTEST_SKIP() << "No uk_UA.UTF-8 locale here";
    // For Pidgin, behind "file://", which its handler strips before passing the rest on
    receive(makeLocation(50.4501, 30.5234, 25),
            "Location: <a href=\"file://geo:50.450100,30.523400;u=25\">50.450100, 30.523400</a>");
    setlocale(LC_NUMERIC, oldLocale.c_str());
}

TEST_F(LocationTest, Location_NotPidgin)
{
    loginWithOneContact();
    setUiName("BitlBee");

    receive(makeLocation(-33.8568, 151.2153, 0),
            "Location: <a href=\"geo:-33.856800,151.215300\">-33.856800, 151.215300</a>");
}

TEST_F(LocationTest, Venue)
{
    loginWithOneContact();

    receive(makeMessageVenue(50.4501, 30.5234, "Mriya <b>cafe</b>", "Khreshchatyk 1"),
            "<b>Mriya &lt;b&gt;cafe&lt;/b&gt;</b>\nKhreshchatyk 1\n"
            "Location: <a href=\"file://geo:50.450100,30.523400\">50.450100, 30.523400</a>");
}

TEST_F(LocationTest, LiveLocationStopped)
{
    loginWithOneContact();
    receive(makeLocation(50.4501, 30.5234, 0, 0x7FFFFFFF, 0x7FFFFFFF),
            "Live location: <a href=\"file://geo:50.450100,30.523400\">50.450100, 30.523400</a>");

    // Moving is no edit
    tgl.update(make_object<updateMessageContent>(chatIds[0], messageId,
                                                 makeLocation(50.4502, 30.5235, 0, 0x7FFFFFFF, 0x7FFFFFFF)));
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();

    // ...but stopping is
    tgl.update(make_object<updateMessageContent>(chatIds[0], messageId,
                                                 makeLocation(50.4503, 30.5236, 0, 0x7FFFFFFF, 0)));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    object_ptr<message> stopped = makeMessage(messageId, userIds[0], chatIds[0], false, date,
                                              makeLocation(50.4503, 30.5236, 0, 0x7FFFFFFF, 0));
    stopped->edit_date_ = editDate;
    tgl.reply(std::move(stopped));

    char sentAt[80];
    time_t timestamp = date;
    strftime(sentAt, sizeof(sentAt), "%x %X", localtime(&timestamp));
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0),
        userFirstNames[0] + " " + userLastNames[0] + " stopped sharing the live location from " +
        sentAt + ": <a href=\"file://geo:50.450300,30.523600\">50.450300, 30.523600</a>",
        PURPLE_MESSAGE_SYSTEM, editDate
    ));
    tgl.verifyNoRequests();
}

TEST_F(LocationTest, ReplyToLocation)
{
    const int64_t replyId = 2;
    loginWithOneContact();
    receive(makeLocation(50.4501, 30.5234, 0),
            "Location: <a href=\"file://geo:50.450100,30.523400\">50.450100, 30.523400</a>");

    object_ptr<message> reply = makeMessage(replyId, userIds[0], chatIds[0], false, date, makeTextMessage("here"));
    reply->reply_to_ = makeReplyTo(messageId);
    tgl.update(make_object<updateNewMessage>(std::move(reply)));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));
    tgl.reply(makeMessage(messageId, userIds[0], chatIds[0], false, date, makeLocation(50.4501, 30.5234, 0)));
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<b>&gt; " + userFirstNames[0] + " " + userLastNames[0] + " wrote:</b>\n&gt; [location]\nhere",
        PURPLE_MESSAGE_RECV, date
    ));
    tgl.verifyRequest(viewMessages(chatIds[0], {replyId}, nullptr, true));
}
