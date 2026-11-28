#ifndef _CALL_H
#define _CALL_H

#include "account-data.h"

bool initiateCall(int64_t userId, TdAccountData &account);
void updateCall(const td::td_api::call &call, TdAccountData &account);
void discardCurrentCall(TdAccountData &account);
void showCallMessage(const td::td_api::chat &chat, const TgMessageInfo &message,
                     const td::td_api::messageCall &callEnded, TdAccountData &account);

#endif
