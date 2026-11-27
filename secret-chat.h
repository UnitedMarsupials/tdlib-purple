#ifndef _SECRET_CHAT_H
#define _SECRET_CHAT_H

#include "account-data.h"
#include "format.h"

void updateSecretChat(td::td_api::object_ptr<td::td_api::secretChat> secretChat, TdAccountData &account);
void updateKnownSecretChat(SecretChatId secretChatId, TdAccountData &account);

#endif
