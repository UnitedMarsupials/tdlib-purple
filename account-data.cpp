#include "account-data.h"
#include "client-utils.h"
#include "config.h"
#include "format.h"
#include <purple.h>
#include <algorithm>
#include <limits>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool isCanonicalPhoneNumber(const char *s)
{
    if (*s == '\0')
        return false;

    for (const char *c = s; *c; c++)
        if (!isdigit(*c))
            return false;

    return true;
}

bool isPhoneNumber(const char *s)
{
    if (*s == '+') s++;
    return isCanonicalPhoneNumber(s);
}

const char *getCanonicalPhoneNumber(const char *s)
{
    if (*s == '+')
        return s+1;
    return s;
}

// The prefix, then digits and nothing else, as the plugin writes them
static bool isNumberedName(const char *s, const char *prefix)
{
    size_t length = strlen(prefix);
    return s && !strncmp(s, prefix, length) && s[length] && (strspn(s + length, "0123456789") == strlen(s + length));
}

bool isUserIdName(const char *s)
{
    return isNumberedName(s, "id");
}

UserId purpleBuddyNameToUserId(const char *s)
{
    return isUserIdName(s) ? UserId::fromString(s + 2) : UserId::invalid;
}

SecretChatId purpleBuddyNameToSecretChatId(const char *s)
{
    return isNumberedName(s, "secret") ? SecretChatId::fromString(s + 6) : SecretChatId::invalid;
}

static bool isPhoneEqual(const std::string &n1, const std::string &n2)
{
    return !strcmp(getCanonicalPhoneNumber(n1.c_str()), getCanonicalPhoneNumber(n2.c_str()));
}

bool isPrivateChat(const td::td_api::chat &chat)
{
    return getUserIdByPrivateChat(chat).valid();
}

UserId getUserIdByPrivateChat(const td::td_api::chat &chat)
{
    if (chat.type_ && (chat.type_->get_id() == td::td_api::chatTypePrivate::ID)) {
        const td::td_api::chatTypePrivate &privType = static_cast<const td::td_api::chatTypePrivate &>(*chat.type_);
        return getUserId(privType);
    }
    return UserId::invalid;
}

// A private chat that a stranger started is offered for reporting as spam, even after we answer:
// the only report Telegram takes about a person
bool canReportAsSpam(const td::td_api::chat &chat)
{
    return chat.action_bar_ && ((chat.action_bar_->get_id() == td::td_api::chatActionBarReportSpam::ID) ||
                                (chat.action_bar_->get_id() == td::td_api::chatActionBarReportAddBlock::ID));
}

// A person not a contact is kept under "Telegram: not in contacts", unsaved, whether we answered or
// reported them or not: until moved into another group, which adds them to contacts, or deleted.
// A buddy saved for them already, by an earlier version or while they were a contact, stays put.
bool isChatInContactList(const TdAccountData &account, const td::td_api::chat &chat,
                         const td::td_api::user *privateChatUser)
{
    if (privateChatUser && privateChatUser->is_contact_)
        return true;
    if (privateChatUser) {
        std::string  buddyName = getPurpleBuddyName(*privateChatUser);
        PurpleBuddy *buddy     = purple_find_buddy(account.purpleAccount, buddyName.c_str());
        if (!buddy || isTransientBuddy(buddy))
            return false;
    }
    return !chat.positions_.empty();
}

BasicGroupId getBasicGroupId(const td::td_api::chat &chat)
{
    if (chat.type_ && (chat.type_->get_id() == td::td_api::chatTypeBasicGroup::ID))
        return getBasicGroupId(static_cast<const td::td_api::chatTypeBasicGroup &>(*chat.type_));

    return BasicGroupId::invalid;
}

SupergroupId getSupergroupId(const td::td_api::chat &chat)
{
    if (chat.type_ && (chat.type_->get_id() == td::td_api::chatTypeSupergroup::ID))
        return getSupergroupId(static_cast<const td::td_api::chatTypeSupergroup &>(*chat.type_));

    return SupergroupId::invalid;
}

SecretChatId getSecretChatId(const td::td_api::chat &chat)
{
    if (chat.type_ && (chat.type_->get_id() == td::td_api::chatTypeSecret::ID))
        return getSecretChatId(static_cast<const td::td_api::chatTypeSecret&>(*chat.type_));

    return SecretChatId::invalid;
}

// Of every ChatMemberStatus TDLib defines, only the creator and a restricted member can be in a
// group's administration without belonging to it; an administrator has no flag saying otherwise.
bool isGroupMember(const td::td_api::object_ptr<td::td_api::ChatMemberStatus> &status)
{
    if (!status)
        return false;
    switch (status->get_id()) {
    case td::td_api::chatMemberStatusRestricted::ID:
        return static_cast<const td::td_api::chatMemberStatusRestricted &>(*status).is_member_;
    case td::td_api::chatMemberStatusCreator::ID:
        return static_cast<const td::td_api::chatMemberStatusCreator &>(*status).is_member_;
    case td::td_api::chatMemberStatusMember::ID:
    case td::td_api::chatMemberStatusAdministrator::ID:
        return true;
    default: // left, banned
        return false;
    }
}

// What map holds for key, or null
template<typename Map, typename Key>
static const typename Map::mapped_type *findIn(const Map &map, const Key &key)
{
    auto it = map.find(key);
    return (it != map.end()) ? &it->second : nullptr;
}

// The first chat of the account matching
template<typename Map, typename Predicate>
static const td::td_api::chat *findChat(const Map &chats, Predicate matches)
{
    auto it = std::find_if(chats.begin(), chats.end(), [&matches](const typename Map::value_type &entry) {
        return matches(*entry.second.chat);
    });
    return (it != chats.end()) ? it->second.chat.get() : nullptr;
}

bool isSameUser(const td::td_api::MessageSender &member1, const td::td_api::MessageSender &member2)
{
    if ((member1.get_id() == td::td_api::messageSenderUser::ID) &&
        (member2.get_id() == td::td_api::messageSenderUser::ID))
    {
        const td::td_api::messageSenderUser &user1 = static_cast<const td::td_api::messageSenderUser &>(member1);
        const td::td_api::messageSenderUser &user2 = static_cast<const td::td_api::messageSenderUser &>(member2);
        return (user1.user_id_ == user2.user_id_);
    }

    return false;
}

auto PendingMessageQueue::getChatQueue(ChatId chatId) -> std::vector<ChatQueue>::iterator 
{
    return std::find_if(m_queues.begin(), m_queues.end(), [chatId](const ChatQueue &queue) {
        return (queue.chatId == chatId);
    });
}

auto PendingMessageQueue::getOrAddChatQueue(ChatId chatId) -> ChatQueue &
{
    auto it = getChatQueue(chatId);
    if (it != m_queues.end())
        return *it;
    m_queues.emplace_back();
    m_queues.back().chatId = chatId;
    return m_queues.back();
}

auto PendingMessageQueue::findMessage(ChatQueue &queue, MessageId messageId) -> std::list<Message>::iterator
{
    return std::find_if(queue.messages.begin(), queue.messages.end(), [messageId](const Message &m) {
        return (getId(*m.message.message) == messageId);
    });
}

PendingMessageQueue::Message &PendingMessageQueue::addMessage(ChatQueue &queue, MessageAction action)
{
    if (action == MessageAction::Append) {
        queue.messages.emplace_back();
        return queue.messages.back();
    }
    queue.messages.emplace_front();
    return queue.messages.front();
}

IncomingMessage &PendingMessageQueue::addPendingMessage(IncomingMessage &&message,
    MessageAction action)
{
    if (!message.message) return message;

    ChatId chatId = getChatId(*message.message);
    purple_debug_misc(config::pluginId,"MessageQueue: chat %" G_GINT64_FORMAT ": "
                      "adding pending message %" G_GINT64_FORMAT " (not ready)\n",
                      chatId.value(), message.message->id_);

    Message &newEntry = addMessage(getOrAddChatQueue(chatId), action);
    newEntry.ready = false;
    newEntry.message = std::move(message);
    return newEntry.message;
}

void PendingMessageQueue::extractReadyMessages(
    std::vector<ChatQueue>::iterator pQueue,
    std::vector<IncomingMessage> &readyMessages)
{
    std::list<Message>::iterator pReady;
    for (pReady = pQueue->messages.begin(); pReady != pQueue->messages.end(); ++pReady) {
        if (!pReady->ready) break;
        purple_debug_misc(config::pluginId,"MessageQueue: chat %" G_GINT64_FORMAT ": "
                            "showing message %" G_GINT64_FORMAT "\n",
                            pQueue->chatId.value(), getId(*pReady->message.message).value());
        readyMessages.push_back(std::move(pReady->message));
    }

    pQueue->messages.erase(pQueue->messages.begin(), pReady);
    if (pQueue->messages.empty())
        m_queues.erase(pQueue);
}

void PendingMessageQueue::setMessageReady(ChatId chatId, MessageId messageId,
                                          std::vector<IncomingMessage> &readyMessages)
{
    readyMessages.clear();

    auto pQueue = getChatQueue(chatId);
    if (pQueue == m_queues.end()) return;

    purple_debug_misc(config::pluginId,"MessageQueue: chat %" G_GINT64_FORMAT ": "
                      "message %" G_GINT64_FORMAT " now ready\n",
                      chatId.value(), messageId.value());

    auto it = findMessage(*pQueue, messageId);
    if (it == pQueue->messages.end()) return;

    it->ready = true;
    if (pQueue->ready && (it == pQueue->messages.begin()))
        extractReadyMessages(pQueue, readyMessages);
}

IncomingMessage PendingMessageQueue::addReadyMessage(IncomingMessage &&message,
    MessageAction action)
{
    if (!message.message) return IncomingMessage();

    ChatId chatId  = getChatId(*message.message);
    auto   queueIt = getChatQueue(chatId);
    if (queueIt == m_queues.end())
        return std::move(message);

    purple_debug_misc(config::pluginId,"MessageQueue: chat %" G_GINT64_FORMAT ": "
                      "adding pending message %" G_GINT64_FORMAT " (ready)\n",
                      chatId.value(), message.message->id_);

    Message &newEntry = addMessage(*queueIt, action);
    newEntry.ready = true;
    newEntry.message = std::move(message);

    return IncomingMessage();
}

IncomingMessage *PendingMessageQueue::findPendingMessage(ChatId chatId, MessageId messageId)
{
    auto queueIt = getChatQueue(chatId);
    if (queueIt == m_queues.end()) return nullptr;

    auto it = findMessage(*queueIt, messageId);
    return (it != queueIt->messages.end()) ? &it->message : nullptr;
}

void PendingMessageQueue::flush(std::vector<IncomingMessage> &messages)
{
    messages.clear();
    for (ChatQueue &queue: m_queues)
        for (Message &message: queue.messages)
            messages.push_back(std::move(message.message));
    m_queues.clear();
}

void PendingMessageQueue::setChatNotReady(ChatId chatId)
{
    getOrAddChatQueue(chatId).ready = false;
}

void PendingMessageQueue::setChatReady(ChatId chatId, std::vector<IncomingMessage>& readyMessages)
{
    readyMessages.clear();
    auto pQueue = getChatQueue(chatId);
    if (pQueue == m_queues.end()) return;

    pQueue->ready = true;
    extractReadyMessages(pQueue, readyMessages);
}

bool PendingMessageQueue::hasWaitingMessages(ChatId chatId)
{
    auto pQueue = getChatQueue(chatId);
    return (pQueue != m_queues.end()) && !pQueue->messages.empty();
}

bool PendingMessageQueue::isChatReady(ChatId chatId)
{
    auto pQueue = getChatQueue(chatId);
    if (pQueue != m_queues.end())
        return pQueue->ready;
    return true;
}

void TdAccountData::updateUser(TdUserPtr userPtr)
{
    const td::td_api::user *user = userPtr.get();
    if (user) {
        UserId    userId = getId(*user);
        UserInfo &entry  = m_userInfo[userId];
        entry.user = std::move(userPtr);
        entry.displayName = makeBasicDisplayName(*user);
        // The first of "Name", "Name #1", ... no other user has: each looks through all users, which
        // is quadratic in the few that share a name
        for (unsigned n = 0; n != UINT32_MAX; n++) {
            std::string displayName = entry.displayName;
            if (n != 0) {
                displayName += " #";
                displayName += std::to_string(n);
            }

            std::vector<const td::td_api::user *> existingUsers;
            getUsersByDisplayName(displayName.c_str(), existingUsers);
            if (std::none_of(existingUsers.begin(), existingUsers.end(),
                                [user](const td::td_api::user *otherUser) {
                                    return (otherUser != user);
                                }))
            {
                entry.displayName = std::move(displayName);
                break;
            }
        }
    }
}

void TdAccountData::setUserStatus(UserId userId, td::td_api::object_ptr<td::td_api::UserStatus> status)
{
    auto it = m_userInfo.find(userId);
    if (it != m_userInfo.end())
        it->second.user->status_ = std::move(status);
}

void TdAccountData::updateSmallProfilePhoto(UserId userId, td::td_api::object_ptr<td::td_api::file> photo)
{
    auto it = m_userInfo.find(userId);
    if (it != m_userInfo.end()) {
        td::td_api::user &user = *it->second.user;
        if (user.profile_photo_)
            user.profile_photo_->small_ = std::move(photo);
    }
}

void TdAccountData::updateBasicGroup(TdGroupPtr group)
{
    if (group)
        m_groups[getId(*group)].group = std::move(group);
}

void TdAccountData::setBasicGroupInfoRequested(BasicGroupId groupId)
{
    auto it = m_groups.find(groupId);
    if (it != m_groups.end())
        it->second.fullInfoRequested = true;
}

bool TdAccountData::isBasicGroupInfoRequested(BasicGroupId groupId)
{
    auto it = m_groups.find(groupId);
    if (it != m_groups.end())
        return it->second.fullInfoRequested;
    return false;
}

void TdAccountData::updateBasicGroupInfo(BasicGroupId groupId, TdGroupInfoPtr groupInfo)
{
    if (groupInfo)
        m_groups[groupId].fullInfo = std::move(groupInfo);
}

void TdAccountData::updateSupergroup(TdSupergroupPtr group)
{
    if (group)
        m_supergroups[getId(*group)].group = std::move(group);
}

void TdAccountData::setSupergroupInfoRequested(SupergroupId groupId)
{
    auto it = m_supergroups.find(groupId);
    if (it != m_supergroups.end())
        it->second.fullInfoRequested = true;
}

bool TdAccountData::isSupergroupInfoRequested(SupergroupId groupId)
{
    auto it = m_supergroups.find(groupId);
    if (it != m_supergroups.end())
        return it->second.fullInfoRequested;
    return false;
}

void TdAccountData::updateSupergroupInfo(SupergroupId groupId, TdSupergroupInfoPtr groupInfo)
{
    if (groupInfo)
        m_supergroups[groupId].fullInfo = std::move(groupInfo);
}

void TdAccountData::updateSupergroupMembers(SupergroupId groupId, TdChatMembersPtr members)
{
    if (members)
        m_supergroups[groupId].members = std::move(members);
}

void TdAccountData::addChat(TdChatPtr chat)
{
    if (!chat)
        return;

    UserId privateUserId = getUserIdByPrivateChat(*chat);
    if (privateUserId.valid()) {
        auto pContact = std::find(m_contactUserIdsNoChat.begin(), m_contactUserIdsNoChat.end(), privateUserId);
        if (pContact != m_contactUserIdsNoChat.end()) {
            purpleDebug("Private chat (id {}) now known for user {}", {
                std::to_string(chat->id_), std::to_string(privateUserId.value())
            });
            m_contactUserIdsNoChat.erase(pContact);
        }
    }

    auto it = m_chatInfo.find(getId(*chat));
    if (it != m_chatInfo.end())
        it->second.chat = std::move(chat);
    else {
        auto entry = m_chatInfo.emplace(getId(*chat), ChatInfo());
        entry.first->second.chat     = std::move(chat);
        entry.first->second.purpleId = ++m_lastChatPurpleId;
    }
}

void TdAccountData::updateChatPosition(ChatId chatId, td::td_api::object_ptr<td::td_api::chatPosition> &&position)
{
    auto it = m_chatInfo.find(chatId);
    if (position && position->list_ && (it != m_chatInfo.end())) {
        auto listId = position->list_->get_id();
        td::td_api::chat &chat = *it->second.chat;
        if (position->order_ == 0) {
            purpleDebug("Removing chat {} from list {}", {std::to_string(chatId.value()), std::to_string(listId)});
            chat.positions_.erase(
                std::remove_if(chat.positions_.begin(), chat.positions_.end(),
                               [listId](const td::td_api::object_ptr<td::td_api::chatPosition> &chatPos) {
                                   return chatPos && (chatPos->list_->get_id() == listId);
                               }),
                chat.positions_.end());
        } else {
            auto pExisting = std::find_if(chat.positions_.begin(), chat.positions_.end(),
                                          [listId](const td::td_api::object_ptr<td::td_api::chatPosition> &chatPos) {
                                              return chatPos && (chatPos->list_->get_id() == listId);
                                          });
            if (pExisting != chat.positions_.end()) {
                purpleDebug("Changing chat {}, list {} order to {}",
                            {std::to_string(chatId.value()), std::to_string(listId), std::to_string(position->order_)});
                *pExisting = std::move(position);
            } else {
                purpleDebug("Adding chat {} to list {}", {std::to_string(chatId.value()), std::to_string(listId)});
                chat.positions_.push_back(std::move(position));
            }
        }
    }
}

void TdAccountData::updateChatTitle(ChatId chatId, const std::string &title)
{
    auto it = m_chatInfo.find(chatId);
    if (it != m_chatInfo.end())
        it->second.chat->title_ = title;
}

void TdAccountData::updateChatReadInbox(ChatId chatId, MessageId lastRead, int32_t unreadCount)
{
    auto it = m_chatInfo.find(chatId);
    if (it != m_chatInfo.end()) {
        it->second.chat->last_read_inbox_message_id_ = lastRead.value();
        it->second.chat->unread_count_               = unreadCount;
    }
}

void TdAccountData::updateChatActionBar(ChatId chatId, td::td_api::object_ptr<td::td_api::ChatActionBar> actionBar)
{
    auto it = m_chatInfo.find(chatId);
    if (it != m_chatInfo.end())
        it->second.chat->action_bar_ = std::move(actionBar);
}

void TdAccountData::updateSmallChatPhoto(ChatId chatId, td::td_api::object_ptr<td::td_api::file> photo)
{
    auto it = m_chatInfo.find(chatId);
    if (it != m_chatInfo.end()) {
        td::td_api::chat &chat = *it->second.chat;
        if (chat.photo_)
            chat.photo_->small_ = std::move(photo);
    }
}

void TdAccountData::setContacts(const td::td_api::users &users)
{
    for (unsigned i = 0; i < users.user_ids_.size(); i++) {
        UserId userId = getUserId(users, i);
        if (getPrivateChatByUserId(userId) == nullptr) {
            purpleDebug("Private chat not yet known for user {}", userId.value());
            m_contactUserIdsNoChat.push_back(userId);
        }
    }
}

void TdAccountData::getContactsWithNoChat(std::vector<UserId> &userIds)
{
    userIds = m_contactUserIdsNoChat;
}

const td::td_api::chat *TdAccountData::getChat(ChatId chatId) const
{
    const ChatInfo *info = findIn(m_chatInfo, chatId);
    return info ? info->chat.get() : nullptr;
}

int TdAccountData::getPurpleChatId(ChatId tdChatId)
{
    const ChatInfo *info = findIn(m_chatInfo, tdChatId);
    return info ? info->purpleId : 0;
}

const td::td_api::chat *TdAccountData::getChatByPurpleId(int32_t purpleChatId) const
{
    auto it = std::find_if(m_chatInfo.begin(), m_chatInfo.end(), [purpleChatId](const ChatMap::value_type &entry) {
        return (entry.second.purpleId == purpleChatId);
    });
    return (it != m_chatInfo.end()) ? it->second.chat.get() : nullptr;
}

const td::td_api::chat *TdAccountData::getPrivateChatByUserId(UserId userId) const
{
    if (!userId.valid())
        return nullptr;
    return findChat(m_chatInfo, [userId](const td::td_api::chat &chat) {
        return (getUserIdByPrivateChat(chat) == userId);
    });
}

const td::td_api::user *TdAccountData::getUser(UserId userId) const
{
    const UserInfo *info = findIn(m_userInfo, userId);
    return info ? info->user.get() : nullptr;
}

const td::td_api::user *TdAccountData::getUserByPhone(const char *phoneNumber) const
{
    auto it = std::find_if(m_userInfo.begin(), m_userInfo.end(), [phoneNumber](const UserMap::value_type &entry) {
        return isPhoneEqual(entry.second.user->phone_number_, phoneNumber);
    });
    return (it != m_userInfo.end()) ? it->second.user.get() : nullptr;
}

const td::td_api::user *TdAccountData::getUserByPrivateChat(const td::td_api::chat &chat) const
{
    UserId userId = getUserIdByPrivateChat(chat);
    if (userId.valid())
        return getUser(userId);
    return nullptr;
}

std::string TdAccountData::getDisplayName(const td::td_api::user &user) const
{
    return getDisplayName(getId(user));
}

std::string TdAccountData::getDisplayName(UserId userId) const
{
    const UserInfo *info = findIn(m_userInfo, userId);
    return info ? info->displayName : std::string();
}

void TdAccountData::getUsersByDisplayName(const char *displayName,
                                          std::vector<const td::td_api::user*> &users)
{
    users.clear();
    if (!displayName || (*displayName == '\0'))
        return;

    for (const UserMap::value_type &entry: m_userInfo)
        if (entry.second.displayName == displayName)
            users.push_back(entry.second.user.get());
}

// Match by Telegram @username. Telegram usernames are case-insensitive; accept an optional leading
// '@'. Checks the editable username and any active usernames.
void TdAccountData::getUsersByUsername(const char *username,
                                       std::vector<const td::td_api::user*> &users)
{
    users.clear();
    if (!username || (*username == '\0'))
        return;
    if (*username == '@')
        username++;
    // A username is of Latin letters, digits and underscores, 32 of them at most: anything else, as a
    // display name in another alphabet, is none
    enum { MAX_USERNAME = 32 };
    static const char USERNAME_CHARACTERS[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    size_t length = strlen(username);
    if ((length == 0) || (length > MAX_USERNAME) || (strspn(username, USERNAME_CHARACTERS) != length))
        return;
    auto equalsIgnoreCase = [username](const std::string &candidate) {
        return !g_ascii_strcasecmp(candidate.c_str(), username);
    };

    for (const UserMap::value_type &entry: m_userInfo) {
        const td::td_api::user *u = entry.second.user.get();
        if (!u || !u->usernames_)
            continue;
        bool hit = equalsIgnoreCase(u->usernames_->editable_username_);
        if (!hit)
            for (const std::string &active: u->usernames_->active_usernames_)
                if (equalsIgnoreCase(active)) { hit = true; break; }
        if (hit)
            users.push_back(u);
    }
}

const td::td_api::basicGroup *TdAccountData::getBasicGroup(BasicGroupId groupId) const
{
    auto info = findIn(m_groups, groupId);
    return info ? info->group.get() : nullptr;
}

const td::td_api::basicGroupFullInfo *TdAccountData::getBasicGroupInfo(BasicGroupId groupId) const
{
    auto info = findIn(m_groups, groupId);
    return info ? info->fullInfo.get() : nullptr;
}

const td::td_api::supergroup *TdAccountData::getSupergroup(SupergroupId groupId) const
{
    auto info = findIn(m_supergroups, groupId);
    return info ? info->group.get() : nullptr;
}

const td::td_api::supergroupFullInfo *TdAccountData::getSupergroupInfo(SupergroupId groupId) const
{
    auto info = findIn(m_supergroups, groupId);
    return info ? info->fullInfo.get() : nullptr;
}

const td::td_api::chatMembers *TdAccountData::getSupergroupMembers(SupergroupId groupId) const
{
    auto info = findIn(m_supergroups, groupId);
    return info ? info->members.get() : nullptr;
}

const td::td_api::chat *TdAccountData::getBasicGroupChatByGroup(BasicGroupId groupId) const
{
    if (!groupId.valid())
        return nullptr;
    return findChat(m_chatInfo, [groupId](const td::td_api::chat &chat) {
        return (getBasicGroupId(chat) == groupId);
    });
}

const td::td_api::chat *TdAccountData::getSupergroupChatByGroup(SupergroupId groupId) const
{
    if (!groupId.valid())
        return nullptr;
    return findChat(m_chatInfo, [groupId](const td::td_api::chat &chat) {
        return (getSupergroupId(chat) == groupId);
    });
}

bool TdAccountData::isGroupChatWithMembership(const td::td_api::chat &chat) const
{
    BasicGroupId groupId = getBasicGroupId(chat);
    if (groupId.valid()) {
        const td::td_api::basicGroup *group = getBasicGroup(groupId);
        return (group && isGroupMember(group->status_));
    }
    SupergroupId supergroupId = getSupergroupId(chat);
    if (supergroupId.valid()) {
        const td::td_api::supergroup *group = getSupergroup(supergroupId);
        return (group && isGroupMember(group->status_));
    }
    return false;
}

const td::td_api::chat *TdAccountData::getChatBySecretChat(SecretChatId secretChatId)
{
    return findChat(m_chatInfo, [secretChatId](const td::td_api::chat &chat) {
        return (getSecretChatId(chat) == secretChatId);
    });
}

void TdAccountData::getChats(std::vector<const td::td_api::chat *> &chats) const
{
    chats.clear();
    for (const ChatMap::value_type &item: m_chatInfo)
        chats.push_back(item.second.chat.get());
}

void TdAccountData::deleteChat(ChatId id)
{
    m_chatInfo.erase(id);
}

void TdAccountData::addExpectedChat(ChatId id)
{
    if (!isExpectedChat(id))
        m_expectedChats.push_back(id);
}

bool TdAccountData::isExpectedChat(ChatId chatId)
{
    return (std::find(m_expectedChats.begin(), m_expectedChats.end(), chatId) != m_expectedChats.end());
}

void TdAccountData::removeExpectedChat(ChatId id)
{
    auto expIt = std::find(m_expectedChats.begin(), m_expectedChats.end(), id);
    if (expIt != m_expectedChats.end())
        m_expectedChats.erase(expIt);

}

using RequestList = std::vector<std::unique_ptr<PendingRequest>>;

static RequestList::iterator findRequestById(RequestList &requests, uint64_t requestId)
{
    return std::find_if(requests.begin(), requests.end(), [requestId](const std::unique_ptr<PendingRequest> &req) {
        return (req->requestId == requestId);
    });
}

// The first request of type Request that matches
template<typename Request, typename Predicate>
static Request *findRequest(RequestList &requests, Predicate matches)
{
    for (const std::unique_ptr<PendingRequest> &req: requests) {
        Request *request = dynamic_cast<Request *>(req.get());
        if (request && matches(*request))
            return request;
    }
    return nullptr;
}

std::unique_ptr<PendingRequest> TdAccountData::getPendingRequestImpl(uint64_t requestId)
{
    auto it = findRequestById(m_requests, requestId);
    if (it == m_requests.end())
        return nullptr;
    auto result = std::move(*it);
    m_requests.erase(it);
    return result;
}

PendingRequest *TdAccountData::findPendingRequestImpl(uint64_t requestId)
{
    auto it = findRequestById(m_requests, requestId);
    return (it != m_requests.end()) ? it->get() : nullptr;
}

const ContactRequest *TdAccountData::findContactRequest(UserId userId)
{
    return findRequest<ContactRequest>(m_requests, [userId](const ContactRequest &request) {
        return (request.userId == userId);
    });
}

DownloadRequest::~DownloadRequest()
{
    if (tempFd >= 0)
        close(tempFd);
    if (!tempFileName.empty())
        remove(tempFileName.c_str());
}

DownloadRequest* TdAccountData::findDownloadRequest(int32_t fileId)
{
    return findRequest<DownloadRequest>(m_requests, [fileId](const DownloadRequest &request) {
        return (request.fileId == fileId);
    });
}

bool TdAccountData::isDownloadingFor(int32_t fileId, ChatId chatId, MessageId messageId)
{
    return findRequest<DownloadRequest>(m_requests, [fileId, chatId, messageId](const DownloadRequest &request) {
        return (request.fileId == fileId) && (request.chatId == chatId) && (request.message.id == messageId);
    }) != nullptr;
}

void TdAccountData::extractFileTransferRequests(std::vector<PurpleXfer *> &transfers)
{
    transfers.clear();

    for (unsigned i = 0; i < m_requests.size(); ) {
        UploadRequest *uploadReq = dynamic_cast<UploadRequest *>(m_requests[i].get());
        NewPrivateChatForMessage *newChatReq = dynamic_cast<NewPrivateChatForMessage *>(m_requests[i].get());
        PurpleXfer *xfer = nullptr;
        if (uploadReq)
            xfer = uploadReq->xfer;
        else if (newChatReq)
            xfer = newChatReq->fileUpload;

        if (xfer) {
            transfers.push_back(xfer);
            m_requests.erase(m_requests.begin() + i);
        } else
            i++;
    }
}

// Images still being sent when the account disconnects are sent by TDLib after the next login, so
// their files have to outlive this session, and so does the knowledge of when they can go.
static const char *TempFileUploadsSetting = "temp-image-uploads";

void TdAccountData::addTempFileUpload(ChatId chatId, int64_t messageId, const std::string &path)
{
    m_sentMessages.emplace_back();
    m_sentMessages.back().chatId = chatId;
    m_sentMessages.back().messageId = messageId;
    m_sentMessages.back().tempFile = path;
    saveTempFileUploads();
}

std::string TdAccountData::extractTempFileUpload(ChatId chatId, int64_t messageId)
{
    // A message id is only unique within its chat
    auto it = std::find_if(m_sentMessages.begin(), m_sentMessages.end(),
                           [chatId, messageId](const SendMessageInfo &item) {
                               return (item.chatId == chatId) && (item.messageId == messageId);
                           });

    std::string result;
    if (it != m_sentMessages.end()) {
        result = it->tempFile;
        m_sentMessages.erase(it);
        saveTempFileUploads();
    }

    return result;
}

void TdAccountData::saveTempFileUploads()
{
    // One line per file: chat id, message id and path, separated by spaces
    std::string value;
    for (const SendMessageInfo &item: m_sentMessages)
        value += std::to_string(item.chatId.value()) + ' ' + std::to_string(item.messageId) + ' ' +
                 item.tempFile + '\n';

    if (value.empty())
        purple_account_remove_setting(purpleAccount, TempFileUploadsSetting);
    else
        purple_account_set_string(purpleAccount, TempFileUploadsSetting, value.c_str());
}

void TdAccountData::loadTempFileUploads()
{
    const char *value = purple_account_get_string(purpleAccount, TempFileUploadsSetting, nullptr);
    if (!value)
        return;

    gchar **lines = g_strsplit(value, "\n", -1);
    for (gchar **line = lines; *line; line++) {
        gchar **fields = g_strsplit(*line, " ", 3);
        if (fields[0] && fields[1] && fields[2] && *fields[2]) {
            m_sentMessages.emplace_back();
            m_sentMessages.back().chatId = ChatId::fromString(fields[0]);
            m_sentMessages.back().messageId = g_ascii_strtoll(fields[1], nullptr, 10);
            m_sentMessages.back().tempFile = fields[2];
        }
        g_strfreev(fields);
    }
    g_strfreev(lines);
}

void TdAccountData::getTempFileUploads(std::vector<std::pair<ChatId, int64_t>> &messages)
{
    for (const SendMessageInfo &item: m_sentMessages)
        messages.emplace_back(item.chatId, item.messageId);
}

template<typename Transfers>
static auto findFileTransfer(Transfers &transfers, int32_t fileId) -> decltype(transfers.begin())
{
    return std::find_if(transfers.begin(), transfers.end(), [fileId](const typename Transfers::value_type &transfer) {
        return (transfer.fileId == fileId);
    });
}

void TdAccountData::addFileTransfer(int32_t fileId, PurpleXfer *xfer, ChatId chatId, AttachmentKind kind)
{
    if (findFileTransfer(m_fileTransfers, fileId) == m_fileTransfers.end()) {
        m_fileTransfers.emplace_back();
        m_fileTransfers.back().fileId = fileId;
        m_fileTransfers.back().xfer = xfer;
        m_fileTransfers.back().chatId = chatId;
        m_fileTransfers.back().kind = kind;
    }
}

bool TdAccountData::getFileTransfer(int32_t fileId, PurpleXfer *&xfer, ChatId &chatId, AttachmentKind *kind)
{
    auto it = findFileTransfer(m_fileTransfers, fileId);
    if (it != m_fileTransfers.end()) {
        xfer = it->xfer;
        chatId = it->chatId;
        if (kind)
            *kind = it->kind;
        return true;
    }

    return false;
}

bool TdAccountData::getFileIdForTransfer(PurpleXfer *xfer, int &fileId)
{
    auto it = std::find_if(m_fileTransfers.begin(), m_fileTransfers.end(),
                           [xfer](const FileTransferInfo &upload) { return (upload.xfer == xfer); });
    if (it != m_fileTransfers.end()) {
        fileId = it->fileId;
        return true;
    }
    return false;
}

void TdAccountData::removeFileTransfer(int32_t fileId)
{
    auto it = findFileTransfer(m_fileTransfers, fileId);
    if (it != m_fileTransfers.end())
        m_fileTransfers.erase(it);
}

void TdAccountData::removeAllFileTransfers(std::vector<PurpleXfer *>& transfers)
{
    transfers.resize(m_fileTransfers.size());
    for (size_t i = 0; i < m_fileTransfers.size(); i++)
        transfers[i] = m_fileTransfers[i].xfer;
    m_fileTransfers.clear();
}

void TdAccountData::addSecretChat(td::td_api::object_ptr<td::td_api::secretChat> secretChat)
{
    if (secretChat)
        m_secretChats[getId(*secretChat)] = std::move(secretChat);
}

const td::td_api::secretChat *TdAccountData::getSecretChat(SecretChatId id)
{
    auto it = m_secretChats.find(id);
    return (it != m_secretChats.end()) ? it->second.get() : nullptr;
}

void TdAccountData::deleteSecretChat(SecretChatId id)
{
    m_secretChats.erase(id);
}

std::vector<std::pair<BasicGroupId, const td::td_api::basicGroupFullInfo *>>
TdAccountData::getBasicGroupsWithMember(UserId userId)
{
    std::vector<std::pair<BasicGroupId, const td::td_api::basicGroupFullInfo *>> result;

    for (const auto &item: m_groups)
        if (item.second.fullInfo) {
            auto &members = item.second.fullInfo->members_;
            if (std::any_of(members.begin(), members.end(),
                            [userId](const td::td_api::object_ptr<td::td_api::chatMember> &member) {
                                return (member && (getUserId(*member) == userId));
                            }))
            {
                result.push_back(std::make_pair(getId(*item.second.group), item.second.fullInfo.get()));
            }
        }

    return result;
}

bool TdAccountData::hasActiveCall()
{
    return (m_callData != nullptr);
}

void TdAccountData::setActiveCall([[maybe_unused]] int32_t callId)
{
#ifndef NoVoip
    if (!m_callData) {
        m_callData = std::make_unique<tgvoip::VoIPController>();
        m_callId = callId;
    }
#endif
}

tgvoip::VoIPController *TdAccountData::getCallData()
{
    return m_callData.get();
}

void TdAccountData::removeActiveCall()
{
    m_callData.reset();
    m_callId = 0;
}

void TdAccountData::addPendingReadReceipt(ChatId chatId, MessageId messageId)
{
    m_pendingReadReceipts[chatId].push_back(ReadReceipt{chatId, messageId});
}

void TdAccountData::extractPendingReadReceipts(ChatId chatId, std::vector<ReadReceipt>& receipts)
{
    auto pChatReceipts = m_pendingReadReceipts.find(chatId);
    if (pChatReceipts != m_pendingReadReceipts.end()) {
        receipts = std::move(pChatReceipts->second);
        m_pendingReadReceipts.erase(pChatReceipts);
    } else
        receipts.clear();
}

void TdAccountData::addShownMessage(ChatId chatId, MessageId messageId, ShownMessage &&message)
{
    // Enough to cover any conversation window that is still open, hopefully
    enum { MAX_SHOWN_MESSAGES = 10000 };

    ShownMessageKey key(chatId, messageId);
    m_shownMessages[key] = std::move(message);
    m_shownMessageOrder.push_back(key);
    while (m_shownMessageOrder.size() > MAX_SHOWN_MESSAGES) {
        // A message forgotten or renamed meanwhile has left its key behind, which is harmless
        m_shownMessages.erase(m_shownMessageOrder.front());
        m_shownMessageOrder.pop_front();
    }
}

ShownMessage *TdAccountData::findShownMessage(ChatId chatId, MessageId messageId)
{
    auto it = m_shownMessages.find(ShownMessageKey(chatId, messageId));
    return (it != m_shownMessages.end()) ? &it->second : nullptr;
}

void TdAccountData::removeShownMessage(ChatId chatId, MessageId messageId)
{
    m_shownMessages.erase(ShownMessageKey(chatId, messageId));
}

// TDLib's news of what becomes of it as it is deleted, as of its messages deleted, would open its
// tab again, and be logged
void TdAccountData::setChatDeleted(ChatId chatId, bool deleted)
{
    if (deleted)
        m_deletedChats.insert(chatId);
    else
        m_deletedChats.erase(chatId);
}

bool TdAccountData::isChatDeleted(ChatId chatId) const
{
    return m_deletedChats.count(chatId) != 0;
}

MessageId TdAccountData::getSentMessageId(ChatId chatId, MessageId messageId) const
{
    auto it = m_sentMessageIds.find(ShownMessageKey(chatId, messageId));
    return (it != m_sentMessageIds.end()) ? it->second : messageId;
}

void TdAccountData::renameShownMessage(ChatId chatId, MessageId oldId, MessageId newId)
{
    m_sentMessageIds[ShownMessageKey(chatId, oldId)] = newId;
    auto it = m_shownMessages.find(ShownMessageKey(chatId, oldId));
    if (it != m_shownMessages.end()) {
        ShownMessage message = std::move(it->second);
        m_shownMessages.erase(it);
        addShownMessage(chatId, newId, std::move(message));
    }
}

void TdAccountData::addUnloggedSent(ChatId chatId, MessageId messageId, UnloggedSent &&sent)
{
    m_unloggedSent[ShownMessageKey(chatId, messageId)] = std::move(sent);
}

bool TdAccountData::extractUnloggedSent(ChatId chatId, MessageId messageId, UnloggedSent &sent)
{
    auto it = m_unloggedSent.find(ShownMessageKey(chatId, messageId));
    if (it == m_unloggedSent.end())
        return false;
    sent = std::move(it->second);
    m_unloggedSent.erase(it);
    return true;
}

bool TdAccountData::hasUnloggedSent(ChatId chatId, MessageId messageId) const
{
    return m_unloggedSent.count(ShownMessageKey(chatId, messageId)) != 0;
}

void TdAccountData::extractAwaitingPictures(std::vector<std::tuple<ChatId, MessageId, UnloggedSent>> &messages)
{
    for (auto it = m_unloggedSent.begin(); it != m_unloggedSent.end(); )
        if (it->second.awaitingPictures) {
            messages.emplace_back(it->first.first, it->first.second, std::move(it->second));
            it = m_unloggedSent.erase(it);
        } else
            ++it;
}

bool TdAccountData::startPictureFetch(ChatId chatId, MessageId messageId, int32_t fileId)
{
    return m_pictureFetches.insert(std::make_tuple(chatId, messageId, fileId)).second;
}

void TdAccountData::endPictureFetch(ChatId chatId, MessageId messageId, int32_t fileId)
{
    m_pictureFetches.erase(std::make_tuple(chatId, messageId, fileId));
}

bool TdAccountData::isFetchingPictures(ChatId chatId, MessageId messageId) const
{
    auto it = m_pictureFetches.lower_bound(std::make_tuple(chatId, messageId, std::numeric_limits<int32_t>::min()));
    return (it != m_pictureFetches.end()) && (std::get<0>(*it) == chatId) && (std::get<1>(*it) == messageId);
}

bool TdAccountData::isRefreshDue(ChatId chatId, MessageId messageId) const
{
    return m_refreshesDue.count(ShownMessageKey(chatId, messageId)) != 0;
}

// 0 once it is done
void TdAccountData::setRefreshDue(ChatId chatId, MessageId messageId, guint sourceId)
{
    if (sourceId)
        m_refreshesDue[ShownMessageKey(chatId, messageId)] = sourceId;
    else
        m_refreshesDue.erase(ShownMessageKey(chatId, messageId));
}

void TdAccountData::addStandIn(ChatId chatId, MessageId messageId, int32_t fileId)
{
    std::vector<int32_t> &standIns = m_standIns[ShownMessageKey(chatId, messageId)];
    if (std::find(standIns.begin(), standIns.end(), fileId) == standIns.end())
        standIns.push_back(fileId);
}

void TdAccountData::forgetStandIns(ChatId chatId, MessageId messageId)
{
    m_standIns.erase(ShownMessageKey(chatId, messageId));
}

std::vector<int32_t> TdAccountData::getStandIns(ChatId chatId, MessageId messageId) const
{
    auto it = m_standIns.find(ShownMessageKey(chatId, messageId));
    return (it != m_standIns.end()) ? it->second : std::vector<int32_t>();
}

void TdAccountData::setConversionFailed(int32_t fileId)
{
    m_unconvertedStickers.insert(fileId);
}

bool TdAccountData::hasConversionFailed(int32_t fileId) const
{
    return m_unconvertedStickers.count(fileId) != 0;
}

TdAccountData::~TdAccountData()
{
    for (const auto &due: m_refreshesDue)
        g_source_remove(due.second);
    for (const auto &converted: m_convertedStickers)
        purple_imgstore_unref_by_id(converted.second);
}

int TdAccountData::getConvertedSticker(int32_t fileId) const
{
    auto it = m_convertedStickers.find(fileId);
    return (it != m_convertedStickers.end()) ? it->second : 0;
}

bool TdAccountData::startStickerConversion(int32_t fileId, ChatId chatId, MessageId messageId)
{
    auto inserted = m_stickerConversions.emplace(fileId, std::vector<std::pair<ChatId, MessageId>>());
    bool started  = inserted.second;
    std::vector<std::pair<ChatId, MessageId>> &waiting = inserted.first->second;
    if (std::find(waiting.begin(), waiting.end(), std::make_pair(chatId, messageId)) == waiting.end())
        waiting.emplace_back(chatId, messageId);
    return started;
}

void TdAccountData::endStickerConversion(int32_t fileId, int imageId,
                                         std::vector<std::pair<ChatId, MessageId>> &waiting)
{
    auto it = m_stickerConversions.find(fileId);
    if (it != m_stickerConversions.end()) {
        waiting = std::move(it->second);
        m_stickerConversions.erase(it);
    }
    // The store's reference, taken as it was added, is the one kept here, for the first converted
    if (imageId && !m_convertedStickers.count(fileId))
        m_convertedStickers[fileId] = imageId;
    else if (imageId)
        purple_imgstore_unref_by_id(imageId);
}

template<typename Edits>
static auto findPendingEdit(Edits &edits, ChatId chatId, MessageId messageId) -> decltype(edits.begin())
{
    return std::find_if(edits.begin(), edits.end(), [chatId, messageId](const PendingEdit &edit) {
        return (edit.chatId == chatId) && (edit.messageId == messageId);
    });
}

void TdAccountData::addPendingEdit(const PendingEdit &edit)
{
    auto it = findPendingEdit(m_pendingEdits, edit.chatId, edit.messageId);
    if (it == m_pendingEdits.end())
        m_pendingEdits.push_back(edit);
    else {
        // One showing will do for both, and the file is new if either brought one. Should it fail,
        // what was shown before the first comes back, but only over what the last put in place
        it->captionOnly = it->captionOnly && edit.captionOnly;
        it->formattingOnly = it->formattingOnly && edit.formattingOnly;
        it->installed   = edit.installed;
    }
}

bool TdAccountData::extractPendingEdit(ChatId chatId, MessageId messageId, PendingEdit &edit)
{
    auto it = findPendingEdit(m_pendingEdits, chatId, messageId);
    if (it == m_pendingEdits.end())
        return false;
    edit = *it;
    m_pendingEdits.erase(it);
    return true;
}
