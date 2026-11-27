#ifndef _STICKER_H
#define _STICKER_H

#include "client-utils.h"
#include <functional>
#include <memory>

enum class ImageSupport {
    Supported,    // the front-end can show it as it is
    Unrecognized, // the front-end shows images, but has no loader for this one
    NoLoader      // the front-end does not show images through GdkPixbuf at all
};

// Whether the front-end shows pictures at all, which it does with GdkPixbuf, if it has it
bool canShowImages();
// Whether an animated (TGS) sticker is converted to be shown moving, as the account may not want
bool canConvertAnimatedSticker(PurpleAccount *purpleAccount);
extern const char *gdkPixbufGetFileInfoName;

// Whether the front-end can show the image, by asking its GdkPixbuf, which Pidgin loads it with, to
// look at the file. The plugin does not link GdkPixbuf, but looks for the front-end's.
ImageSupport probeImageFile(const std::string &path);

// Says, once for each format, that a GdkPixbuf loader for it would show such pictures
void warnMissingImageLoader(const std::string &path);

void showStickerFile(const td::td_api::chat &chat, const TgMessageInfo &message,
                     const std::string &filePath, const std::string &fileDescription,
                     TdAccountData &account);

class StickerConversionThread: public AccountThread {
public:
    static constexpr unsigned DEFAULT_SIZE = 200;
private:
    std::string   m_errorMessage;
    std::string   m_outputFileName;
    unsigned      m_size = DEFAULT_SIZE; // of each side of the animation, in pixels
    void run() override;

    static Callback g_callback;
    void callback(PurpleTdClient *tdClient) override;
    TgMessageInfo m_message;
public:
    const std::string inputFileName;
    const ChatId chatId;
    // Of the sticker, whose animation is kept, once converted, for every message showing it, by the
    // account's connection it was converted for, not the next one
    int32_t fileId = 0;
    std::weak_ptr<int> owner;
    StickerConversionThread(PurpleAccount *purpleAccount, const std::string &filename,
                            ChatId chatId, const TgMessageInfo *message)
    : AccountThread(purpleAccount), inputFileName(filename), chatId(chatId)
    {
        if (message)
            m_message.assign(*message);
    }
    ~StickerConversionThread() override;

    const std::string &getOutputFileName() const { return m_outputFileName; }
    const std::string &getErrorMessage()   const { return m_errorMessage; }
    const TgMessageInfo &message()         const { return m_message; }
    void setSize(unsigned size) { m_size = size; }

    static void setCallback(Callback callback);
};

// Converts the animation of a custom emoji given as a reaction, for a callback of its own
class ReactionStickerThread: public StickerConversionThread {
public:
    // Given the imgstore id of the animation, or 0 if it could not be converted
    using Done = std::function<void(TdAccountData &account, int imageId)>;
    const Done done;

    ReactionStickerThread(PurpleAccount *purpleAccount, const std::string &filename, Done done)
    : StickerConversionThread(purpleAccount, filename, ChatId::invalid, nullptr), done(std::move(done)) {}

    static void setCallback(Callback callback);
private:
    static Callback g_reactionCallback;
    void callback(PurpleTdClient *tdClient) override;
};

#endif
