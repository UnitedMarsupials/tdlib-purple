#include "custom-emoji.h"
#include "config.h"
#include "file-transfer.h"
#include "purple-info.h"
#include "receiving.h"
#include "sticker.h"
#include <functional>

namespace {

// Where a smiley's picture goes, once it is here: the conversation may be gone by then
struct SmileyTarget {
    PurpleAccount          *account;
    PurpleConversationType  type;
    std::string             conversation;
    std::string             code;
};

// What is needed of a file to get it, kept apart from the TDLib object it came with
struct FileRef {
    int32_t     id = 0;
    std::string path; // if downloaded

    FileRef() = default;
    explicit FileRef(const td::td_api::file *file)
    {
        if (file) {
            id = file->id_;
            if (file->local_ && file->local_->is_downloading_completed_)
                path = file->local_->path_;
        }
    }
};

}

size_t utf16OffsetToBytes(const std::string &text, int32_t offset)
{
    size_t  bytes = 0;
    int32_t units = 0;
    while ((units < offset) && (bytes < text.size())) {
        const char *start = text.c_str() + bytes;
        const char *next  = g_utf8_next_char(start);
        gunichar    c     = g_utf8_get_char(start);
        units += (c > 0xFFFF) ? 2 : 1;
        bytes  = std::min(text.size(), (size_t)(next - text.c_str()));
    }
    return bytes;
}

void getCustomEmoji(const td::td_api::formattedText &text, std::vector<CustomEmoji> &emoji)
{
    for (const auto &entity: text.entities_)
        if (entity && entity->type_ && (entity->type_->get_id() == td::td_api::textEntityTypeCustomEmoji::ID)) {
            size_t start = utf16OffsetToBytes(text.text_, entity->offset_);
            size_t end   = utf16OffsetToBytes(text.text_, entity->offset_ + entity->length_);
            emoji.push_back(CustomEmoji{
                static_cast<const td::td_api::textEntityTypeCustomEmoji &>(*entity->type_).custom_emoji_id_,
                text.text_.substr(start, end - start)
            });
        }
}

// Unique to the custom emoji, and readable where it shows: in its tooltip, and copied as text
static std::string makeSmileyCode(const CustomEmoji &emoji)
{
    return ":" + emoji.emoji + std::to_string(emoji.id) + ":";
}

static void finishSmiley(const SmileyTarget &target, const gchar *data, gsize size)
{
    PurpleConversation *conv = purple_find_conversation_with_account(target.type, target.conversation.c_str(),
                                                                     target.account);
    if (!conv)
        return;
    // Without a picture, the front-end goes on showing that there is none
    if (data && size)
        purple_conv_custom_smiley_write(conv, target.code.c_str(), reinterpret_cast<const guchar *>(data), size);
    purple_conv_custom_smiley_close(conv, target.code.c_str());
}

static void finishSmileyFromFile(const SmileyTarget &target, const std::string &path)
{
    gchar *data = NULL;
    gsize  size = 0;
    if (!path.empty() && g_file_get_contents(path.c_str(), &data, &size, NULL)) {
        finishSmiley(target, data, size);
        g_free(data);
    } else
        finishSmiley(target, NULL, 0);
}

using FileReady = std::function<void(TdAccountData &account, const std::string &path)>;

static void getFile(TdAccountData &account, const FileRef &file, FileReady ready)
{
    if (!file.path.empty()) {
        ready(account, file.path);
        return;
    }
    if (!file.id) {
        ready(account, std::string());
        return;
    }

    // Shown already, in the conversation, so someone is waiting for it
    auto request = td::td_api::make_object<td::td_api::downloadFile>(file.id, OPEN_DOWNLOAD_PRIORITY, 0, 0, true);
    account.transceiver.sendQuery(std::move(request),
        [&account, ready](uint64_t, td::td_api::object_ptr<td::td_api::Object> object) {
            const td::td_api::file *file = nullptr;
            if (object && (object->get_id() == td::td_api::file::ID))
                file = static_cast<const td::td_api::file *>(object.get());
            ready(account, (file && file->local_ && file->local_->is_downloading_completed_) ?
                           file->local_->path_ : std::string());
        });
}

// The picture as the front-end can show it: the sticker itself, as it is or, for TGS, converted;
// failing that, its thumbnail, which is a still picture of it
static void fetchSmileyPicture(TdAccountData &account, const td::td_api::sticker &sticker, const SmileyTarget &target)
{
    // Custom emoji are 100 pixels square
    enum { CUSTOM_EMOJI_SIZE = 100 };

    FileRef stickerFile(sticker.sticker_.get());
    FileRef thumbnail(sticker.thumbnail_ ? sticker.thumbnail_->file_.get() : nullptr);
    int32_t format = sticker.format_ ? sticker.format_->get_id() : 0;
    auto    useThumbnail = [thumbnail, target](TdAccountData &account) {
        getFile(account, thumbnail, [target](TdAccountData &, const std::string &path) {
            finishSmileyFromFile(target, path);
        });
    };

    // Those who would rather not see stickers move, would not like reactions to either
    if ((format != td::td_api::stickerFormatWebp::ID) &&
        !purple_account_get_bool(account.purpleAccount, AccountOptions::AnimatedStickers,
                                 AccountOptions::AnimatedStickersDefault))
    {
        useThumbnail(account);
        return;
    }

    if (format == td::td_api::stickerFormatTgs::ID) {
        if (!canConvertAnimatedSticker(account.purpleAccount)) {
            useThumbnail(account);
            return;
        }
        getFile(account, stickerFile, [target, useThumbnail](TdAccountData &account, const std::string &path) {
            if (path.empty()) {
                useThumbnail(account);
                return;
            }
            ReactionStickerThread *thread = new ReactionStickerThread(account.purpleAccount, path,
                [target, useThumbnail](TdAccountData &account, int imageId) {
                    PurpleStoredImage *image = imageId ? purple_imgstore_find_by_id(imageId) : NULL;
                    if (image) {
                        finishSmiley(target, static_cast<const gchar *>(purple_imgstore_get_data(image)),
                                     purple_imgstore_get_size(image));
                        purple_imgstore_unref_by_id(imageId);
                    } else
                        useThumbnail(account);
                });
            thread->setSize(CUSTOM_EMOJI_SIZE);
            thread->startThread();
        });
        return;
    }

    getFile(account, stickerFile, [target, useThumbnail](TdAccountData &account, const std::string &path) {
        ImageSupport support = path.empty() ? ImageSupport::NoLoader : probeImageFile(path);
        if (support == ImageSupport::Supported)
            finishSmileyFromFile(target, path);
        else {
            if (support == ImageSupport::Unrecognized)
                warnMissingImageLoader(path);
            useThumbnail(account);
        }
    });
}

static bool foldCustomEmoji(TdAccountData &account)
{
    return purple_account_get_bool(account.purpleAccount, AccountOptions::FoldCustomEmoji,
                                   AccountOptions::FoldCustomEmojiDefault);
}

// Announces the smiley to the conversation, once for each; false if the front-end will not have it
static bool registerSmiley(PurpleConversation *conv, const CustomEmoji &emoji, const std::string &code,
                           bool &isNew)
{
    std::string key = "tdlib-purple custom emoji " + code;
    isNew = !purple_conversation_get_data(conv, key.c_str());
    if (!isNew)
        return true;
    if (!purple_conv_custom_smiley_add(conv, code.c_str(), "id", std::to_string(emoji.id).c_str(), TRUE))
        return false;
    purple_conversation_set_data(conv, key.c_str(), GINT_TO_POINTER(1));
    return true;
}

static SmileyTarget makeTarget(PurpleConversation *conv, const std::string &code)
{
    return SmileyTarget{purple_conversation_get_account(conv), purple_conversation_get_type(conv),
                        purple_conversation_get_name(conv), code};
}

std::map<int64_t, std::string> addCustomEmojiSmileys(TdAccountData &account, PurpleConversation *conv,
                                                     const std::vector<CustomEmoji> &emoji)
{
    std::map<int64_t, std::string> codes;
    if (!conv || foldCustomEmoji(account))
        return codes;

    auto targets = std::make_shared<std::map<int64_t, SmileyTarget>>();
    std::vector<int64_t> newIds;
    for (const CustomEmoji &item: emoji) {
        if (codes.count(item.id))
            continue;
        std::string code = makeSmileyCode(item);
        bool        isNew;
        if (!registerSmiley(conv, item, code, isNew))
            continue;
        codes[item.id] = code;
        if (isNew) {
            (*targets)[item.id] = makeTarget(conv, code);
            newIds.push_back(item.id);
        }
    }

    if (!newIds.empty()) {
        purple_debug_misc(config::pluginId, "Fetching %zu custom emoji\n", newIds.size());
        account.transceiver.sendQuery(td::td_api::make_object<td::td_api::getCustomEmojiStickers>(std::move(newIds)),
            [&account, targets](uint64_t, td::td_api::object_ptr<td::td_api::Object> object) {
                if (object && (object->get_id() == td::td_api::stickers::ID))
                    for (const auto &sticker: static_cast<const td::td_api::stickers &>(*object).stickers_)
                        if (sticker && sticker->full_type_ &&
                            (sticker->full_type_->get_id() == td::td_api::stickerFullTypeCustomEmoji::ID))
                        {
                            int64_t id = static_cast<const td::td_api::stickerFullTypeCustomEmoji &>(
                                             *sticker->full_type_).custom_emoji_id_;
                            auto it = targets->find(id);
                            if (it != targets->end()) {
                                fetchSmileyPicture(account, *sticker, it->second);
                                targets->erase(it);
                            }
                        }
                // Those Telegram did not know
                for (const auto &item: *targets)
                    finishSmiley(item.second, NULL, 0);
            });
    }

    return codes;
}

std::string addCustomEmojiSmiley(TdAccountData &account, PurpleConversation *conv,
                                 const td::td_api::sticker &sticker)
{
    if (!conv || foldCustomEmoji(account) || !sticker.full_type_ ||
        (sticker.full_type_->get_id() != td::td_api::stickerFullTypeCustomEmoji::ID))
        return std::string();

    CustomEmoji emoji{static_cast<const td::td_api::stickerFullTypeCustomEmoji &>(*sticker.full_type_).custom_emoji_id_,
                      sticker.emoji_};
    std::string code = makeSmileyCode(emoji);
    bool        isNew;
    if (!registerSmiley(conv, emoji, code, isNew))
        return std::string();
    if (isNew)
        fetchSmileyPicture(account, sticker, makeTarget(conv, code));
    return code;
}
