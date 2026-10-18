#include "supergroup-test.h"
#include <ctime>
#include <glib/gstrfuncs.h>

static const char *NotificationWho = " ";

void SupergroupTest::loginWithSupergroup(object_ptr<supergroupFullInfo> fullInfo,
                                         object_ptr<chatMembers> recentMembers,
                                         object_ptr<chatMembers> administrators)
{
    if (!fullInfo)
        fullInfo = make_object<supergroupFullInfo>();
    if (!recentMembers)
        recentMembers = make_object<chatMembers>();
    if (!administrators)
        administrators = make_object<chatMembers>();
    login(
        {
            make_object<updateUser>(makeUser(
                // Chat member
                userIds[0],
                userFirstNames[0],
                userLastNames[0],
                "",
                make_object<userStatusOffline>()
            )),
            make_object<updateUser>(makeUser(
                // Chat member
                userIds[1],
                userFirstNames[1],
                userLastNames[1],
                "",
                make_object<userStatusOffline>()
            )),
            make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusMember>(), 2)),
            make_object<updateNewChat>(makeChat(
                groupChatId, make_object<chatTypeSupergroup>(groupId, false), groupChatTitle,
                nullptr, 0, 0, 0
            )),
            makeUpdateChatListMain(groupChatId)
        },
        make_object<users>(),
        make_object<chats>(1, std::vector<int64_t>(1, groupChatId)),
        {
            std::make_unique<AddChatEvent>(
                groupChatPurpleName, groupChatTitle, account, nullptr, nullptr
            ),
        },
        {
            make_object<getSupergroupFullInfo>(groupId),
            make_object<getSupergroupMembers>(
                groupId,
                make_object<supergroupMembersFilterRecent>(),
                0,
                200
            ),
            std::move(fullInfo),
            std::move(recentMembers),
            make_object<getSupergroupMembers>(
                groupId,
                make_object<supergroupMembersFilterAdministrators>(),
                0, 200
            ),
            std::move(administrators)
        }
    );
}

TEST_F(SupergroupTest, AddSupergroupChatAtLogin)
{
    loginWithSupergroup();
}

TEST_F(SupergroupTest, AddSupergroupChatAtLogin_WithMemberList_OpenChatAfterFullInfo)
{
    constexpr int     purpleChatId = 1;

    auto fullInfo = make_object<supergroupFullInfo>();
    fullInfo->description_ = "Description";

    auto members = make_object<chatMembers>();
    members->members_.push_back(makeChatMember(
        userIds[1],
        userIds[1],
        0,
        makeChatMemberStatusCreator("", false, true),
        nullptr
    ));
    members->members_.push_back(makeChatMember(
        selfId,
        userIds[1],
        0,
        make_object<chatMemberStatusMember>(),
        nullptr
    ));
    members->members_.push_back(nullptr);

    auto admins = make_object<chatMembers>();
    admins->members_.push_back(makeChatMember(
        userIds[0],
        userIds[1],
        0,
        make_object<chatMemberStatusAdministrator>(),
        nullptr
    ));
    admins->members_.push_back(makeChatMember(
        userIds[1],
        userIds[1],
        0,
        makeChatMemberStatusCreator("", false, true),
        nullptr
    ));
    admins->members_.push_back(nullptr);
    loginWithSupergroup(std::move(fullInfo), std::move(members), std::move(admins));

    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert(components, (char *)"id", g_strdup((groupChatPurpleName).c_str()));
    pluginInfo().join_chat(connection, components);
    g_hash_table_destroy(components);

    prpl.verifyEvents(
        ServGotJoinedChatEvent(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
        ChatSetTopicEvent(groupChatPurpleName, "Description", ""),
        ChatClearUsersEvent(groupChatPurpleName),
        ChatAddUserEvent(
            groupChatPurpleName,
            // This user is not in our contact list so first/last name is used
            userFirstNames[1] + " " + userLastNames[1],
            "", PURPLE_CBFLAGS_FOUNDER, false
        ),
        ChatAddUserEvent(
            groupChatPurpleName,
            // This is us (with + to match account name)
            "+" + selfPhoneNumber,
            "", PURPLE_CBFLAGS_NONE, false
        ),
        ChatAddUserEvent(
            groupChatPurpleName,
            // This user is not in our contact list so first/last name is used
            userFirstNames[0] + " " + userLastNames[0],
            "", PURPLE_CBFLAGS_OP, false
        ),
        PresentConversationEvent(groupChatPurpleName)
    );
    tgl.verifyNoRequests();
}

TEST_F(SupergroupTest, ExistingSupergroupChatAtLogin)
{
    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert(components, (char *)"id", g_strdup((groupChatPurpleName).c_str()));
    purple_blist_add_chat(purple_chat_new(account, groupChatTitle.c_str(), components), NULL, NULL);
    prpl.discardEvents();

    auto chat = makeChat(
        groupChatId, make_object<chatTypeSupergroup>(groupId, false), groupChatTitle,
        nullptr, 0, 0, 0
    );
    chat->positions_.push_back(make_object<chatPosition>(make_object<chatListMain>(), 1, false, nullptr));
    login(
        {
            make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusMember>(), 2)),
            make_object<updateNewChat>(std::move(chat)),
        },
        make_object<users>(),
        make_object<chats>(1, std::vector<int64_t>(1, groupChatId)),
        {},
        {
            make_object<getSupergroupFullInfo>(groupId),
            make_object<getSupergroupMembers>(
                groupId,
                make_object<supergroupMembersFilterRecent>(),
                0,
                200
            ),
        }
    );
}

TEST_F(SupergroupTest, ExistingSupergroupReceiveMessageAtLogin_WithMemberList_OpenChatBeforeFullInfo)
{
    constexpr int64_t messageId    = 10001;
    constexpr int32_t date         = 12345;
    constexpr int     purpleChatId = 1;

    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert(components, (char *)"id", g_strdup((groupChatPurpleName).c_str()));
    purple_blist_add_chat(purple_chat_new(account, groupChatTitle.c_str(), components), NULL, NULL);
    prpl.discardEvents();

    auto chat = makeChat(
        groupChatId, make_object<chatTypeSupergroup>(groupId, false), groupChatTitle,
        nullptr, 0, 0, 0
    );
    chat->positions_.push_back(make_object<chatPosition>(make_object<chatListMain>(), 1, false, nullptr));

    auto fullInfo = make_object<supergroupFullInfo>();
    fullInfo->description_ = "Description";

    auto members = make_object<chatMembers>();
    members->members_.push_back(makeChatMember(
        userIds[1],
        userIds[1],
        0,
        makeChatMemberStatusCreator("", false, true),
        nullptr
    ));
    members->members_.push_back(makeChatMember(
        selfId,
        userIds[1],
        0,
        make_object<chatMemberStatusMember>(),
        nullptr
    ));
    members->members_.push_back(nullptr);

    auto admins = make_object<chatMembers>();
    admins->members_.push_back(makeChatMember(
        userIds[0],
        userIds[1],
        0,
        make_object<chatMemberStatusAdministrator>(),
        nullptr
    ));
    admins->members_.push_back(makeChatMember(
        userIds[1],
        userIds[1],
        0,
        makeChatMemberStatusCreator("", false, true),
        nullptr
    ));
    admins->members_.push_back(nullptr);

    login(
        {
            make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusMember>(), 2)),
            make_object<updateNewChat>(std::move(chat)),
            standardUpdateUser(0), // Incoming message will be from this guy
            standardUpdateUser(1), // Another group member
            make_object<updateNewMessage>(
                makeMessage(messageId, userIds[0], groupChatId, false, date, makeTextMessage("Hello"))
            )
        },
        make_object<users>(),
        make_object<chats>(1, std::vector<int64_t>(1, groupChatId)),
        {
            std::make_unique<ServGotJoinedChatEvent>(connection, purpleChatId, groupChatPurpleName,
                                                     groupChatTitle),
            std::make_unique<ServGotChatEvent>(connection, purpleChatId, userFirstNames[0] + " " + userLastNames[0],
                                               "Hello", PURPLE_MESSAGE_RECV, date)
        },
        {
            make_object<getSupergroupFullInfo>(groupId),
            make_object<getSupergroupMembers>(
                groupId,
                make_object<supergroupMembersFilterRecent>(),
                0,
                200
            ),
            make_object<viewMessages>(groupChatId, std::vector<int64_t>(1, messageId), nullptr, true),
            std::move(fullInfo),
            std::move(members),
            make_object<getSupergroupMembers>(
                groupId,
                make_object<supergroupMembersFilterAdministrators>(),
                0, 200
            ),
            std::move(admins)
        },
        {
            std::make_unique<ChatSetTopicEvent>(groupChatPurpleName, "Description", ""),
            std::make_unique<ChatClearUsersEvent>(groupChatPurpleName),
            std::make_unique<ChatAddUserEvent>(
                groupChatPurpleName,
                // This user is not in our contact list so first/last name is used
                userFirstNames[1] + " " + userLastNames[1],
                "", PURPLE_CBFLAGS_FOUNDER, false
            ),
            std::make_unique<ChatAddUserEvent>(
                groupChatPurpleName,
                // This is us (with + to match account name)
                "+" + selfPhoneNumber,
                "", PURPLE_CBFLAGS_NONE, false
            ),
            std::make_unique<ChatAddUserEvent>(
                groupChatPurpleName,
                // This user is not in our contact list so first/last name is used
                userFirstNames[0] + " " + userLastNames[0],
                "", PURPLE_CBFLAGS_OP, false
            ),
            std::make_unique<AccountSetAliasEvent>(account, selfFirstName + " " + selfLastName),
            std::make_unique<ShowAccountEvent>(account)
        }
    );
}

TEST_F(SupergroupTest, DeleteSupergroup_Fail)
{
    loginWithSupergroup();
    PurpleChat *chat = purple_blist_find_chat(account, groupChatPurpleName.c_str());
    ASSERT_NE(nullptr, chat);
    GList *actions = pluginInfo().blist_node_menu(&chat->node);

    nodeMenuAction(&chat->node, actions, "Delete group");
    prpl.verifyEvents(RequestActionEvent(connection, account, NULL, NULL, 2));
    prpl.requestedAction("_No");
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();

    nodeMenuAction(&chat->node, actions, "Delete group");
    prpl.verifyEvents(RequestActionEvent(connection, account, NULL, NULL, 2));
    prpl.requestedAction("_Yes");
    tgl.verifyRequest(deleteChat(groupId));
    tgl.reply(make_object<error>(100, "error"));

    g_list_free_full(actions, (GDestroyNotify)purple_menu_action_free);
}

TEST_F(SupergroupTest, LeaveSupergroup)
{
    const int32_t date         = 12345;
    const int64_t messageId    = 10001;
    constexpr int purpleChatId = 1;

    loginWithSupergroup();
    PurpleChat *chat = purple_blist_find_chat(account, groupChatPurpleName.c_str());
    ASSERT_NE(nullptr, chat);
    GList *actions = pluginInfo().blist_node_menu(&chat->node);

    nodeMenuAction(&chat->node, actions, "Leave group");
    prpl.verifyEvents(RequestActionEvent(connection, account, NULL, NULL, 2));
    prpl.requestedAction("_No");
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();

    nodeMenuAction(&chat->node, actions, "Leave group");
    prpl.verifyEvents(RequestActionEvent(connection, account, NULL, NULL, 2));
    prpl.requestedAction("_Yes");
    tgl.verifyRequests({
        make_object<leaveChat>(groupChatId),
        make_object<deleteChatHistory>(groupChatId, true, false)
    });

    tgl.update(makeUpdateRemoveFromChatList(groupChatId, make_object<chatListMain>()));
    prpl.verifyEvents(RemoveChatEvent(groupChatPurpleName, ""));
    tgl.update(make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusBanned>(0), 0)));
    tgl.update(make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusLeft>(), 0)));

    prpl.verifyNoEvents();
    tgl.update(make_object<updateNewMessage>(
        makeMessage(messageId, selfId, groupChatId, true, date,
                    make_object<messageChatDeleteMember>(selfId))
    ));
    tgl.verifyRequest(viewMessages(groupChatId, {messageId}, nullptr, true));
    prpl.verifyEvents(
        AddChatEvent(groupChatPurpleName, groupChatTitle, account, notJoinedGroup(), NULL),
        ServGotJoinedChatEvent(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
        ConvSetTitleEvent(groupChatPurpleName, groupChatTitle),
        ChatSetTopicEvent(groupChatPurpleName, "", ""),
        ChatClearUsersEvent(groupChatPurpleName),
        ConversationWriteEvent(groupChatPurpleName, NotificationWho,
                               selfFirstName + " " + selfLastName +
                               ": Unsupported message type messageChatDeleteMember",
                               PURPLE_MESSAGE_SYSTEM, date),
        // ...followed by what the message holds, as TDLib describes it. The message is outgoing,
        // so it is written to the conversation rather than arriving through serv_got_chat_in.
        ConversationWriteEvent(groupChatPurpleName, selfFirstName + " " + selfLastName,
                               "\n<font face=\"monospace\">messageChatDeleteMember {\n"
                               "  user_id = 1\n}</font>",
                               PURPLE_MESSAGE_SEND, date)
    );

    // There is a check that fails message sending if we are not a group member
    ASSERT_LT(pluginInfo().chat_send(connection, purpleChatId, "message", PURPLE_MESSAGE_SEND), 0);

    g_list_free_full(actions, (GDestroyNotify)purple_menu_action_free);
}

TEST_F(SupergroupTest, GetInviteLink)
{
    constexpr int     purpleChatId = 1;
    loginWithSupergroup();
    // Only those allowed to invite can make an invitation
    tgl.update(make_object<updateSupergroup>(makeSupergroup(groupId, makeCreatorStatus(), 2)));
    prpl.discardEvents();

    PurpleChat *chat = purple_blist_find_chat(account, groupChatPurpleName.c_str());
    ASSERT_NE(nullptr, chat);
    GList *actions = pluginInfo().blist_node_menu(&chat->node);

    nodeMenuAction(&chat->node, actions, "Show invite link");
    tgl.verifyRequest(*makeInviteLinkRequest(groupChatId));

    tgl.reply(make_object<error>(100, "error"));
    prpl.verifyEvents(
        ServGotJoinedChatEvent(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
        ChatSetTopicEvent(groupChatPurpleName, "", ""),
        ChatClearUsersEvent(groupChatPurpleName),
        ConversationWriteEvent(
            groupChatPurpleName, NotificationWho,
            "Cannot generate invite link: code 100 (error)",
            PURPLE_MESSAGE_SYSTEM, 0
        )
    );

    nodeMenuAction(&chat->node, actions, "Show invite link");
    tgl.verifyRequest(*makeInviteLinkRequest(groupChatId));
    auto fullInfo = make_object<supergroupFullInfo>();
    fullInfo->invite_link_ = makeChatInviteLink("http://invite");
    tgl.update(make_object<updateSupergroupFullInfo>(
        groupChatId,
        std::move(fullInfo)
    ));
    prpl.verifyNoEvents();

    tgl.reply(makeChatInviteLink("http://invite"));
    prpl.verifyEvents(
        ConversationWriteEvent(
            groupChatPurpleName, NotificationWho,
            "http://invite",
            PURPLE_MESSAGE_SYSTEM, 0
        )
    );

    g_list_free_full(actions, (GDestroyNotify)purple_menu_action_free);
}

TEST_F(SupergroupTest, GetInviteLink_Public)
{
    loginWithSupergroup();
    auto group = makeSupergroup(groupId, make_object<chatMemberStatusMember>(), 2);
    group->usernames_ = make_object<usernames>();
    group->usernames_->active_usernames_.push_back("somechannel");
    tgl.update(make_object<updateSupergroup>(std::move(group)));
    prpl.discardEvents();

    PurpleChat *chat = purple_blist_find_chat(account, groupChatPurpleName.c_str());
    ASSERT_NE(nullptr, chat);
    GList *actions = pluginInfo().blist_node_menu(&chat->node);

    // Its address, which needs no invitation, nor anyone's rights to make one
    nodeMenuAction(&chat->node, actions, "Show invite link");
    tgl.verifyNoRequests();
    prpl.verifyEvents(
        ServGotJoinedChatEvent(connection, 1, groupChatPurpleName, groupChatTitle),
        ChatSetTopicEvent(groupChatPurpleName, "", ""),
        ChatClearUsersEvent(groupChatPurpleName),
        ConversationWriteEvent(groupChatPurpleName, NotificationWho, "https://t.me/somechannel",
                               PURPLE_MESSAGE_SYSTEM, 0)
    );

    g_list_free_full(actions, (GDestroyNotify)purple_menu_action_free);
}

TEST_F(SupergroupTest, GetInviteLink_NotAllowed)
{
    loginWithSupergroup();

    PurpleChat *chat = purple_blist_find_chat(account, groupChatPurpleName.c_str());
    ASSERT_NE(nullptr, chat);
    GList *actions = pluginInfo().blist_node_menu(&chat->node);

    nodeMenuAction(&chat->node, actions, "Show invite link");
    tgl.verifyNoRequests();
    prpl.verifyEvents(
        ServGotJoinedChatEvent(connection, 1, groupChatPurpleName, groupChatTitle),
        ChatSetTopicEvent(groupChatPurpleName, "", ""),
        ChatClearUsersEvent(groupChatPurpleName),
        ConversationWriteEvent(groupChatPurpleName, NotificationWho,
                               "This chat has no public link or invite link, and only its administrators can create one",
                               PURPLE_MESSAGE_SYSTEM, 0)
    );

    g_list_free_full(actions, (GDestroyNotify)purple_menu_action_free);
}

TEST_F(SupergroupTest, JoinByLink_InvalidLink1)
{
    login();

    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert(components, (char *)"id", g_strdup(""));
    g_hash_table_insert(components, (char *)"link", g_strdup("http://example.com"));

    pluginInfo().join_chat(connection, components);
    // Not a join link nor a group link - fail immediately
    tgl.verifyNoRequests();

    g_hash_table_destroy(components);
}

TEST_F(SupergroupTest, JoinByLink_InvalidLink2)
{
    login();

    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert(components, (char *)"id", g_strdup(""));
    g_hash_table_insert(components, (char *)"link", g_strdup("https://t.me/invalid/link"));

    pluginInfo().join_chat(connection, components);
    // Not a join link nor a group link - fail immediately
    tgl.verifyNoRequests();

    g_hash_table_destroy(components);
}

TEST_F(SupergroupTest, JoinByPublicLink1)
{
    const char *const NAME         = "groupname";
    const char *const LINK         = "https://t.me/groupname";
    constexpr int     purpleChatId = 1;

    login();

    // As if "Add chat" function in pidgin was used
    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert(components, (char *)"id", g_strdup(""));
    g_hash_table_insert(components, (char *)"link", g_strdup(LINK));

    PurpleChat *chat = purple_chat_new(account, "old chat", components);
    purple_blist_add_chat(chat, NULL, NULL);
    prpl.discardEvents();

    // And now that chat is being joined
    pluginInfo().join_chat(connection, components);

    tgl.verifyRequest(searchPublicChat(NAME));
    tgl.update(make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusLeft>(), 2)));
    tgl.update(make_object<updateNewChat>(makeChat(
        groupChatId, make_object<chatTypeSupergroup>(groupId, false), groupChatTitle,
        nullptr, 0, 0, 0
    )));
    tgl.reply(makeChat(
        groupChatId, make_object<chatTypeSupergroup>(groupId, false), groupChatTitle,
        nullptr, 0, 0, 0
    ));

    uint64_t joinRequestId = tgl.verifyRequest(joinChat(groupChatId));
    prpl.verifyNoEvents();

    tgl.update(make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusMember>(), 2)));
    tgl.update(makeUpdateChatList(groupChatId, make_object<chatListMain>()));
    prpl.verifyEvents(AddChatEvent(
        groupChatPurpleName, groupChatTitle, account, NULL, NULL
    ));
    tgl.verifyRequests({
        make_object<getSupergroupFullInfo>(groupId),
        make_object<getSupergroupMembers>(
            groupId,
            make_object<supergroupMembersFilterRecent>(),
            0,
            200
        ),
    });

    tgl.reply(joinRequestId, make_object<ok>());
    prpl.verifyEvents(
        RemoveChatEvent("", LINK),
        ServGotJoinedChatEvent(
            connection, purpleChatId, groupChatPurpleName, groupChatTitle
        )
    );
}

TEST_F(SupergroupTest, JoinByPublicLink2)
{
    login();

    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert(components, (char *)"id", g_strdup(""));
    g_hash_table_insert(components, (char *)"link", g_strdup("https://t.me/s/groupname"));

    pluginInfo().join_chat(connection, components);
    g_hash_table_destroy(components);
    tgl.verifyRequest(searchPublicChat("groupname"));
}

TEST_F(SupergroupTest, JoinByPublicLink_SearchFail)
{
    login();

    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert(components, (char *)"id", g_strdup(""));
    g_hash_table_insert(components, (char *)"link", g_strdup("https://t.me/s/groupname"));

    pluginInfo().join_chat(connection, components);
    g_hash_table_destroy(components);
    tgl.verifyRequest(searchPublicChat("groupname"));
    tgl.reply(make_object<error>(100, "error"));

    tgl.verifyNoRequests();
    prpl.verifyNoEvents();
}

TEST_F(SupergroupTest, JoinByPublicLink_NotGroupChat)
{
    login();

    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert(components, (char *)"id", g_strdup(""));
    g_hash_table_insert(components, (char *)"link", g_strdup("https://t.me/s/groupname"));

    pluginInfo().join_chat(connection, components);
    g_hash_table_destroy(components);
    tgl.verifyRequest(searchPublicChat("groupname"));

    tgl.update(make_object<updateUser>(makeUser(
        userIds[0],
        userFirstNames[0],
        userLastNames[0],
        "",
        make_object<userStatusOffline>()
    )));
    tgl.update(standardPrivateChat(0));
    tgl.reply(std::move(standardPrivateChat(0)->chat_));

    tgl.verifyNoRequests();
    prpl.verifyNoEvents();
}

TEST_F(SupergroupTest, JoinByPublicLink_JoinFail)
{
    login();

    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert(components, (char *)"id", g_strdup(""));
    g_hash_table_insert(components, (char *)"link", g_strdup("https://t.me/s/groupname"));

    pluginInfo().join_chat(connection, components);
    g_hash_table_destroy(components);
    tgl.verifyRequest(searchPublicChat("groupname"));

    tgl.update(make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusLeft>(), 2)));
    tgl.update(make_object<updateNewChat>(makeChat(
        groupChatId, make_object<chatTypeSupergroup>(groupId, false), groupChatTitle,
        nullptr, 0, 0, 0
    )));
    tgl.reply(makeChat(
        groupChatId, make_object<chatTypeSupergroup>(groupId, false), groupChatTitle,
        nullptr, 0, 0, 0
    ));

    tgl.verifyRequest(joinChat(groupChatId));
    tgl.reply(make_object<error>(100, "error"));
    prpl.verifyNoEvents();
}

TEST_F(SupergroupTest, ReceiveChannelPost)
{
    constexpr int32_t date         = 12345;
    constexpr int64_t messageId    = 10000;
    constexpr int     purpleChatId = 1;
    loginWithSupergroup();

    auto message = makeMessage(messageId, 0, groupChatId, false, date, makeTextMessage("Hello"));
    message->is_channel_post_ = true;
    tgl.update(make_object<updateNewMessage>(std::move(message)));

    tgl.verifyRequest(viewMessages(
        groupChatId,
        {messageId}, nullptr,
        true
    ));
    prpl.verifyEvents(
        ServGotJoinedChatEvent(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
        ChatSetTopicEvent(groupChatPurpleName, "", ""),
        ChatClearUsersEvent(groupChatPurpleName),
        ServGotChatEvent(connection, purpleChatId, "Channel post",
                         "Hello", PURPLE_MESSAGE_RECV, date)
    );

}

TEST_F(SupergroupTest, ChannelPostNotice)
{
    constexpr int32_t date         = 12345;
    constexpr int64_t messageId    = 10000;
    constexpr int     purpleChatId = 1;
    constexpr int32_t fileId       = 1234;
    purple_account_set_string(account, "media-size-threshold", "0.5");
    purple_account_set_string(account, "media-handling-behavior", "discard");
    loginWithSupergroup();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 655360, 655360,
            make_object<localFile>("", true, true, false, false, 0, 0, 0),
            make_object<remoteFile>("beh", "bleh", false, true, 655360)
        ), 640, 480));
    auto message = makeMessage(messageId, 0, groupChatId, false, date, makeMessagePhoto(
        make_object<photo>(false, nullptr, std::move(sizes)),
        make_object<formattedText>("caption", std::vector<object_ptr<textEntity>>()),
        false, false, false
    ));
    message->is_channel_post_ = true;
    tgl.update(make_object<updateNewMessage>(std::move(message)));

    tgl.verifyRequest(viewMessages(groupChatId, {messageId}, nullptr, true));
    // Every message in a channel is a channel post, which the notice need not repeat
    prpl.verifyEvents(
        ServGotJoinedChatEvent(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
        ChatSetTopicEvent(groupChatPurpleName, "", ""),
        ChatClearUsersEvent(groupChatPurpleName),
        ServGotChatEvent(connection, purpleChatId, "Channel post", "caption", PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(groupChatPurpleName, NotificationWho,
                               "Ignoring photo download (purple_str_size_to_units)",
                               PURPLE_MESSAGE_SYSTEM, date)
    );
}

TEST_F(SupergroupTest, ChannelPostEdited)
{
    constexpr int32_t date         = 12345;
    constexpr int32_t editDate     = 12400;
    constexpr int64_t messageId    = 10000;
    constexpr int     purpleChatId = 1;
    loginWithSupergroup();

    auto message = makeMessage(messageId, 0, groupChatId, false, date, makeTextMessage("old"));
    message->is_channel_post_ = true;
    tgl.update(make_object<updateNewMessage>(std::move(message)));
    tgl.verifyRequest(viewMessages(groupChatId, {messageId}, nullptr, true));
    prpl.verifyEvents(
        ServGotJoinedChatEvent(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
        ChatSetTopicEvent(groupChatPurpleName, "", ""),
        ChatClearUsersEvent(groupChatPurpleName),
        ServGotChatEvent(connection, purpleChatId, "Channel post", "old", PURPLE_MESSAGE_RECV, date)
    );

    tgl.update(make_object<updateMessageContent>(groupChatId, messageId, makeTextMessage("new")));
    tgl.verifyRequest(getMessage(groupChatId, messageId));
    auto edited = makeMessage(messageId, 0, groupChatId, false, date, makeTextMessage("new"));
    edited->is_channel_post_ = true;
    edited->edit_date_       = editDate;
    tgl.reply(std::move(edited));

    // "Channel post" is no one who could have edited it
    char   sentAt[80];
    time_t timestamp = date;
    strftime(sentAt, sizeof(sentAt), "%x %X", localtime(&timestamp));
    prpl.verifyEvents(ConversationWriteEvent(groupChatPurpleName, NotificationWho,
                                             std::string("The message from ") + sentAt + " was edited: new",
                                             PURPLE_MESSAGE_SYSTEM, editDate));
    tgl.verifyNoRequests();
}

TEST_F(SupergroupTest, ChannelPostQuotesChannelPost)
{
    constexpr int32_t date         = 12345;
    constexpr int64_t messageId    = 10000;
    constexpr int64_t quotedId     = 9000;
    constexpr int     purpleChatId = 1;
    loginWithSupergroup();

    // Both sent by the channel, as channels' posts are
    auto message = makeMessage(messageId, 0, groupChatId, false, date, makeTextMessage("reply"));
    message->is_channel_post_ = true;
    message->sender_id_       = make_object<messageSenderChat>(groupChatId);
    message->reply_to_        = makeReplyTo(quotedId);
    tgl.update(make_object<updateNewMessage>(std::move(message)));
    tgl.verifyRequest(getMessage(groupChatId, quotedId));

    auto quoted = makeMessage(quotedId, 0, groupChatId, false, date - 1, makeTextMessage("original"));
    quoted->is_channel_post_ = true;
    quoted->sender_id_       = make_object<messageSenderChat>(groupChatId);
    tgl.reply(std::move(quoted));
    tgl.verifyRequest(viewMessages(groupChatId, {messageId}, nullptr, true));
    prpl.verifyEvents(
        ServGotJoinedChatEvent(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
        ChatSetTopicEvent(groupChatPurpleName, "", ""),
        ChatClearUsersEvent(groupChatPurpleName),
        ServGotChatEvent(connection, purpleChatId, "Channel post",
                         "<b>&gt; " + groupChatTitle + " wrote:</b>\n&gt; original\nreply",
                         PURPLE_MESSAGE_RECV, date)
    );
}

// TODO: test non-user member

TEST_F(SupergroupTest, ForeignChannel)
{
    constexpr int32_t date         = 12345;
    constexpr int64_t messageId    = 10000;
    constexpr int     purpleChatId = 1;
    login();

    // A channel TDLib knows of, such as one linked to a group we are in, without our being a
    // member of it: its posts are none of ours
    tgl.update(make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusLeft>(), 2)));
    tgl.update(make_object<updateNewChat>(makeChat(
        groupChatId, make_object<chatTypeSupergroup>(groupId, true), groupChatTitle,
        nullptr, 0, 0, 0
    )));
    prpl.discardEvents();

    auto message = makeMessage(messageId, 0, groupChatId, false, date, makeTextMessage("Hello"));
    message->is_channel_post_ = true;
    tgl.update(make_object<updateNewMessage>(std::move(message)));
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();

    // ...until we join it
    tgl.update(make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusMember>(), 2)));
    tgl.update(makeUpdateChatListMain(groupChatId));
    prpl.discardEvents();
    tgl.verifyRequests({
        make_object<getSupergroupFullInfo>(groupId),
        make_object<getSupergroupMembers>(groupId, make_object<supergroupMembersFilterRecent>(), 0, 200),
    });

    message = makeMessage(messageId + 1, 0, groupChatId, false, date, makeTextMessage("Welcome"));
    message->is_channel_post_ = true;
    tgl.update(make_object<updateNewMessage>(std::move(message)));
    tgl.verifyRequest(viewMessages(groupChatId, {messageId + 1}, nullptr, true));
    prpl.verifyEvents(
        ServGotJoinedChatEvent(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
        ServGotChatEvent(connection, purpleChatId, "Channel post", "Welcome", PURPLE_MESSAGE_RECV, date)
    );

    // ...and once we leave it, its posts are none of ours again
    tgl.update(make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusLeft>(), 2)));
    prpl.discardEvents();
    message = makeMessage(messageId + 2, 0, groupChatId, false, date, makeTextMessage("Still here"));
    message->is_channel_post_ = true;
    tgl.update(make_object<updateNewMessage>(std::move(message)));
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();
}

TEST_F(SupergroupTest, UnjoinedChannelShownOnRequest)
{
    constexpr int32_t date         = 12345;
    constexpr int64_t messageId    = 10000;
    constexpr int     purpleChatId = 1;
    login();
    purple_account_set_bool(account, "show-unjoined-chats", TRUE);

    tgl.update(make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusLeft>(), 2)));
    tgl.update(make_object<updateNewChat>(makeChat(
        groupChatId, make_object<chatTypeSupergroup>(groupId, true), groupChatTitle,
        nullptr, 0, 0, 0
    )));
    prpl.discardEvents();

    auto message = makeMessage(messageId, 0, groupChatId, false, date, makeTextMessage("Hello"));
    message->is_channel_post_ = true;
    tgl.update(make_object<updateNewMessage>(std::move(message)));
    tgl.verifyRequest(viewMessages(groupChatId, {messageId}, nullptr, true));
    // An entry in the buddy list for this run only, which Pidgin takes the tab's title from
    PurpleGroup *notJoined = purple_find_group("Telegram: not joined");
    ASSERT_NE(nullptr, notJoined);
    ASSERT_TRUE(purple_blist_node_get_flags(&notJoined->node) & PURPLE_BLIST_NODE_FLAG_NO_SAVE);
    prpl.verifyEvents(
        AddChatEvent(groupChatPurpleName, groupChatTitle, account, notJoinedGroup(), NULL),
        ServGotJoinedChatEvent(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
        ConvSetTitleEvent(groupChatPurpleName, groupChatTitle),
        ServGotChatEvent(connection, purpleChatId, "Channel post", "Hello", PURPLE_MESSAGE_RECV, date)
    );
    PurpleChat *purpleChat = purple_blist_find_chat(account, groupChatPurpleName.c_str());
    ASSERT_NE(nullptr, purpleChat);
    ASSERT_TRUE(purple_blist_node_get_flags(&purpleChat->node) & PURPLE_BLIST_NODE_FLAG_NO_SAVE);

    // Pidgin, updating the tab, finds it
    PurpleConversation *conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT,
                                                                     groupChatPurpleName.c_str(), account);
    ASSERT_NE(nullptr, conv);
    purple_conversation_autoset_title(conv);
    prpl.verifyEvents(ConvSetTitleEvent(groupChatPurpleName, groupChatTitle));

    // Telegram's news of the chat, such as its title, leave the entry be
    tgl.update(make_object<updateChatTitle>(groupChatId, groupChatTitle));
    ASSERT_EQ(purpleChat, purple_blist_find_chat(account, groupChatPurpleName.c_str()));
    prpl.verifyNoEvents();

    // Joined, it is ours to keep, and moves to where joined chats are
    tgl.update(make_object<updateSupergroup>(makeSupergroup(groupId, make_object<chatMemberStatusMember>(), 2)));
    ASSERT_FALSE(purple_blist_node_get_flags(&purpleChat->node) & PURPLE_BLIST_NODE_FLAG_NO_SAVE);
    prpl.verifyEvents(AddChatEvent(groupChatPurpleName, groupChatTitle, account, NULL, NULL));
}
