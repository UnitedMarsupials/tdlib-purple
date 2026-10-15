#ifndef _STICKER_H
#define _STICKER_H

#include "client-utils.h"
#include <functional>

// Returns the imgstore id of the picture, or 0 if it could not be read
int p2tgl_imgstore_add_with_id_webp(const char *filename);

enum class ImageSupport {
    Supported,    // the front-end can show it as it is
    Unrecognized, // the front-end shows images, but has no loader for this one
    NoLoader      // the front-end does not show images through GdkPixbuf at all
};

// Whether the front-end can show the image, by asking its GdkPixbuf, the way Pidgin will load it.
// The library is looked up at run time, the plugin itself not depending on GTK. When the answer is
// no, error says why.
ImageSupport probeImage(const gchar *data, gsize size, std::string &error);

void showWebpSticker(const td::td_api::chat &chat, const TgMessageInfo &message,
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
