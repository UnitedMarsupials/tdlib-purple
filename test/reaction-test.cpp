#include "fixture.h"
#include "libpurple-mock.h"
#include "buildopt.h"
#include <ctime>

// A reaction, the number of those who gave it, whether we did, and the latest of them
struct Given {
    std::string          emoji;
    int32_t              totalCount;
    bool                 chosen;
    std::vector<int64_t> senders;
};

class ReactionTest: public CommTest {
protected:
    const int64_t messageId = 1;
    const int32_t date      = 10001;

    void receive(object_ptr<MessageContent> content, const std::string &expectedText,
                 PurpleMessageFlags flags = PURPLE_MESSAGE_RECV);
    void react(std::initializer_list<Given> reactions, int64_t id = 0);
    std::string messageTime(time_t timestamp);
};

void ReactionTest::receive(object_ptr<MessageContent> content, const std::string &expectedText,
                           PurpleMessageFlags flags)
{
    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId, userIds[0], chatIds[0], false, date, std::move(content)
    )));
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0), expectedText, flags, date));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
}

static object_ptr<messageInteractionInfo> makeInteractionInfo(int32_t selfId,
                                                              std::initializer_list<Given> reactions)
{
    std::vector<object_ptr<messageReaction>> list;
    for (const Given &given: reactions) {
        std::vector<object_ptr<MessageSender>> senders;
        for (int64_t sender: given.senders)
            senders.push_back(make_object<messageSenderUser>(sender));
        list.push_back(make_object<messageReaction>(
            make_object<reactionTypeEmoji>(given.emoji), given.totalCount, given.chosen,
            given.chosen ? make_object<messageSenderUser>(selfId) : nullptr, std::move(senders)
        ));
    }
    return make_object<messageInteractionInfo>(0, 0, nullptr, make_object<messageReactions>(
        std::move(list), false, std::vector<object_ptr<paidReactor>>(), false
    ));
}

void ReactionTest::react(std::initializer_list<Given> reactions, int64_t id)
{
    tgl.update(make_object<updateMessageInteractionInfo>(chatIds[0], id ? id : messageId,
                                                         makeInteractionInfo(selfId, reactions)));
}

std::string ReactionTest::messageTime(time_t timestamp)
{
    char buf[80];
    strftime(buf, sizeof(buf), "%x %X", localtime(&timestamp));
    return buf;
}

TEST_F(ReactionTest, AddedChangedRemoved)
{
    loginWithOneContact();
    receive(makeTextMessage("Did you ask already?"), "Did you ask already?");

    react({{"\U0001F63F", 1, false, {userIds[0]}}});
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0),
                                     "reacted with \U0001F63F to message „Did you ask already?“.",
                                     PURPLE_MESSAGE_RECV, 0));

    // Nothing new, as with a view count going up
    react({{"\U0001F63F", 1, false, {userIds[0]}}});
    prpl.verifyNoEvents();

    // One reaction for another
    react({{"❤", 1, false, {userIds[0]}}});
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0),
                                     "reacted with ❤ to message „Did you ask already?“.",
                                     PURPLE_MESSAGE_RECV, 0));

    react({});
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0),
                                     "removed their reaction to message „Did you ask already?“.",
                                     PURPLE_MESSAGE_RECV, 0));
    tgl.verifyNoRequests();
}

TEST_F(ReactionTest, OwnFromElsewhere)
{
    loginWithOneContact();
    receive(makeTextMessage("hi"), "hi");

    // From the phone, say; our own counts among the recent ones as well
    react({{"\U0001F44D", 1, true, {selfId}}});
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), selfFirstName + " " + selfLastName,
        "reacted with \U0001F44D to message „hi“.",
        (PurpleMessageFlags)(PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND), 0
    ));

    react({});
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), selfFirstName + " " + selfLastName,
        "removed their reaction to message „hi“.",
        (PurpleMessageFlags)(PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND), 0
    ));
}

TEST_F(ReactionTest, LongTextWithMarkup)
{
    loginWithOneContact();
    const std::string text = "<b>Ще не вмерла</b> України і слава, і воля,\nЩе нам, браття молодії, усміхнеться доля";
    receive(makeTextMessage(text),
            "&lt;b&gt;Ще не вмерла&lt;/b&gt; України і слава, і воля,\nЩе нам, браття молодії, усміхнеться доля");

    react({{"\U0001F44D", 1, false, {userIds[0]}}});
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        // 50 characters as written, on one line
        "reacted with \U0001F44D to message „&lt;b&gt;Ще не вмерла&lt;/b&gt; України і слава, і воля, Ще на…“.",
        PURPLE_MESSAGE_RECV, 0
    ));
}

TEST_F(ReactionTest, NoText)
{
    loginWithOneContact();
    receive(make_object<messageLocation>(make_object<location>(50.4501, 30.5234, 0)
#if !TDLIB_API_LIVE_LOCATION
                                         , 0, 0, 0, 0
#endif
                                         ),
            "Location: <a href=\"file://geo:50.450100,30.523400\">50.450100, 30.523400</a>");

    react({{"\U0001F44D", 1, false, {userIds[0]}}});
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0),
                                     "reacted with \U0001F44D to location from " + messageTime(date) + ".",
                                     PURPLE_MESSAGE_RECV, 0));
}

TEST_F(ReactionTest, NotAllNamed)
{
    loginWithOneContact();
    receive(makeTextMessage("hi"), "hi");

    // Channels name nobody
    react({{"\U0001F44D", 40, false, {}}});
    prpl.verifyNoEvents();

    react({{"\U0001F44D", 41, false, {userIds[0]}}});
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0),
                                     "reacted with \U0001F44D to message „hi“.", PURPLE_MESSAGE_RECV, 0));

    // No longer among those named, which is no taking it back
    react({{"\U0001F44D", 42, false, {}}});
    prpl.verifyNoEvents();
}

TEST_F(ReactionTest, NotShownInThisSession)
{
    loginWithOneContact();
    react({{"\U0001F44D", 1, false, {userIds[0]}}});
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();
}

TEST_F(ReactionTest, UnreadToEarlierMessage)
{
    loginWithOneContact();

    std::vector<object_ptr<unreadReaction>> unread;
    unread.push_back(make_object<unreadReaction>(make_object<reactionTypeEmoji>("\U0001F525"),
                                                 make_object<messageSenderUser>(userIds[0]), false));
    tgl.update(make_object<updateMessageUnreadReactions>(chatIds[0], messageId, std::move(unread), 1));
    tgl.verifyRequest(getMessage(chatIds[0], messageId));

    // The other reaction is old news
    object_ptr<message> reacted = makeMessage(messageId, selfId, chatIds[0], true, date, makeTextMessage("my news"));
    reacted->sending_state_    = nullptr;
    reacted->interaction_info_ = makeInteractionInfo(selfId, {
        {"\U0001F525", 1, false, {userIds[0]}},
        {"\U0001F44D", 1, true, {selfId}}
    });
    tgl.reply(std::move(reacted));
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0),
                                     "reacted with \U0001F525 to message „my news“.", PURPLE_MESSAGE_RECV, 0));

    // And now it is known
    react({{"\U0001F525", 1, false, {userIds[0]}}, {"\U0001F44D", 1, true, {selfId}}});
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();
}
