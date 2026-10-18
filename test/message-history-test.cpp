#include "supergroup-test.h"

class MessageHistoryTest: public SupergroupTest {
protected:
    const std::string userNameInChat = userFirstNames[0] + " " + userLastNames[0];
};

TEST_F(MessageHistoryTest, TdlibSkipMessages_LastMessageUnknown)
{
    loginWithSupergroup();

    tgl.update(make_object<updateChatLastMessage>(
        groupChatId, nullptr, std::vector<object_ptr<chatPosition>>()
    ));
    tgl.verifyNoRequests();
}

TEST_F(MessageHistoryTest, TdlibSkipMessages)
{
    const int purpleChatId = 1;
    purple_account_set_string(account, ("last-message-chat" + std::to_string(groupChatId)).c_str(), "1");
    loginWithSupergroup();

    tgl.update(make_object<updateChatLastMessage>(
        groupChatId, nullptr, std::vector<object_ptr<chatPosition>>()
    ));

    tgl.update(make_object<updateNewMessage>(
        makeMessage(6, userIds[0], groupChatId, false, 6, makeTextMessage("6"))
    ));
    tgl.verifyRequest(getChatHistory(groupChatId, 6, 0, 30, false));
    tgl.update(make_object<updateChatLastMessage>(
        groupChatId,
        makeMessage(6, userIds[0], groupChatId, false, 6, makeTextMessage("6")),
        std::vector<object_ptr<chatPosition>>()
    ));

    tgl.update(make_object<updateNewMessage>(
        makeMessage(7, userIds[0], groupChatId, false, 7, makeTextMessage("7"))
    ));
    tgl.update(make_object<updateChatLastMessage>(
        groupChatId,
        makeMessage(7, userIds[0], groupChatId, false, 7, makeTextMessage("7")),
        std::vector<object_ptr<chatPosition>>()
    ));
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();

    std::vector<object_ptr<message>> history;
    history.push_back(makeMessage(5, userIds[0], groupChatId, false, 5, makeTextMessage("5")));
    history.push_back(makeMessage(4, userIds[0], groupChatId, false, 4, makeTextMessage("4")));
    tgl.reply(make_object<messages>(history.size(), std::move(history)));
    prpl.verifyNoEvents();
    tgl.verifyRequest(getChatHistory(groupChatId, 4, 0, 30, false));

    history.clear();
    history.push_back(makeMessage(3, userIds[0], groupChatId, false, 3, makeTextMessage("3")));
    history.push_back(makeMessage(2, userIds[0], groupChatId, false, 2, makeTextMessage("2")));
    history.push_back(makeMessage(1, userIds[0], groupChatId, false, 1, makeTextMessage("1")));
    tgl.reply(make_object<messages>(history.size(), std::move(history)));

    prpl.verifyEvents(
        ServGotJoinedChatEvent(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
        ChatSetTopicEvent(groupChatPurpleName, "", ""),
        ChatClearUsersEvent(groupChatPurpleName),
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "2", PURPLE_MESSAGE_RECV, 2),
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "3", PURPLE_MESSAGE_RECV, 3),
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "4", PURPLE_MESSAGE_RECV, 4),
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "5", PURPLE_MESSAGE_RECV, 5),
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "6", PURPLE_MESSAGE_RECV, 6),
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "7", PURPLE_MESSAGE_RECV, 7)
    );
    tgl.verifyRequest(viewMessages(groupChatId, {6, 7, 5, 4, 3, 2}, nullptr, true));

    tgl.update(make_object<updateNewMessage>(
        makeMessage(8, userIds[0], groupChatId, false, 8, makeTextMessage("8"))
    ));
    tgl.update(make_object<updateChatLastMessage>(
        groupChatId,
        makeMessage(8, userIds[0], groupChatId, false, 8, makeTextMessage("8")),
        std::vector<object_ptr<chatPosition>>()
    ));
    prpl.verifyEvents(
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "8", PURPLE_MESSAGE_RECV, 8)
    );
    tgl.verifyRequest(viewMessages(groupChatId, {8}, nullptr, true));

    ASSERT_EQ(std::string("8"), std::string(purple_account_get_string(
        account, ("last-message-chat" + std::to_string(groupChatId)).c_str(), "")));
}

TEST_F(MessageHistoryTest, TdlibSkipMessages_FlushAtLogout)
{
    const int purpleChatId = 1;
    purple_account_set_string(account, ("last-message-chat" + std::to_string(groupChatId)).c_str(), "1");
    loginWithSupergroup();

    tgl.update(make_object<updateChatLastMessage>(
        groupChatId, nullptr, std::vector<object_ptr<chatPosition>>()
    ));

    tgl.update(make_object<updateNewMessage>(
        makeMessage(6, userIds[0], groupChatId, false, 6, makeTextMessage("6"))
    ));
    tgl.verifyRequest(getChatHistory(groupChatId, 6, 0, 30, false));
    tgl.update(make_object<updateChatLastMessage>(
        groupChatId,
        makeMessage(6, userIds[0], groupChatId, false, 6, makeTextMessage("6")),
        std::vector<object_ptr<chatPosition>>()
    ));
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();

    std::vector<object_ptr<message>> history;
    history.push_back(makeMessage(5, userIds[0], groupChatId, false, 5, makeTextMessage("5")));
    history.push_back(makeMessage(4, userIds[0], groupChatId, false, 4, makeTextMessage("4")));
    tgl.reply(make_object<messages>(history.size(), std::move(history)));
    prpl.verifyNoEvents();
    tgl.verifyRequest(getChatHistory(groupChatId, 4, 0, 30, false));

    history.clear();
    history.push_back(makeMessage(3, userIds[0], groupChatId, false, 3, makeTextMessage("3")));
    history.push_back(makeMessage(2, userIds[0], groupChatId, false, 2, makeTextMessage("2")));
    tgl.reply(make_object<messages>(history.size(), std::move(history)));
    prpl.verifyNoEvents();
    tgl.verifyRequest(getChatHistory(groupChatId, 2, 0, 30, false));

    pluginInfo().close(connection);
    prpl.verifyEvents(
        ServGotJoinedChatEvent(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
        ChatSetTopicEvent(groupChatPurpleName, "", ""),
        ChatClearUsersEvent(groupChatPurpleName),
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "2", PURPLE_MESSAGE_RECV, 2),
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "3", PURPLE_MESSAGE_RECV, 3),
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "4", PURPLE_MESSAGE_RECV, 4),
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "5", PURPLE_MESSAGE_RECV, 5),
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "6", PURPLE_MESSAGE_RECV, 6)
    );
    tgl.verifyRequest(viewMessages(groupChatId, {6, 5, 4, 3, 2}, nullptr, true));

    ASSERT_EQ(std::string("6"), std::string(purple_account_get_string(
        account, ("last-message-chat" + std::to_string(groupChatId)).c_str(), "")));
}

TEST_F(MessageHistoryTest, CatchingUpWaitsLonger)
{
    const int purpleChatId = 1;
    purple_account_set_string(account, ("last-message-chat" + std::to_string(groupChatId)).c_str(), "1");
    loginWithSupergroup();

    tgl.update(make_object<updateChatLastMessage>(
        groupChatId, nullptr, std::vector<object_ptr<chatPosition>>()
    ));
    tgl.update(make_object<updateNewMessage>(
        makeMessage(4, userIds[0], groupChatId, false, 4, makeTextMessage("4"))
    ));
    tgl.verifyRequest(getChatHistory(groupChatId, 4, 0, 30, false));

    // A message from the backlog quotes one older still
    std::vector<object_ptr<message>> history;
    object_ptr<message> reply = makeMessage(3, userIds[0], groupChatId, false, 3, makeTextMessage("3"));
    reply->reply_to_ = makeReplyTo(2);
    history.push_back(std::move(reply));
    tgl.reply(makeMessages(std::move(history)));
    std::vector<uint64_t> requestIds = tgl.verifyRequests({
        make_object<getMessage>(groupChatId, 2),
        make_object<getChatHistory>(groupChatId, 3, 0, 30, false)
    });
    // Catching up, it can wait longer than a message just sent would
    ASSERT_EQ(std::vector<guint>{5}, tgl.getTimeoutIntervals());

    // The end of the backlog, which still waits for the quote
    tgl.reply(requestIds[1], makeMessages({}));
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();
    tgl.reply(requestIds[0], makeMessage(2, userIds[0], groupChatId, false, 2, makeTextMessage("2")));
    prpl.verifyEvents(
        ServGotJoinedChatEvent(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
        ChatSetTopicEvent(groupChatPurpleName, "", ""),
        ChatClearUsersEvent(groupChatPurpleName),
        ServGotChatEvent(connection, purpleChatId, userNameInChat,
                         "<b>&gt; " + userNameInChat + " wrote:</b>\n&gt; 2\n3", PURPLE_MESSAGE_RECV, 3),
        ServGotChatEvent(connection, purpleChatId, userNameInChat, "4", PURPLE_MESSAGE_RECV, 4)
    );
    tgl.verifyRequest(viewMessages(groupChatId, {4, 3}, nullptr, true));
}
