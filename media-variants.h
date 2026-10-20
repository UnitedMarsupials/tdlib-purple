#ifndef _MEDIA_VARIANTS_H
#define _MEDIA_VARIANTS_H

#include "account-data.h"
#include <string>
#include <vector>

// One of the sizes Telegram has a photo or video in, each a file of its own
struct MediaVariant {
    std::string             key;    // in a link to it: "p" and the photo size's type, "v" for the
                                    // video itself, "a" and the id of an alternative video
    int32_t                 width;
    int32_t                 height;
    int64_t                 size;   // in bytes, as far as known
    std::string             codec;  // of an alternative video
    const td::td_api::file *file;
};

// Smallest first; empty for a message that is neither photo nor video
void getMediaVariants(const td::td_api::MessageContent &content, std::vector<MediaVariant> &variants);

// The file of the variant a link names; null if there is none such
const td::td_api::file *findMediaVariant(const td::td_api::MessageContent &content, const std::string &key,
                                         std::string &description);

// A line of links, one to each variant but a video as sent, for the plugin to open when clicked; empty if
// there is but one
std::string makeMediaVariantLinks(PurpleAccount *account, ChatId chatId, MessageId messageId,
                                  const std::vector<MediaVariant> &variants);

// Which variant of a video to download beforehand, as the account would have it; null for none
const td::td_api::file *choosePrefetchVariant(PurpleAccount *account, const std::vector<MediaVariant> &variants,
                                              unsigned limitKb);

#endif
