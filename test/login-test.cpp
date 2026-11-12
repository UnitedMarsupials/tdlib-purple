#include "fixture.h"

class LoginTest: public CommTest {};

TEST_F(LoginTest, Login)
{
    login();
}

TEST_F(LoginTest, ConnectionReadyBeforeAuthReady)
{
    pluginInfo().login(account);
    prpl.verifyEvents(
        ConnectionSetStateEvent(connection, PURPLE_CONNECTING),
        ConnectionUpdateProgressEvent(connection, 1, 2)
    );

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitTdlibParameters>()));
    tgl.verifyRequests({
        make_object<disableProxy>(),
        make_object<getProxies>(),
        makeTdlibParameters(
            std::string(purple_user_dir()) + G_DIR_SEPARATOR_S +
            "tdlib" + G_DIR_SEPARATOR_S + "+" + selfPhoneNumber,
            true // use secret chats
        )
    });
    tgl.reply(make_object<ok>());

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitPhoneNumber>()));

    tgl.verifyRequest(setAuthenticationPhoneNumber("+" + selfPhoneNumber, nullptr));
    tgl.update(make_object<updateConnectionState>(make_object<connectionStateConnecting>()));
    tgl.update(make_object<updateConnectionState>(make_object<connectionStateReady>()));
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateReady>()));
    prpl.verifyEvents(ConnectionSetStateEvent(connection, PURPLE_CONNECTED));
    tgl.reply(make_object<ok>());

    tgl.verifyRequest(getContacts());
    tgl.update(make_object<updateUser>(makeUser(
        selfId,
        selfFirstName,
        selfLastName,
        selfPhoneNumber, // Phone number here without + to make it more interesting
        make_object<userStatusOffline>()
    )));
    tgl.reply(make_object<users>());

    tgl.verifyRequest(*getChatsRequest());
    prpl.verifyNoEvents();
    tgl.reply(getChatsNoChatsResponse());

    prpl.verifyEvents(
        AccountSetAliasEvent(account, selfFirstName + " " + selfLastName),
        ShowAccountEvent(account)
    );
}

TEST_F(LoginTest, RegisterNewAccount_WithAlias_ConnectionReadyBeforeAuthReady)
{
    purple_account_set_alias(account, (selfFirstName + " " + selfLastName).c_str());
    prpl.discardEvents();
    pluginInfo().login(account);
    prpl.verifyEvents(
        ConnectionSetStateEvent(connection, PURPLE_CONNECTING),
        ConnectionUpdateProgressEvent(connection, 1, 2)
    );

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitTdlibParameters>()));
    tgl.verifyRequests({
        make_object<disableProxy>(),
        make_object<getProxies>(),
        makeTdlibParameters(
            std::string(purple_user_dir()) + G_DIR_SEPARATOR_S +
            "tdlib" + G_DIR_SEPARATOR_S + "+" + selfPhoneNumber,
            true // use secret chats
        )
    });
    tgl.reply(make_object<ok>());

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitPhoneNumber>()));

    tgl.verifyRequest(setAuthenticationPhoneNumber("+" + selfPhoneNumber, nullptr));

    tgl.update(make_object<updateAuthorizationState>(
        make_object<authorizationStateWaitCode>(
            make_object<authenticationCodeInfo>(
                selfPhoneNumber,
                make_object<authenticationCodeTypeTelegramMessage>(5),
                make_object<authenticationCodeTypeSms>(5),
                1800
            )
        )
    ));

    prpl.verifyEvents(RequestInputEvent(connection, account, NULL, NULL));
    prpl.inputEnter("12345");
    tgl.verifyRequest(checkAuthenticationCode("12345"));

    tgl.update(make_object<updateConnectionState>(make_object<connectionStateConnecting>()));
    tgl.update(make_object<updateConnectionState>(make_object<connectionStateReady>()));

    tgl.update(make_object<updateAuthorizationState>(
        make_object<authorizationStateWaitRegistration>(
            make_object<termsOfService>(
                make_object<formattedText>(
                    "Terms of service",
                    std::vector<object_ptr<textEntity>>()
                ),
                0, false
            )
        )
    ));
    tgl.reply(make_object<ok>());

    tgl.verifyRequest(registerUser(selfFirstName, selfLastName, false));
    prpl.verifyNoEvents();

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateReady>()));
    prpl.verifyEvents(ConnectionSetStateEvent(connection, PURPLE_CONNECTED));
    tgl.reply(make_object<ok>());

    tgl.verifyRequest(getContacts());
    tgl.update(make_object<updateUser>(makeUser(
        selfId,
        selfFirstName,
        selfLastName,
        selfPhoneNumber, // Phone number here without + to make it more interesting
        make_object<userStatusOffline>()
    )));
    tgl.reply(make_object<users>());

    tgl.verifyRequest(*getChatsRequest());
    prpl.verifyNoEvents();
    tgl.reply(getChatsNoChatsResponse());

    prpl.verifyEvents(
        AccountSetAliasEvent(account, selfFirstName + " " + selfLastName),
        ShowAccountEvent(account)
    );
}

TEST_F(LoginTest, RegisterNewAccount_NoAlias)
{
    pluginInfo().login(account);
    prpl.verifyEvents(
        ConnectionSetStateEvent(connection, PURPLE_CONNECTING),
        ConnectionUpdateProgressEvent(connection, 1, 2)
    );

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitTdlibParameters>()));
    tgl.verifyRequests({
        make_object<disableProxy>(),
        make_object<getProxies>(),
        makeTdlibParameters(
            std::string(purple_user_dir()) + G_DIR_SEPARATOR_S +
            "tdlib" + G_DIR_SEPARATOR_S + "+" + selfPhoneNumber,
            true // use secret chats
        )
    });
    tgl.reply(make_object<ok>());

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitPhoneNumber>()));

    tgl.verifyRequest(setAuthenticationPhoneNumber("+" + selfPhoneNumber, nullptr));

    tgl.update(make_object<updateAuthorizationState>(
        make_object<authorizationStateWaitCode>(
            make_object<authenticationCodeInfo>(
                selfPhoneNumber,
                make_object<authenticationCodeTypeTelegramMessage>(5),
                make_object<authenticationCodeTypeSms>(5),
                1800
            )
        )
    ));

    prpl.verifyEvents(RequestInputEvent(connection, account, NULL, NULL));
    prpl.inputEnter("12345");
    tgl.verifyRequest(checkAuthenticationCode("12345"));

    tgl.update(make_object<updateAuthorizationState>(
        make_object<authorizationStateWaitRegistration>(
            make_object<termsOfService>(
                make_object<formattedText>(
                    "Terms of service",
                    std::vector<object_ptr<textEntity>>()
                ),
                0, false
            )
        )
    ));
    tgl.reply(make_object<ok>());

    tgl.verifyNoRequests();
    prpl.verifyEvents(RequestInputEvent(connection, account, NULL, NULL));

    prpl.inputEnter((selfFirstName + "     " + selfLastName).c_str());
    tgl.verifyRequest(registerUser(selfFirstName, selfLastName, false));

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateReady>()));
    prpl.verifyEvents(ConnectionSetStateEvent(connection, PURPLE_CONNECTED));
    tgl.reply(make_object<ok>());
    tgl.update(make_object<updateConnectionState>(make_object<connectionStateReady>()));
    tgl.verifyRequest(getContacts());
}

TEST_F(LoginTest, TwoFactorAuthentication)
{
    purple_account_set_alias(account, (selfFirstName + " " + selfLastName).c_str());
    prpl.discardEvents();
    pluginInfo().login(account);
    prpl.verifyEvents(
        ConnectionSetStateEvent(connection, PURPLE_CONNECTING),
        ConnectionUpdateProgressEvent(connection, 1, 2)
    );

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitTdlibParameters>()));
    tgl.verifyRequests({
        make_object<disableProxy>(),
        make_object<getProxies>(),
        makeTdlibParameters(
            std::string(purple_user_dir()) + G_DIR_SEPARATOR_S +
            "tdlib" + G_DIR_SEPARATOR_S + "+" + selfPhoneNumber,
            true // use secret chats
        )
    });
    tgl.reply(make_object<ok>());

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitPhoneNumber>()));

    tgl.verifyRequest(setAuthenticationPhoneNumber("+" + selfPhoneNumber, nullptr));

    tgl.update(make_object<updateAuthorizationState>(
        make_object<authorizationStateWaitCode>(
            make_object<authenticationCodeInfo>(
                selfPhoneNumber,
                make_object<authenticationCodeTypeTelegramMessage>(5),
                make_object<authenticationCodeTypeSms>(5),
                1800
            )
        )
    ));

    prpl.verifyEvents(RequestInputEvent(connection, account, NULL, NULL));
    prpl.inputEnter("12345");
    tgl.verifyRequest(checkAuthenticationCode("12345"));

    tgl.update(make_object<updateConnectionState>(make_object<connectionStateConnecting>()));
    tgl.update(make_object<updateConnectionState>(make_object<connectionStateReady>()));
    prpl.verifyNoEvents();

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitPassword>(
        "hint", true, false, "user@example.com"
    )));
    tgl.reply(make_object<ok>());

    prpl.verifyEvents(RequestInputEvent(connection, account, NULL, NULL));
    prpl.inputEnter("password");
    tgl.verifyRequest(checkAuthenticationPassword("password"));

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateReady>()));
    prpl.verifyEvents(ConnectionSetStateEvent(connection, PURPLE_CONNECTED));
    tgl.reply(make_object<ok>());
    tgl.verifyRequest(getContacts());
}

TEST_F(LoginTest, RenameBuddyAtConnect)
{
    purple_blist_add_buddy(purple_buddy_new(account, purpleUserName(0).c_str(), "whatever"), NULL,
                           &standardPurpleGroup, NULL);
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

TEST_F(LoginTest, RenameBuddy)
{
    loginWithOneContact();

    purple_blist_alias_buddy(purple_find_buddy(account, purpleUserName(0).c_str()), "New Name");
    prpl.discardEvents();
    pluginInfo().alias_buddy(connection, purpleUserName(0).c_str(), "New Name");

    tgl.verifyRequest(*makeAddContact(userIds[0], "", "New", "Name"));

    tgl.update(make_object<updateChatTitle>(chatIds[0], "New Name"));
    object_ptr<updateUser> updateUser = standardUpdateUser(0);
    updateUser->user_->first_name_ = "New";
    updateUser->user_->last_name_ = "Name";
    tgl.update(std::move(updateUser));
}

TEST_F(LoginTest, BuddyRenamedByServer)
{
    loginWithOneContact();

    tgl.update(make_object<updateChatTitle>(chatIds[0], "New Name"));
    prpl.verifyEvents(AliasBuddyEvent(purpleUserName(0), "New Name"));

    object_ptr<updateUser> updateUser = standardUpdateUser(0);
    updateUser->user_->first_name_ = "New";
    updateUser->user_->last_name_ = "Name";
    tgl.update(std::move(updateUser));
}

TEST_F(LoginTest, AddedProxyCofiguration)
{
    char host[] = "host";
    const int port = 10;
    char username[] = "username";
    char password[] = "password";
    PurpleProxyInfo purpleProxy;
    purpleProxy.type = PURPLE_PROXY_SOCKS5;
    purpleProxy.host = host;
    purpleProxy.port = port;
    purpleProxy.username = username;
    purpleProxy.password = password;
    account->proxy_info = &purpleProxy;

    pluginInfo().login(account);
    prpl.verifyEvents(
        ConnectionSetStateEvent(connection, PURPLE_CONNECTING),
        ConnectionUpdateProgressEvent(connection, 1, 2)
    );

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitTdlibParameters>()));
    tgl.verifyRequests({
        make_object<disableProxy>(),
        makeAddProxy(host, port, make_object<proxyTypeSocks5>(username, password)),
        make_object<getProxies>(),
        makeTdlibParameters(
            std::string(purple_user_dir()) + G_DIR_SEPARATOR_S +
            "tdlib" + G_DIR_SEPARATOR_S + "+" + selfPhoneNumber,
            true // use secret chats
        )
    });

    tgl.reply(make_object<ok>()); // reply to disableProxy
    tgl.reply(makeAddedProxy(2, false));
    tgl.reply(makeAddedProxies({{2, true}}));
    tgl.reply(make_object<ok>());
}

TEST_F(LoginTest, ChangedProxyCofiguration)
{
    char host[] = "host";
    const int port = 10;
    char username[] = "username";
    char password[] = "password";
    PurpleProxyInfo purpleProxy;
    purpleProxy.type = PURPLE_PROXY_SOCKS5;
    purpleProxy.host = host;
    purpleProxy.port = port;
    purpleProxy.username = username;
    purpleProxy.password = password;
    account->proxy_info = &purpleProxy;

    pluginInfo().login(account);
    prpl.verifyEvents(
        ConnectionSetStateEvent(connection, PURPLE_CONNECTING),
        ConnectionUpdateProgressEvent(connection, 1, 2)
    );

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitTdlibParameters>()));
    tgl.verifyRequests({
        make_object<disableProxy>(),
        makeAddProxy(host, port, make_object<proxyTypeSocks5>(username, password)),
        make_object<getProxies>(),
        makeTdlibParameters(
            std::string(purple_user_dir()) + G_DIR_SEPARATOR_S +
            "tdlib" + G_DIR_SEPARATOR_S + "+" + selfPhoneNumber,
            true // use secret chats
        )
    });

    tgl.reply(make_object<ok>()); // reply to disableProxy
    tgl.reply(makeAddedProxy(2, false));
    tgl.reply(makeAddedProxies({{1, false}, {2, true}}));
    tgl.reply(make_object<ok>());

    tgl.verifyRequest(removeProxy(1));
}

TEST_F(LoginTest, RemovedProxyCofiguration)
{
    pluginInfo().login(account);
    prpl.verifyEvents(
        ConnectionSetStateEvent(connection, PURPLE_CONNECTING),
        ConnectionUpdateProgressEvent(connection, 1, 2)
    );

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitTdlibParameters>()));
    tgl.verifyRequests({
        make_object<disableProxy>(),
        make_object<getProxies>(),
        makeTdlibParameters(
            std::string(purple_user_dir()) + G_DIR_SEPARATOR_S +
            "tdlib" + G_DIR_SEPARATOR_S + "+" + selfPhoneNumber,
            true // use secret chats
        )
    });

    tgl.reply(make_object<ok>()); // reply to disableProxy
    tgl.reply(makeAddedProxies({{1, false}}));
    tgl.reply(make_object<ok>());

    tgl.verifyRequest(removeProxy(1));
}

TEST_F(LoginTest, getChatsSequence)
{
    pluginInfo().login(account);
    prpl.verifyEvents(
        ConnectionSetStateEvent(connection, PURPLE_CONNECTING),
        ConnectionUpdateProgressEvent(connection, 1, 2)
    );

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitTdlibParameters>()));
    tgl.verifyRequests({
        make_object<disableProxy>(),
        make_object<getProxies>(),
        makeTdlibParameters(
            std::string(purple_user_dir()) + G_DIR_SEPARATOR_S +
            "tdlib" + G_DIR_SEPARATOR_S + "+" + selfPhoneNumber,
            true // use secret chats
        )
    });
    tgl.reply(make_object<ok>());

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitPhoneNumber>()));

    tgl.verifyRequest(setAuthenticationPhoneNumber("+" + selfPhoneNumber, nullptr));
    tgl.update(make_object<updateConnectionState>(make_object<connectionStateConnecting>()));
    tgl.update(make_object<updateConnectionState>(make_object<connectionStateReady>()));
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateReady>()));
    prpl.verifyEvents(ConnectionSetStateEvent(connection, PURPLE_CONNECTED));
    tgl.reply(make_object<ok>());

    tgl.verifyRequest(getContacts());
    tgl.update(make_object<updateUser>(makeUser(
        selfId,
        selfFirstName,
        selfLastName,
        selfPhoneNumber, // Phone number here without + to make it more interesting
        make_object<userStatusOffline>()
    )));
    tgl.reply(make_object<users>());

    tgl.verifyRequest(loadChats(make_object<chatListMain>(), 200));

    object_ptr<updateNewChat> chat1 = standardPrivateChat(0, make_object<chatListMain>());
    object_ptr<updateNewChat> chat2 = standardPrivateChat(1, make_object<chatListMain>());
    object_ptr<updateNewChat> chat3 = standardPrivateChat(1, make_object<chatListMain>());
    chat1->chat_->positions_.push_back(
        make_object<chatPosition>(make_object<chatListMain>(), 10, false, nullptr));
    chat2->chat_->positions_.push_back(
        make_object<chatPosition>(make_object<chatListMain>(), 20, false, nullptr));
    chat3->chat_->positions_.push_back(
        make_object<chatPosition>(make_object<chatListMain>(), 30, false, nullptr));
    chat3->chat_->id_ = chatIds[1]+1;
    tgl.update(std::move(chat1));
    tgl.update(std::move(chat2));
    tgl.update(std::move(chat3));
    tgl.update(make_object<updateChatPosition>(
        chatIds[1]+1, make_object<chatPosition>(
            make_object<chatListMain>(), 15, false, nullptr
        )
    ));
    tgl.update(make_object<updateChatPosition>(
        chatIds[0], make_object<chatPosition>(
            make_object<chatListArchive>(), 10, false, nullptr
        )
    ));
    tgl.reply(make_object<ok>());

    tgl.verifyRequest(*getChatsRequest());
    tgl.update(standardPrivateChat(1));
    tgl.reply(getChatsNoChatsResponse());

    // updateUser were missing (not realistic though), so no buddies
    prpl.verifyEvents(
        AccountSetAliasEvent(account, selfFirstName + " " + selfLastName),
        ShowAccountEvent(account)
    );
}

TEST_F(LoginTest, KeepInlineDownloads)
{
    purple_account_set_bool(account, "keep-inline-downloads", TRUE);
    pluginInfo().login(account);
    prpl.verifyEvents(
        ConnectionSetStateEvent(connection, PURPLE_CONNECTING),
        ConnectionUpdateProgressEvent(connection, 1, 2)
    );

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitTdlibParameters>()));
    tgl.verifyRequests({
        make_object<disableProxy>(),
        make_object<getProxies>(),
        makeTdlibParameters(
            std::string(purple_user_dir()) + G_DIR_SEPARATOR_S +
            "tdlib" + G_DIR_SEPARATOR_S + "+" + selfPhoneNumber,
            true // use secret chats
        )
    });
}

TEST_F(LoginTest, IncomingGroupChatMessageAtLoginWhileChatListStillNull)
{
    const int32_t     groupId             = 700;
    const int64_t     groupChatId         = 7000;
    const std::string groupChatTitle      = "Title";
    const std::string groupChatPurpleName = "chat" + std::to_string(groupChatId);
    constexpr int64_t messageId    = 10000;
    constexpr int32_t date         = 123456;
    constexpr int     purpleChatId = 1;

    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert(components, (char *)"id", g_strdup((groupChatPurpleName).c_str()));
    purple_blist_add_chat(purple_chat_new(account, groupChatTitle.c_str(), components), NULL, NULL);
    prpl.discardEvents();

    login(
        {
            standardUpdateUser(0),
            make_object<updateBasicGroup>(make_object<basicGroup>(
                groupId, 2, make_object<chatMemberStatusMember>(), true, 0
            )),
            make_object<updateNewChat>(makeChat(
                groupChatId, make_object<chatTypeBasicGroup>(groupId), groupChatTitle, nullptr, 0, 0, 0
            )),
            make_object<updateNewMessage>(makeMessage(
                messageId, userIds[0], groupChatId, false, date, makeTextMessage("text")
            )),
            makeUpdateChatListMain(groupChatId),
        },
        make_object<users>(), make_object<chats>(),
        {
            // Removal is unnecessary but nothing too bad is happening
            std::make_unique<RemoveChatEvent>(groupChatPurpleName, ""),
            std::make_unique<AddChatEvent>(groupChatPurpleName, groupChatTitle, account, notJoinedGroup(), nullptr),
            std::make_unique<ServGotJoinedChatEvent>(connection, purpleChatId, groupChatPurpleName, groupChatTitle),
            std::make_unique<ConvSetTitleEvent>(groupChatPurpleName, groupChatTitle),
            std::make_unique<ServGotChatEvent>(connection, purpleChatId, purpleUserName(0),
                             "text", PURPLE_MESSAGE_RECV, date),
            std::make_unique<AddChatEvent>(groupChatPurpleName, groupChatTitle, account, nullptr, nullptr)
        },
        {
            make_object<viewMessages>(groupChatId, std::vector<int64_t>(1, messageId), nullptr, true),
            make_object<getBasicGroupFullInfo>(groupId)
        }
    );
}

// Without request dialogs, the login code, the password and the display name are asked for in a
// conversation with "Telegram", which is kept out of the log meanwhile.
class LoginThroughChatTest: public CommTest {
protected:
    const std::string codePrompt = "Enter authentication code\n"
                                   "Code sent via: Telegram message (length: 5)\n"
                                   "Next code will be: SMS (length: 5)\n"
                                   "\n"
                                   "Reply to this chat with the value above to continue.";
    const std::string passwordPrompt = "Enter password for two-factor authentication\n"
                                       "Hint: &lt;hint&gt;\n"
                                       "Recovery e-mail may have been sent to user@example.com\n"
                                       "The password is sent as a message: it will be visible on screen as you type it.\n"
                                       "Reply to this chat with the value above to continue.";
    const std::string displayNamePrompt = "New account is being created. Please enter your display name.\n"
                                          "Reply to this chat with the value above to continue.";

    void SetUp() override
    {
        CommTest::SetUp();
        setRequestInputSupported(false);
    }

    PurpleConversation *telegramConversation()
    {
        return purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, "Telegram", account);
    }

    int send(const char *who, const char *message)
    {
        return pluginInfo().send_im(connection, who, message, PURPLE_MESSAGE_SEND);
    }

    // The password's hint has markup characters in it, to be shown as they are
    static object_ptr<updateAuthorizationState> waitPassword()
    {
        auto state = make_object<authorizationStateWaitPassword>();
        state->password_hint_                  = "<hint>";
        state->has_recovery_email_address_     = true;
        state->recovery_email_address_pattern_ = "user@example.com";
        return make_object<updateAuthorizationState>(std::move(state));
    }

    static object_ptr<updateAuthorizationState> waitRegistration()
    {
        auto terms   = make_object<termsOfService>();
        terms->text_ = makeFormattedText("Terms of service");
        auto state   = make_object<authorizationStateWaitRegistration>();
        state->terms_of_service_ = std::move(terms);
        return make_object<updateAuthorizationState>(std::move(state));
    }

    // Without an alias, a new account's display name is asked for
    void loginUntilCode(bool withAlias = true)
    {
        if (withAlias)
            purple_account_set_alias(account, (selfFirstName + " " + selfLastName).c_str());
        prpl.discardEvents();
        pluginInfo().login(account);
        prpl.verifyEvents(
            ConnectionSetStateEvent(connection, PURPLE_CONNECTING),
            ConnectionUpdateProgressEvent(connection, 1, 2)
        );

        tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitTdlibParameters>()));
        tgl.verifyRequests({
            make_object<disableProxy>(),
            make_object<getProxies>(),
            makeTdlibParameters(
                std::string(purple_user_dir()) + G_DIR_SEPARATOR_S +
                "tdlib" + G_DIR_SEPARATOR_S + "+" + selfPhoneNumber,
                true // use secret chats
            )
        });
        tgl.reply(make_object<ok>());

        tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitPhoneNumber>()));
        tgl.verifyRequest(setAuthenticationPhoneNumber("+" + selfPhoneNumber, nullptr));

        auto codeInfo           = make_object<authenticationCodeInfo>();
        codeInfo->phone_number_ = selfPhoneNumber;
        codeInfo->type_         = make_object<authenticationCodeTypeTelegramMessage>(5);
        codeInfo->next_type_    = make_object<authenticationCodeTypeSms>(5);
        codeInfo->timeout_      = 1800;
        tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitCode>(std::move(codeInfo))));
    }

    void finishLogin()
    {
        tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateReady>()));
        prpl.verifyEvents(ConnectionSetStateEvent(connection, PURPLE_CONNECTED));
        tgl.reply(make_object<ok>());
        tgl.verifyRequest(getContacts());
    }
};

TEST_F(LoginThroughChatTest, AnswersOnlyFromTelegramChat)
{
    loginUntilCode();
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, "Telegram"),
        ServGotImEvent(connection, "Telegram", codePrompt, PURPLE_MESSAGE_RECV, 0)
    );

    // To anyone else, a message goes as usual, here to nobody yet known
    ASSERT_GT(0, send(purpleUserName(0).c_str(), "12345"));
    prpl.verifyEvents(NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
                      ConversationWriteEvent(purpleUserName(0), purpleUserName(0), "User not found",
                                             PURPLE_MESSAGE_ERROR, 0));
    tgl.verifyNoRequests();

    // The code is shown as sent
    ASSERT_LT(0, send("Telegram", "<b>12345</b> "));
    tgl.verifyRequest(checkAuthenticationCode("12345"));

    tgl.update(waitPassword());
    tgl.reply(make_object<ok>());
    prpl.verifyEvents(ServGotImEvent(connection, "Telegram", passwordPrompt, PURPLE_MESSAGE_RECV, 0));

    ASSERT_GT(0, send(purpleUserName(1).c_str(), "password"));
    prpl.verifyEvents(NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(1)),
                      ConversationWriteEvent(purpleUserName(1), purpleUserName(1), "User not found",
                                             PURPLE_MESSAGE_ERROR, 0));
    tgl.verifyNoRequests();

    // The password is not, and is taken with its spaces
    ASSERT_EQ(0, send("Telegram", " pass word "));
    tgl.verifyRequest(checkAuthenticationPassword(" pass word "));

    finishLogin();

    // Nothing is waited for any more
    ASSERT_GT(0, send("Telegram", "12345"));
    prpl.verifyEvents(ConversationWriteEvent("Telegram", "Telegram", "User not found",
                                             PURPLE_MESSAGE_ERROR, 0));
}

TEST_F(LoginThroughChatTest, NotLoggedWhileWaiting)
{
    loginUntilCode();
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, "Telegram"),
        ServGotImEvent(connection, "Telegram", codePrompt, PURPLE_MESSAGE_RECV, 0)
    );
    PurpleConversation *conv = telegramConversation();
    ASSERT_NE(nullptr, conv);
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    // Taken until the next authorization state, as libpurple writes the code after send_im
    ASSERT_LT(0, send("Telegram", "12345"));
    tgl.verifyRequest(checkAuthenticationCode("12345"));
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    tgl.update(waitPassword());
    tgl.reply(make_object<ok>());
    prpl.verifyEvents(ServGotImEvent(connection, "Telegram", passwordPrompt, PURPLE_MESSAGE_RECV, 0));
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    // Closed and opened anew while the password is waited for
    purple_conversation_destroy(conv);
    conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, "Telegram");
    prpl.verifyEvents(NewConversationEvent(PURPLE_CONV_TYPE_IM, account, "Telegram"));
    ASSERT_TRUE(purple_conversation_is_logging(conv));
    ASSERT_EQ(0, send("Telegram", "password"));
    tgl.verifyRequest(checkAuthenticationPassword("password"));
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    finishLogin();
    EXPECT_TRUE(purple_conversation_is_logging(conv));
}

TEST_F(LoginThroughChatTest, LoggingLeftOffWhenItWasOff)
{
    PurpleConversation *conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, "Telegram");
    purple_conversation_set_logging(conv, FALSE);
    loginUntilCode();
    prpl.verifyEvents(ServGotImEvent(connection, "Telegram", codePrompt, PURPLE_MESSAGE_RECV, 0));

    ASSERT_LT(0, send("Telegram", "12345"));
    tgl.verifyRequest(checkAuthenticationCode("12345"));
    finishLogin();
    EXPECT_FALSE(purple_conversation_is_logging(conv));
}

TEST_F(LoginThroughChatTest, LoggedAgainAfterDisconnectWhileWaiting)
{
    loginUntilCode();
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, "Telegram"),
        ServGotImEvent(connection, "Telegram", codePrompt, PURPLE_MESSAGE_RECV, 0)
    );
    PurpleConversation *conv = telegramConversation();
    ASSERT_NE(nullptr, conv);
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    pluginInfo().close(connection);
    EXPECT_TRUE(purple_conversation_is_logging(conv));
}

TEST_F(LoginThroughChatTest, Registration)
{
    loginUntilCode(false);
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, "Telegram"),
        ServGotImEvent(connection, "Telegram", codePrompt, PURPLE_MESSAGE_RECV, 0)
    );
    PurpleConversation *conv = telegramConversation();
    ASSERT_NE(nullptr, conv);

    ASSERT_LT(0, send("Telegram", "12345"));
    tgl.verifyRequest(checkAuthenticationCode("12345"));

    tgl.update(waitRegistration());
    tgl.reply(make_object<ok>());
    prpl.verifyEvents(ServGotImEvent(connection, "Telegram", displayNamePrompt, PURPLE_MESSAGE_RECV, 0));
    tgl.verifyNoRequests();
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    // The name is shown as sent, and trimmed
    ASSERT_LT(0, send("Telegram", (" " + selfFirstName + " " + selfLastName + " ").c_str()));
    tgl.verifyRequest(registerUser(selfFirstName, selfLastName, false));
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    finishLogin();
    EXPECT_TRUE(purple_conversation_is_logging(conv));
}
