#ifndef _PURPLE_INFO_H
#define _PURPLE_INFO_H

#include "identifiers.h"
#include <purple.h>
#include <td/telegram/td_api.h>

#if !GLIB_CHECK_VERSION(2,32,0)
#define G_SOURCE_REMOVE         FALSE
#define G_SOURCE_CONTINUE       TRUE
#endif

static constexpr int
    GROUP_TYPE_BASIC   = 1,
    GROUP_TYPE_SUPER   = 2,
    GROUP_TYPE_CHANNEL = 3;

class PurpleTdClient;

const char  *getChatNameComponent();
GList       *getChatJoinInfo();
std::string  getPurpleChatName(const td::td_api::chat &chat);
GHashTable  *getChatComponents(const td::td_api::chat &chat);

const char  *getChatName(GHashTable *components);
const char  *getChatJoinString(GHashTable *components);
const char  *getChatGroupName(GHashTable *components);
int          getChatGroupType(GHashTable *components);

ChatId       getTdlibChatId(const char *chatName);

namespace AccountOptions {
    constexpr const char *AutoDownloadLimit          = "media-size-threshold";
    constexpr const char *AutoDownloadLimitDefault   = "32";
    constexpr const char *BigDownloadHandling        = "media-handling-behavior";
    constexpr const char *BigDownloadHandlingAsk     = "ask";
    constexpr const char *BigDownloadHandlingDiscard = "discard";
    constexpr const char *BigDownloadHandlingDefault = BigDownloadHandlingAsk;
    constexpr const char *EnableSecretChats          = "enable-secret-chats";
    constexpr gboolean    EnableSecretChatsDefault   = TRUE;
    constexpr const char *AnimatedStickers           = "animated-stickers";
    constexpr gboolean    AnimatedStickersDefault    = TRUE;
    constexpr const char *ShowSelfDestruct           = "show-self-destruct";
    constexpr gboolean    ShowSelfDestructDefault    = TRUE;
    constexpr const char *StrikeOutDeleted           = "strike-out-deleted";
    constexpr gboolean    StrikeOutDeletedDefault    = FALSE;
    constexpr const char *HistoryDays                = "history-days";
    constexpr int         HistoryDaysDefault         = 1;
    constexpr const char *ShowUnjoinedChats          = "show-unjoined-chats";
    constexpr gboolean    ShowUnjoinedChatsDefault   = FALSE;
    constexpr const char *FoldCustomEmoji            = "fold-custom-emoji";
    constexpr const char *PrefetchVideos             = "prefetch-videos";
    constexpr const char *PrefetchVideosNone         = "none";
    constexpr const char *PrefetchVideosSmallest     = "smallest";
    constexpr const char *PrefetchVideosLargestWithinLimit = "largest-within-limit";
    constexpr const char *PrefetchVideosLargest      = "largest";
    constexpr const char *PrefetchVideosDefault      = PrefetchVideosLargestWithinLimit;
    constexpr gboolean    FoldCustomEmojiDefault     = FALSE;
    constexpr const char *DownloadBehaviour          = "download-behaviour";
    constexpr const char *DownloadBehaviourHyperlink = "hyperlink";
    constexpr const char *DownloadBehaviourStandard  = "file-transfer";
    const char           *DownloadBehaviourDefault();
    constexpr const char *KeepInlineDownloads        = "keep-inline-downloads";
    constexpr gboolean    KeepInlineDownloadsDefault = FALSE;
    constexpr const char *ReadReceipts               = "read-receipts";
    constexpr gboolean    ReadReceiptsDefault        = TRUE;
};

namespace BuddyOptions {
    constexpr const char *ProfilePhotoId = "tdlib-profile-photo-id";
    // Of a deleted account: the group it was in, and its name
    constexpr const char *GroupBeforeDeletion = "tdlib-group-before-deletion";
    constexpr const char *AliasBeforeDeletion = "tdlib-alias-before-deletion";
};

namespace GroupOptions {
    // Marks the group of deleted accounts, holding the name we gave it
    constexpr const char *DeletedAccounts = "tdlib-deleted-accounts";
};

unsigned getAutoDownloadLimitKb(PurpleAccount *account);
bool     isSizeWithinLimit(unsigned size, unsigned limit);
bool     ignoreBigDownloads(PurpleAccount *account);
PurpleTdClient *getTdClient(PurpleAccount *account);
const char *getUiName();
bool        canDisableReadReceipts();
bool        isReadReceiptsEnabled(PurpleAccount *account);

#endif
