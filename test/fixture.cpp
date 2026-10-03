#include "fixture.h"
#include "tdlib-purple.h"
#include "libpurple-mock.h"
#include "printout.h"

CommTest::CommTest()
{
    tgprpl_set_test_backend(&tgl);
    tgprpl_set_single_thread();
    purple_init_plugin(&purplePlugin);
    purplePlugin.info->load(&purplePlugin);
}

void CommTest::SetUp()
{
    account = purple_account_new(("+" + selfPhoneNumber).c_str(), NULL);
    connection = new PurpleConnection;
    connection->state = PURPLE_DISCONNECTED;
    connection->account = account;
    purple_connection_set_protocol_data(connection, NULL);
    account->gc = connection;
    prpl.discardEvents();
    setUiName("Pidgin");
}

void CommTest::TearDown()
{
    if (purple_connection_get_protocol_data(connection))
        pluginInfo().close(connection);
    tgl.verifyNoRequests();
    prpl.verifyNoEvents();

    delete connection;
    account->gc = NULL;
    tgl.runTimeouts();
    purple_account_destroy(account);
    account = NULL;
    clearFakeFiles();
}

// Is this a request the plugin sent, or something TDLib sent back? This used to be decided by
// whether a hand-written table of class names in printout.cpp happened to know the object.
//
// dynamic_cast cannot answer it: TDLib is built without RTTI for its API classes -- libtdapi
// carries no typeinfo for td_api::Function at all -- so the cast reads a vtable that is not
// there and the process dies. downcast_call switches on get_id(), which is a plain virtual
// call, and returns false when the id does not belong to that hierarchy. That is the question
// being asked, and it is what the old name tables were doing the long way round.
static bool isFunction(const td::TlObject &object)
{
    auto &function = const_cast<td::td_api::Function &>(static_cast<const td::td_api::Function &>(object));

    return td::td_api::downcast_call(function, [](auto &) {});
}

static bool isObject(const td::TlObject &object)
{
    auto &reply = const_cast<td::td_api::Object &>(static_cast<const td::td_api::Object &>(object));

    return td::td_api::downcast_call(reply, [](auto &) {});
}

void CommTest::login(std::initializer_list<object_ptr<Object>> extraUpdates, object_ptr<users> getContactsReply,
                     object_ptr<Object> getChatsReply,
                     std::initializer_list<std::unique_ptr<PurpleEvent>> postUpdateEvents,
                     std::initializer_list<object_ptr<td::TlObject>> postUpdateRequestsAndResponses,
                     std::initializer_list<std::unique_ptr<PurpleEvent>> postChatListEvents)
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

    // TDLib 1.8 sets the database encryption key itself, so the
    // authorizationStateWaitEncryptionKey / checkDatabaseEncryptionKey exchange that used to
    // sit here is gone, and the plugin no longer has a handler for it.

    tgl.update(make_object<updateAuthorizationState>(make_object<authorizationStateReady>()));
    prpl.verifyEvents(ConnectionSetStateEvent(connection, PURPLE_CONNECTED));
    uint64_t contactRequestId = tgl.verifyRequest(getContacts());

    tgl.update(make_object<updateConnectionState>(make_object<connectionStateConnecting>()));
    tgl.update(make_object<updateConnectionState>(make_object<connectionStateUpdating>()));
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();

    tgl.update(make_object<updateUser>(makeUser(
        selfId,
        selfFirstName,
        selfLastName,
        selfPhoneNumber, // Phone number here without + to make it more interesting
        make_object<userStatusOffline>()
    )));
    for (const object_ptr<Object> &update: extraUpdates)
        tgl.update(std::move(const_cast<object_ptr<Object> &>(update))); // Take that!
    prpl.verifyEvents2(std::move(postUpdateEvents));

    std::vector<const Function *> postUpdateRequests;
    for (const object_ptr<td::TlObject> &object: postUpdateRequestsAndResponses) {
        if (isFunction(*object))
            postUpdateRequests.push_back(static_cast<const Function*>(object.get()));
        if (isObject(*object)) {
            if (!postUpdateRequests.empty())
                tgl.verifyRequests(postUpdateRequests);
            postUpdateRequests.clear();
            tgl.reply(td::move_tl_object_as<Object>(const_cast<object_ptr<td::TlObject> &>(object)));
        }
    }
    tgl.verifyRequests(postUpdateRequests);

    tgl.update(make_object<updateConnectionState>(make_object<connectionStateReady>()));

    tgl.reply(contactRequestId, std::move(getContactsReply));

    tgl.verifyRequest(*getChatsRequest());
    bool hasChats = getChatsReply->get_id() == td::td_api::ok::ID;
    tgl.reply(std::move(getChatsReply));
    if (hasChats) {
        tgl.verifyRequest(*getChatsRequest());
        tgl.reply(getChatsNoChatsResponse());
    }

    if ((postChatListEvents.size() == 1) && (*postChatListEvents.begin() == nullptr))
        prpl.verifyEvents(
            AccountSetAliasEvent(account, selfFirstName + " " + selfLastName),
            ShowAccountEvent(account)
        );
    else
        prpl.verifyEvents2(std::move(postChatListEvents));
}

void CommTest::loginWithOneContact()
{
    login(
        {standardUpdateUser(0), standardPrivateChat(0), makeUpdateChatListMain(chatIds[0])},
        // User and chat ids are 53-bit as of TDLib 1.8, and `chats` gained a total_count_
        make_object<users>(1, std::vector<int64_t>(1, userIds[0])),
        make_object<chats>(1, std::vector<int64_t>(1, chatIds[0])),
        {
            std::make_unique<AddBuddyEvent>(purpleUserName(0), userFirstNames[0] + " " + userLastNames[0],
                                            account, nullptr, nullptr, nullptr),
        }, {},
        {
            std::make_unique<UserStatusEvent>(account, purpleUserName(0), PURPLE_STATUS_AWAY),
            std::make_unique<AccountSetAliasEvent>(account, selfFirstName + " " + selfLastName),
            std::make_unique<ShowAccountEvent>(account)
        }
    );
}

object_ptr<updateUser> CommTest::standardUpdateUser(unsigned index)
{
    return make_object<updateUser>(makeUser(
        userIds[index],
        userFirstNames[index],
        userLastNames[index],
        userPhones[index],
        make_object<userStatusOffline>()
    ));
}

object_ptr<updateUser> CommTest::standardUpdateUserNoPhone(unsigned index)
{
    return make_object<updateUser>(makeUser(
        userIds[index],
        userFirstNames[index],
        userLastNames[index],
        "",
        make_object<userStatusOffline>()
    ));
}

object_ptr<updateNewChat> CommTest::standardPrivateChat(unsigned index, object_ptr<ChatList> chatList)
{
    object_ptr<chat> chat = makeChat(
        chatIds[index],
        make_object<chatTypePrivate>(userIds[index]),
        userFirstNames[index] + " " + userLastNames[index],
        nullptr, 0, 0, 0
    );
    // A chat no longer names one list it belongs to; it carries a position in each list it is
    // in, and that is what the plugin reads (see isChatInContactList() in account-data.cpp).
    if (chatList)
        chat->positions_.push_back(make_object<chatPosition>(std::move(chatList), 1, false, nullptr));
    return make_object<updateNewChat>(std::move(chat));
}

PurplePluginProtocolInfo &CommTest::pluginInfo()
{
    return *(PurplePluginProtocolInfo *)purplePlugin.info->extra_info;
}

void checkFile(const char *filename, void *content, unsigned size)
{
    gchar *actualContent;
    gsize  actualSize;
    ASSERT_TRUE(g_file_get_contents(filename, &actualContent, &actualSize, NULL)) << filename << " does not exist";
    ASSERT_EQ(actualSize, size) << "Wrong file size for " << filename;
    ASSERT_EQ(0, memcmp(content, actualContent, size)) << "Wrong content for " << filename;
    g_free(actualContent);
}
