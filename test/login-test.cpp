#include "fixture.h"
#include "client-utils.h"
#include <glib/gstdio.h>

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
        ),
        makeStorageOptimizerOption()
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
    tgl.verifyRequest(*makeOnlineOption(true));

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
        ),
        makeStorageOptimizerOption()
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

    prpl.verifyEvents(RequestInputEvent(connection, account, nullptr, nullptr));
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
    tgl.verifyRequest(*makeOnlineOption(true));

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
        ),
        makeStorageOptimizerOption()
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

    prpl.verifyEvents(RequestInputEvent(connection, account, nullptr, nullptr));
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
    prpl.verifyEvents(RequestInputEvent(connection, account, nullptr, nullptr));

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
        ),
        makeStorageOptimizerOption()
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

    prpl.verifyEvents(RequestInputEvent(connection, account, nullptr, nullptr));
    prpl.inputEnter("12345");
    tgl.verifyRequest(checkAuthenticationCode("12345"));

    tgl.update(make_object<updateConnectionState>(make_object<connectionStateConnecting>()));
    tgl.update(make_object<updateConnectionState>(make_object<connectionStateReady>()));
    prpl.verifyNoEvents();

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateWaitPassword>(
        "hint", true, false, "user@example.com"
    )));
    tgl.reply(make_object<ok>());

    prpl.verifyEvents(RequestInputEvent(connection, account, nullptr, nullptr));
    prpl.inputEnter("password");
    tgl.verifyRequest(checkAuthenticationPassword("password"));

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateReady>()));
    prpl.verifyEvents(ConnectionSetStateEvent(connection, PURPLE_CONNECTED));
    tgl.reply(make_object<ok>());
    tgl.verifyRequest(getContacts());
}

TEST_F(LoginTest, RenameBuddyAtConnect)
{
    purple_blist_add_buddy(purple_buddy_new(account, purpleUserName(0).c_str(), "whatever"), nullptr,
                           &standardPurpleGroup, nullptr);
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

    tgl.verifyRequest(*makeAddContact(userIds[0], "", "New", "Name", false));

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
        ),
        makeStorageOptimizerOption()
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
        ),
        makeStorageOptimizerOption()
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
        ),
        makeStorageOptimizerOption()
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
        ),
        makeStorageOptimizerOption()
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
    tgl.verifyRequest(*makeOnlineOption(true));

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
        ),
        makeStorageOptimizerOption(false) // keep-inline-downloads
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

    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, nullptr, g_free);
    g_hash_table_insert(components, (char *)"id", g_strdup((groupChatPurpleName).c_str()));
    purple_blist_add_chat(purple_chat_new(account, groupChatTitle.c_str(), components), nullptr, nullptr);
    prpl.discardEvents();

    chatsOpenedAtLogin = {groupChatId};
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
// conversation with a buddy no Telegram user is, shown as "Telegram", which is kept out of the log
// meanwhile and taken out of the buddy list once logged in.
class LoginThroughChatTest: public CommTest {
protected:
    static constexpr const char *authBuddy = "id0";
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
        return purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, authBuddy, account);
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
            ),
            makeStorageOptimizerOption()
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
        prpl.verifyEvents(RemoveBuddyEvent(account, authBuddy),
                          ConnectionSetStateEvent(connection, PURPLE_CONNECTED));
        tgl.reply(make_object<ok>());
        tgl.verifyRequest(getContacts());
    }
};

TEST_F(LoginThroughChatTest, AnswersOnlyFromTelegramChat)
{
    loginUntilCode();
    prpl.verifyEvents(
        AddBuddyEvent(authBuddy, "Telegram", account, nullptr, nullptr, nullptr),
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, authBuddy),
        ServGotImEvent(connection, authBuddy, codePrompt, PURPLE_MESSAGE_RECV, 0)
    );

    // To anyone else, a message goes as usual, here to nobody yet known
    ASSERT_GT(0, send(purpleUserName(0).c_str(), "12345"));
    prpl.verifyEvents(NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
                      ConversationWriteEvent(purpleUserName(0), purpleUserName(0), "User not found",
                                             PURPLE_MESSAGE_ERROR, 0));
    tgl.verifyNoRequests();

    // The code is shown as sent
    ASSERT_LT(0, send(authBuddy, "<b>12345</b> "));
    tgl.verifyRequest(checkAuthenticationCode("12345"));

    tgl.update(waitPassword());
    tgl.reply(make_object<ok>());
    prpl.verifyEvents(ServGotImEvent(connection, authBuddy, passwordPrompt, PURPLE_MESSAGE_RECV, 0));

    ASSERT_GT(0, send(purpleUserName(1).c_str(), "password"));
    prpl.verifyEvents(NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(1)),
                      ConversationWriteEvent(purpleUserName(1), purpleUserName(1), "User not found",
                                             PURPLE_MESSAGE_ERROR, 0));
    tgl.verifyNoRequests();

    // The password is not, and is taken with its spaces
    ASSERT_EQ(0, send(authBuddy, " pass word "));
    tgl.verifyRequest(checkAuthenticationPassword(" pass word "));

    finishLogin();

    // Nothing is waited for any more
    ASSERT_GT(0, send(authBuddy, "12345"));
    prpl.verifyEvents(ConversationWriteEvent(authBuddy, authBuddy, "User not found",
                                             PURPLE_MESSAGE_ERROR, 0));
}

// The chat goes by an id, not by the name it is shown under: a reply to someone called "Telegram"
// is not the value
TEST_F(LoginThroughChatTest, AnswerOnlyToChatId)
{
    loginUntilCode();
    prpl.verifyEvents(
        AddBuddyEvent(authBuddy, "Telegram", account, nullptr, nullptr, nullptr),
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, authBuddy),
        ServGotImEvent(connection, authBuddy, codePrompt, PURPLE_MESSAGE_RECV, 0)
    );

    ASSERT_GT(0, send("Telegram", "12345"));
    prpl.verifyEvents(NewConversationEvent(PURPLE_CONV_TYPE_IM, account, "Telegram"),
                      ConversationWriteEvent("Telegram", "Telegram", "User not found",
                                             PURPLE_MESSAGE_ERROR, 0));
    tgl.verifyNoRequests();

    ASSERT_LT(0, send(authBuddy, "12345"));
    tgl.verifyRequest(checkAuthenticationCode("12345"));

    finishLogin();
}

// Once logged in, what is typed into the chat, still open, goes to no one, even to a user calling
// themselves by the name it goes by
TEST_F(LoginThroughChatTest, NobodyAfterLogin)
{
    loginUntilCode();
    prpl.verifyEvents(
        AddBuddyEvent(authBuddy, "Telegram", account, nullptr, nullptr, nullptr),
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, authBuddy),
        ServGotImEvent(connection, authBuddy, codePrompt, PURPLE_MESSAGE_RECV, 0)
    );
    ASSERT_LT(0, send(authBuddy, "12345"));
    tgl.verifyRequest(checkAuthenticationCode("12345"));
    finishLogin();

    tgl.update(make_object<updateUser>(makeUser(7000, authBuddy, "", "", make_object<userStatusOffline>())));
    prpl.discardEvents();
    ASSERT_GT(0, send(authBuddy, "the code was 12345"));
    tgl.verifyNoRequests();
    prpl.discardEvents();
}

TEST_F(LoginThroughChatTest, NotLoggedWhileWaiting)
{
    loginUntilCode();
    prpl.verifyEvents(
        AddBuddyEvent(authBuddy, "Telegram", account, nullptr, nullptr, nullptr),
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, authBuddy),
        ServGotImEvent(connection, authBuddy, codePrompt, PURPLE_MESSAGE_RECV, 0)
    );
    PurpleConversation *conv = telegramConversation();
    ASSERT_NE(nullptr, conv);
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    // Taken until the next authorization state, as libpurple writes the code after send_im
    ASSERT_LT(0, send(authBuddy, "12345"));
    tgl.verifyRequest(checkAuthenticationCode("12345"));
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    tgl.update(waitPassword());
    tgl.reply(make_object<ok>());
    prpl.verifyEvents(ServGotImEvent(connection, authBuddy, passwordPrompt, PURPLE_MESSAGE_RECV, 0));
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    // Closed and opened anew while the password is waited for
    purple_conversation_destroy(conv);
    conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, authBuddy);
    prpl.verifyEvents(NewConversationEvent(PURPLE_CONV_TYPE_IM, account, authBuddy));
    ASSERT_TRUE(purple_conversation_is_logging(conv));
    ASSERT_EQ(0, send(authBuddy, "password"));
    tgl.verifyRequest(checkAuthenticationPassword("password"));
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    finishLogin();
    EXPECT_TRUE(purple_conversation_is_logging(conv));
}

TEST_F(LoginThroughChatTest, LoggingLeftOffWhenItWasOff)
{
    PurpleConversation *conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, authBuddy);
    purple_conversation_set_logging(conv, FALSE);
    loginUntilCode();
    prpl.verifyEvents(AddBuddyEvent(authBuddy, "Telegram", account, nullptr, nullptr, nullptr),
                      ServGotImEvent(connection, authBuddy, codePrompt, PURPLE_MESSAGE_RECV, 0));

    ASSERT_LT(0, send(authBuddy, "12345"));
    tgl.verifyRequest(checkAuthenticationCode("12345"));
    finishLogin();
    EXPECT_FALSE(purple_conversation_is_logging(conv));
}

TEST_F(LoginThroughChatTest, LoggedAgainAfterDisconnectWhileWaiting)
{
    loginUntilCode();
    prpl.verifyEvents(
        AddBuddyEvent(authBuddy, "Telegram", account, nullptr, nullptr, nullptr),
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, authBuddy),
        ServGotImEvent(connection, authBuddy, codePrompt, PURPLE_MESSAGE_RECV, 0)
    );
    PurpleConversation *conv = telegramConversation();
    ASSERT_NE(nullptr, conv);
    EXPECT_FALSE(purple_conversation_is_logging(conv));
    // Never saved, should the program quit meanwhile
    PurpleBuddy *buddy = purple_find_buddy(account, authBuddy);
    ASSERT_NE(nullptr, buddy);
    EXPECT_TRUE(purple_blist_node_get_flags(PURPLE_BLIST_NODE(buddy)) & PURPLE_BLIST_NODE_FLAG_NO_SAVE);

    // The login given up, nothing is left to answer
    pluginInfo().close(connection);
    prpl.verifyEvents(RemoveBuddyEvent(account, authBuddy));
    EXPECT_TRUE(purple_conversation_is_logging(conv));
}

TEST_F(LoginThroughChatTest, Registration)
{
    loginUntilCode(false);
    prpl.verifyEvents(
        AddBuddyEvent(authBuddy, "Telegram", account, nullptr, nullptr, nullptr),
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, authBuddy),
        ServGotImEvent(connection, authBuddy, codePrompt, PURPLE_MESSAGE_RECV, 0)
    );
    PurpleConversation *conv = telegramConversation();
    ASSERT_NE(nullptr, conv);

    ASSERT_LT(0, send(authBuddy, "12345"));
    tgl.verifyRequest(checkAuthenticationCode("12345"));

    tgl.update(waitRegistration());
    tgl.reply(make_object<ok>());
    prpl.verifyEvents(ServGotImEvent(connection, authBuddy, displayNamePrompt, PURPLE_MESSAGE_RECV, 0));
    tgl.verifyNoRequests();
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    // The name is shown as sent, and trimmed
    ASSERT_LT(0, send(authBuddy, (" " + selfFirstName + " " + selfLastName + " ").c_str()));
    tgl.verifyRequest(registerUser(selfFirstName, selfLastName, false));
    EXPECT_FALSE(purple_conversation_is_logging(conv));

    finishLogin();
    EXPECT_TRUE(purple_conversation_is_logging(conv));
}

// The local database encrypted with the account's password, as its option says
class DatabasePasswordTest: public CommTest {
protected:
    const std::string password = "correct horse";
    const std::string directory = std::string(purple_user_dir()) + G_DIR_SEPARATOR_S + "tdlib" +
                                  G_DIR_SEPARATOR_S + "+" + selfPhoneNumber;

    void TearDown() override
    {
        setRequestAnswers({});
        CommTest::TearDown();
    }
    // As the plugin makes it of a password, which takes long: once
    std::string keyOf(const std::string &password)
    {
        static std::map<std::string, std::string> keys;
        auto it = keys.find(password);
        if (it == keys.end())
            it = keys.emplace(password, deriveKey(password, "tdlib-purple database +" + selfPhoneNumber, 600000)).first;
        return it->second;
    }
    // Up to TDLib asking for its parameters
    void startLogin(bool encrypt)
    {
        purple_account_set_bool(account, "encrypt-database", encrypt);
        pluginInfo().login(account);
        prpl.verifyEvents(
            ConnectionSetStateEvent(connection, PURPLE_CONNECTING),
            ConnectionUpdateProgressEvent(connection, 1, 2)
        );
        auto update                  = make_object<updateAuthorizationState>();
        update->authorization_state_ = make_object<authorizationStateWaitTdlibParameters>();
        tgl.update(std::move(update));
    }
    // The request to reply to
    uint64_t verifyFirstParameters(const std::string &key)
    {
        return tgl.verifyRequests({
            make_object<disableProxy>(),
            make_object<getProxies>(),
            makeTdlibParameters(directory, true, key),
            makeStorageOptimizerOption()
        })[2];
    }
    void replyWrongKey(uint64_t requestId)
    {
        tgl.reply(requestId, makeError(401, "Wrong database encryption key"));
    }
};

TEST_F(DatabasePasswordTest, PasswordRemembered)
{
    purple_account_set_password(account, password.c_str());
    startLogin(true);
    tgl.reply(verifyFirstParameters(keyOf(password)), make_object<ok>());
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();
}

TEST_F(DatabasePasswordTest, PasswordAskedFor)
{
    setRequestAnswers({{{"password", password}}});
    startLogin(true);
    prpl.verifyEvents(RequestPasswordEvent(account));
    uint64_t requestId = verifyFirstParameters(keyOf(password));
    EXPECT_EQ(password, purple_account_get_password(account));
    EXPECT_FALSE(account->remember_pass);
    tgl.reply(requestId, make_object<ok>());
    tgl.verifyNoRequests();
}

TEST_F(DatabasePasswordTest, PasswordNotGiven)
{
    startLogin(true);
    prpl.verifyEvents(RequestPasswordEvent(account),
                      ConnectionErrorEvent(connection, "The local database cannot be opened without its password"));
    tgl.verifyRequests({make_object<disableProxy>(), make_object<getProxies>()});
}

TEST_F(DatabasePasswordTest, Encrypted)
{
    // Not encrypted yet: encrypted once the password is typed again
    purple_account_set_password(account, password.c_str());
    startLogin(true);
    replyWrongKey(verifyFirstParameters(keyOf(password)));
    tgl.verifyRequest(*makeTdlibParameters(directory, true));
    setRequestAnswers({{{"password", password}}});
    tgl.reply(make_object<ok>());
    tgl.verifyRequest(setDatabaseEncryptionKey(keyOf(password)));
    tgl.reply(make_object<ok>());
    tgl.verifyNoRequests();
}

TEST_F(DatabasePasswordTest, NotEncryptedWithAnotherPassword)
{
    purple_account_set_password(account, password.c_str());
    startLogin(true);
    replyWrongKey(verifyFirstParameters(keyOf(password)));
    tgl.verifyRequest(*makeTdlibParameters(directory, true));
    setRequestAnswers({{{"password", "correct horsf"}}});
    tgl.reply(make_object<ok>());
    tgl.verifyNoRequests();
    EXPECT_EQ(0u, getRequestAnswersLeft());
}

TEST_F(DatabasePasswordTest, WrongPassword)
{
    purple_account_set_password(account, password.c_str());
    startLogin(true);
    replyWrongKey(verifyFirstParameters(keyOf(password)));
    replyWrongKey(tgl.verifyRequest(*makeTdlibParameters(directory, true)));
    prpl.verifyEvents(ConnectionErrorEvent(connection, "The password does not open the account's local database"));
    // Asked for at the next connection
    EXPECT_EQ(nullptr, purple_account_get_password(account));
    tgl.verifyNoRequests();
}

TEST_F(DatabasePasswordTest, Decrypted)
{
    // Encryption turned off: the database decrypted with its password
    startLogin(false);
    uint64_t requestId = verifyFirstParameters("");
    setRequestAnswers({{{"password", password}}});
    replyWrongKey(requestId);
    tgl.verifyRequest(*makeTdlibParameters(directory, true, keyOf(password)));
    tgl.reply(make_object<ok>());
    tgl.verifyRequest(setDatabaseEncryptionKey(""));
    tgl.reply(make_object<ok>());
    tgl.verifyNoRequests();
}

TEST(DatabaseFileTest, PlainSqlite)
{
    char *dir = g_dir_make_tmp("tdlib-purple-test-XXXXXX", nullptr);
    ASSERT_NE(nullptr, dir);
    std::string plain     = std::string(dir) + G_DIR_SEPARATOR_S + "plain";
    std::string encrypted = std::string(dir) + G_DIR_SEPARATOR_S + "encrypted";
    std::string shorter   = std::string(dir) + G_DIR_SEPARATOR_S + "short";
    ASSERT_TRUE(g_file_set_contents(plain.c_str(), "SQLite format 3\0\x10\0\1\1", 20, nullptr));
    ASSERT_TRUE(g_file_set_contents(encrypted.c_str(), "\x8e\x12\x55\xa0SQLite format 3\0", 20, nullptr));
    ASSERT_TRUE(g_file_set_contents(shorter.c_str(), "SQLite", 6, nullptr));
    EXPECT_TRUE(isPlainSqliteFile(plain));
    EXPECT_FALSE(isPlainSqliteFile(encrypted));
    EXPECT_FALSE(isPlainSqliteFile(shorter));
    EXPECT_FALSE(isPlainSqliteFile(std::string(dir) + G_DIR_SEPARATOR_S + "none"));
    for (const std::string &path: {plain, encrypted, shorter})
        g_unlink(path.c_str());
    g_rmdir(dir);
    g_free(dir);
}

TEST(DatabaseFileTest, KeyDerivation)
{
    // The vectors published for PBKDF2-HMAC-SHA256 with RFC 6070's inputs, of 32 bytes
    auto hex = [](const std::string &bytes) {
        std::string result;
        for (unsigned char c: bytes) {
            char digits[3];
            snprintf(digits, sizeof(digits), "%02x", c);
            result += digits;
        }
        return result;
    };
    EXPECT_EQ("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b", hex(deriveKey("password", "salt", 1)));
    EXPECT_EQ("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a",
              hex(deriveKey("password", "salt", 4096)));
}
