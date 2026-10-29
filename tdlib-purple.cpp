#include "tdlib-purple.h"
#include "config.h"
#include "td-client.h"
#include "client-utils.h"
#include "purple-info.h"
#include "format.h"
#include "buildopt.h"
#include "pidgin-ui.h"
#include "receiving.h"
#include "clipboard.h"
#include <purple.h>

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>
#include <array>
#include <deque>
#include <mutex>
#include <ctype.h>
#include <unistd.h>

#ifndef NoLottie
#include <rlottie.h>
#endif

static const char *tgprpl_list_icon (PurpleAccount *acct, PurpleBuddy *buddy)
{
    return "telegram";
}

static const char *getLastOnline(const td::td_api::UserStatus &status)
{
    switch (status.get_id()) {
        case td::td_api::userStatusOnline::ID:
            // TRANSLATOR: Buddy infobox, value for "last online"
            return _("now");
        case td::td_api::userStatusOffline::ID: {
            const td::td_api::userStatusOffline &offline = static_cast<const td::td_api::userStatusOffline &>(status);
            time_t timestamp = offline.was_online_;
            return ctime(&timestamp);
        }
        case td::td_api::userStatusRecently::ID:
            // TRANSLATOR: Buddy infobox, value for "last online"
            return _("recently");
        case td::td_api::userStatusLastWeek::ID:
            // TRANSLATOR: Buddy infobox, value for "last online"
            return _("last week");
        case td::td_api::userStatusLastMonth::ID:
            // TRANSLATOR: Buddy infobox, value for "last online"
            return _("last month");
    }

    return "";
}

static void tgprpl_tooltip_text (PurpleBuddy *buddy, PurpleNotifyUserInfo *info, gboolean full)
{
    PurpleTdClient *tdClient = getTdClient(purple_buddy_get_account(buddy));
    if (!tdClient) return;

    std::vector<const td::td_api::user *> users;
    tdClient->getUsers(purple_buddy_get_name(buddy), users);

    if ((users.size() == 1) && users[0]->status_) {
        const char *lastOnline = getLastOnline(*users[0]->status_);
        if (lastOnline && *lastOnline) {
            // TRANSLATOR: Buddy infobox, key
            purple_notify_user_info_add_pair(info, _("Last online"), lastOnline);
        }
    }
}

static GList *tgprpl_status_types (PurpleAccount *acct)
{
    GList *types = NULL;
    PurpleStatusType *type;

    type = purple_status_type_new_full (PURPLE_STATUS_AVAILABLE, NULL, NULL, FALSE, TRUE, FALSE);
    types = g_list_prepend (types, type);

    type = purple_status_type_new_full (PURPLE_STATUS_AWAY, NULL, NULL, FALSE, FALSE, FALSE);
    types = g_list_prepend (types, type);

    type = purple_status_type_new_full (PURPLE_STATUS_OFFLINE, NULL, NULL, FALSE, TRUE, FALSE);
    types = g_list_prepend (types, type);

    return g_list_reverse (types);
}

struct RequestData {
    PurpleAccount *account;
    std::string stringData;

    RequestData(PurpleAccount *account) : account(account) {}
};

static void cancelRequest(RequestData *data, int action)
{
    delete data;
}

static void deleteGroupConfirm(RequestData *data, int action)
{
    std::unique_ptr<RequestData> request(data);
    PurpleTdClient *tdClient = getTdClient(request->account);

    if (tdClient)
        tdClient->leaveGroup(request->stringData, true);
}

static void leaveGroupConfirm(RequestData *data, int action)
{
    std::unique_ptr<RequestData> request(data);
    PurpleTdClient *tdClient = getTdClient(request->account);

    if (tdClient)
        tdClient->leaveGroup(request->stringData, false);
}

static void leaveGroup(PurpleBlistNode *node, gpointer data)
{
    if (! PURPLE_BLIST_NODE_IS_CHAT(node))
        return;

    PurpleChat     *chat     = PURPLE_CHAT(node);
    PurpleAccount  *account  = purple_chat_get_account(chat);
    PurpleTdClient *tdClient = getTdClient(account);
    if (tdClient) {
        const char  *chatName  = getChatName(purple_chat_get_components(chat));
        RequestData *request   = new RequestData(account);
        request->stringData = chatName ? chatName : "";

        if (tdClient->getBasicGroupMembership(chatName) == BasicGroupMembership::Creator) {
            // TRANSLATOR: Owning group deletion dialog, title
            purple_request_action(purple_account_get_connection(account), _("Leaving group"),
                                  // TRANSLATOR: Owning group deletion dialog, primary content
                                  _("Confirm deleting group"),
                                  // TRANSLATOR: Owning group deletion dialog, secondary content
                                  _("Leaving basic group you created will delete the group. Continue?"),
                                  0, account, NULL, NULL, request, 2,
                                  // TRANSLATOR: Owning group deletion dialog, alternative is "_No"
                                  _("_Yes"), leaveGroupConfirm,
                                  // TRANSLATOR: Owning group deletion dialog, alternative is "_Yes"
                                  _("_No"), cancelRequest);
        } else {
            // TRANSLATOR: Group leave dialog, title
            purple_request_action(purple_account_get_connection(account), _("Leaving group"),
                                  // TRANSLATOR: Group leave dialog, content
                                  _("Leave the group?"), NULL,
                                  0, account, NULL, NULL, request, 2,
                                  // TRANSLATOR: Group leave dialog, alternative is "_No"
                                  _("_Yes"), leaveGroupConfirm,
                                  // TRANSLATOR: Group leave dialog, alternative is "_Yes"
                                  _("_No"), cancelRequest);
        }
    }
}

static void deleteGroup(PurpleBlistNode *node, gpointer data)
{
    if (! PURPLE_BLIST_NODE_IS_CHAT(node))
        return;

    PurpleChat     *chat     = PURPLE_CHAT(node);
    PurpleAccount  *account  = purple_chat_get_account(chat);
    PurpleTdClient *tdClient = getTdClient(account);
    if (tdClient) {
        const char  *chatName  = getChatName(purple_chat_get_components(chat));

        if (tdClient->getBasicGroupMembership(chatName) == BasicGroupMembership::NonCreator) {
            // TRANSLATOR: Group deletion error dialog, title
            purple_notify_error(account, _("Cannot delete group"),
                                // TRANSLATOR: Group deletion error dialog, content
                                _("Cannot delete basic group created by someone else"), NULL);
        } else {
            RequestData *request = new RequestData(account);
            request->stringData = chatName ? chatName : "";
            // TRANSLATOR: Group deletion confirmation dialog, title
            purple_request_action(purple_account_get_connection(account), _("Deleting group"),
                                  // TRANSLATOR: Group deletion confirmation dialog, content
                                  _("Delete the group?"), NULL,
                                  0, account, NULL, NULL, request, 2,
                                  // TRANSLATOR: Group deletion dialog, alternative is "_No"
                                  _("_Yes"), deleteGroupConfirm,
                                  // TRANSLATOR: Group deletion dialog, alternative is "_Yes"
                                  _("_No"), cancelRequest);
        }
    }
}

static void showInviteLink(PurpleBlistNode *node, gpointer data)
{
    if (! PURPLE_BLIST_NODE_IS_CHAT(node))
        return;

    PurpleChat     *chat     = PURPLE_CHAT(node);
    PurpleAccount  *account  = purple_chat_get_account(chat);
    PurpleTdClient *tdClient = getTdClient(account);
    if (tdClient)
        tdClient->showInviteLink(getChatName(purple_chat_get_components(chat)));
}

static void createSecretChat(PurpleBlistNode *node, gpointer data)
{
    if (! PURPLE_BLIST_NODE_IS_BUDDY(node))
        return;

    PurpleBuddy    *buddy    = PURPLE_BUDDY(node);
    PurpleAccount  *account  = purple_buddy_get_account(buddy);
    PurpleTdClient *tdClient = getTdClient(account);
    if (tdClient)
        tdClient->createSecretChat(purple_buddy_get_name(buddy));
}

// Shown in Pidgin's conversation window's Conversation > More menu as well
static void toggleDeletedMessages(PurpleBlistNode *node, gpointer data)
{
    PurpleAccount *account = PURPLE_BLIST_NODE_IS_CHAT(node) ? purple_chat_get_account(PURPLE_CHAT(node))
                                                             : purple_buddy_get_account(PURPLE_BUDDY(node));
    setKeepsDeletedMessages(node, !keepsDeletedMessages(account, node));
}

static PurpleMenuAction *makeDeletedMessagesAction(PurpleBlistNode *node, PurpleAccount *account)
{
    // What picking it does, there being no check items here
    return purple_menu_action_new(keepsDeletedMessages(account, node) ?
                                  // TRANSLATOR: Contact and conversation menu item, while deleted messages stay in the conversation window, struck out. It has them removed from now on.
                                  _("Remove deleted messages") :
                                  // TRANSLATOR: Contact and conversation menu item, while deleted messages are removed from the conversation window. It has them stay, struck out, from now on.
                                  _("Keep deleted messages, struck out"),
                                  PURPLE_CALLBACK(toggleDeletedMessages), NULL, NULL);
}

// A chat names its members, and the senders of its messages, by user id, idNNN, so that two of one
// display name are told apart: shown by the contact's alias, or failing that, by this
static char *tgprpl_get_cb_alias(PurpleConnection *gc, int id, const char *who)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    std::string     alias    = (tdClient && who) ? tdClient->getChatMemberAlias(who) : std::string();
    return alias.empty() ? NULL : g_strdup(alias.c_str());
}

// The conversation Pidgin's IM opens with a chat member, from the menu of their name, is the one
// with them the plugin writes to; Pidgin uses the member's name if there is none
static char *tgprpl_get_cb_real_name(PurpleConnection *gc, int id, const char *who)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    std::string     name     = (tdClient && who) ? tdClient->getChatMemberImName(who) : std::string();
    return name.empty() ? NULL : g_strdup(name.c_str());
}

// Pidgin's list of blocked users is Telegram's
static void setBlocked(PurpleConnection *gc, const char *name, bool blocked)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    UserId          userId   = purpleBuddyNameToUserId(name);
    if (tdClient && userId.valid())
        tdClient->setUserBlocked(userId, blocked);
}

static void tgprpl_add_deny(PurpleConnection *gc, const char *name)
{
    setBlocked(gc, name, true);
}

static void tgprpl_rem_deny(PurpleConnection *gc, const char *name)
{
    setBlocked(gc, name, false);
}

// The chat of a contact, or of a group, in the contact list
static ChatId getBlistNodeChatId(PurpleTdClient *tdClient, PurpleBlistNode *node)
{
    if (PURPLE_BLIST_NODE_IS_CHAT(node))
        return getTdlibChatId(getChatName(purple_chat_get_components(PURPLE_CHAT(node))));
    if (PURPLE_BLIST_NODE_IS_BUDDY(node))
        return tdClient->getPrivateChatId(purpleBuddyNameToUserId(purple_buddy_get_name(PURPLE_BUDDY(node))));
    return ChatId();
}

static PurpleAccount *getBlistNodeAccount(PurpleBlistNode *node)
{
    return PURPLE_BLIST_NODE_IS_CHAT(node) ? purple_chat_get_account(PURPLE_CHAT(node))
                                           : purple_buddy_get_account(PURPLE_BUDDY(node));
}

static void reportBlistNode(PurpleBlistNode *node, gpointer data)
{
    PurpleTdClient *tdClient = getTdClient(getBlistNodeAccount(node));
    ChatId          chatId   = tdClient ? getBlistNodeChatId(tdClient, node) : ChatId();
    if (chatId.valid())
        tdClient->reportChat(chatId, {});
}

// Where Telegram takes a report about the chat as a whole
static void addReportAction(GList *&menu, PurpleBlistNode *node)
{
    PurpleTdClient *tdClient = getTdClient(getBlistNodeAccount(node));
    ChatId          chatId   = tdClient ? getBlistNodeChatId(tdClient, node) : ChatId();
    if (chatId.valid() && tdClient->canReportChat(chatId))
        // TRANSLATOR: Contact, group and conversation menu item; it asks why, in a dialog or more.
        menu = g_list_append(menu, purple_menu_action_new(_("Report…"), PURPLE_CALLBACK(reportBlistNode), NULL, NULL));
}

static GList* tgprpl_blist_node_menu (PurpleBlistNode *node)
{
    GList *menu = NULL;

    if (PURPLE_BLIST_NODE_IS_CHAT(node)) {
        PurpleChat *chat = PURPLE_CHAT(node);
        if (!getTdlibChatId(getChatName(purple_chat_get_components(chat))).valid())
            return menu;

        PurpleMenuAction* action;
        // TRANSLATOR: Group menu action item
        action = purple_menu_action_new(_("Leave group"),
                                        PURPLE_CALLBACK(leaveGroup),
                                        NULL, NULL);
        menu = g_list_append(menu, action);

        // TRANSLATOR: Group menu action item
        action = purple_menu_action_new(_("Delete group"),
                                        PURPLE_CALLBACK(deleteGroup),
                                        NULL, NULL);
        menu = g_list_append(menu, action);

        // TRANSLATOR: Group menu action item
        action = purple_menu_action_new(_("Show invite link"),
                                        PURPLE_CALLBACK(showInviteLink),
                                        NULL, NULL);
        menu = g_list_append(menu, action);

        menu = g_list_append(menu, makeDeletedMessagesAction(node, purple_chat_get_account(chat)));
        addReportAction(menu, node);
    } else if ( PURPLE_BLIST_NODE_IS_BUDDY(node) &&
                purpleBuddyNameToUserId(purple_buddy_get_name(PURPLE_BUDDY(node))).valid() )
    {
        PurpleMenuAction *action;
        // TRANSLATOR: Buddy menu action item
        action = purple_menu_action_new(_("Start secret chat"),
                                        PURPLE_CALLBACK(createSecretChat),
                                        NULL, NULL);
        menu = g_list_append(menu, action);

        menu = g_list_append(menu, makeDeletedMessagesAction(node, purple_buddy_get_account(PURPLE_BUDDY(node))));
        addReportAction(menu, node);
    }

    return menu;
}

static GList *tgprpl_chat_join_info (PurpleConnection *gc)
{
    return getChatJoinInfo();
}

static GHashTable *tgprpl_chat_info_defaults (PurpleConnection *gc, const char *chat_name)
{
    GHashTable *components = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
    g_hash_table_insert(components, (gpointer)getChatNameComponent(), g_strdup(chat_name));
    return components;
}

static ITransceiverBackend *g_testBackend = nullptr;

void tgprpl_set_test_backend(ITransceiverBackend *backend)
{
    g_testBackend = backend;
}

void tgprpl_set_single_thread()
{
    AccountThread::setSingleThread();
}

struct PurpleConversationInfo {
    std::string accountName;
    std::string convName;
    PurpleConversationType type;
};

static gboolean sendConversationReadReceipts(void *arg)
{
    std::unique_ptr<PurpleConversationInfo> info(static_cast<PurpleConversationInfo *>(arg));
    PurpleAccount *account = purple_accounts_find(info->accountName.c_str(), config::pluginId);
    PurpleConversation *conv = NULL;
    PurpleTdClient *tdClient = NULL;
    if (account != NULL) {
        tdClient = getTdClient(account);
        conv = purple_find_conversation_with_account(info->type, info->convName.c_str(), account);
    }

    if (conv && tdClient)
        tdClient->sendReadReceipts(conv);

    return G_SOURCE_REMOVE;
}

static void
conversation_updated_cb(PurpleConversation *conv, PurpleConvUpdateType type)
{
    PurpleAccount *account = purple_conversation_get_account(conv);
    if (!strcmp(purple_account_get_protocol_id(account), config::pluginId) &&
        (type == PURPLE_CONV_UPDATE_TITLE))
    {
        // Pidgin sets it anew, from an alias, without the mark
        PurpleTdClient *tdClient = getTdClient(account);
        if (tdClient)
            tdClient->markConversation(conv);
    }
    if (!strcmp(purple_account_get_protocol_id(account), config::pluginId) &&
        (type == PURPLE_CONV_UPDATE_UNSEEN))
    {
        // With pidgin, when this callback is triggered by conversation window coming into window
        // manager focus, purple_conversation_has_focus will still return false if called right now,
        // so postpone the rest by half a second to get a better value when
        // purple_conversation_has_focus is called down the line to determine if the read receipts
        // should actually be sent.
        PurpleConversationInfo *arg = new PurpleConversationInfo;
        arg->accountName = purple_account_get_username(account);
        arg->convName = purple_conversation_get_name(conv);
        arg->type = purple_conversation_get_type(conv);
        g_timeout_add(500, sendConversationReadReceipts, arg);
    }
}

// A conversation just opened, or a group chat's joined again, filled once Pidgin has set it up,
// the account is connected and knows the chat, and the chat is not busy
struct ConversationToFill: PurpleConversationInfo {
    unsigned tries = 0;
};

static const char FILL_PENDING_KEY[] = "tdlib-fill-pending";

static gboolean fillConversation(void *arg)
{
    std::unique_ptr<ConversationToFill> info(static_cast<ConversationToFill *>(arg));
    PurpleAccount      *account  = purple_accounts_find(info->accountName.c_str(), config::pluginId);
    PurpleConversation *conv     = account ? purple_find_conversation_with_account(info->type, info->convName.c_str(),
                                                                                   account)
                                           : nullptr;
    if (!conv)
        return G_SOURCE_REMOVE;
    PurpleTdClient *tdClient = getTdClient(account);
    // Waiting so long, it is fetching more than the tab would hold anyway, or will never know it
    enum { RETRY_MS = 1000, MAX_TRIES = 120 };
    if ((!tdClient || !tdClient->fillConversation(conv)) && (++info->tries < MAX_TRIES))
        g_timeout_add(RETRY_MS, fillConversation, info.release());
    else
        purple_conversation_set_data(conv, FILL_PENDING_KEY, nullptr);
    return G_SOURCE_REMOVE;
}

// Both for a conversation just created, and for a group chat's, kept from an earlier connection,
// joined again: Pidgin may well have opened it before the account knew the chat
static void conversationCreated(PurpleConversation *conv, void *data)
{
    PurpleAccount *account = purple_conversation_get_account(conv);
    if (account != data)
        return;
    PurpleTdClient *tdClient = getTdClient(account);
    if (tdClient)
        tdClient->markConversation(conv);
    if (!canRedrawConversation(conv) || purple_conversation_get_data(conv, FILL_PENDING_KEY))
        return;
    purple_conversation_set_data(conv, FILL_PENDING_KEY, conv);
    ConversationToFill *info = new ConversationToFill;
    info->accountName = purple_account_get_username(account);
    info->convName    = purple_conversation_get_name(conv);
    info->type        = purple_conversation_get_type(conv);
    g_idle_add(fillConversation, info);
}

static void tgprpl_login (PurpleAccount *acct)
{
    purple_debug_misc(config::pluginId, "version %s\n", config::versionString);
    PurpleConnection *gc       = purple_account_get_connection (acct);
    PurpleTdClient   *tdClient = new PurpleTdClient(acct, g_testBackend);

    purple_connection_set_protocol_data (gc, tdClient);
    // As Telegram has it: those listed are blocked, everyone else is not. Pidgin's Block, in another
    // mode, would first empty the list, unblocking everyone else on the server too.
    acct->perm_deny = PURPLE_PRIVACY_DENY_USERS;
    // Pidgin's formatting toolbar, but for what Telegram has no formatting for: font faces and colors
    // it cannot be told to leave out, which are dropped from what is sent
    gc->flags = static_cast<PurpleConnectionFlags>(gc->flags | PURPLE_CONNECTION_HTML |
                                                   PURPLE_CONNECTION_NO_BGCOLOR | PURPLE_CONNECTION_NO_FONTSIZE);

    purple_signal_connect(purple_conversations_get_handle(), "conversation-updated",
                          acct, PURPLE_CALLBACK(conversation_updated_cb), NULL);
    purple_signal_connect(purple_conversations_get_handle(), "conversation-created",
                          acct, PURPLE_CALLBACK(conversationCreated), acct);
    purple_signal_connect(purple_conversations_get_handle(), "chat-joined",
                          acct, PURPLE_CALLBACK(conversationCreated), acct);
}

static void tgprpl_close (PurpleConnection *gc)
{
    // Connected again at the next login
    PurpleAccount *account = purple_connection_get_account(gc);
    purple_signal_disconnect(purple_conversations_get_handle(), "conversation-updated",
                             account, PURPLE_CALLBACK(conversation_updated_cb));
    purple_signal_disconnect(purple_conversations_get_handle(), "conversation-created",
                             account, PURPLE_CALLBACK(conversationCreated));
    purple_signal_disconnect(purple_conversations_get_handle(), "chat-joined",
                             account, PURPLE_CALLBACK(conversationCreated));
    delete static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    purple_connection_set_protocol_data(gc, NULL);
}

// What is sent while a message is being edited in the conversation's input field is the edit
static bool sendEdit(PurpleTdClient *tdClient, PurpleConversation *conv, const char *message)
{
    EntryEdit edit;
    if (!endEntryEdit(conv, &edit))
        return false;
    // With its formatting, as messages are sent; compared so, as Pidgin writes its markup its own way
    auto text = parseFormattedText(message);
    if (isSameFormattedText(*text, *parseFormattedText(edit.original)))
        // TRANSLATOR: In-chat notification, not logged, after a message being edited was sent unchanged.
        showEditOutcome(conv, _("The message is left as it was."));
    else
        tdClient->editMessage(edit.chatId, edit.messageId, std::move(text), edit.isCaption);
    return true;
}

static int tgprpl_send_im (PurpleConnection *gc, const char *who, const char *message, PurpleMessageFlags flags)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    purple_debug_misc(config::pluginId, "tgprpl_send_im to '%s' flags=0x%x\n", who, (unsigned)flags);
    PurpleConversation *conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, who,
                                                                     purple_connection_get_account(gc));
    if (sendEdit(tdClient, conv, message))
        return 0;
    return tdClient->sendMessage(who, message);
}

static unsigned int tgprpl_send_typing (PurpleConnection *gc, const char *who, PurpleTypingState typing)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    tdClient->sendTyping(who, typing == PURPLE_TYPING);
    return 0;
}

static void tgprpl_info_show (PurpleConnection *gc, const char *who)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    std::vector<const td::td_api::user *> users;
    tdClient->getUsers(who, users);

    PurpleNotifyUserInfo *info = purple_notify_user_info_new();
    if (users.empty()) {
        // TRANSLATOR: Buddy infobox, error
        purple_notify_user_info_add_pair(info, _("User not found"), NULL);
    }

    for (const td::td_api::user *user: users) {
        if (purple_notify_user_info_get_entries(info))
            purple_notify_user_info_add_section_break(info);

        // TRANSLATOR: Buddy infobox, key
        purple_notify_user_info_add_pair(info, _("First name"), user->first_name_.c_str());
        // TRANSLATOR: Buddy infobox, key
        purple_notify_user_info_add_pair(info, _("Last name"), user->last_name_.c_str());
        // Username field was removed in newer tdlib versions
        if (!user->phone_number_.empty()) {
            // TRANSLATOR: Buddy infobox, key
            purple_notify_user_info_add_pair(info, _("Phone number"), user->phone_number_.c_str());
        }
        if (user->status_) {
            const char *lastOnline = getLastOnline(*user->status_);
            if (lastOnline && *lastOnline) {
                // TRANSLATOR: Buddy infobox, key
                purple_notify_user_info_add_pair(info, _("Last online"), lastOnline);
            }
        }
        std::string username = getPurpleBuddyName(*user);
        // TRANSLATOR: Buddy infobox, key
        purple_notify_user_info_add_pair(info, _("Internal id"), username.c_str());
    }

    purple_notify_userinfo(gc, who, info, NULL, NULL);
}

static void tgprpl_set_status (PurpleAccount *acct, PurpleStatus *status)
{
}

static void tgprpl_add_buddy (PurpleConnection *gc, PurpleBuddy *buddy, PurpleGroup *group)
{
    const char  *phoneNumber = purple_buddy_get_name(buddy);
    const char  *alias       = purple_buddy_get_alias(buddy);
    const char  *groupName   = group ? purple_group_get_name(group) : NULL;

    std::string  phoneNumberStr = phoneNumber;
    std::string  aliasStr       = alias ? alias : "";

    // Buddy will be re-added with different username if it succeeds
    purple_blist_remove_buddy(buddy);
    // Now original C strings are gone

    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    tdClient->addContact(phoneNumberStr, aliasStr, groupName ? groupName : "");
}

static void request_delete_contact_on_server_yes (RequestData *data, int action)
{
    std::unique_ptr<RequestData> request(data);
    PurpleTdClient *tdClient = getTdClient(request->account);

    if (tdClient)
        tdClient->removeContactAndPrivateChat(request->stringData);
}

static void tgprpl_request_delete_contact (PurpleConnection *gc, PurpleBuddy *buddy, PurpleGroup *group)
{
    g_return_if_fail(buddy);

    RequestData *data = new RequestData(purple_connection_get_account(gc));
    data->stringData = purple_buddy_get_name(buddy);

    // TRANSLATOR: Buddy deletion confirmation, title
//     purple_request_yes_no(gc, _("Remove contact"),
//                           // TRANSLATOR: Buddy deletion confirmation, content
//                           _("Remove from global contact list and delete chat history from the server?\n"),
//                           NULL,
//                           0, purple_connection_get_account(gc), purple_buddy_get_name(buddy),
//                           NULL, data, request_delete_contact_on_server_yes,
//                           cancelRequest);
    request_delete_contact_on_server_yes(data, 0);
}

static std::array<const char *, 3> invitePrefixes {
    "https://t.me/joinchat/",
    "https://telegram.me/joinchat/",
    "https://telegram.dog/joinchat/"
};

static std::array<const char *, 2> groupLinkPrefixes {
    "https://t.me/s/",
    "https://t.me/"
};

static bool isValidInviteLink(const char *link)
{
    return !strncmp(link, invitePrefixes[0], strlen(invitePrefixes[0])) ||
           !strncmp(link, invitePrefixes[1], strlen(invitePrefixes[1])) ||
           !strncmp(link, invitePrefixes[2], strlen(invitePrefixes[2]));
}

static std::string getGroupUsernameFromLink(const char *link)
{
    for (const char *prefix: groupLinkPrefixes) {
        size_t prefixLen = strlen(prefix);
        if (!strncmp(link, prefix, prefixLen) && (strchr(link + prefixLen, '/') == NULL))
            return link + prefixLen;
    }

    return std::string();
}

static void create_group_chat_cb (RequestData *data, PurpleRequestFields* fields)
{
    std::unique_ptr<RequestData> request(data);

    std::vector<std::string> members;
    for (const char *label: {"user1", "user2", "user3"}) {
        const char *userName = purple_request_fields_get_string(fields, label);
        if (userName && *userName)
            members.emplace_back(userName);
    }

    PurpleTdClient *tdClient = getTdClient(request->account);
    if (tdClient)
        tdClient->createGroup(request->stringData.c_str(), GROUP_TYPE_BASIC, members);
}

static void requestCreateBasicGroup(PurpleConnection *gc, const char *name)
{
    // Telegram doesn't allow to create chats with only one user, so we need to force
    // the user to specify at least one other one.
    PurpleRequestFields* fields = purple_request_fields_new ();
    PurpleRequestFieldGroup* group = purple_request_field_group_new (
        // TRANSLATOR: Group creation dialog, secondary content
        _("Invite at least one additional user by specifying their full name (autocompletion available)."));

    // TRANSLATOR: Group creation dialog, label
    PurpleRequestField *field = purple_request_field_string_new ("user1", _("Username"), NULL, FALSE);
    purple_request_field_set_type_hint (field, "screenname");
    purple_request_field_group_add_field (group, field);

    field = purple_request_field_string_new ("user2", _("Username"), NULL, FALSE);
    purple_request_field_set_type_hint (field, "screenname");
    purple_request_field_group_add_field (group, field);

    field = purple_request_field_string_new ("user3", _("Username"), NULL, FALSE);
    purple_request_field_set_type_hint (field, "screenname");
    purple_request_field_group_add_field (group, field);

    purple_request_fields_add_group (fields, group);

    RequestData *data = new RequestData(purple_connection_get_account(gc));
    data->stringData = name;
    // TRANSLATOR: Group creation dialog, title
    purple_request_fields (gc, _("Create group chat"),
                           // TRANSLATOR: Group creation dialog, primary content
                           _("Invite users"), NULL, fields,
                           // TRANSLATOR: Group creation dialog, alternative is "_Cancel". The underscore marks accelerator keys, they must be different!
                           _("_OK"), G_CALLBACK(create_group_chat_cb),
                           // TRANSLATOR: Group creation dialog, alternative is "_OK". The underscore marks accelerator keys, they must be different!
                           _("_Cancel"), G_CALLBACK(cancelRequest),
                           purple_connection_get_account(gc), NULL, NULL, data);
}

static void tgprpl_chat_join (PurpleConnection *gc, GHashTable *data)
{
    PurpleTdClient *tdClient   = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    const char     *name       = getChatName(data);
    const char     *joinString = getChatJoinString(data);

    if (name && *name) {
        if (!tdClient->joinChat(name))
            purple_serv_got_join_chat_failed (gc, data);
    } else if (joinString && *joinString) {
        if (isValidInviteLink(joinString))
            tdClient->joinChatByInviteLink(joinString);
        else {
            std::string name = getGroupUsernameFromLink(joinString);
            if (name.empty() && (strchr(joinString, '/') == NULL))
                name = joinString;

            if (!name.empty())
                tdClient->joinChatByGroupName(joinString, name.c_str());
            else {
                // TRANSLATOR: Join error dialog, secondary content. all five arguments are URLs. "name" should be part of the URL, and indicate that it can be a name in your language.
                std::string extraMessage = formatMessage(_("Invite link must start with {0}, {1} or {2}. Public group link must be {3}name or {4}name."),
                                                         {invitePrefixes[0], invitePrefixes[1], invitePrefixes[2],
                                                         groupLinkPrefixes[0], groupLinkPrefixes[1]});
                // TRANSLATOR: Join error dialog, title
                purple_notify_error(gc, _("Failed to join chat"),
                                    // TRANSLATOR: Join error dialog, primary content
                                    _("Must be invite link, public group link or group name"),
                                    extraMessage.c_str());
                purple_serv_got_join_chat_failed (gc, data);
            }
        }
    } else {
        const char *groupName  = getChatGroupName(data);
        int         groupType  = getChatGroupType(data);
        if (groupName && *groupName && ((groupType == GROUP_TYPE_BASIC) ||
                                        (groupType == GROUP_TYPE_SUPER) ||
                                        (groupType == GROUP_TYPE_CHANNEL))) {
            if (groupType == GROUP_TYPE_BASIC)
                requestCreateBasicGroup(gc, groupName);
            else
                tdClient->createGroup(groupName, groupType, {});
        } else {
            // TRANSLATOR: Join error dialog, title
            purple_notify_error(gc, _("Failed to join chat"),
                                // TRANSLATOR: Join error dialog, primary content
                                _("Please enter group name and valid type"), NULL);
            purple_serv_got_join_chat_failed (gc, data);
        }
    }
}

static char *tgprpl_get_chat_name (GHashTable * data)
{
    return g_strdup(getChatName(data));
}

static void tgprpl_chat_invite (PurpleConnection *gc, int id, const char *message, const char *who)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    if (tdClient)
        tdClient->addUserToChat(id, who);
}

static int tgprpl_send_chat (PurpleConnection *gc, int id, const char *message, PurpleMessageFlags flags)
{
    purple_debug_misc(config::pluginId, "Sending group chat message: purple chat id %d, flags=0x%x\n",
                      id, (unsigned)flags);
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    if (sendEdit(tdClient, purple_find_chat(gc, id), message))
        return 0;
    return tdClient->sendGroupMessage(id, message);
}

static void tgprpl_rename_buddy(PurpleConnection *gc, const char *who, const char *alias)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    tdClient->renameContact(who, alias);
}

static void renameGroup(PurpleConnection *, const char *old_name,
						PurpleGroup *group, GList *moved_buddies)
{
    // If PRPL does not have this function, libpurple will mass-remove all buddies that were in the
    // group (really remove, using remove_buddy) and then mass-create them again with add_buddy.
    // With some effort, it might even work, but it's completely unnecessary, so avoid it by
    // implementing a rename_group that does nothing.
}

static PurpleRoomlist *tgprpl_roomlist_get_list (PurpleConnection *gc)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    PurpleRoomlist *roomlist = purple_roomlist_new(purple_connection_get_account(gc));

    if (tdClient)
        tdClient->getGroupChatList(roomlist);

    return roomlist;
}

static void tgprpl_roomlist_cancel (PurpleRoomlist *list)
{
}

static char *getRoomlistChatName(PurpleRoomlistRoom *room)
{
    return g_strdup((char *)purple_roomlist_room_get_fields(room)->data);
}

static gboolean tgprpl_can_receive_file (PurpleConnection *gc, const char *who)
{
    return TRUE;
}

struct UploadData {
    enum class Type {
        User,
        Chat
    };
    PurpleAccount *account;
    Type           type;
    int            chatId;
};

static void startUpload(PurpleXfer *xfer)
{
    const UploadData *data     = static_cast<UploadData *>(xfer->data);
    PurpleTdClient   *tdClient = getTdClient(data->account);
    if (!tdClient)
        return;

    switch (data->type) {
        case UploadData::Type::User: {
            const char *username = purple_xfer_get_remote_user(xfer);
            if (username)
                tdClient->sendFileToChat(xfer, username, PURPLE_CONV_TYPE_IM, 0);
            break;
        }
        case UploadData::Type::Chat: {
            tdClient->sendFileToChat(xfer, NULL, PURPLE_CONV_TYPE_CHAT, data->chatId);
            break;
        }
    }
}

static void cancelUpload(PurpleXfer *xfer)
{
    std::unique_ptr<UploadData> data(static_cast<UploadData *>(xfer->data));
    PurpleTdClient *tdClient = getTdClient(data->account);

    if (tdClient)
        tdClient->cancelUpload(xfer);
}

static void endUpload(PurpleXfer *xfer)
{
    delete static_cast<UploadData *>(xfer->data);
}

static PurpleXfer *newUploadTransfer(PurpleConnection *gc, const char *who)
{
    PurpleAccount *account = purple_connection_get_account (gc);
    PurpleXfer    *xfer    = purple_xfer_new(account, PURPLE_XFER_SEND, who);
    if (xfer) {
        purple_xfer_set_init_fnc(xfer, startUpload);
        purple_xfer_set_cancel_send_fnc(xfer, cancelUpload);
        purple_xfer_set_end_fnc(xfer, endUpload);
    }

    return xfer;
}

static void tgprpl_send_file (PurpleConnection * gc, const char *who, const char *file)
{
    PurpleXfer *X = newUploadTransfer (gc, who);
    X->data = new UploadData{purple_connection_get_account(gc), UploadData::Type::User, 0};
    if (file) {
        purple_xfer_request_accepted (X, file);
    } else {
        purple_xfer_request (X);
    }
}

static void sendFileToChat(PurpleConnection *gc, int id, const char *filename)
{
    PurpleConversation *conv = purple_find_chat(gc, id);
    if (!conv) {
        purple_debug_warning(config::pluginId, "No chat conversation with id %d\n", id);
        return;
    }

    PurpleXfer *X = newUploadTransfer(gc, purple_conversation_get_title(conv));
    X->data = new UploadData{purple_connection_get_account(gc), UploadData::Type::Chat, id};

    if (filename) {
        purple_xfer_request_accepted (X, filename);
    } else {
        purple_xfer_request (X);
    }
}

static GHashTable *tgprpl_get_account_text_table (PurpleAccount *pa)
{
    GHashTable *HT;
    HT = g_hash_table_new (g_str_hash, g_str_equal);
    static char label[] = "login_label";
    // TRANSLATOR: Account creation, telephone hint. Keep it short!
    g_hash_table_insert(HT, label, _("phone no. (+ country prefix)"));
    return HT;
}

static void tgprpl_set_chat_topic(PurpleConnection *gc, int id, const char *topic)
{
    PurpleTdClient *tdClient = static_cast<PurpleTdClient *>(purple_connection_get_protocol_data(gc));
    tdClient->setGroupDescription(id, topic);
}

static PurpleCmdRet tgprpl_cmd_kick(PurpleConversation *conv, const gchar *cmd, gchar **args, gchar **error, void *data)
{
    PurpleTdClient *tdClient = getTdClient(purple_conversation_get_account(conv));

    if (!*args || !tdClient)
        return PURPLE_CMD_RET_FAILED;

    tdClient->kickUserFromChat(conv, *args);
    return PURPLE_CMD_RET_OK;
}

static PurpleMediaCaps getMediaCaps(PurpleAccount *account, const char *who)
{
#ifndef NoVoip
    return PURPLE_MEDIA_CAPS_AUDIO;
#else
    return PURPLE_MEDIA_CAPS_NONE;
#endif
}

gboolean initiateMedia(PurpleAccount *account, const char *who, PurpleMediaSessionType type)
{
#ifndef NoVoip
    if (!(type & PURPLE_MEDIA_AUDIO))
        return FALSE;

    PurpleTdClient *tdClient = getTdClient(account);
    if (tdClient)
        return tdClient->startVoiceCall(who) ? TRUE : FALSE;
    else
        return FALSE;
#else
    return FALSE;
#endif
}

static PurpleCmdRet cancelEditCommand(PurpleConversation *conv, const gchar *cmd, gchar **args, gchar **error,
                                      void *data)
{
    if (!endEntryEdit(conv, nullptr)) {
        // TRANSLATOR: Error after the /cancel command.
        *error = g_strdup(_("No message is being edited."));
        return PURPLE_CMD_RET_FAILED;
    }
    // TRANSLATOR: In-chat notification, not logged, after the /cancel command.
    showEditOutcome(conv, _("The message is left as it was."));
    return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet hangupCommand(PurpleConversation *conv, const gchar *cmd, gchar **args, gchar **error, void *data)
{
    PurpleTdClient *tdClient = getTdClient(purple_conversation_get_account(conv));

    if (!tdClient)
        return PURPLE_CMD_RET_FAILED;

    if (tdClient->terminateCall(conv))
        return PURPLE_CMD_RET_OK;
    else
        return PURPLE_CMD_RET_FAILED;
}

// Placeholders for the files, typed into the input field where the clipboard is pasted
static std::vector<std::string> filesPasted(PurpleConversation *conv, const std::vector<std::string> &files)
{
    PurpleTdClient  *tdClient = getTdClient(purple_conversation_get_account(conv));
    ClipboardContent content;
    content.files = files;
    return tdClient ? tdClient->addPasted(content) : std::vector<std::string>();
}

static std::string joinPlaceholders(const std::vector<std::string> &placeholders)
{
    std::string text;
    for (const std::string &placeholder: placeholders)
        text += (text.empty() ? "" : " ") + placeholder;
    return text;
}

// What is on the clipboard, attached to the message being written: the files copied to it, or else
// its picture. Pidgin reads the clipboard and puts the picture into the input field, and the files'
// placeholders; elsewhere xclip reads it, and the placeholders, to be written into the message where
// they go, are said in the conversation.
static PurpleCmdRet pasteCommand(PurpleConversation *conv, const gchar *cmd, gchar **args, gchar **error, void *data)
{
    PurpleTdClient  *tdClient = getTdClient(purple_conversation_get_account(conv));
    ClipboardContent content;
    std::string      problem;
    bool             inPidgin = readPidginClipboard(conv, content);
    if (inPidgin) {
        if (content.files.empty() && !content.imageId)
            // TRANSLATOR: Error after the /paste command.
            problem = _("There are no files or picture on the clipboard.");
    } else
        readXclip(content, problem);
    if (!problem.empty() || !tdClient) {
        if (content.imageId)
            purple_imgstore_unref_by_id(content.imageId);
        if (!problem.empty())
            *error = g_strdup(problem.c_str());
        return PURPLE_CMD_RET_FAILED;
    }

    if (inPidgin) {
        int imageId = content.imageId;
        content.imageId = 0;
        // Pidgin empties the field once the command is done
        pasteIntoEntryLater(conv, joinPlaceholders(tdClient->addPasted(content)), imageId);
    } else {
        std::vector<std::string> placeholders = tdClient->addPasted(content);
        if (placeholders.empty()) {
            *error = g_strdup(_("There are no files or picture on the clipboard."));
            return PURPLE_CMD_RET_FAILED;
        }
        // TRANSLATOR: In-chat notification, not logged, after /paste. The argument is the pasted things' placeholders, such as [Image#1] or [report.pdf].
        std::string notice = formatMessage(_("Pasted {}: write it into the message, where it goes, to send it there."),
                                           joinPlaceholders(placeholders));
        purple_conversation_write(conv, NULL, notice.c_str(),
                                  (PurpleMessageFlags)(PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG), time(NULL));
    }
    return PURPLE_CMD_RET_OK;
}

static char png[] = "png";

static PurplePluginProtocolInfo prpl_info = {
    .options          = (PurpleProtocolOptions)(OPT_PROTO_NO_PASSWORD | OPT_PROTO_IM_IMAGE | OPT_PROTO_CHAT_TOPIC),
    .user_splits      = NULL, // was initialized in tgprpl_init()
    .protocol_options = NULL, // was initialized in tgprpl_init()

    .icon_spec = {
        .format       = png,
        .min_width    = 1,
        .min_height   = 1,
        .max_width    = 512,
        .max_height   = 512,
        .max_filesize = 64000,
        .scale_rules  = PURPLE_ICON_SCALE_SEND,
    },

    .list_icon                = tgprpl_list_icon,
    .list_emblem              = NULL,
    .status_text              = NULL,
    .tooltip_text             = tgprpl_tooltip_text,
    .status_types             = tgprpl_status_types,
    .blist_node_menu          = tgprpl_blist_node_menu,
    .chat_info                = tgprpl_chat_join_info,
    .chat_info_defaults       = tgprpl_chat_info_defaults,
    .login                    = tgprpl_login,
    .close                    = tgprpl_close,
    .send_im                  = tgprpl_send_im,
    .set_info                 = NULL,
    .send_typing              = tgprpl_send_typing,
    .get_info                 = tgprpl_info_show,
    .set_status               = tgprpl_set_status,
    .set_idle                 = NULL,
    .change_passwd            = NULL,
    .add_buddy                = tgprpl_add_buddy,
    .add_buddies              = NULL,
    .remove_buddy             = tgprpl_request_delete_contact,
    .remove_buddies           = NULL,
    .add_permit               = NULL,
    .add_deny                 = tgprpl_add_deny,
    .rem_permit               = NULL,
    .rem_deny                 = tgprpl_rem_deny,
    .set_permit_deny          = NULL,
    .join_chat                = tgprpl_chat_join,
    .reject_chat              = NULL,
    .get_chat_name            = tgprpl_get_chat_name,
    .chat_invite              = tgprpl_chat_invite,
    .chat_leave               = NULL,
    .chat_whisper             = NULL,
    .chat_send                = tgprpl_send_chat,
    .keepalive                = NULL,
    .register_user            = NULL,
    .get_cb_info              = NULL,
    .get_cb_away              = NULL,
    .alias_buddy              = tgprpl_rename_buddy,
    .group_buddy              = NULL,
    .rename_group             = renameGroup,
    .buddy_free               = NULL,
    .convo_closed             = NULL,
    .normalize                = NULL,
    .set_buddy_icon           = NULL,
    .remove_group             = NULL,
    .get_cb_real_name         = tgprpl_get_cb_real_name,
    .set_chat_topic           = tgprpl_set_chat_topic,
    .find_blist_chat          = NULL,
    .roomlist_get_list        = tgprpl_roomlist_get_list,
    .roomlist_cancel          = tgprpl_roomlist_cancel,
    .roomlist_expand_category = NULL,
    .can_receive_file         = tgprpl_can_receive_file,
    .send_file                = tgprpl_send_file,
    .new_xfer                 = newUploadTransfer,
    .offline_message          = NULL,
    .whiteboard_prpl_ops      = NULL,
    .send_raw                 = NULL,
    .roomlist_room_serialize  = getRoomlistChatName,
    .unregister_user          = NULL,
    .send_attention           = NULL,
    .get_attention_types      = NULL,
    .struct_size              = sizeof(PurplePluginProtocolInfo),
    .get_account_text_table   = tgprpl_get_account_text_table,
    .initiate_media           = initiateMedia,
    .get_media_caps           = getMediaCaps,
    .get_moods                = NULL,
    .set_public_alias         = NULL,
    .get_public_alias         = NULL,
    .add_buddy_with_invite    = NULL,
    .add_buddies_with_invite  = NULL,
#if PURPLE_VERSION_CHECK(2,14,0)
    .get_cb_alias             = tgprpl_get_cb_alias,
    .chat_can_receive_file    = NULL,
    .chat_send_file           = sendFileToChat,
#endif
};

// TDLib's C++ API is classes defined in its headers, whose layout changes from one release to the
// next, while its shared libraries carry no version in their sonames. Built against one TDLib and
// run with another, the plugin crashes on the first update holding a class that changed -- so it
// refuses to load instead. The version option is the one thing safe to ask about across such a
// mismatch: getOption and optionValueString hold just a string.
static bool checkTdlibVersion()
{
    if (TDLIB_VERSION_NUMBER == 0)
        return true; // unknown at build time, nothing to compare with

    td::Client::Response resp = td::Client::execute({0, td::td_api::make_object<td::td_api::getOption>("version")});
    if (!resp.object || (resp.object->get_id() != td::td_api::optionValueString::ID)) {
        purple_debug_warning(config::pluginId, "Cannot tell which version of tdlib is running\n");
        return true;
    }

    const std::string &running = static_cast<const td::td_api::optionValueString &>(*resp.object).value_;
    if (running != TDLIB_VERSION_STRING) {
        g_critical("%s: built against tdlib %s, but running with tdlib %s; rebuild it against the latter",
                   config::pluginId, TDLIB_VERSION_STRING, running.c_str());
        return false;
    }

    return true;
}

void tgprpl_open_message_link(const char *link)
{
    std::string accountName;
    ChatId      chatId;
    MessageId   messageId;
    std::string variant;
    if (!parseMessageLink(link, accountName, chatId, messageId, variant)) {
        purple_debug_warning(config::pluginId, "Not a link to a message: %s\n", link);
        return;
    }

    PurpleTdClient *client = PurpleTdClient::findConnected(accountName.c_str());
    if (!client) {
        // TRANSLATOR: Error dialog, secondary content. Argument is the account's name, its phone number.
        std::string reason = formatMessage(_("Account {} is not connected."), accountName);
        // TRANSLATOR: Error dialog, primary content, after a file's link in a conversation was clicked.
        purple_notify_error(NULL, config::pluginName, _("Cannot open the file"), reason.c_str());
        return;
    }
    client->openMessageFile(chatId, messageId, variant);
}

// Pidgin's conversation window, GtkIMHtml, lets a program handle links of a scheme of its own.
// Looked up by name, as the plugin does not depend on GTK; front-ends without it get links to
// files instead. Their types come from gtkimhtml.h (pidgin-ui.h).
using LinkActivate     = gboolean (*)(PidginIMHtml *imhtml, PidginIMHtmlLink *link);
using LinkContextMenu  = gboolean (*)(PidginIMHtml *imhtml, PidginIMHtmlLink *link, PidginWidget *menu);
using RegisterProtocol = gboolean (*)(const char *name, LinkActivate activate, LinkContextMenu contextMenu);
using LinkGetUrl       = const char *(*)(PidginIMHtmlLink *link);
#ifndef NoPidgin
static_assert(std::is_same<RegisterProtocol, decltype(&gtk_imhtml_class_register_protocol)>::value,
              "gtk_imhtml_class_register_protocol() differs from what is called");
static_assert(std::is_same<LinkGetUrl, decltype(&gtk_imhtml_link_get_url)>::value,
              "gtk_imhtml_link_get_url() differs from what is called");
#endif
static RegisterProtocol g_registerProtocol;
static LinkGetUrl       g_linkGetUrl;

static gboolean messageLinkClicked(PidginIMHtml *imhtml, PidginIMHtmlLink *link)
{
    const char *url = g_linkGetUrl(link);
    if (url)
        tgprpl_open_message_link(url);
    return TRUE;
}

// Scrolls the very tab the link is in, as the message it names is shown there, if anywhere
static gboolean jumpLinkClicked(PidginIMHtml *imhtml, PidginIMHtmlLink *link)
{
    const char *url = g_linkGetUrl(link);
    std::string accountName;
    ChatId      chatId;
    MessageId   messageId;
    if (!url || !parseJumpLink(url, accountName, chatId, messageId)) {
        purple_debug_warning(config::pluginId, "Not a link to a message: %s\n", url ? url : "");
        return TRUE;
    }
    if (!scrollToMessage(imhtml, messageId))
        // TRANSLATOR: Information dialog, after a link to a message in a conversation was clicked. Primary content.
        purple_notify_info(NULL, config::pluginName, _("The message is not shown"),
                           // TRANSLATOR: Information dialog, secondary content.
                           _("It is no longer in this conversation's window."));
    return TRUE;
}

// Called whenever the link is right-clicked; it adds nothing to the menu
static gboolean messageLinkContextMenu(PidginIMHtml *imhtml, PidginIMHtmlLink *link, PidginWidget *linkMenu)
{
    return TRUE;
}

// A contact whose chat has reactions to our messages unread is marked in the buddy list
static bool buddyMarked(PurpleBuddy *buddy)
{
    PurpleAccount  *account  = purple_buddy_get_account(buddy);
    PurpleTdClient *tdClient = strcmp(purple_account_get_protocol_id(account), config::pluginId) ? nullptr
                                                                                              : getTdClient(account);
    return tdClient && tdClient->hasUnreadReactions(buddy);
}

// Scrolled to the top of its tab
static void topReached(PurpleConversation *conv)
{
    PurpleTdClient *tdClient = getTdClient(purple_conversation_get_account(conv));
    if (tdClient)
        tdClient->showEarlierMessages(conv);
}

// The menu of a tab's text, opened on a message
static bool messagePopupOpened(PurpleConversation *conv, MessageId messageId, const OpenMenuRef &menu)
{
    PurpleTdClient *tdClient = getTdClient(purple_conversation_get_account(conv));
    return tdClient && tdClient->showMessagePopup(conv, messageId, menu);
}

static void registerMessageLinks()
{
    g_registerProtocol = reinterpret_cast<RegisterProtocol>(findFrontEndSymbol("gtk_imhtml_class_register_protocol"));
    g_linkGetUrl       = reinterpret_cast<LinkGetUrl>(findFrontEndSymbol("gtk_imhtml_link_get_url"));
    setMessageLinksEnabled(g_registerProtocol && g_linkGetUrl &&
                           g_registerProtocol(MESSAGE_LINK_SCHEME, messageLinkClicked, messageLinkContextMenu));
    purple_debug_info(config::pluginId, "Links to messages' files: %s\n",
                      messageLinksEnabled() ? MESSAGE_LINK_SCHEME : "file://");
    // Only where Pidgin's tab can be scrolled to the message
    setJumpLinksEnabled(messageLinksEnabled() && pidginUiFound() &&
                        g_registerProtocol(JUMP_LINK_SCHEME, jumpLinkClicked, messageLinkContextMenu));
    // Not where messages go on to other clients, markup and all
    setLogMarkersEnabled(!canDisableReadReceipts());
    setMessagePopupHandler(messagePopupOpened);
    setFilesPastedHandler(filesPasted);
    setBuddyMarkHandler(buddyMarked, UNREAD_REACTIONS_MARK);
    setTopReachedHandler(topReached);
}

static void unregisterMessageLinks()
{
    // Registering a scheme without a handler removes it
    if (jumpLinksEnabled())
        g_registerProtocol(JUMP_LINK_SCHEME, NULL, NULL);
    if (messageLinksEnabled())
        g_registerProtocol(MESSAGE_LINK_SCHEME, NULL, NULL);
    setJumpLinksEnabled(false);
    setMessageLinksEnabled(false);
    setLogMarkersEnabled(false);
    setMessagePopupHandler(nullptr);
    setBuddyMarkHandler(nullptr, nullptr);
    setTopReachedHandler(nullptr);
}

static gboolean tgprpl_unload (PurplePlugin *plugin)
{
    unregisterMessageLinks();
    pidginUiUnload();
    return TRUE;
}

static gboolean tgprpl_load (PurplePlugin *plugin)
{
    if (!checkTdlibVersion())
        return FALSE;
    pidginUiLoad(plugin);
    registerMessageLinks();

    purple_cmd_register("kick", "s", PURPLE_CMD_P_PLUGIN,
                        (PurpleCmdFlag)(PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY),
                        config::pluginId, tgprpl_cmd_kick,
                        // TRANSLATOR: Command description, the initial "kick <user>" must remain verbatim!
                        _("kick <user>: Kick a user from the room using name or internal id"), NULL);

    purple_cmd_register("hangup", "", PURPLE_CMD_P_PLUGIN,
                        (PurpleCmdFlag)(PURPLE_CMD_FLAG_IM | PURPLE_CMD_FLAG_PRPL_ONLY),
                        config::pluginId, hangupCommand,
                        // TRANSLATOR: Command description, the initial "hangup" must remain verbatim!
                        _("hangup: Terminate any active call (with any user)"), NULL);

    purple_cmd_register("cancel", "", PURPLE_CMD_P_PLUGIN,
                        (PurpleCmdFlag)(PURPLE_CMD_FLAG_IM | PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY),
                        config::pluginId, cancelEditCommand,
                        // TRANSLATOR: Command description, the initial "cancel" must remain verbatim!
                        _("cancel: Leave the message being edited as it was"), NULL);

    purple_cmd_register("paste", "", PURPLE_CMD_P_PLUGIN,
                        (PurpleCmdFlag)(PURPLE_CMD_FLAG_IM | PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY),
                        config::pluginId, pasteCommand,
                        // TRANSLATOR: Command description, the initial "paste" must remain verbatim!
                        _("paste: Attach the files copied to the clipboard, or its picture, to the message being written"), NULL);

    return TRUE;
}

static void addChoice(GList *&choices, const char *description, const char *value)
{
    PurpleKeyValuePair *kvp = g_new0(PurpleKeyValuePair, 1);
    kvp->key = g_strdup(description);
    kvp->value = g_strdup(value);
    choices = g_list_append(choices, kvp);
}

static PurplePluginInfo *getPluginInfo();

static gboolean tdlibFatalErrorHandler(void *data)
{
    char *message = static_cast<char *>(data);
    const char *dbMessage =
        // TRANSLATOR: Tdlib crash dialog, secondary content. Argument is a filesystem path. Please keep the space after it!
        _("The error may be caused by corrupt account data. "
          "If this is the case, it could be fixed by removing account data under {} . "
          "You will be required to log in into the account again.");

    // tdlib messages are untranslated, so can as well leave "tdlib error" untranslated as well
    std::string details = formatMessage("tdlib error: {}", std::string(message));
    details += '\n';
    details += formatMessage(dbMessage, PurpleTdClient::getBaseDatabasePath());

    // TRANSLATOR: Tdlib crash dialog, title
    purple_notify_error(getPluginInfo(), _("Fatal error encountered in telegram plugin"),
                        details.c_str(), NULL);

    free(message);
    return FALSE; // this idle handler will not be called again
}

static void tdlibFatalErrorCallback(const char *message)
{
    g_idle_add(tdlibFatalErrorHandler, strdup(message));
    // The error must have come either from the poll thread or from one of the threads created by tdlib.
    // So, hang the thread to avoid crash. All other accounts will be unaffected until an attempt to
    // disconnect this account is made, because then TdTransceiver destructor will wait forever for
    // poll thread to terminate, and everything will hang.
    // However, it's still possible to disable auto-login on the problematic account using
    // purple_account_set_enabled (Account -> Disable in pidgin, etc.), because it will first disable
    // auto-login, and only then disconnect the account and hang.
    while (1) sleep(1000);
}

struct TdlibLogMessage {
    int         level;
    std::string text;
};

// TDLib's threads can log faster than the main loop goes idle, so the messages wait in a queue of
// bounded length, which one idle source at a time empties
enum { TDLIB_LOG_QUEUE_LENGTH = 1000 };
static std::mutex                  g_tdlibLogMutex;
static std::deque<TdlibLogMessage> g_tdlibLogQueue;
static unsigned                    g_tdlibLogDropped   = 0;
static bool                        g_tdlibLogScheduled = false;

static void showTdlibLogMessage(const TdlibLogMessage &message)
{
    // The text ends with a newline already
    if (message.level <= 1)
        purple_debug_error("tdlib", "%s", message.text.c_str());
    else if (message.level == 2)
        purple_debug_warning("tdlib", "%s", message.text.c_str());
    else if (message.level == 3)
        purple_debug_info("tdlib", "%s", message.text.c_str());
    else
        purple_debug_misc("tdlib", "%s", message.text.c_str());
}

static gboolean tdlibLogHandler(void *)
{
    std::deque<TdlibLogMessage> messages;
    unsigned                    dropped;
    {
        std::lock_guard<std::mutex> lock(g_tdlibLogMutex);
        messages.swap(g_tdlibLogQueue);
        dropped             = g_tdlibLogDropped;
        g_tdlibLogDropped   = 0;
        g_tdlibLogScheduled = false;
    }
    for (const TdlibLogMessage &message: messages)
        showTdlibLogMessage(message);
    if (dropped)
        purple_debug_warning(config::pluginId, "%u TDLib log messages dropped\n", dropped);
    return FALSE; // this idle handler will not be called again
}

static void tdlibLogCallback(int level, const char *message)
{
    if (level == 0) {
        tdlibFatalErrorCallback(message);
        return;
    }

    // Called on TDLib's threads, while libpurple's debug output, Pidgin's debug window included,
    // belongs to the main one
    std::lock_guard<std::mutex> lock(g_tdlibLogMutex);
    if (g_tdlibLogQueue.size() >= TDLIB_LOG_QUEUE_LENGTH)
        g_tdlibLogDropped++;
    else
        g_tdlibLogQueue.push_back(TdlibLogMessage{level, message});
    if (!g_tdlibLogScheduled) {
        g_tdlibLogScheduled = true;
        g_idle_add(tdlibLogHandler, NULL);
    }
}

static void tgprpl_init (PurplePlugin *plugin)
{
#ifndef NoTranslations
#ifndef _WIN32
    // libintl's default directory is ours only when we happen to share its prefix. On Windows
    // the installer puts the catalogues among Pidgin's own instead, wherever that lives.
    bindtextdomain(GETTEXT_PACKAGE, LOCALE_DIR);
#endif
    // Pidgin wants UTF-8, and gettext would otherwise convert to the encoding of the locale
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
#endif

#if !PURPLE_VERSION_CHECK(2,14,0)
    (void)sendFileToChat;
#endif
    if (purple_debug_is_verbose())
        // Log everything
        PurpleTdClient::setLogLevel(1024);
    else if (purple_debug_is_enabled())
        // Log up to info
        PurpleTdClient::setLogLevel(3);
    else
        // Log up to fatal errors and errors
        PurpleTdClient::setLogLevel(1);
    PurpleTdClient::setTdlibLogCallback(tdlibLogCallback);

#ifndef NoLottie
    rlottie::configureModelCacheSize(0);
#endif

    GList *choices = NULL;
    if (!strcmp(AccountOptions::DownloadBehaviourDefault(), AccountOptions::DownloadBehaviourHyperlink)) {
        // TRANSLATOR: Account settings, value for file downloads (hyperlink link file:///tmp/asdf)
        addChoice(choices, _("Inline (hyperlinks in chat)"), AccountOptions::DownloadBehaviourHyperlink);
        // TRANSLATOR: Account settings, value for file downloads (file transfer dialog)
        addChoice(choices, _("Standard file transfers"), AccountOptions::DownloadBehaviourStandard);
    } else {
        addChoice(choices, _("Standard file transfers"), AccountOptions::DownloadBehaviourStandard);
        addChoice(choices, _("Inline (hyperlinks in chat)"), AccountOptions::DownloadBehaviourHyperlink);
    }
    
    // TRANSLATOR: Account settings, key (choice)
    PurpleAccountOption *opt = purple_account_option_list_new (_("File downloads"),
                                                               AccountOptions::DownloadBehaviour, choices);
    prpl_info.protocol_options = g_list_append (prpl_info.protocol_options, opt);

    // TRANSLATOR: Account settings, key (choice)
    opt = purple_account_option_string_new (_("Inline auto-download size limit, MB (0 for unlimited)"),
                                            AccountOptions::AutoDownloadLimit,
                                            AccountOptions::AutoDownloadLimitDefault);
    prpl_info.protocol_options = g_list_append (prpl_info.protocol_options, opt);

    // Telegram may have a video in several sizes, of which one can be downloaded beforehand, to be
    // played as soon as clicked; the default first
    choices = NULL;
    // TRANSLATOR: Account settings, value for "Prefetch videos"; "the auto-download limit" is the option "Inline auto-download size limit".
    addChoice(choices, _("Highest resolution within the auto-download limit (if one is set)"),
              AccountOptions::PrefetchVideosLargestWithinLimit);
    // TRANSLATOR: Account settings, value for "Prefetch videos": the highest resolution, or nothing if that is above the auto-download limit.
    addChoice(choices, _("Highest resolution only"), AccountOptions::PrefetchVideosLargest);
    // TRANSLATOR: Account settings, value for "Prefetch videos"
    addChoice(choices, _("Smallest file"), AccountOptions::PrefetchVideosSmallest);
    // TRANSLATOR: Account settings, value for "Prefetch videos"
    addChoice(choices, _("Don't prefetch"), AccountOptions::PrefetchVideosNone);
    // TRANSLATOR: Account settings, key (choice). Videos are downloaded beforehand, so that they play as soon as clicked.
    opt = purple_account_option_list_new(_("Prefetch videos"), AccountOptions::PrefetchVideos, choices);
    prpl_info.protocol_options = g_list_append (prpl_info.protocol_options, opt);

    static_assert(AccountOptions::BigDownloadHandlingDefault == AccountOptions::BigDownloadHandlingAsk,
                  "default choice must be first");
    choices = NULL;
    // TRANSLATOR: Account settings, value for large file downloads
    addChoice(choices, _("Ask"), AccountOptions::BigDownloadHandlingAsk);
    // TRANSLATOR: Account settings, value for large file downloads
    addChoice(choices, _("Discard"), AccountOptions::BigDownloadHandlingDiscard);

    // TRANSLATOR: Account settings, check box label
    opt = purple_account_option_bool_new(_("Do not clean up inline downloads"),
                                         AccountOptions::KeepInlineDownloads,
                                         AccountOptions::KeepInlineDownloadsDefault);
    prpl_info.protocol_options = g_list_append(prpl_info.protocol_options, opt);

    // TRANSLATOR: Account settings, key (choice)
    opt = purple_account_option_list_new (_("Bigger inline file downloads"), AccountOptions::BigDownloadHandling, choices);
    prpl_info.protocol_options = g_list_append (prpl_info.protocol_options, opt);

    // TRANSLATOR: Account settings, key (boolean)
    opt = purple_account_option_bool_new (_("Enable secret chats (takes effect at reconnect)"),
                                          AccountOptions::EnableSecretChats,
                                          AccountOptions::EnableSecretChatsDefault);
    prpl_info.protocol_options = g_list_append(prpl_info.protocol_options, opt);

#ifndef NoLottie
    // TRANSLATOR: Account settings, key (boolean)
    opt = purple_account_option_bool_new(_("Show animated stickers"), AccountOptions::AnimatedStickers,
                                         AccountOptions::AnimatedStickersDefault);
    prpl_info.protocol_options = g_list_append(prpl_info.protocol_options, opt);
#endif

    // TRANSLATOR: Account settings, key (boolean)
    opt = purple_account_option_bool_new(_("Show self-destructing messages anyway"), AccountOptions::ShowSelfDestruct,
                                         AccountOptions::ShowSelfDestructDefault);
    prpl_info.protocol_options = g_list_append(prpl_info.protocol_options, opt);

    // TRANSLATOR: Account settings, key (boolean)
    opt = purple_account_option_bool_new(_("Strike out deleted messages in Pidgin, rather than remove them"),
                                         AccountOptions::StrikeOutDeleted, AccountOptions::StrikeOutDeletedDefault);
    prpl_info.protocol_options = g_list_append(prpl_info.protocol_options, opt);

    // TRANSLATOR: Account settings, key (number): days counted back from the conversation's latest message. 0 leaves the conversation window as Pidgin opens it.
    opt = purple_account_option_int_new(_("Days of messages to show on opening a conversation, in Pidgin"),
                                        AccountOptions::HistoryDays, AccountOptions::HistoryDaysDefault);
    prpl_info.protocol_options = g_list_append(prpl_info.protocol_options, opt);

    // TRANSLATOR: Account settings, key (boolean)
    opt = purple_account_option_bool_new(_("Show messages from channels and groups you have not joined"),
                                         AccountOptions::ShowUnjoinedChats,
                                         AccountOptions::ShowUnjoinedChatsDefault);
    prpl_info.protocol_options = g_list_append(prpl_info.protocol_options, opt);

    // TRANSLATOR: Account settings, key (boolean). Custom emoji are pictures of their own, each standing for an ordinary emoji.
    opt = purple_account_option_bool_new(_("Show custom emoji in reactions as ordinary emoji"),
                                         AccountOptions::FoldCustomEmoji,
                                         AccountOptions::FoldCustomEmojiDefault);
    prpl_info.protocol_options = g_list_append(prpl_info.protocol_options, opt);

    if (canDisableReadReceipts()) {
        opt = purple_account_option_bool_new ("Send read receipts",
                                              AccountOptions::ReadReceipts,
                                              AccountOptions::ReadReceiptsDefault);
        prpl_info.protocol_options = g_list_append(prpl_info.protocol_options, opt);
    }
}

static void setTwoFactorAuth(RequestData *data, PurpleRequestFields* fields);

static void requestTwoFactorAuth(PurpleConnection *gc, const char *primaryText, const char *email)
{
    PurpleRequestFields     *fields  = purple_request_fields_new();
    PurpleRequestFieldGroup *group   = purple_request_field_group_new(NULL);

    // TRANSLATOR: 2FA settings, key
    PurpleRequestField *field = purple_request_field_string_new ("oldpw", _("Current password"), NULL, FALSE);
    purple_request_field_string_set_masked(field, TRUE);
    purple_request_field_group_add_field (group, field);

    // TRANSLATOR: 2FA settings, key
    field = purple_request_field_string_new ("pw1", _("New password"), NULL, FALSE);
    purple_request_field_string_set_masked(field, TRUE);
    purple_request_field_group_add_field (group, field);

    // TRANSLATOR: 2FA settings, key
    field = purple_request_field_string_new ("pw2", _("Repeat password"), NULL, FALSE);
    purple_request_field_string_set_masked(field, TRUE);
    purple_request_field_group_add_field (group, field);

    // TRANSLATOR: 2FA settings, key
    field = purple_request_field_string_new ("hint", _("Password hint"), NULL, FALSE);
    purple_request_field_group_add_field (group, field);

    // TRANSLATOR: 2FA settings, key
    field = purple_request_field_string_new ("email", _("Recovery e-mail"), email, FALSE);
    purple_request_field_group_add_field (group, field);

    purple_request_fields_add_group (fields, group);

    RequestData *data = new RequestData(purple_connection_get_account(gc));
    data->account = purple_connection_get_account(gc);
    // TRANSLATOR: 2FA settings, title
    purple_request_fields (gc, _("Two-factor authentication"), primaryText, NULL, fields,
                           // TRANSLATOR: 2FA settings, alternative is "_Cancel". The underscore marks accelerator keys, they must be different!
                           _("_OK"), G_CALLBACK(setTwoFactorAuth),
                           // TRANSLATOR: 2FA settings, alternative is "_OK". The underscore marks accelerator keys, they must be different!
                           _("_Cancel"), G_CALLBACK(cancelRequest),
                           purple_connection_get_account(gc), NULL, NULL, data);
}

static int reRequestTwoFactorAuth(gpointer user_data)
{
    std::unique_ptr<RequestData> request(static_cast<RequestData *>(user_data));
    requestTwoFactorAuth(purple_account_get_connection(request->account),
                        // TRANSLATOR: 2FA settings, primary content (after mistype)
                        _("Please enter same password twice"), request->stringData.c_str());
    return FALSE; // this idle handler will not be called again
}

static void setTwoFactorAuth(RequestData *data, PurpleRequestFields* fields)
{
    std::unique_ptr<RequestData> request(data);
    PurpleTdClient *tdClient = getTdClient(request->account);

    if (tdClient) {
        const char *oldPass   = purple_request_fields_get_string(fields, "oldpw");
        const char *password1 = purple_request_fields_get_string(fields, "pw1");
        const char *password2 = purple_request_fields_get_string(fields, "pw2");
        const char *hint      = purple_request_fields_get_string(fields, "hint");
        const char *email     = purple_request_fields_get_string(fields, "email");

        if ((password1 != password2) && (!password1 || !password2 || strcmp(password1, password2))) {
            // Calling purple_request_fields synchronously causes glitch in pidgin
            RequestData *newRequest = new RequestData(request->account);
            if (email)
                newRequest->stringData = email;
            g_idle_add(reRequestTwoFactorAuth, newRequest);
        } else if (tdClient)
            tdClient->setTwoFactorAuth(oldPass, password1, hint, email);
    }
}

static void configureTwoFactorAuth(PurplePluginAction *action)
{
    PurpleConnection *gc = static_cast<PurpleConnection *>(action->context);
    // TRANSLATOR: 2FA settings, primary content
    requestTwoFactorAuth(gc, _("Enter new password and recovery e-mail address"), NULL);
}

static GList *tgprpl_actions (PurplePlugin *plugin, gpointer context)
{
    GList *actionsList = NULL;
    PurplePluginAction *action;

    // TRANSLATOR: 2FA settings, title
    action = purple_plugin_action_new(_("Configure two-factor authentication..."),
                                      configureTwoFactorAuth);
    actionsList = g_list_append(actionsList, action);

    return actionsList;
}

static PurplePluginInfo plugin_info = {
    .magic             = PURPLE_PLUGIN_MAGIC,
    .major_version     = PURPLE_MAJOR_VERSION,
    .minor_version     = PURPLE_MINOR_VERSION,
    .type              = PURPLE_PLUGIN_PROTOCOL,
    .ui_requirement    = NULL,
    .flags             = 0,
    .dependencies      = NULL,
    .priority          = PURPLE_PRIORITY_DEFAULT,
    .id                = config::pluginId,
    .name              = config::pluginName,
    .version           = config::versionString,
    .summary           = config::pluginSummary,
    .description       = _(config::pluginDesc),
    .author            = config::pluginAuthor,
    .homepage          = config::projectUrl,
    .load              = tgprpl_load,
    .unload            = tgprpl_unload,
    .destroy           = NULL,
    .ui_info           = NULL,
    .extra_info        = &prpl_info,
    .prefs_info        = NULL,
    .actions           = tgprpl_actions,
    ._purple_reserved1 = NULL,
    ._purple_reserved2 = NULL,
    ._purple_reserved3 = NULL,
    ._purple_reserved4 = NULL,
};

extern "C" {
    PURPLE_INIT_PLUGIN (telegram_tdlib, tgprpl_init, plugin_info)
}

static PurplePluginInfo *getPluginInfo()
{
    return &plugin_info;
}
