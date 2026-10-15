#ifndef _STICKER_H
#define _STICKER_H

#include "client-utils.h"
#include <functional>

enum class ImageSupport {
    Supported,    // the front-end can show it as it is
    Unrecognized, // the front-end shows images, but has no loader for this one
    NoLoader      // the front-end does not show images through GdkPixbuf at all
};

// Whether the front-end can show the image, by asking its GdkPixbuf, which Pidgin loads it with, to
// look at the file. The plugin does not link GdkPixbuf, but looks for the front-end's.
ImageSupport probeImageFile(const std::string &path);

// Says, once for each format, that a GdkPixbuf loader for it would show such pictures
void warnMissingImageLoader(const std::string &path);

void showStickerFile(const td::td_api::chat &chat, const TgMessageInfo &message,
                     const std::string &filePath, const std::string &fileDescription,
                     TdAccountData &account);

class StickerConversionThread: public AccountThread {
private:
    std::string   m_errorMessage;
    std::string   m_outputFileName;
    unsigned      m_size = 200; // of each side of the animation, in pixels
    void run() override;

    static Callback g_callback;
    void callback(PurpleTdClient *tdClient) override;
    TgMessageInfo m_message;
public:
    const std::string inputFileName;
    const ChatId chatId;
    StickerConversionThread(PurpleAccount *purpleAccount, const std::string &filename,
                            ChatId chatId, TgMessageInfo &&message)
    : AccountThread(purpleAccount), m_message(std::move(message)), inputFileName(filename),
        chatId(chatId) {}
    StickerConversionThread(PurpleAccount *purpleAccount, const std::string &filename,
                            ChatId chatId, const TgMessageInfo *message)
    : AccountThread(purpleAccount), inputFileName(filename), chatId(chatId)
    {
        if (message)
            m_message.assign(*message);
    }

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
