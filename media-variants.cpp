#include "media-variants.h"
#include "client-utils.h"
#include "config.h"
#include "purple-info.h"
#include "receiving.h"
#include <algorithm>

static int64_t getFileSize(const td::td_api::file *file)
{
    if (!file)
        return 0;
    return file->size_ ? file->size_ : file->expected_size_;
}

void getMediaVariants(const td::td_api::MessageContent &content, std::vector<MediaVariant> &variants)
{
    if (content.get_id() == td::td_api::messagePhoto::ID) {
        const auto &photo = static_cast<const td::td_api::messagePhoto &>(content);
        if (photo.photo_)
            for (const auto &size: photo.photo_->sizes_)
                if (size && size->photo_)
                    variants.push_back(MediaVariant{"p" + size->type_, size->width_, size->height_,
                                                    getFileSize(size->photo_.get()), std::string(),
                                                    size->photo_.get()});
    } else if (content.get_id() == td::td_api::messageVideo::ID) {
        const auto &video = static_cast<const td::td_api::messageVideo &>(content);
        if (video.video_ && video.video_->video_)
            variants.push_back(MediaVariant{"v", video.video_->width_, video.video_->height_,
                                            getFileSize(video.video_->video_.get()), std::string(),
                                            video.video_->video_.get()});
        for (const auto &alternative: video.alternative_videos_)
            if (alternative && alternative->video_)
                variants.push_back(MediaVariant{"a" + std::to_string(alternative->id_), alternative->width_,
                                                alternative->height_, getFileSize(alternative->video_.get()),
                                                alternative->codec_, alternative->video_.get()});
    }

    std::stable_sort(variants.begin(), variants.end(), [](const MediaVariant &a, const MediaVariant &b) {
        return ((int64_t)a.width * a.height < (int64_t)b.width * b.height) ||
               (((int64_t)a.width * a.height == (int64_t)b.width * b.height) && (a.size < b.size));
    });
}

static std::string describeVariant(const MediaVariant &variant)
{
    std::string result = std::to_string(variant.width) + "×" + std::to_string(variant.height);
    if (!variant.codec.empty())
        result += " (" + variant.codec + ")";
    if (variant.size) {
        char *size = purple_str_size_to_units(variant.size);
        result += ", ";
        result += size;
        g_free(size);
    }
    return result;
}

const td::td_api::file *findMediaVariant(const td::td_api::MessageContent &content, const std::string &key,
                                         std::string &description)
{
    std::vector<MediaVariant> variants;
    getMediaVariants(content, variants);
    for (const MediaVariant &variant: variants)
        if (variant.key == key) {
            description = describeVariant(variant);
            return variant.file;
        }
    return nullptr;
}

std::string makeMediaVariantLinks(PurpleAccount *account, ChatId chatId, MessageId messageId,
                                  const std::vector<MediaVariant> &variants)
{
    if (variants.size() < 2)
        return std::string();

    std::string result;
    for (const MediaVariant &variant: variants) {
        if (!result.empty())
            result += " · ";
        result += "<a href=\"" + escapeForDisplay(makeMessageLink(account, chatId, messageId, variant.key)) +
                  "\">" + escapeForDisplay(describeVariant(variant)) + "</a>";
    }
    return result;
}

const td::td_api::file *choosePrefetchVariant(PurpleAccount *account, const std::vector<MediaVariant> &variants,
                                              unsigned limitKb)
{
    if (variants.empty())
        return nullptr;
    const char *choice = purple_account_get_string(account, AccountOptions::PrefetchVideos,
                                                   AccountOptions::PrefetchVideosDefault);
    // A file of unknown size may be any size, so a limit rules it out
    auto fits = [limitKb](const MediaVariant &variant) {
        if (limitKb && (variant.size <= 0))
            return false;
        return isSizeWithinLimit((variant.size + 1023) / 1024, limitKb);
    };

    if (!strcmp(choice, AccountOptions::PrefetchVideosSmallest)) {
        // The smallest file, which is not always the smallest picture, of those that may be fetched
        const MediaVariant *smallest = nullptr;
        for (const MediaVariant &variant: variants)
            if (fits(variant) && (!smallest || (variant.size < smallest->size)))
                smallest = &variant;
        return smallest ? smallest->file : nullptr;
    }
    if (!strcmp(choice, AccountOptions::PrefetchVideosLargest))
        return fits(variants.back()) ? variants.back().file : nullptr;
    if (!strcmp(choice, AccountOptions::PrefetchVideosLargestWithinLimit)) {
        for (auto it = variants.rbegin(); it != variants.rend(); ++it)
            if (fits(*it))
                return it->file;
        return nullptr;
    }
    return nullptr;
}
