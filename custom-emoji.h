#ifndef _CUSTOM_EMOJI_H
#define _CUSTOM_EMOJI_H

#include "account-data.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

// Custom emoji are 100 pixels square
enum { CUSTOM_EMOJI_SIZE = 100 };

// A custom emoji in a message: its id, and the ordinary emoji it stands for
struct CustomEmoji {
    int64_t     id;
    std::string emoji;
};

// The custom emoji of a text, which marks them as entities over the ordinary emoji
void getCustomEmoji(const td::td_api::formattedText &text, std::vector<CustomEmoji> &emoji);

// Where the front-end has custom smileys, as Pidgin does, custom emoji are shown as such: a message
// names each by a code of its own, and the front-end puts the picture wherever the code appears in
// the conversation, once it has been fetched, which the message does not wait for. Returns the codes
// of those so taken, by id; the others are to be shown as their ordinary emoji. Not if the account
// would have custom emoji shown as ordinary ones.
std::map<int64_t, std::string> addCustomEmojiSmileys(TdAccountData &account, PurpleConversation *conv,
                                                     const std::vector<CustomEmoji> &emoji);

// The same, for one whose sticker is known already; empty if it is to be shown as its emoji
std::string addCustomEmojiSmiley(TdAccountData &account, PurpleConversation *conv,
                                 const td::td_api::sticker &sticker);

// Whether the account shows custom emoji as the ordinary ones they stand for
bool foldCustomEmoji(PurpleAccount *account);
// Whether a custom emoji of the sticker format may be shown as its picture, as far as moving goes:
// one that moves only where the account shows stickers moving
bool animationsWanted(PurpleAccount *account, int32_t format);
// The same, and as TGS only where it can be converted
bool canShowCustomEmojiFormat(PurpleAccount *account, int32_t format);
// The path of a custom emoji's file, once it is here, fetched if it is not; empty if it cannot be
using CustomEmojiFileReady = std::function<void(TdAccountData &account, const std::string &path)>;
void getCustomEmojiFile(TdAccountData &account, const td::td_api::file *file, CustomEmojiFileReady ready);
// The picture of a custom emoji's file, as the front-end can show it, converted if it is TGS: an id
// in the image store, with a reference for the one given it, or 0
using CustomEmojiImageReady = std::function<void(TdAccountData &account, int imageId)>;
void makeCustomEmojiImage(TdAccountData &account, const std::string &path, int32_t format,
                          CustomEmojiImageReady ready);

// TDLib counts positions in text in UTF-16 code units; this is the byte offset of one in UTF-8
size_t utf16OffsetToBytes(const std::string &text, int32_t offset);

#endif
