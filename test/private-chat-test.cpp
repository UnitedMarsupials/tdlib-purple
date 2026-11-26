#include "buildopt.h"
#include "fixture.h"
#include "sticker.h"
#include "libpurple-mock.h"
#include "purple-info.h"
#include "td-client.h"
#include <fmt/format.h>

class PrivateChatTest: public CommTest {
protected:
    void testReadReceipt(bool shouldSend);
    void contactedByStranger(object_ptr<ChatActionBar> &&actionBar);
};

TEST_F(PrivateChatTest, AddContactByPhone)
{
    login();

    // Adding new buddy
    PurpleBuddy *buddy = purple_buddy_new(account, userPhones[0].c_str(), "Local Alias");
    purple_blist_add_buddy(buddy, NULL, &standardPurpleGroup, NULL);
    prpl.discardEvents();
    pluginInfo().add_buddy(connection, buddy, &standardPurpleGroup);

    // The buddy is deleted right away, to be replaced later
    prpl.verifyEvents(RemoveBuddyEvent(account, userPhones[0]));

    // Adding user to contact list by phone number
    tgl.verifyRequest(*makeImportContacts(userPhones[0]));

    tgl.update(make_object<updateUser>(makeUser(
        userIds[0],
        "",
        "",
        userPhones[0],
        make_object<userStatusOffline>()
    )));
    tgl.reply(make_object<importedContacts>(
        std::vector<int64_t>(1, userIds[0]),
        std::vector<int32_t>()
    ));

    tgl.verifyRequest(*makeAddContact(userIds[0], userPhones[0], "Local", "Alias"));

    // We are notified that the user is now a contact
    object_ptr<user> userInfo = makeUser(
        userIds[0],
        "Local",
        "Alias",
        userPhones[0],
        make_object<userStatusOffline>()
    );
    userInfo->is_contact_ = true;
    tgl.update(make_object<updateUser>(std::move(userInfo)));
    // is_contact was true, so add buddy
    tgl.reply(make_object<ok>());

    tgl.verifyRequest(createPrivateChat(userIds[0], false));

    tgl.update(make_object<updateNewChat>(makeChat(
        chatIds[0],
        make_object<chatTypePrivate>(userIds[0]),
        "Local Alias",
        nullptr, 0, 0, 0
    )));
    prpl.verifyEvents(AddBuddyEvent(
        purpleUserName(0),
        "Local Alias",
        account,
        NULL,
        &standardPurpleGroup,
        NULL
    ));

    tgl.reply(makeChat(
        chatIds[0],
        make_object<chatTypePrivate>(userIds[0]),
        userFirstNames[0] + " " + userLastNames[0],
        nullptr, 0, 0, 0
    ));
}

TEST_F(PrivateChatTest, AddContactByUsername)
{
    login();

    // Adding new buddy
    PurpleBuddy *buddy = purple_buddy_new(account, "username", "Local Alias");
    purple_blist_add_buddy(buddy, NULL, &standardPurpleGroup, NULL);
    prpl.discardEvents();
    pluginInfo().add_buddy(connection, buddy, &standardPurpleGroup);

    // The buddy is deleted right away, to be replaced later
    prpl.verifyEvents(RemoveBuddyEvent(account, "username"));

    tgl.verifyRequest(searchPublicChat("username"));

    tgl.update(make_object<updateUser>(makeUser(
        userIds[0],
        userFirstNames[0],
        userLastNames[0],
        "",
        make_object<userStatusOffline>()
    )));
    tgl.update(standardPrivateChat(0));
    tgl.reply(makeChat(
        chatIds[0],
        make_object<chatTypePrivate>(userIds[0]),
        userFirstNames[0] + " " + userLastNames[0],
        nullptr, 0, 0, 0
    ));
    prpl.verifyNoEvents();

    tgl.verifyRequest(*makeAddContact(userIds[0], "", "Local", "Alias"));

    // tdlib actually sends updateChatTitle first (if it didn't, it should still work, the buddy
    // should be created as userFirstNames[0] + " " + userLastNames[0] and then renamed to "Local Alias"
    tgl.update(make_object<updateChatTitle>(chatIds[0], "Local Alias"));
    // We are notified that the user is now a contact
    object_ptr<user> userInfo = makeUser(
        userIds[0],
        "Local",
        "Alias",
        "",
        make_object<userStatusOffline>()
    );
    userInfo->is_contact_ = true;
    tgl.update(make_object<updateUser>(std::move(userInfo)));

    tgl.reply(make_object<ok>());

    tgl.verifyRequest(createPrivateChat(userIds[0], false));

    // is_contact was true, so add buddy
    prpl.verifyEvents(AddBuddyEvent(
        purpleUserName(0),
        "Local Alias",
        account,
        NULL,
        &standardPurpleGroup,
        NULL
    ));
    tgl.reply(makeChat(
        chatIds[0],
        make_object<chatTypePrivate>(userIds[0]),
        "Local Alias",
        nullptr, 0, 0, 0
    ));
}

TEST_F(PrivateChatTest, AddContactByUsername_DoesntBecomeContact)
{
    login();

    // Adding new buddy
    PurpleBuddy *buddy = purple_buddy_new(account, "username", "Local Alias");
    purple_blist_add_buddy(buddy, NULL, &standardPurpleGroup, NULL);
    prpl.discardEvents();
    pluginInfo().add_buddy(connection, buddy, &standardPurpleGroup);

    // The buddy is deleted right away, to be replaced later
    prpl.verifyEvents(RemoveBuddyEvent(account, "username"));

    tgl.verifyRequest(searchPublicChat("username"));

    tgl.update(make_object<updateUser>(makeUser(
        userIds[0],
        userFirstNames[0],
        userLastNames[0],
        "",
        make_object<userStatusOffline>()
    )));
    tgl.update(standardPrivateChat(0));
    tgl.reply(makeChat(
        chatIds[0],
        make_object<chatTypePrivate>(userIds[0]),
        userFirstNames[0] + " " + userLastNames[0],
        nullptr, 0, 0, 0
    ));
    prpl.verifyNoEvents();

    tgl.verifyRequest(*makeAddContact(userIds[0], "", "Local", "Alias"));

    tgl.update(make_object<updateChatTitle>(chatIds[0], "Local Alias"));
    tgl.update(make_object<updateUser>(makeUser(
        userIds[0],
        "Local",
        "Alias",
        "",
        make_object<userStatusOffline>()
    )));
    tgl.reply(make_object<ok>());

    tgl.verifyRequest(createPrivateChat(userIds[0], false));
    prpl.verifyNoEvents();

    tgl.reply(makeChat(
        chatIds[0],
        make_object<chatTypePrivate>(userIds[0]),
        "Local Alias",
        nullptr, 0, 0, 0
    ));
    // Its conversation, by id, with a buddy until the program quits showing the name
    prpl.verifyEvents(
        AddBuddyEvent(purpleUserName(0), "Local Alias", account, nullptr,
                      purple_find_group("Telegram: not in contacts"), nullptr),
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0))
    );
}

TEST_F(PrivateChatTest, ContactedByNew)
{
    login();
    constexpr int64_t messageId = 10000;
    constexpr int32_t date      = 123456;

    // Seems to happen when they add us to contacts
    tgl.update(make_object<updateUser>(makeUser(
        userIds[0],
        userFirstNames[0],
        userLastNames[0],
        "", // No phone number yet
        make_object<userStatusOffline>()
    )));

    // They message us
    object_ptr<updateNewChat> chatUpdate = standardPrivateChat(0);
    chatUpdate->chat_->positions_.push_back(make_object<chatPosition>(make_object<chatListMain>(), 1, false, nullptr));
    tgl.update(std::move(chatUpdate));
    prpl.verifyEvents(AddBuddyEvent(
        purpleUserName(0),
        userFirstNames[0] + " " + userLastNames[0],
        account,
        nullptr, nullptr, nullptr
    ));

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeTextMessage("text")
    )));
    prpl.verifyEvents(ServGotImEvent(
        connection,
        purpleUserName(0),
        "text",
        PURPLE_MESSAGE_RECV,
        date
    ));
    tgl.verifyRequest(viewMessages(
        chatIds[0],
        {messageId}, nullptr,
        true
    ));

    // And only now we get phone number
    tgl.update(make_object<updateUser>(makeUser(
        userIds[0],
        userFirstNames[0],
        userLastNames[0],
        "+" + userPhones[0],
        make_object<userStatusOffline>()
    )));
}

// Either offer to report the chat as spam keeps a stranger out of the lasting buddy list
void PrivateChatTest::contactedByStranger(object_ptr<ChatActionBar> &&actionBar)
{
    login();
    constexpr int64_t messageId = 10000;
    constexpr int32_t date      = 123456;
    const std::string displayName = userFirstNames[0] + " " + userLastNames[0];

    tgl.update(make_object<updateUser>(makeUser(
        userIds[0],
        userFirstNames[0],
        userLastNames[0],
        "",
        make_object<userStatusOffline>()
    )));

    // A stranger messages us: Telegram offers to report the chat as spam, so no lasting buddy
    object_ptr<updateNewChat> chatUpdate = standardPrivateChat(0);
    auto position = make_object<chatPosition>();
    position->list_  = make_object<chatListMain>();
    position->order_ = 1;
    chatUpdate->chat_->positions_.push_back(std::move(position));
    chatUpdate->chat_->action_bar_ = std::move(actionBar);
    tgl.update(std::move(chatUpdate));
    // The conversation goes by their id, a buddy until the program quits showing their name
    PurpleGroup *transient = purple_find_group("Telegram: not in contacts");
    ASSERT_NE(nullptr, transient);
    EXPECT_TRUE(purple_blist_node_get_flags(PURPLE_BLIST_NODE(transient)) & PURPLE_BLIST_NODE_FLAG_NO_SAVE);
    prpl.verifyEvents(AddBuddyEvent(purpleUserName(0), displayName, account, nullptr, transient, nullptr));

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeTextMessage("text")
    )));
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0), "text", PURPLE_MESSAGE_RECV, date));
    PurpleBuddy *buddy = purple_find_buddy(account, purpleUserName(0).c_str());
    ASSERT_NE(nullptr, buddy);
    EXPECT_TRUE(purple_blist_node_get_flags(PURPLE_BLIST_NODE(buddy)) & PURPLE_BLIST_NODE_FLAG_NO_SAVE);
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));

    // The offer is what Telegram takes a report about a person by: with nothing chosen
    GList *actions = pluginInfo().blist_node_menu(&buddy->node);
    nodeMenuAction(&buddy->node, actions, "Report…");
    g_list_free_full(actions, (GDestroyNotify)purple_menu_action_free);
    tgl.verifyRequest(reportChat(chatIds[0], "", {}, ""));
    tgl.reply(make_object<reportChatResultOk>());
    prpl.verifyEvents(ConversationWriteEvent(purpleUserName(0), purpleUserName(0),
                                             "Reported to Telegram's moderators.", PURPLE_MESSAGE_SYSTEM, 0));

    // We dismiss the offer: the same buddy is a lasting one, keeping the conversation's tab
    auto actionBarUpdate = make_object<updateChatActionBar>();
    actionBarUpdate->chat_id_ = chatIds[0];
    tgl.update(std::move(actionBarUpdate));
    prpl.verifyEvents(AddBuddyEvent(purpleUserName(0), displayName, account, nullptr, nullptr, nullptr));
    EXPECT_EQ(buddy, purple_find_buddy(account, purpleUserName(0).c_str()));
    EXPECT_FALSE(purple_blist_node_get_flags(PURPLE_BLIST_NODE(buddy)) & PURPLE_BLIST_NODE_FLAG_NO_SAVE);
}

TEST_F(PrivateChatTest, ContactedByStranger)
{
    contactedByStranger(make_object<chatActionBarReportSpam>());
}

TEST_F(PrivateChatTest, ContactedByStranger_ReportAddBlock)
{
    contactedByStranger(make_object<chatActionBarReportAddBlock>());
}

TEST_F(PrivateChatTest, ContactedByNew_ImmediatePhoneNumber)
{
    login();
    constexpr int64_t messageId = 10000;
    constexpr int32_t date      = 123456;

    // Phone number sent right away - this has not been observed in real life
    tgl.update(standardUpdateUser(0));
    prpl.verifyNoEvents();

    object_ptr<updateNewChat> chatUpdate = standardPrivateChat(0);
    chatUpdate->chat_->positions_.push_back(make_object<chatPosition>(make_object<chatListMain>(), 1, false, nullptr));
    tgl.update(std::move(chatUpdate));
    prpl.verifyEvents(AddBuddyEvent(
        purpleUserName(0),
        userFirstNames[0] + " " + userLastNames[0],
        account,
        nullptr, nullptr, nullptr
    ));

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeTextMessage("text")
    )));
    prpl.verifyEvents(ServGotImEvent(
        connection,
        purpleUserName(0),
        "text",
        PURPLE_MESSAGE_RECV,
        date
    ));
    tgl.verifyRequest(viewMessages(
        chatIds[0],
        {messageId}, nullptr,
        true
    ));
}

TEST_F(PrivateChatTest, ContactWithoutChatAtLogin)
{
    auto userUpdate = standardUpdateUser(0);
    userUpdate->user_->is_contact_ = true;
    // Only once the contact's chat is
    chatListReadyAtLogin = false;
    login(
        {std::move(userUpdate)},
        make_object<users>(1, std::vector<int64_t>(1, userIds[0])),
        make_object<chats>(),
        {}, {}, {}
    );

    tgl.verifyRequest(createPrivateChat(userIds[0], false));

    tgl.update(standardPrivateChat(0));
    prpl.verifyEvents(
        AddBuddyEvent(purpleUserName(0), userFirstNames[0] + " " + userLastNames[0],
                      account, nullptr, nullptr, nullptr)
    );

    tgl.reply(makeChat(
        chatIds[0],
        make_object<chatTypePrivate>(userIds[0]),
        userFirstNames[0] + " " + userLastNames[0],
        nullptr, 0, 0, 0
    ));
    tgl.verifyRequest(*makeOnlineOption(true));
    prpl.verifyEvents(
        UserStatusEvent(account, purpleUserName(0), PURPLE_STATUS_AWAY),
        AccountSetAliasEvent(account, selfFirstName + " " + selfLastName),
        ShowAccountEvent(account)
    );
}

// More contacts without chats than are asked for at once: sixteen at a time, another as each answer
// comes, in whatever order, failed or not; the login done only with the last
TEST_F(PrivateChatTest, ManyContactsWithoutChatsAtLogin)
{
    enum { CONTACTS = 17, WINDOW = 16, FIRST_ID = 1000 };
    auto contact = [](int index) {
        auto user = makeUser(FIRST_ID + index, "First" + std::to_string(index), "Last", "",
                             make_object<userStatusOffline>());
        user->is_contact_ = true;
        auto update = make_object<updateUser>();
        update->user_ = std::move(user);
        return update;
    };
    std::vector<int64_t> ids;
    for (int i = 0; i < CONTACTS; i++)
        ids.push_back(FIRST_ID + i);
    auto contacts = make_object<users>();
    contacts->total_count_ = CONTACTS;
    contacts->user_ids_    = ids;
    // Online only once the chats are all known
    chatListReadyAtLogin = false;
    login({contact(0), contact(1), contact(2), contact(3), contact(4), contact(5), contact(6), contact(7), contact(8), contact(9), contact(10), contact(11), contact(12), contact(13), contact(14), contact(15), contact(16)}, std::move(contacts), make_object<chats>(), {}, {}, {});

    // From the last of the list, all at once
    auto request = [](int index) {
        auto result = make_object<createPrivateChat>();
        result->user_id_ = FIRST_ID + CONTACTS - 1 - index;
        return result;
    };
    static_assert(WINDOW == 16, "as many requested as listed");
    std::vector<uint64_t> requestIds = tgl.verifyRequests({
        request(0),
        request(1),
        request(2),
        request(3),
        request(4),
        request(5),
        request(6),
        request(7),
        request(8),
        request(9),
        request(10),
        request(11),
        request(12),
        request(13),
        request(14),
        request(15)
    });

    // One answered out of order, failed: the last one goes out
    tgl.reply(requestIds[2], makeError(400, "Failed"));
    requestIds.push_back(tgl.verifyRequest(createPrivateChat(FIRST_ID, false)));
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();

    // Not done until all have answered
    for (size_t i = 0; i < requestIds.size(); i++) {
        if (i == 2)
            continue;
        if (i + 1 == requestIds.size())
            prpl.discardEvents();
        tgl.reply(requestIds[i], makeError(400, "Failed"));
        if (i + 1 < requestIds.size()) {
            tgl.verifyNoRequests();
            prpl.verifyNoEvents();
        } else
            tgl.verifyRequest(setOption("online", make_object<optionValueBoolean>(true)));
    }
    prpl.verifyEvents(
        AccountSetAliasEvent(account, selfFirstName + " " + selfLastName),
        ShowAccountEvent(account)
    );
}

TEST_F(PrivateChatTest, Document)
{
    const int64_t messageId = 1;
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageDocument>(
            make_object<document>(
                "doc.file.name", "mime/type", nullptr, nullptr,
                make_object<file>(
                    fileId, 10000, 10000,
                    make_object<localFile>("", true, true, false, false, 0, 0, 0),
                    make_object<remoteFile>("beh", "bleh", false, true, 10000)
                )
            ),
            make_object<formattedText>("document", std::vector<object_ptr<textEntity>>())
        )
    )));
    tgl.verifyRequest(downloadFile(fileId, 1, 0, 0, true));
    prpl.verifyNoEvents();

    tgl.reply(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<a href=\"file://file:///path\">doc.file.name [mime/type]</a>\ndocument",
        PURPLE_MESSAGE_RECV, date
    ));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
}

TEST_F(PrivateChatTest, Document_NotPidgin)
{
    const int64_t messageId = 1;
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;
    loginWithOneContact();
    // Only Pidgin needs the scheme written twice
    setUiName("BitlBee");
    purple_account_set_string(account, "download-behaviour", "hyperlink");

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessageDocument("doc.file.name", "mime/type", makeFile(fileId, 10000, "/path"), "document")
    )));
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<a href=\"file:///path\">doc.file.name [mime/type]</a>\ndocument",
        PURPLE_MESSAGE_RECV, date
    ));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
}

TEST_F(PrivateChatTest, Video)
{
    const int64_t messageId = 1;
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessageVideo(
            make_object<video>(
                120, 640, 480, "video.avi", "video/whatever", false, false, nullptr, nullptr,
                make_object<file>(
                    fileId, 10000, 10000,
                    make_object<localFile>("", true, true, false, false, 0, 0, 0),
                    make_object<remoteFile>("beh", "bleh", false, true, 10000)
                )
            ),
            std::vector<object_ptr<alternativeVideo>>(), nullptr, 0,
            make_object<formattedText>("video", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    )));
    tgl.verifyRequest(downloadFile(fileId, 1, 0, 0, true));
    prpl.verifyNoEvents();

    tgl.reply(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));
    prpl.verifyEvents(
        ServGotImEvent(
            connection,
            purpleUserName(0),
            "<a href=\"file://file:///path\">video.avi [video/whatever, 640×480, purple_str_size_to_units]</a>\nvideo",
            PURPLE_MESSAGE_RECV,
            date
        )
    );
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
}

TEST_F(PrivateChatTest, Audio)
{
    const int64_t messageId = 1;
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageAudio>(
            make_object<audio>(
                25*60, "Symphony #40", "Wolfgang Amadeus Mozart",
                "symphony.ogg", "audio/whatever", nullptr, nullptr, std::vector<object_ptr<thumbnail>>(),
                make_object<file>(
                    fileId, 10000, 10000,
                    make_object<localFile>("", true, true, false, false, 0, 0, 0),
                    make_object<remoteFile>("beh", "bleh", false, true, 10000)
                )
            ),
            make_object<formattedText>("audio", std::vector<object_ptr<textEntity>>())
        )
    )));
    tgl.verifyRequest(downloadFile(fileId, 1, 0, 0, true));
    prpl.verifyNoEvents();

    tgl.reply(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));
    prpl.verifyEvents(
        ServGotImEvent(
            connection,
            purpleUserName(0),
            "<a href=\"file://file:///path\">symphony.ogg [audio/whatever]</a>\naudio",
            PURPLE_MESSAGE_RECV,
            date
        )
    );
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
}

TEST_F(PrivateChatTest, OtherMessage)
{
    const int32_t date = 10001;
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageGame>()
    )));
    tgl.verifyRequest(viewMessages(
        chatIds[0],
        {1}, nullptr,
        true
    ));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + ": Unsupported message type messageGame",
            PURPLE_MESSAGE_SYSTEM, date
        ),
        // ...followed by what the message actually holds, as TDLib describes it
        ServGotImEvent(connection, purpleUserName(0),
                       "\n<font face=\"monospace\">messageGame {\n}</font>",
                       PURPLE_MESSAGE_RECV, date)
    );
}

#ifndef NoPidgin
// Where the account does not fetch pictures unasked, a photo is left as its stand-in; once the
// conversation does, it is fetched as it comes into view
TEST_F(PrivateChatTest, PhotoNotDownloaded)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    purple_account_set_bool(account, "download-media", FALSE);
    loginWithOneContact();
    PurpleConversation *conv = getImConversation(account, purpleUserName(0).c_str());
    prpl.discardEvents();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(::makeFile(fileId, 10000), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(1, userIds[0], chatIds[0], false, date,
        makeMessagePhoto(makePhotoWithSizes(std::move(sizes)), makeFormattedText("photo"), false, false, false))));
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0), "[photo]\nphoto", PURPLE_MESSAGE_RECV, date));
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));

    // Seen while still not fetched unasked: nothing
    PurpleTdClient *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    ASSERT_NE(nullptr, client);
    client->messagesViewed(conv, {MessageId::fromString("1")});
    tgl.verifyNoRequests();

    // The conversation's own setting: shown again, to fetch it
    PurpleBuddy *buddy = purple_find_buddy(account, purpleUserName(0).c_str());
    ASSERT_NE(nullptr, buddy);
    purple_blist_node_set_string(PURPLE_BLIST_NODE(buddy), "tdlib-download-media", "yes");
    client->messagesViewed(conv, {MessageId::fromString("1")});
    uint64_t refresh = tgl.verifyRequest(getMessage(chatIds[0], 1));

    // Not got: tried again as it is seen again
    tgl.reply(refresh, makeError(400, "Message not found"));
    client->messagesViewed(conv, {MessageId::fromString("1")});
    tgl.verifyRequest(getMessage(chatIds[0], 1));
}

// A stranger's photo is not fetched unasked, whatever the account's setting
TEST_F(PrivateChatTest, StrangerPhotoNotDownloaded)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();
    PurpleBuddy *buddy = purple_find_buddy(account, purpleUserName(0).c_str());
    ASSERT_NE(nullptr, buddy);
    purple_blist_node_set_flags(PURPLE_BLIST_NODE(buddy), PURPLE_BLIST_NODE_FLAG_NO_SAVE);

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(::makeFile(fileId, 10000), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(1, userIds[0], chatIds[0], false, date,
        makeMessagePhoto(makePhotoWithSizes(std::move(sizes)), makeFormattedText("photo"), false, false, false))));
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0), "[photo]\nphoto", PURPLE_MESSAGE_RECV, date));
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));
}
#else
// Where pictures cannot be written over their stand-ins, a photo not fetched unasked is let be, as
// one too big is, and the message is shown at once without it
TEST_F(PrivateChatTest, PhotoNotDownloaded)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    purple_account_set_bool(account, "download-media", FALSE);
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(::makeFile(fileId, 10000), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(1, userIds[0], chatIds[0], false, date,
        makeMessagePhoto(makePhotoWithSizes(std::move(sizes)), makeFormattedText("photo"), false, false, false))));
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));
    prpl.verifyEvents(
        ServGotImEvent(connection, purpleUserName(0), "photo", PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(purpleUserName(0), purpleUserName(0),
                               userFirstNames[0] + " " + userLastNames[0] +
                               ": Ignoring photo download (purple_str_size_to_units)",
                               PURPLE_MESSAGE_SYSTEM, date)
    );
}

// Turned on while such a photo's message waits for what it replies to, the message is still shown
// without it, rather than waiting for a download never begun
TEST_F(PrivateChatTest, PhotoNotDownloaded_TurnedOnMeanwhile)
{
    const int32_t date     = 10002;
    const int32_t fileId   = 1234;
    const int64_t msgId    = 2;
    const int64_t srcMsgId = 1;
    purple_account_set_bool(account, "download-media", FALSE);
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(::makeFile(fileId, 10000), 640, 480));
    object_ptr<message> msg = makeMessage(msgId, userIds[0], chatIds[0], false, date,
        makeMessagePhoto(makePhotoWithSizes(std::move(sizes)), makeFormattedText("photo"), false, false, false));
    msg->reply_to_ = makeReplyTo(srcMsgId);
    tgl.update(make_object<updateNewMessage>(std::move(msg)));
    tgl.verifyRequest(getMessage(chatIds[0], srcMsgId));
    prpl.verifyNoEvents();

    purple_account_set_bool(account, "download-media", TRUE);
    tgl.reply(makeError(404, "Not found"));
    tgl.verifyRequest(viewMessages(chatIds[0], {msgId}, nullptr, true));
    prpl.verifyEvents(
        ServGotImEvent(connection, purpleUserName(0),
                       "<b>&gt; Unknown user wrote:</b>\n&gt; [message unavailable]\nphoto",
                       PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(purpleUserName(0), purpleUserName(0),
                               userFirstNames[0] + " " + userLastNames[0] +
                               ": Ignoring photo download (purple_str_size_to_units)",
                               PURPLE_MESSAGE_SYSTEM, date)
    );
}
#endif

#ifndef NoPidgin
// A photo not here yet is shown at once, as its stand-in, and fetched; once it is, the message is
// shown again with it, where the tab can take it, and logged as it is shown then, or else as it was
TEST_F(PrivateChatTest, Photo)
{
    purple_account_set_string(account, "media-size-threshold", "0");
    static const int32_t date   = 10001;
    static const int32_t fileId = 1234;
    setLogMarkersEnabled(true);
    loginWithOneContact();
    auto makePhotoMessage = [this](bool here) {
        std::vector<object_ptr<photoSize>> sizes;
        sizes.push_back(makePhotoSize(::makeFile(fileId, 10000, here ? "/path" : ""), 640, 480));
        return makeMessage(1, userIds[0], chatIds[0], false, date,
                           makeMessagePhoto(makePhotoWithSizes(std::move(sizes)), makeFormattedText("photo"),
                                            false, false, false));
    };

    tgl.update(make_object<updateNewMessage>(makePhotoMessage(false)));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ServGotImEvent(connection, purpleUserName(0), "[photo]\nphoto<!--tdlib-message 1-->", PURPLE_MESSAGE_RECV,
                       date)
    );
    // Logged once the picture is here
    ASSERT_TRUE(takeLoggedMessages().empty());
    uint64_t download = tgl.verifyRequests({
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, 1), nullptr, true),
        make_object<downloadFile>(fileId, 1, 0, 0, true)
    })[1];

    tgl.reply(download, ::makeFile(fileId, 10000, "/path"));
    tgl.verifyRequest(getMessage(chatIds[0], 1));
    tgl.reply(makePhotoMessage(true));
    // Without Pidgin's tab there is nothing to write it over: logged as it was shown, for the log alone
    std::string logged = "<!--tdlib-log-only-->[photo]\nphoto<!--tdlib-message 1 " + std::to_string(date) +
                         "--><!--/tdlib-log-only-->";
    prpl.verifyEvents(ConversationWriteEvent(purpleUserName(0), purpleUserName(0), logged, PURPLE_MESSAGE_RECV,
                                             date));
    ASSERT_EQ(std::vector<std::string>{logged}, takeLoggedMessages());
}
#endif

TEST_F(PrivateChatTest, AlreadyDownloadedPhoto)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 10000, 10000,
            make_object<localFile>(TEST_SOURCE_DIR "/test.webp", true, true, false, true, 0, 10000, 10000),
            make_object<remoteFile>("beh", "bleh", false, true, 10000)
        ), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            make_object<photo>(false, nullptr, std::move(sizes)),
            make_object<formattedText>("photo", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    )));
    tgl.verifyRequest(
        viewMessages(chatIds[0], std::vector<int64_t>(1, 1), nullptr, true)
    );
    prpl.verifyEvents(
        ServGotImEvent(
            connection,
            purpleUserName(0),
            "\n<img id=\"" + std::to_string(getLastImgstoreId()) + "\">\n"
            "photo",
            (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES),
            date
        )
    );

    tgl.reply(make_object<ok>()); // reply to viewMessages
}

TEST_F(PrivateChatTest, ForwardedPhoto)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(makeFile(fileId, 10000, TEST_SOURCE_DIR "/test.webp"), 640, 480));
    object_ptr<message> forwarded = makeMessage(
        1, userIds[0], chatIds[0], false, date,
        makeMessagePhoto(makePhotoWithSizes(std::move(sizes)), makeFormattedText(""), false, false, false)
    );
    auto origin = make_object<messageOriginHiddenUser>();
    origin->sender_name_   = "Someone";
    forwarded->forward_info_ = make_object<messageForwardInfo>();
    forwarded->forward_info_->origin_ = std::move(origin);
    tgl.update(make_object<updateNewMessage>(std::move(forwarded)));
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));
    // The picture on the line after the header, as without it
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0),
        "<b>Forwarded from Someone:</b>\n<img id=\"" + std::to_string(getLastImgstoreId()) + "\">",
        (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES), date
    ));
}

TEST_F(PrivateChatTest, AlreadyDownloadedPhoto_Unreadable)
{
    // Shown as the link to its file, Pidgin showing no picture but from its image store
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 10000, 10000,
            make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
            make_object<remoteFile>("beh", "bleh", false, true, 10000)
        ), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            make_object<photo>(false, nullptr, std::move(sizes)),
            make_object<formattedText>("photo", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    )));
    tgl.verifyRequest(
        viewMessages(chatIds[0], std::vector<int64_t>(1, 1), nullptr, true)
    );
    prpl.verifyEvents(
        ServGotImEvent(
            connection,
            purpleUserName(0),
            "<a href=\"file://file:///path\">photo</a>\n"
            "photo",
            PURPLE_MESSAGE_RECV,
            date
        )
    );

    tgl.reply(make_object<ok>()); // reply to viewMessages
}

TEST_F(PrivateChatTest, AlreadyDownloadedPhoto_NoGdkPixbuf)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    // Like BitlBee, which shows no pictures
    gdkPixbufGetFileInfoName = "no_such_function";
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(makeFile(fileId, 10000, "/path"), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            makePhotoWithSizes(std::move(sizes)),
            makeFormattedText("caption"),
            false, false, false
        )
    )));
    tgl.verifyRequest(
        viewMessages(chatIds[0], std::vector<int64_t>(1, 1), nullptr, true)
    );
    prpl.verifyEvents(
        ServGotImEvent(
            connection,
            purpleUserName(0),
            "<a href=\"file://file:///path\">photo</a>\n"
            "caption",
            PURPLE_MESSAGE_RECV,
            date
        )
    );

    tgl.reply(make_object<ok>()); // reply to viewMessages
}

TEST_F(PrivateChatTest, IgnoredUpdateUserAndNewPrivateChat)
{
    loginWithOneContact();

    tgl.update(standardUpdateUser(0));
    tgl.update(standardPrivateChat(0));
    tgl.update(standardUpdateUser(0));
    tgl.update(standardPrivateChat(0));

    tgl.verifyNoRequests();
    prpl.verifyNoEvents();
}

TEST_F(PrivateChatTest, SendImage)
{
    loginWithOneContact();

    const int64_t msgIdOld[3] = {10, 11, 12};
    const int64_t msgIdNew[3] = {20, 21, 22};
    const int32_t fileId[2] = {101, 102};
    const int32_t messageFailureDate = 1234;
    uint8_t data1[] = {1, 2, 3, 4, 5};
    uint8_t data2[] = {11, 12, 13, 14};

    const int id1 = purple_imgstore_add_with_id(arrayDup(data1, sizeof(data1)), sizeof(data1), "filename1");
    const int id2 = purple_imgstore_add_with_id(arrayDup(data2, sizeof(data2)), sizeof(data2), "filename2");
    const std::string messageText = fmt::format("prefix<img id=\"{}\">caption1<img id=\"{}\">\ncaption2", id1, id2);

    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), messageText.c_str(), PURPLE_MESSAGE_SEND));
    tgl.verifyRequests({
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, make_object<inputMessageText>(
                make_object<formattedText>("prefix", std::vector<object_ptr<textEntity>>()),
                nullptr, false
            )
        ),
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, makeInputMessagePhoto(
                make_object<inputFileLocal>(),
                nullptr, std::vector<std::int32_t>(), 0, 0,
                make_object<formattedText>("caption1", std::vector<object_ptr<textEntity>>()),
                false, nullptr, false
            )
        ),
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, makeInputMessagePhoto(
                make_object<inputFileLocal>(),
                nullptr, std::vector<std::int32_t>(), 0, 0,
                make_object<formattedText>("caption2", std::vector<object_ptr<textEntity>>()),
                false, nullptr, false
            )
        )
    });

    object_ptr<message> msg = makeMessage(
        msgIdOld[0],
        userIds[0],
        chatIds[0],
        true,
        1,
        makeTextMessage("prefix")
    );
    tgl.reply(std::move(msg));

    msg = makeMessage(
        msgIdOld[1],
        userIds[0],
        chatIds[0],
        true,
        1,
        makeMessagePhoto(
            makePhotoUploading(fileId[0], sizeof(data1), 0, "/path", 0, 0),
            make_object<formattedText>("caption1", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    );
    msg->sending_state_ = make_object<messageSendingStatePending>();
    tgl.reply(std::move(msg));

    msg = makeMessage(
        msgIdOld[2],
        userIds[0],
        chatIds[0],
        true,
        1,
        makeMessagePhoto(
            makePhotoUploading(fileId[1], sizeof(data2), 0, "/path", 0, 0),
            make_object<formattedText>("caption2", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    );
    msg->sending_state_ = make_object<messageSendingStatePending>();
    tgl.reply(std::move(msg));

    checkFile(tgl.getInputPhotoPath(0).c_str(), data1, sizeof(data1));
    tgl.update(makeUpdateMessageSendSucceeded(
        makeMessage(
            msgIdNew[1],
            userIds[0],
            chatIds[0],
            true,
            1,
            makeMessagePhoto(
                makePhotoLocal(fileId[0], sizeof(data1), "/path", 0, 0),
                make_object<formattedText>("caption1", std::vector<object_ptr<textEntity>>()),
                false, false, false
            )
        ),
        msgIdOld[1]
    ));
    ASSERT_FALSE(g_file_test(tgl.getInputPhotoPath(0).c_str(), G_FILE_TEST_EXISTS));

    checkFile(tgl.getInputPhotoPath(1).c_str(), data2, sizeof(data2));
    tgl.update(make_object<updateMessageSendFailed>(
        makeMessage(
            msgIdNew[2],
            userIds[0],
            chatIds[0],
            true,
            messageFailureDate,
            makeMessagePhoto(
                makePhotoLocal(fileId[1], sizeof(data1), "/path", 0, 0),
                make_object<formattedText>("caption2", std::vector<object_ptr<textEntity>>()),
                false, false, false
            )
        ),
        msgIdOld[2],
        make_object<error>(100, "whatever error")
    ));
    ASSERT_FALSE(g_file_test(tgl.getInputPhotoPath(1).c_str(), G_FILE_TEST_EXISTS));

    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(purpleUserName(0), purpleUserName(0),
                               "Failed to send message: code 100 (whatever error)",
                               PURPLE_MESSAGE_SYSTEM, messageFailureDate)
    );
}

TEST_F(PrivateChatTest, SendImageRejected)
{
    loginWithOneContact();

    uint8_t data[] = {1, 2, 3, 4, 5};
    const int id = purple_imgstore_add_with_id(arrayDup(data, sizeof(data)), sizeof(data), "filename");
    const std::string messageText = fmt::format("<img id=\"{}\">caption", id);

    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), messageText.c_str(), PURPLE_MESSAGE_SEND));
    tgl.verifyRequest(*makeSendMessage(
        chatIds[0],
        0,
        nullptr,
        nullptr,
        nullptr, makeInputMessagePhoto(
            make_object<inputFileLocal>(),
            nullptr, std::vector<std::int32_t>(), 0, 0,
            makeFormattedText("caption"),
            false, nullptr, false
        )
    ));
    checkFile(tgl.getInputPhotoPath(0).c_str(), data, sizeof(data));

    // No message is created, so nothing else would ever free the file
    tgl.reply(makeError(400, "PHOTO_INVALID"));
    ASSERT_FALSE(g_file_test(tgl.getInputPhotoPath(0).c_str(), G_FILE_TEST_EXISTS));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(purpleUserName(0), purpleUserName(0),
                               "Failed to send message: code 400 (PHOTO_INVALID)",
                               PURPLE_MESSAGE_SYSTEM, 0)
    );    tgl.verifyNoRequests();
}

TEST_F(PrivateChatTest, SendImageCancelled)
{
    const int64_t messageId = 10;
    const int32_t fileId    = 101;
    loginWithOneContact();

    uint8_t data[] = {1, 2, 3, 4, 5};
    const int id = purple_imgstore_add_with_id(arrayDup(data, sizeof(data)), sizeof(data), "filename");
    const std::string messageText = fmt::format("<img id=\"{}\">caption", id);

    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), messageText.c_str(), PURPLE_MESSAGE_SEND));
    tgl.verifyRequest(*makeSendMessage(
        chatIds[0],
        0,
        nullptr,
        nullptr,
        nullptr, makeInputMessagePhoto(
            make_object<inputFileLocal>(),
            nullptr, std::vector<std::int32_t>(), 0, 0,
            makeFormattedText("caption"),
            false, nullptr, false
        )
    ));
    object_ptr<message> msg = makeMessage(
        messageId, userIds[0], chatIds[0], true, 1,
        makeMessagePhoto(
            makePhotoUploading(fileId, sizeof(data), 0, "/path", 0, 0),
            makeFormattedText("caption"),
            false, false, false
        )
    );
    msg->sending_state_ = make_object<messageSendingStatePending>();
    tgl.reply(std::move(msg));
    checkFile(tgl.getInputPhotoPath(0).c_str(), data, sizeof(data));
    // Remembered in case the account disconnects meanwhile
    ASSERT_EQ(std::to_string(chatIds[0]) + " " + std::to_string(messageId) + " " + tgl.getInputPhotoPath(0) + "\n",
              std::string(purple_account_get_string(account, "temp-image-uploads", "")));

    // The same message id in another chat is another message
    tgl.update(makeUpdateDeleteMessages(chatIds[1], messageId, true, false));
    ASSERT_TRUE(g_file_test(tgl.getInputPhotoPath(0).c_str(), G_FILE_TEST_EXISTS));

    // Deleted before the upload finished, which is the end of it as far as TDLib is concerned
    tgl.update(makeUpdateDeleteMessages(chatIds[0], messageId, true, false));
    ASSERT_FALSE(g_file_test(tgl.getInputPhotoPath(0).c_str(), G_FILE_TEST_EXISTS));
    ASSERT_EQ(nullptr, purple_account_get_string(account, "temp-image-uploads", nullptr));
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();
}

TEST_F(PrivateChatTest, SendImageFromEarlierSession)
{
    // Still sending, sent while we were away but never reported, forgotten by TDLib, and one TDLib
    // could not answer for
    const int64_t messageIds[4] = {10, 11, 12, 13};
    std::string   paths[4];
    std::string   setting;
    for (unsigned i = 0; i < 4; i++) {
        char *path = NULL;
        int   fd   = g_file_open_tmp("tdlib_test_XXXXXX", &path, NULL);
        ASSERT_TRUE(fd >= 0);
        ::close(fd);
        paths[i] = path;
        g_free(path);
        setting += std::to_string(chatIds[0]) + " " + std::to_string(messageIds[i]) + " " + paths[i] + "\n";
    }
    purple_account_set_string(account, "temp-image-uploads", setting.c_str());

    loginWithOneContact();
    tgl.verifyRequests({
        make_object<getMessage>(chatIds[0], messageIds[0]),
        make_object<getMessage>(chatIds[0], messageIds[1]),
        make_object<getMessage>(chatIds[0], messageIds[2]),
        make_object<getMessage>(chatIds[0], messageIds[3])
    });
    tgl.reply(makeMessage(messageIds[0], userIds[0], chatIds[0], true, 1, makeTextMessage("")));
    object_ptr<message> msg = makeMessage(messageIds[1], userIds[0], chatIds[0], true, 1, makeTextMessage(""));
    msg->sending_state_ = nullptr;
    tgl.reply(std::move(msg));
    tgl.reply(makeError(404, "Not Found"));
    tgl.reply(makeError(400, "Bad Request"));
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();

    ASSERT_TRUE(g_file_test(paths[0].c_str(), G_FILE_TEST_EXISTS));
    ASSERT_FALSE(g_file_test(paths[1].c_str(), G_FILE_TEST_EXISTS));
    ASSERT_FALSE(g_file_test(paths[2].c_str(), G_FILE_TEST_EXISTS));
    ASSERT_TRUE(g_file_test(paths[3].c_str(), G_FILE_TEST_EXISTS));
    ASSERT_EQ(std::to_string(chatIds[0]) + " " + std::to_string(messageIds[0]) + " " + paths[0] + "\n" +
              std::to_string(chatIds[0]) + " " + std::to_string(messageIds[3]) + " " + paths[3] + "\n",
              std::string(purple_account_get_string(account, "temp-image-uploads", "")));

    // TDLib finishes what it began in the earlier session
    tgl.update(makeUpdateMessageSendSucceeded(
        makeMessage(20, userIds[0], chatIds[0], true, 1, makeTextMessage("")), messageIds[0]
    ));
    ASSERT_FALSE(g_file_test(paths[0].c_str(), G_FILE_TEST_EXISTS));
    // The one unaccounted for waits for the next login
    ASSERT_EQ(std::to_string(chatIds[0]) + " " + std::to_string(messageIds[3]) + " " + paths[3] + "\n",
              std::string(purple_account_get_string(account, "temp-image-uploads", "")));
    remove(paths[3].c_str());
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();
}

TEST_F(PrivateChatTest, ReplyToOldMessage)
{
    const int32_t date     = 10002;
    const int64_t msgId    = 2;
    const int32_t srcDate  = 10001;
    const int64_t srcMsgId = 1;
    loginWithOneContact();

    object_ptr<message> message = makeMessage(
        msgId,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeTextMessage("reply")
    );
    message->reply_to_ = makeReplyTo(srcMsgId);

    tgl.update(make_object<updateNewMessage>(std::move(message)));
    tgl.verifyRequest(getMessage(chatIds[0], srcMsgId));
    prpl.verifyNoEvents();

    tgl.reply(makeMessage(
        srcMsgId,
        userIds[0],
        chatIds[0],
        false,
        srcDate,
        makeTextMessage("1<2")
    ));
    prpl.verifyEvents(
        ServGotImEvent(
            connection,
            purpleUserName(0),
            fmt::format(replyPattern, userFirstNames[0] + " " + userLastNames[0], "1&lt;2", "reply"),
            PURPLE_MESSAGE_RECV,
            date
        )
    );
    tgl.verifyRequest(viewMessages(chatIds[0], {msgId}, nullptr, true));
}

TEST_F(PrivateChatTest, ReplyToOldMessage_FetchTimeout)
{
    const int32_t date     = 10002;
    const int64_t msgId    = 2;
    const int32_t srcDate  = 10001;
    const int64_t srcMsgId = 1;
    loginWithOneContact();

    object_ptr<message> message = makeMessage(
        msgId,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeTextMessage("reply")
    );
    message->reply_to_ = makeReplyTo(srcMsgId);

    tgl.update(make_object<updateNewMessage>(std::move(message)));
    uint64_t getMessageReqId = tgl.verifyRequest(
        getMessage(chatIds[0], srcMsgId)
    );
    prpl.verifyNoEvents();

    runTimeouts();
    prpl.verifyEvents(
        ServGotImEvent(
            connection,
            purpleUserName(0),
            fmt::format(replyPattern, "Unknown user", "[message unavailable]", "reply"),
            PURPLE_MESSAGE_RECV,
            date
        )
    );
    tgl.verifyRequest(viewMessages(chatIds[0], {msgId}, nullptr, true));

    tgl.reply(getMessageReqId, makeMessage(
        srcMsgId, userIds[0], chatIds[0], false, srcDate,
        makeTextMessage("1<2")
    ));
    prpl.verifyNoEvents();
}

TEST_F(PrivateChatTest, TypingNotification)
{
    loginWithOneContact();

    pluginInfo().send_typing(connection, purpleUserName(0).c_str(), PURPLE_TYPING);
    tgl.verifyRequest(sendChatAction(chatIds[0], 0, "", make_object<chatActionTyping>()));

    pluginInfo().send_typing(connection, purpleUserName(0).c_str(), PURPLE_TYPED);
    tgl.verifyRequest(sendChatAction(chatIds[0], 0, "", make_object<chatActionCancel>()));

    pluginInfo().send_typing(connection, purpleUserName(0).c_str(), PURPLE_TYPING);
    tgl.verifyRequest(sendChatAction(chatIds[0], 0, "", make_object<chatActionTyping>()));

    pluginInfo().send_typing(connection, purpleUserName(0).c_str(), PURPLE_NOT_TYPING);
    tgl.verifyRequest(sendChatAction(chatIds[0], 0, "", make_object<chatActionCancel>()));
}

TEST_F(PrivateChatTest, DeleteContact)
{
    loginWithOneContact();

    PurpleBuddy *buddy = purple_find_buddy(account, purpleUserName(0).c_str());
    ASSERT_NE(nullptr, buddy);
    PurpleBuddy *dup   = purple_buddy_new(account, buddy->name, buddy->alias);
    purple_blist_remove_buddy(buddy);
    prpl.discardEvents();

    // Nothing of the chat stays here: TDLib's messages and files, Pidgin's log
    gchar *logDir  = g_build_filename(purple_user_dir(), "logs", "telegram", ("+" + selfPhoneNumber).c_str(),
                                      purpleUserName(0).c_str(), NULL);
    gchar *logFile = g_build_filename(logDir, "2026-11-24.120000-0500EST.html", NULL);
    ASSERT_EQ(0, g_mkdir_with_parents(logDir, 0700));
    ASSERT_TRUE(g_file_set_contents(logFile, "<html></html>", -1, NULL));
    pluginInfo().remove_buddy(connection, dup, NULL);
    purple_buddy_destroy(dup);
    {
        auto files = make_object<optimizeStorage>();
        files->chat_ids_ = {chatIds[0]};
        tgl.verifyRequests({
            make_object<deleteChatHistory>(chatIds[0], true, false),
            std::move(files),
            make_object<removeContacts>(std::vector<std::int64_t>(1, userIds[0]))
        });
    }
    EXPECT_FALSE(g_file_test(logDir, G_FILE_TEST_EXISTS));
    remove(logFile);
    rmdir(logDir);
    g_free(logFile);
    g_free(logDir);

    auto userUpdate1 = standardUpdateUser(0);
    userUpdate1->user_->is_contact_ = true;
    tgl.update(std::move(userUpdate1));
    tgl.update(standardUpdateUser(0));
    tgl.update(make_object<updateChatTitle>(chatIds[0], "New Title"));
    tgl.update(makeUpdateChatList(chatIds[0], make_object<chatListArchive>()));
    tgl.update(makeUpdateRemoveFromChatList(chatIds[0], make_object<chatListMain>()));
    tgl.update(makeUpdateRemoveFromChatList(chatIds[0], make_object<chatListArchive>()));
}

// TODO: test moving from main to archive or vice versa now that it's not atomic

// A contact written to by their username, in any case, with or without the "@"; a name that could be
// no username, as one in another alphabet, is not looked for as one
TEST_F(PrivateChatTest, SendToUsername)
{
    loginWithOneContact();
    auto user = makeUser(userIds[0], userFirstNames[0], userLastNames[0], userPhones[0],
                         make_object<userStatusOffline>());
    user->is_contact_ = true;
    user->usernames_  = make_object<usernames>();
    user->usernames_->active_usernames_.push_back("galileo_g");
    user->usernames_->editable_username_ = "galileo_g";
    auto update = make_object<updateUser>();
    update->user_ = std::move(user);
    tgl.update(std::move(update));
    prpl.discardEvents();

    for (const char *name: {"@Galileo_G", "GALILEO_G"}) {
        ASSERT_EQ(0, pluginInfo().send_im(connection, name, "message", PURPLE_MESSAGE_SEND));
        auto text = make_object<inputMessageText>();
        text->text_ = makeFormattedText("message");
        tgl.verifyRequest(*makeSendMessage(chatIds[0], 0, nullptr, nullptr, nullptr, std::move(text)));
    }

    pluginInfo().send_im(connection, "\xd0\x93\xd0\xb0\xd0\xbb\xd0\xb8\xd0\xbb\xd0\xb5\xd0\xb9", "message",
                         PURPLE_MESSAGE_SEND);
    tgl.verifyNoRequests();
    prpl.discardEvents();
}

TEST_F(PrivateChatTest, MessageSendResponseError)
{
    loginWithOneContact();

    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), "message", PURPLE_MESSAGE_SEND));
    tgl.verifyRequest(*makeSendMessage(
        chatIds[0],
        0,
        nullptr,
        nullptr,
        nullptr,
        make_object<inputMessageText>(
            make_object<formattedText>("message", std::vector<object_ptr<textEntity>>()),
            nullptr, false
        )
    ));

    tgl.reply(make_object<error>(100, "error"));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            "Failed to send message: code 100 (error)",
            PURPLE_MESSAGE_SYSTEM, 0
        )
    );
}

TEST_F(PrivateChatTest, SendMessage_SpecialCharactersAndHtml)
{
    loginWithOneContact();

    ASSERT_EQ(0, pluginInfo().send_im(
        connection,
        purpleUserName(0).c_str(),
        "<font size=\"3\">1&lt;2 3&gt;2</font>",
        PURPLE_MESSAGE_SEND
    ));
    tgl.verifyRequest(*makeSendMessage(
        chatIds[0],
        0,
        nullptr,
        nullptr,
        nullptr,
        make_object<inputMessageText>(
            // Our mock purple_unescape_html handles these two characters
            make_object<formattedText>("1<2 3>2", std::vector<object_ptr<textEntity>>()),
            nullptr, false
        )
    ));
}

TEST_F(PrivateChatTest, SentMessageLoggedOnceSent)
{
    const int64_t tempId  = 0x100001;
    const int64_t finalId = 0x200000;
    const int32_t date    = 123456;
    setLogMarkersEnabled(true);
    loginWithOneContact();
    getImConversation(account, purpleUserName(0).c_str());
    prpl.discardEvents();

    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), "Bye", PURPLE_MESSAGE_SEND));
    auto input = make_object<inputMessageText>();
    input->text_ = makeFormattedText("Bye");
    tgl.verifyRequest(*makeSendMessage(chatIds[0], 0, nullptr, nullptr, nullptr, std::move(input)));
    object_ptr<message> pending = makeMessage(tempId, userIds[0], chatIds[0], true, date, makeTextMessage("Bye"));
    pending->sending_state_ = make_object<messageSendingStatePending>();
    tgl.update(make_object<updateNewMessage>(std::move(pending)));
    prpl.verifyEvents(ConversationWriteEvent(purpleUserName(0), selfFirstName + " " + selfLastName, "Bye",
                                             PURPLE_MESSAGE_SEND, date));
    tgl.verifyRequest(viewMessages(chatIds[0], {tempId}, nullptr, true));
    // Not logged under an id it will not keep
    ASSERT_TRUE(takeLoggedMessages().empty());

    // Logged once sent, for the log alone, under the id it keeps, saying when it was written
    tgl.update(makeUpdateMessageSendSucceeded(
        makeMessage(finalId, userIds[0], chatIds[0], true, date, makeTextMessage("Bye")), tempId));
    std::string logged = "<!--tdlib-log-only-->Bye<!--tdlib-message " + std::to_string(finalId) + " " +
                         std::to_string(date) + "--><!--/tdlib-log-only-->";
    prpl.verifyEvents(ConversationWriteEvent(purpleUserName(0), selfFirstName + " " + selfLastName, logged,
                                             PURPLE_MESSAGE_SEND, date));
    ASSERT_EQ(std::vector<std::string>{logged}, takeLoggedMessages());
}

TEST_F(PrivateChatTest, SentMessageLoggedIfSentBeforeShown)
{
    const int64_t tempId    = 0x100001;
    const int64_t finalId   = 0x200000;
    const int64_t repliedId = 0x80000;
    const int32_t date      = 123456;
    setLogMarkersEnabled(true);
    loginWithOneContact();
    getImConversation(account, purpleUserName(0).c_str());
    prpl.discardEvents();

    // Waiting for the message it replies to, and sent meanwhile
    object_ptr<message> pending = makeMessage(tempId, userIds[0], chatIds[0], true, date, makeTextMessage("Bye"));
    pending->sending_state_ = make_object<messageSendingStatePending>();
    pending->reply_to_      = makeReplyTo(repliedId);
    tgl.update(make_object<updateNewMessage>(std::move(pending)));
    tgl.verifyRequest(getMessage(chatIds[0], repliedId));
    tgl.update(makeUpdateMessageSendSucceeded(
        makeMessage(finalId, userIds[0], chatIds[0], true, date, makeTextMessage("Bye")), tempId));
    prpl.verifyNoEvents();
    ASSERT_TRUE(takeLoggedMessages().empty());

    // Logged as it is shown, under the id it keeps
    tgl.reply(makeMessage(repliedId, userIds[0], chatIds[0], false, date - 1, makeTextMessage("Hi")));
    prpl.discardEvents();
    tgl.verifyRequest(viewMessages(chatIds[0], {tempId}, nullptr, true));
    std::vector<std::string> logged = takeLoggedMessages();
    ASSERT_EQ(1u, logged.size());
    ASSERT_NE(std::string::npos, logged[0].find("Bye<!--tdlib-message " + std::to_string(finalId) + "-->"));
}

TEST_F(PrivateChatTest, SentMessageNotLoggedWhenShownUnlogged)
{
    const int64_t tempId  = 0x100001;
    const int64_t finalId = 0x200000;
    const int32_t date    = 123456;
    setLogMarkersEnabled(true);
    loginWithOneContact();
    PurpleConversation *conv = getImConversation(account, purpleUserName(0).c_str());
    prpl.discardEvents();

    // Shown with the conversation not logging, it is not logged once sent, logging or not by then
    purple_conversation_set_logging(conv, FALSE);
    object_ptr<message> pending = makeMessage(tempId, userIds[0], chatIds[0], true, date, makeTextMessage("Bye"));
    pending->sending_state_ = make_object<messageSendingStatePending>();
    tgl.update(make_object<updateNewMessage>(std::move(pending)));
    tgl.verifyRequest(viewMessages(chatIds[0], {tempId}, nullptr, true));
    prpl.discardEvents();
    purple_conversation_set_logging(conv, TRUE);
    tgl.update(makeUpdateMessageSendSucceeded(
        makeMessage(finalId, userIds[0], chatIds[0], true, date, makeTextMessage("Bye")), tempId));
    prpl.verifyNoEvents();
    ASSERT_TRUE(takeLoggedMessages().empty());
}

TEST_F(PrivateChatTest, SentMessageLoggedWithTabClosed)
{
    const int64_t tempId  = 0x100001;
    const int64_t finalId = 0x200000;
    const int32_t date    = 123456;
    setLogMarkersEnabled(true);
    loginWithOneContact();
    PurpleConversation *conv = getImConversation(account, purpleUserName(0).c_str());
    prpl.discardEvents();

    object_ptr<message> pending = makeMessage(tempId, userIds[0], chatIds[0], true, date, makeTextMessage("Bye"));
    pending->sending_state_ = make_object<messageSendingStatePending>();
    tgl.update(make_object<updateNewMessage>(std::move(pending)));
    tgl.verifyRequest(viewMessages(chatIds[0], {tempId}, nullptr, true));
    prpl.discardEvents();
    ASSERT_TRUE(takeLoggedMessages().empty());

    // Its tab closed before it is sent, it is logged all the same, straight into the log
    purple_conversation_destroy(conv);
    prpl.discardEvents();
    tgl.update(makeUpdateMessageSendSucceeded(
        makeMessage(finalId, userIds[0], chatIds[0], true, date, makeTextMessage("Bye")), tempId));
    prpl.verifyNoEvents();
    ASSERT_EQ(std::vector<std::string>{"Bye<!--tdlib-message " + std::to_string(finalId) + " " +
                                       std::to_string(date) + "-->"},
              takeLoggedMessages());
}

TEST_F(PrivateChatTest, SentMessageNotLoggedWithTabClosedIfNotLogging)
{
    const int64_t tempId  = 0x100001;
    const int64_t finalId = 0x200000;
    const int32_t date    = 123456;
    setLogMarkersEnabled(true);
    loginWithOneContact();
    PurpleConversation *conv = getImConversation(account, purpleUserName(0).c_str());
    prpl.discardEvents();

    object_ptr<message> pending = makeMessage(tempId, userIds[0], chatIds[0], true, date, makeTextMessage("Bye"));
    pending->sending_state_ = make_object<messageSendingStatePending>();
    tgl.update(make_object<updateNewMessage>(std::move(pending)));
    tgl.verifyRequest(viewMessages(chatIds[0], {tempId}, nullptr, true));
    prpl.discardEvents();

    // Its tab closed before it is sent, with Pidgin not logging new conversations, it is not logged
    purple_conversation_destroy(conv);
    setLoggingPreferred(false);
    prpl.discardEvents();
    tgl.update(makeUpdateMessageSendSucceeded(
        makeMessage(finalId, userIds[0], chatIds[0], true, date, makeTextMessage("Bye")), tempId));
    prpl.verifyNoEvents();
    ASSERT_TRUE(takeLoggedMessages().empty());
}

TEST_F(PrivateChatTest, SentMessageNotLoggedIfLoggingTurnedOff)
{
    const int64_t tempId  = 0x100001;
    const int64_t finalId = 0x200000;
    const int32_t date    = 123456;
    setLogMarkersEnabled(true);
    loginWithOneContact();
    PurpleConversation *conv = getImConversation(account, purpleUserName(0).c_str());
    prpl.discardEvents();

    object_ptr<message> pending = makeMessage(tempId, userIds[0], chatIds[0], true, date, makeTextMessage("Bye"));
    pending->sending_state_ = make_object<messageSendingStatePending>();
    tgl.update(make_object<updateNewMessage>(std::move(pending)));
    tgl.verifyRequest(viewMessages(chatIds[0], {tempId}, nullptr, true));
    prpl.discardEvents();

    // Logging turned off in its tab before it is sent: not logged
    purple_conversation_set_logging(conv, FALSE);
    tgl.update(makeUpdateMessageSendSucceeded(
        makeMessage(finalId, userIds[0], chatIds[0], true, date, makeTextMessage("Bye")), tempId));
    prpl.discardEvents();
    ASSERT_TRUE(takeLoggedMessages().empty());
}

TEST_F(PrivateChatTest, ReceiveMessage_SpecialCharacters)
{
    constexpr int64_t messageId = 10000;
    constexpr int32_t date      = 123456;

    loginWithOneContact();
    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeTextMessage("1<2 3>2")
    )));
    prpl.verifyEvents(ServGotImEvent(
        connection,
        purpleUserName(0),
        "1&lt;2 3&gt;2", // mock purple_markup_escape_text handles these two
        PURPLE_MESSAGE_RECV,
        date
    ));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
}

TEST_F(PrivateChatTest, WriteToNonContact_CreatePrivateChatFail)
{
    login();
    // Normaly users without chats have to be group chat members, but for the test it doesn't matter
    tgl.update(standardUpdateUser(1));

    ASSERT_EQ(0, pluginInfo().send_im(
        connection,
        purpleUserName(1).c_str(),
        "message",
        PURPLE_MESSAGE_SEND
    ));
    tgl.verifyRequest(createPrivateChat(userIds[1], false));

    tgl.reply(make_object<error>(100, "error"));
    prpl.verifyEvents(
        NewConversationEvent(
            PURPLE_CONV_TYPE_IM, account,
            purpleUserName(1)
        ),
        ConversationWriteEvent(
            purpleUserName(1),
            purpleUserName(1),
            "Failed to open chat: code 100 (error)",
            PURPLE_MESSAGE_ERROR, 0
        )
    );
}

TEST_F(PrivateChatTest, WriteToUnknownUser)
{
    login();
    ASSERT_EQ(-1, pluginInfo().send_im(
        connection,
        "Antonie van Leeuwenhoek",
        "message",
        PURPLE_MESSAGE_SEND
    ));

    prpl.verifyEvents(
        NewConversationEvent(
            PURPLE_CONV_TYPE_IM, account,
            "Antonie van Leeuwenhoek"
        ),
        ConversationWriteEvent(
            "Antonie van Leeuwenhoek", "Antonie van Leeuwenhoek",
            "User not found",
            PURPLE_MESSAGE_ERROR, 0
        )
    );
}

TEST_F(PrivateChatTest, BuddyWithNullAlias)
{
    PurpleBuddy *buddy = purple_buddy_new(account, purpleUserName(0).c_str(), NULL);
    purple_blist_add_buddy(buddy, NULL, &standardPurpleGroup, NULL);
    prpl.discardEvents();

    login(
        {standardUpdateUser(0), standardPrivateChat(0), makeUpdateChatListMain(chatIds[0])},
        make_object<users>(1, std::vector<int64_t>(1, userIds[0])),
        make_object<chats>(1, std::vector<int64_t>(1, chatIds[0])),
        {
            std::make_unique<AliasBuddyEvent>(purpleUserName(0), userFirstNames[0] + " " + userLastNames[0]),
        }, {},
        {
            std::make_unique<UserStatusEvent>(account, purpleUserName(0), PURPLE_STATUS_AWAY),
            std::make_unique<AccountSetAliasEvent>(account, selfFirstName + " " + selfLastName),
            std::make_unique<ShowAccountEvent>(account)
        }
    );
}

TEST_F(PrivateChatTest, CallEnded)
{
    constexpr int64_t messageId = 10000;
    constexpr int32_t date      = 123456;
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessageCall(false, nullptr, 137)
    )));

    tgl.verifyRequest(viewMessages(
        chatIds[0],
        {messageId}, nullptr,
        true
    ));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            "Call ended (00:02:17): reason unknown",
            PURPLE_MESSAGE_SYSTEM, 0
        )
    );
}

TEST_F(PrivateChatTest, RemoteSend)
{
    constexpr int64_t messageId = 10000;
    constexpr int32_t date      = 123456;

    loginWithOneContact();

    object_ptr<message> message = makeMessage(
        messageId,
        selfId,
        chatIds[0],
        true,
        date,
        makeTextMessage("text")
    );
    ASSERT_NE(nullptr, message->sending_state_);
    message->sending_state_ = nullptr;
    tgl.update(make_object<updateNewMessage>(std::move(message)));
    tgl.verifyRequest(viewMessages(
        chatIds[0],
        {messageId}, nullptr,
        true
    ));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(
            purpleUserName(0),
            selfFirstName + " " + selfLastName,
            "text",
            (PurpleMessageFlags)(PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND),
            date
        )
    );
}

void PrivateChatTest::testReadReceipt(bool shouldSend)
{
    const int64_t messageId = 1;
    const int32_t date      = 10001;

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId, userIds[0], chatIds[0], false, date,
        makeTextMessage("text")
    )));
    prpl.verifyEvents(ServGotImEvent(
        connection, purpleUserName(0), "text", PURPLE_MESSAGE_RECV, date
    ));

    if (shouldSend)
        tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    else
        tgl.verifyNoRequests();
}

TEST_F(PrivateChatTest, DisablingReadReceipts)
{
    loginWithOneContact();

    setUiName("BitlBee");
    testReadReceipt(true);
    purple_account_set_bool(account, "read-receipts", FALSE);
    testReadReceipt(false);
    setUiName("Spectrum");
    testReadReceipt(false);
    setUiName("pidgin");
    testReadReceipt(true);
}

static void countMessage(const gchar *, GLogLevelFlags, const gchar *, gpointer count)
{
    ++*static_cast<int *>(count);
}

TEST(PurpleInfoTest, UiNameWithoutUiInfo)
{
    // GLib would return NULL from the lookup anyway; what the guard spares is its CRITICAL
    int criticals = 0;
    guint handler = g_log_set_handler("GLib", G_LOG_LEVEL_CRITICAL, countMessage, &criticals);
    setUiInfoMissing(true);
    EXPECT_STREQ("", getUiName());
    setUiInfoMissing(false);
    g_log_remove_handler("GLib", handler);
    EXPECT_EQ(0, criticals);
}

TEST_F(PrivateChatTest, DeletedAccount)
{
    const std::string name = userFirstNames[0] + " " + userLastNames[0];
    loginWithOneContact();
    purple_conversation_new(PURPLE_CONV_TYPE_IM, account, purpleUserName(0).c_str());
    prpl.discardEvents();

    // TDLib empties a deleted user's names; the buddy keeps its own, marked, in a group of its own
    auto deletedUser = makeUser(userIds[0], "", "", "", make_object<userStatusEmpty>());
    deletedUser->type_ = make_object<userTypeDeleted>();
    tgl.update(make_object<updateUser>(std::move(deletedUser)));

    PurpleGroup *group = purple_find_group("Deleted Accounts (Telegram)");
    ASSERT_NE(nullptr, group);
    prpl.verifyEvents(
        AliasBuddyEvent(purpleUserName(0), name + " (deleted)"),
        AddBuddyEvent(purpleUserName(0), name + " (deleted)", account, nullptr, group, nullptr),
        UserStatusEvent(account, purpleUserName(0), PURPLE_STATUS_OFFLINE),
        ConversationWriteEvent(purpleUserName(0), "", "This account has been deleted.", PURPLE_MESSAGE_SYSTEM, 0)
    );

    // The chat's title follows, empty, and changes nothing
    tgl.update(makeUpdateChatTitle(chatIds[0], ""));
    prpl.verifyNoEvents();

    // Should the account come back, so does the buddy
    tgl.update(standardUpdateUser(0));
    tgl.update(makeUpdateChatTitle(chatIds[0], name));
    prpl.verifyEvents(
        AddBuddyEvent(purpleUserName(0), name + " (deleted)", account, nullptr, nullptr, nullptr),
        ConversationWriteEvent(purpleUserName(0), "", "This account has been restored.", PURPLE_MESSAGE_SYSTEM, 0),
        AliasBuddyEvent(purpleUserName(0), name)
    );
}

TEST_F(PrivateChatTest, DeletedAccountsGroupRenamed)
{
    // Made under another translation, so renamed after this one
    PurpleGroup *group = purple_group_new("Gelöschte Konten (Telegram)");
    purple_blist_add_group(group, NULL);
    purple_blist_node_set_string(PURPLE_BLIST_NODE(group), "tdlib-deleted-accounts", "Gelöschte Konten (Telegram)");
    // Named by the user, so left alone
    PurpleGroup *ownGroup = purple_group_new("Gone");
    purple_blist_add_group(ownGroup, NULL);

    loginWithOneContact();
    EXPECT_STREQ("Deleted Accounts (Telegram)", purple_group_get_name(group));

    purple_blist_rename_group(group, "Gone for good");
    tgl.update(makeUpdateChatTitle(chatIds[0], "x"));
    prpl.discardEvents();
    auto deletedUser = makeUser(userIds[0], "", "", "", make_object<userStatusEmpty>());
    deletedUser->type_ = make_object<userTypeDeleted>();
    tgl.update(make_object<updateUser>(std::move(deletedUser)));
    // Found by its setting, whatever its name
    EXPECT_EQ(group, purple_buddy_get_group(purple_find_buddy(account, purpleUserName(0).c_str())));
    EXPECT_STREQ("Gone for good", purple_group_get_name(group));
    prpl.discardEvents();
}

TEST_F(PrivateChatTest, DeletedAccountsFoundAtLogin)
{
    // Buddies from an earlier session: one deleted meanwhile, the other no longer in any chat
    for (unsigned i: {0, 1}) {
        PurpleBuddy *buddy = purple_buddy_new(account, purpleUserName(i).c_str(),
                                              (userFirstNames[i] + " " + userLastNames[i]).c_str());
        purple_blist_add_buddy(buddy, NULL, NULL, NULL);
    }
    PurpleGroup *group = purple_group_new("Deleted Accounts (Telegram)");
    purple_blist_add_group(group, NULL);
    purple_blist_node_set_string(PURPLE_BLIST_NODE(group), "tdlib-deleted-accounts", "Deleted Accounts (Telegram)");
    prpl.discardEvents();

    auto deletedUser = makeUser(userIds[0], "", "", "", make_object<userStatusEmpty>());
    deletedUser->type_ = make_object<userTypeDeleted>();
    login({make_object<updateUser>(std::move(deletedUser))}, make_object<users>(), getChatsNoChatsResponse(),
          {
              std::make_unique<AliasBuddyEvent>(purpleUserName(0), userFirstNames[0] + " " + userLastNames[0] + " (deleted)"),
              std::make_unique<AddBuddyEvent>(purpleUserName(0), userFirstNames[0] + " " + userLastNames[0] + " (deleted)",
                                              account, nullptr, group, nullptr),
              std::make_unique<UserStatusEvent>(account, purpleUserName(0), PURPLE_STATUS_OFFLINE)
          }, {}, {
              std::make_unique<AccountSetAliasEvent>(account, selfFirstName + " " + selfLastName),
              std::make_unique<ShowAccountEvent>(account)
          });

    // TDLib is asked about the other
    tgl.verifyRequest(getUser(userIds[1]));
    deletedUser = makeUser(userIds[1], "", "", "", make_object<userStatusEmpty>());
    deletedUser->type_ = make_object<userTypeDeleted>();
    tgl.reply(std::move(deletedUser));
    prpl.verifyEvents(
        AliasBuddyEvent(purpleUserName(1), userFirstNames[1] + " " + userLastNames[1] + " (deleted)"),
        AddBuddyEvent(purpleUserName(1), userFirstNames[1] + " " + userLastNames[1] + " (deleted)",
                      account, nullptr, group, nullptr),
        UserStatusEvent(account, purpleUserName(1), PURPLE_STATUS_OFFLINE)
    );

    // Back, though still without a chat
    tgl.update(standardUpdateUser(1));
    prpl.verifyEvents(
        AddBuddyEvent(purpleUserName(1), userFirstNames[1] + " " + userLastNames[1] + " (deleted)",
                      account, nullptr, nullptr, nullptr),
        AliasBuddyEvent(purpleUserName(1), userFirstNames[1] + " " + userLastNames[1])
    );
}

TEST_F(PrivateChatTest, ContactRegistered)
{
    constexpr int64_t messageId = 10000;
    constexpr int32_t date      = 123456;
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageContactRegistered>()
    )));

    tgl.verifyRequest(viewMessages(
        chatIds[0],
        {messageId}, nullptr,
        true
    ));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + " joined Telegram",
            PURPLE_MESSAGE_SYSTEM, date
        )
    );
}

TEST_F(PrivateChatTest, FindOurselvesByAccountName)
{
    loginWithOneContact();
    // As group chats list us, with "+" or without
    auto *client = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(connection));
    for (const std::string &name: {"+" + selfPhoneNumber, selfPhoneNumber}) {
        std::vector<const td::td_api::user *> users;
        client->getUsers(name.c_str(), users);
        ASSERT_EQ(1u, users.size());
        EXPECT_EQ(selfId, users[0]->id_);
    }
}

TEST_F(PrivateChatTest, OpenWhileShown)
{
    loginWithOneContact();
    PurpleTdClient     *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    PurpleConversation *conv   = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, purpleUserName(0).c_str());
    ASSERT_NE(nullptr, client);
    prpl.discardEvents();

    // Not open yet, so not seen
    client->messagesViewed(conv, {MessageId::fromString("1")});
    tgl.verifyNoRequests();

    client->conversationShown(conv, true);
    tgl.verifyRequest(openChat(chatIds[0]));
    client->conversationShown(conv, true);
    tgl.verifyNoRequests();

    client->messagesViewed(conv, {MessageId::fromString("1"), MessageId::fromString("2")});
    tgl.verifyRequest(viewMessages(chatIds[0], {1, 2}, nullptr, false));
    // The same again: TDLib knows
    client->messagesViewed(conv, {MessageId::fromString("1"), MessageId::fromString("2")});
    tgl.verifyNoRequests();
    client->messagesViewed(conv, {MessageId::fromString("2"), MessageId::fromString("3")});
    tgl.verifyRequest(viewMessages(chatIds[0], {2, 3}, nullptr, false));

    client->conversationShown(conv, false);
    tgl.verifyRequest(closeChat(chatIds[0]));
    client->conversationShown(conv, false);
    client->messagesViewed(conv, {MessageId::fromString("3")});
    tgl.verifyNoRequests();

    // Seen anew once opened again
    client->conversationShown(conv, true);
    tgl.verifyRequest(openChat(chatIds[0]));
    client->messagesViewed(conv, {MessageId::fromString("2"), MessageId::fromString("3")});
    tgl.verifyRequest(viewMessages(chatIds[0], {2, 3}, nullptr, false));
}

TEST_F(PrivateChatTest, OfflineWhileIdle)
{
    loginWithOneContact();
    PurpleTdClient     *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    PurpleConversation *conv   = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, purpleUserName(0).c_str());
    ASSERT_NE(nullptr, client);
    prpl.discardEvents();
    client->conversationShown(conv, true);
    tgl.verifyRequest(openChat(chatIds[0]));
    client->messagesViewed(conv, {MessageId::fromString("1"), MessageId::fromString("2")});
    tgl.verifyRequest(viewMessages(chatIds[0], {1, 2}, nullptr, false));

    pluginInfo().set_idle(connection, 300);
    tgl.verifyRequests({
        makeOnlineOption(false),
        makeCloseChat(chatIds[0])
    });
    pluginInfo().set_idle(connection, 310);
    tgl.verifyNoRequests();
    // Still shown, but nobody looks
    client->conversationShown(conv, true);
    client->messagesViewed(conv, {MessageId::fromString("2"), MessageId::fromString("3")});
    tgl.verifyNoRequests();

    // Back: the chat is opened again, and the messages seen told anew, for TDLib to poll them
    pluginInfo().set_idle(connection, 0);
    tgl.verifyRequests({
        makeOnlineOption(true),
        makeOpenChat(chatIds[0])
    });
    client->messagesViewed(conv, {MessageId::fromString("1"), MessageId::fromString("2")});
    tgl.verifyRequest(viewMessages(chatIds[0], {1, 2}, nullptr, false));
}

TEST_F(PrivateChatTest, ReadElsewhere)
{
    loginWithOneContact();
    PurpleConversation *conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, purpleUserName(0).c_str());
    prpl.discardEvents();
    // As Pidgin keeps them, for a conversation not in a window
    purple_conversation_set_data(conv, "unseen-count", GINT_TO_POINTER(3));
    purple_conversation_set_data(conv, "unseen-state", GINT_TO_POINTER(2));

    // Still some unread: Pidgin's count stays, being its own
    tgl.update(makeUpdateChatReadInbox(chatIds[0], 2, 1));
    EXPECT_EQ(3, GPOINTER_TO_INT(purple_conversation_get_data(conv, "unseen-count")));
    EXPECT_EQ(2, GPOINTER_TO_INT(purple_conversation_get_data(conv, "unseen-state")));

    tgl.update(makeUpdateChatReadInbox(chatIds[0], 3, 0));
#ifndef NoPidgin
    EXPECT_EQ(0, GPOINTER_TO_INT(purple_conversation_get_data(conv, "unseen-count")));
    EXPECT_EQ(0, GPOINTER_TO_INT(purple_conversation_get_data(conv, "unseen-state")));
#else
    // Built without Pidgin, for which the counts are
    EXPECT_EQ(3, GPOINTER_TO_INT(purple_conversation_get_data(conv, "unseen-count")));
#endif
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();
}

TEST_F(PrivateChatTest, IdleAtLogin)
{
    // Neither online nor any chat open, until the user is back
    setPresenceIdle(true);
    chatListReadyAtLogin = false;
    loginWithOneContact();
    tgl.verifyNoRequests();
    PurpleTdClient     *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    PurpleConversation *conv   = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, purpleUserName(0).c_str());
    ASSERT_NE(nullptr, client);
    prpl.discardEvents();
    client->conversationShown(conv, true);
    tgl.verifyNoRequests();

    pluginInfo().set_idle(connection, 0);
    tgl.verifyRequests({
        makeOnlineOption(true),
        makeOpenChat(chatIds[0])
    });
}

TEST_F(PrivateChatTest, ShownBeforeChatKnown)
{
    login();
    // A contact in the buddy list whose chat TDLib tells of only later
    tgl.update(standardUpdateUser(1));
    purple_blist_add_buddy(purple_buddy_new(account, purpleUserName(1).c_str(), NULL), NULL, NULL, NULL);
    PurpleTdClient     *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    PurpleConversation *conv   = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, purpleUserName(1).c_str());
    ASSERT_NE(nullptr, client);
    client->conversationShown(conv, true);
    tgl.verifyNoRequests();

    // Opened once known
    tgl.update(standardPrivateChat(1, make_object<chatListMain>()));
    tgl.verifyRequest(openChat(chatIds[1]));
    prpl.discardEvents();
}

TEST_F(PrivateChatTest, OpenOrViewRejected)
{
    loginWithOneContact();
    PurpleTdClient     *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    PurpleConversation *conv   = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, purpleUserName(0).c_str());
    ASSERT_NE(nullptr, client);
    prpl.discardEvents();

    // Not open, so opened again when next shown
    client->conversationShown(conv, true);
    uint64_t requestId = tgl.verifyRequest(openChat(chatIds[0]));
    tgl.reply(requestId, makeError(400, "Chat not found"));
    client->conversationShown(conv, true);
    requestId = tgl.verifyRequest(openChat(chatIds[0]));
    tgl.reply(requestId, make_object<ok>());

    // The same messages told again once seen again
    std::vector<MessageId> seen = {MessageId::fromString("1"), MessageId::fromString("2")};
    client->messagesViewed(conv, seen);
    requestId = tgl.verifyRequest(viewMessages(chatIds[0], {1, 2}, nullptr, false));
    tgl.reply(requestId, makeError(400, "Bad request"));
    client->messagesViewed(conv, seen);
    requestId = tgl.verifyRequest(viewMessages(chatIds[0], {1, 2}, nullptr, false));
    tgl.reply(requestId, make_object<ok>());
    client->messagesViewed(conv, seen);
    tgl.verifyNoRequests();
}

// Names the plugin gives are "id" or "secret" and digits only: anything else is no id, however it
// begins, and is not read past its end
TEST(BuddyNameTest, IdsAreDigitsOnly)
{
    EXPECT_EQ(1972, purpleBuddyNameToUserId("id1972").value());
    EXPECT_TRUE(isUserIdName("id0"));
    EXPECT_FALSE(purpleBuddyNameToUserId("id0").valid());
    for (const char *name: {"id1972abc", "id 1972", "idris", "id", "Id1972", "+1972"}) {
        EXPECT_FALSE(isUserIdName(name)) << name;
        EXPECT_FALSE(purpleBuddyNameToUserId(name).valid()) << name;
    }
    EXPECT_EQ(12, purpleBuddyNameToSecretChatId("secret12").value());
    for (const char *name: {"se", "Sean", "secret", "secret12x", "secrets"})
        EXPECT_FALSE(purpleBuddyNameToSecretChatId(name).valid()) << name;
}

TEST_F(PrivateChatTest, SentByIdOnly)
{
    loginWithOneContact();
    tgl.update(standardUpdateUser(1));
    prpl.discardEvents();

    // A display name, which anyone may choose, is no one to send to; nor is an id no user has
    for (const std::string &name: {userFirstNames[1] + " " + userLastNames[1], std::string("id0")}) {
        EXPECT_EQ(-1, pluginInfo().send_im(connection, name.c_str(), "text", PURPLE_MESSAGE_SEND)) << name;
        tgl.verifyNoRequests();
        prpl.discardEvents();
    }
    // The id is
    EXPECT_EQ(0, pluginInfo().send_im(connection, purpleUserName(1).c_str(), "text", PURPLE_MESSAGE_SEND));
    tgl.verifyRequest(createPrivateChat(userIds[1], false));
}

TEST_F(PrivateChatTest, TransientBuddyRemoved)
{
    loginWithOneContact();
    tgl.update(standardUpdateUser(1));
    PurpleTdClient *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    ASSERT_NE(nullptr, client);
    // As a group's member's conversation is asked for, from the member list
    EXPECT_EQ(purpleUserName(1), client->getChatMemberImName(purpleUserName(1).c_str()));
    PurpleBuddy *buddy = purple_find_buddy(account, purpleUserName(1).c_str());
    ASSERT_NE(nullptr, buddy);
    EXPECT_TRUE(purple_blist_node_get_flags(PURPLE_BLIST_NODE(buddy)) & PURPLE_BLIST_NODE_FLAG_NO_SAVE);
    prpl.discardEvents();

    // Taken out of the list by hand: nothing is removed from Telegram, no contact nor chat
    pluginInfo().remove_buddy(connection, buddy, purple_buddy_get_group(buddy));
    tgl.verifyNoRequests();
}

TEST_F(PrivateChatTest, AddBuddyById)
{
    loginWithOneContact();
    tgl.update(standardUpdateUser(1));
    prpl.discardEvents();

    // As from a group's member list, the member being named by id
    PurpleBuddy *buddy = purple_buddy_new(account, purpleUserName(1).c_str(), NULL);
    purple_blist_add_buddy(buddy, NULL, &standardPurpleGroup, NULL);
    prpl.discardEvents();
    pluginInfo().add_buddy(connection, buddy, &standardPurpleGroup);
    prpl.verifyEvents(RemoveBuddyEvent(account, purpleUserName(1)));
    tgl.verifyRequest(*makeAddContact(userIds[1], "", userFirstNames[1], userLastNames[1]));
}

TEST_F(PrivateChatTest, OpenUnknownBuddy)
{
    login();
    PurpleTdClient *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    ASSERT_NE(nullptr, client);

    // A buddy TDLib did not tell of at login: the chat is asked for once, however often filled
    PurpleConversation *conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, purpleUserName(0).c_str());
    prpl.verifyEvents(NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)));
    ASSERT_TRUE(client->fillConversation(conv));
    ASSERT_TRUE(client->fillConversation(conv));
    tgl.verifyRequest(createPrivateChat(userIds[0], false));
    tgl.verifyNoRequests();

    // TDLib tells of the user and the chat, then answers with the chat, which fills the tab: in Pidgin
    tgl.update(standardUpdateUser(0));
    tgl.update(standardPrivateChat(0));
    tgl.reply(std::move(standardPrivateChat(0)->chat_));
    // Open in Pidgin: TDLib is told so, for the chat's updates; and the chat is not asked for again
    tgl.verifyRequest(openChat(chatIds[0]));
    tgl.verifyNoRequests();
    prpl.discardEvents();
    ASSERT_TRUE(client->fillConversation(conv));
    tgl.verifyNoRequests();
}

TEST_F(PrivateChatTest, OpenBuddyUnknownToTelegram)
{
    login();
    PurpleTdClient *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    ASSERT_NE(nullptr, client);

    PurpleConversation *conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, purpleUserName(0).c_str());
    prpl.verifyEvents(NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)));
    ASSERT_TRUE(client->fillConversation(conv));
    tgl.verifyRequest(createPrivateChat(userIds[0], false));
    tgl.reply(makeError(400, "USER_NOT_FOUND <x>"));
    prpl.verifyEvents(ConversationWriteEvent(purpleUserName(0), "",
                                             "Telegram has no chat with this user: code 400 (USER_NOT_FOUND &lt;x&gt;)",
                                             (PurpleMessageFlags)(PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG), 0));
}

#if TDLIB_API_RICH_MESSAGE
static object_ptr<RichText> plainText(const std::string &text)
{
    auto result = make_object<richTextPlain>();
    result->text_ = text;
    return std::move(result);
}

// A rich message, laid out as an instant view page is: its text and structure, its media as
// placeholders with their captions
TEST_F(PrivateChatTest, RichMessage)
{
    const int32_t date = 10001;
    loginWithOneContact();

    auto message = make_object<richMessage>();
    {
        auto heading = make_object<pageBlockSectionHeading>();
        heading->text_ = plainText("Vase <Verona>");
        heading->size_ = 1;
        message->blocks_.push_back(std::move(heading));
    }
    {
        auto bold = make_object<richTextBold>();
        bold->text_ = plainText("800");
        auto link = make_object<richTextUrl>();
        link->text_ = plainText("shop");
        link->url_  = "https://example.com/?a=1&b=2";
        auto texts = make_object<richTexts>();
        texts->texts_.push_back(std::move(bold));
        texts->texts_.push_back(plainText(" UAH at the "));
        texts->texts_.push_back(std::move(link));
        auto paragraph = make_object<pageBlockParagraph>();
        paragraph->text_ = std::move(texts);
        message->blocks_.push_back(std::move(paragraph));
    }
    {
        auto collage = make_object<pageBlockCollage>();
        for (int i = 0; i < 2; i++)
            collage->blocks_.push_back(make_object<pageBlockPhoto>());
        collage->caption_ = make_object<pageBlockCaption>();
        collage->caption_->text_ = plainText("Both sides");
        message->blocks_.push_back(std::move(collage));
    }
    {
        auto list = make_object<pageBlockList>();
        for (bool checked: {true, false}) {
            auto paragraph = make_object<pageBlockParagraph>();
            paragraph->text_ = plainText(checked ? "glazed" : "signed");
            auto item = make_object<pageBlockListItem>();
            item->has_checkbox_ = true;
            item->is_checked_   = checked;
            item->blocks_.push_back(std::move(paragraph));
            list->items_.push_back(std::move(item));
        }
        message->blocks_.push_back(std::move(list));
    }
    {
        auto table = make_object<pageBlockTable>();
        table->cells_.resize(2);
        for (const char *text: {"Size", "Price"}) {
            auto cell = make_object<pageBlockTableCell>();
            cell->text_      = plainText(text);
            cell->is_header_ = true;
            table->cells_[0].push_back(std::move(cell));
        }
        for (const char *text: {"30 cm", "800"}) {
            auto cell = make_object<pageBlockTableCell>();
            cell->text_ = plainText(text);
            table->cells_[1].push_back(std::move(cell));
        }
        message->blocks_.push_back(std::move(table));
    }
    {
        // A link to a file is its text alone; a spoiler, hidden until selected
        auto local = make_object<richTextUrl>();
        local->text_ = plainText("here");
        local->url_  = "file:///etc/passwd";
        auto spoiler = make_object<richTextSpoiler>();
        spoiler->text_ = plainText("secret");
        auto texts = make_object<richTexts>();
        texts->texts_.push_back(std::move(local));
        texts->texts_.push_back(plainText(" "));
        texts->texts_.push_back(std::move(spoiler));
        auto paragraph = make_object<pageBlockParagraph>();
        paragraph->text_ = std::move(texts);
        message->blocks_.push_back(std::move(paragraph));
    }
    message->blocks_.push_back(make_object<pageBlockAnchor>());
    auto content = make_object<messageRichMessage>();
    content->message_ = std::move(message);

    tgl.update(make_object<updateNewMessage>(makeMessage(1, userIds[0], chatIds[0], false, date, std::move(content))));
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0),
        "<b><font size=\"4\">Vase &lt;Verona&gt;</font></b><br>"
        // The mock escapes only < and >, as the rest of the tests have it
        "<b>800</b> UAH at the <a href=\"https://example.com/?a=1&b=2\">shop</a><br>"
        "[photo] [photo] Both sides<br>"
        "\xe2\x98\x91 glazed<br>"
        "\xe2\x98\x90 signed<br>"
        "<b>Size</b> | <b>Price</b><br>"
        "30 cm | 800<br>"
        "here <font color=\"#808080\" back=\"#808080\">secret</font>",
        PURPLE_MESSAGE_RECV, date));
}
#endif

#if TDLIB_API_RICH_MESSAGE
// A rich message's pictures are shown in it where they are, those still to come fetched
TEST_F(PrivateChatTest, RichMessagePictures)
{
    const int32_t date = 10001;
    purple_account_set_string(account, "media-size-threshold", "0");
    loginWithOneContact();

    auto collage = make_object<pageBlockCollage>();
    for (bool here: {true, false}) {
        auto photoBlock = make_object<pageBlockPhoto>();
        photoBlock->photo_ = make_object<photo>();
        photoBlock->photo_->sizes_.push_back(makePhotoSize(::makeFile(here ? 101 : 102, 10000,
                                                                       here ? TEST_SOURCE_DIR "/test.webp" : ""),
                                                           640, 480));
        collage->blocks_.push_back(std::move(photoBlock));
    }
    collage->caption_ = make_object<pageBlockCaption>();
    collage->caption_->text_ = plainText("Both sides");
    auto rich = make_object<richMessage>();
    rich->blocks_.push_back(std::move(collage));
    auto content = make_object<messageRichMessage>();
    content->message_ = std::move(rich);

    // Shown at once, the one here as it is, the other fetched, its placeholder written over once it is
    // here, where Pidgin's tab can take it
    tgl.update(make_object<updateNewMessage>(makeMessage(1, userIds[0], chatIds[0], false, date, std::move(content))));
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0),
                                     "<img id=\"" + std::to_string(getLastImgstoreId()) + "\"> [photo] Both sides",
                                     (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES), date));
    uint64_t download = tgl.verifyRequests({
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, 1), nullptr, true),
        make_object<downloadFile>(102, 1, 0, 0, true)
    })[1];
    tgl.reply(download, ::makeFile(102, 10000, TEST_SOURCE_DIR "/test.webp"));
    tgl.verifyRequest(getMessage(chatIds[0], 1));
}
#endif

#if TDLIB_API_RICH_MESSAGE && !defined(NoPidgin)
// A message waiting to be logged with its pictures keeps those shown already in the image store,
// until it is logged, here as the account goes
TEST_F(PrivateChatTest, RichMessagePicturesHeldForLog)
{
    const int32_t date = 10001;
    purple_account_set_string(account, "media-size-threshold", "0");
    setLogMarkersEnabled(true);
    loginWithOneContact();

    auto collage = make_object<pageBlockCollage>();
    for (bool here: {true, false}) {
        auto photoBlock = make_object<pageBlockPhoto>();
        photoBlock->photo_ = make_object<photo>();
        photoBlock->photo_->sizes_.push_back(makePhotoSize(::makeFile(here ? 101 : 102, 10000,
                                                                       here ? TEST_SOURCE_DIR "/test.webp" : ""),
                                                           640, 480));
        collage->blocks_.push_back(std::move(photoBlock));
    }
    auto rich = make_object<richMessage>();
    rich->blocks_.push_back(std::move(collage));
    auto content = make_object<messageRichMessage>();
    content->message_ = std::move(rich);
    tgl.update(make_object<updateNewMessage>(makeMessage(1, userIds[0], chatIds[0], false, date, std::move(content))));
    int shown = getLastImgstoreId();
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ServGotImEvent(connection, purpleUserName(0),
                       "<img id=\"" + std::to_string(shown) + "\"> [photo]<!--tdlib-message 1-->",
                       (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES), date)
    );
    tgl.verifyRequests({
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, 1), nullptr, true),
        make_object<downloadFile>(102, 1, 0, 0, true)
    });
    ASSERT_EQ(1, getImageRefs(shown));

    takeLoggedMessages();
    pluginInfo().close(connection);
    std::string logged = "<!--tdlib-log-only--><img id=\"" + std::to_string(shown) +
                         "\"> [photo]<!--tdlib-message 1 " + std::to_string(date) + "--><!--/tdlib-log-only-->";
    prpl.verifyEvents(ConversationWriteEvent(purpleUserName(0), purpleUserName(0), logged,
                                             (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES),
                                             date));
    ASSERT_EQ(std::vector<std::string>{logged}, takeLoggedMessages());
    ASSERT_EQ(0, getImageRefs(shown));
}
#endif

#if TDLIB_API_RICH_MESSAGE
// A picture not here yet is shown once it is, the message written again over itself, not said to
// be edited
TEST_F(PrivateChatTest, RichMessagePictureLate)
{
    const int32_t date = 10001;
    purple_account_set_string(account, "media-size-threshold", "0");
    loginWithOneContact();
    auto makeRich = [this](bool downloaded) {
        auto photoBlock = make_object<pageBlockPhoto>();
        photoBlock->photo_ = make_object<photo>();
        photoBlock->photo_->sizes_.push_back(makePhotoSize(::makeFile(102, 10000,
                                                                       downloaded ? TEST_SOURCE_DIR "/test.webp" : ""),
                                                           640, 480));
        auto rich = make_object<richMessage>();
        rich->blocks_.push_back(std::move(photoBlock));
        auto content = make_object<messageRichMessage>();
        content->message_ = std::move(rich);
        return makeMessage(1, userIds[0], chatIds[0], false, date, std::move(content));
    };

    tgl.update(make_object<updateNewMessage>(makeRich(false)));
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0), "[photo]", PURPLE_MESSAGE_RECV, date));
    uint64_t download = tgl.verifyRequests({
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, 1), nullptr, true),
        make_object<downloadFile>(102, 1, 0, 0, true)
    })[1];

    // Without Pidgin's tab there is nothing to write it over: only the message is asked for again
    tgl.reply(download, ::makeFile(102, 10000, TEST_SOURCE_DIR "/test.webp"));
    tgl.verifyRequest(getMessage(chatIds[0], 1));
    tgl.reply(makeRich(true));
    prpl.verifyNoEvents();
}
#endif
