#include "sticker.h"
#include "buildopt.h"
#include "client-utils.h"
#include "config.h"
#include "format.h"
#include "receiving.h"
#include <algorithm>
#include <set>
#include <string.h>

#ifndef NoLottie
#include "gif.h"
#include <zlib.h>
#include <rlottie.h>
#endif

void showStickerFile(const td::td_api::chat &chat, const TgMessageInfo &message,
                     const std::string &filePath, const std::string &fileDescription,
                     TdAccountData &account)
{
    gchar *data = NULL;
    gsize  size = 0;
    ImageSupport support = probeImageFile(filePath);
    if ((support == ImageSupport::Supported) && g_file_get_contents(filePath.c_str(), &data, &size, NULL)) {
        // The imgstore takes the data
        int id = purple_imgstore_add_with_id(data, size, NULL);
        std::string text = makeInlineImageText(id);
        showMessageText(account, chat, message, text.c_str(), NULL, PURPLE_MESSAGE_IMAGES);
    } else {
        if (support == ImageSupport::Unrecognized)
            warnMissingImageLoader(filePath);
        // The emoji it goes with, where it has one
        showGenericFileInline(chat, message, filePath, NULL,
                              message.altText.empty() ? fileDescription : message.altText, std::string(), account);
    }
}

#ifndef NoLottie

static bool gunzip(gchar *compressedData, gsize compressedSize, std::string &output,
                   std::string &errorMessage)
{
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    int unzipResult = inflateInit2(&strm, MAX_WBITS + 16);
    if (unzipResult != Z_OK) {
        // Unlikely error message not worth translating
        errorMessage = "Failed to initialize unzip stream";
        return false;
    }

    if (compressedSize) {
        char unzipBuffer[16384];
        strm.avail_in = compressedSize;
        strm.next_in  = reinterpret_cast<uint8_t *>(compressedData);
        do {
            strm.avail_out = sizeof(unzipBuffer);
            strm.next_out = reinterpret_cast<uint8_t *>(unzipBuffer);
            unzipResult = inflate(&strm, Z_NO_FLUSH);
            if ((unzipResult != Z_OK) && (unzipResult != Z_STREAM_END))
                break;

            if (strm.avail_out > sizeof(unzipBuffer)) {
                unzipResult = Z_STREAM_ERROR;
                break;
            }
            unsigned have = sizeof(unzipBuffer) - strm.avail_out;
            output.append(unzipBuffer, have);
        } while (strm.avail_out == 0);
    }
    (void)inflateEnd(&strm);

    if ((unzipResult != Z_OK) && (unzipResult != Z_STREAM_END)) {
        // Unlikely error message not worth translating
        errorMessage = "Decompression error";
        return false;
    }
    return true;
}

class GifBuilder {
public:
    explicit GifBuilder(int fd, const uint32_t width,
                        const uint32_t height, const uint32_t bgColor=0xffffffff, const uint32_t delay = 2)
    {
        GifBegin(&handle, fd, width, height, delay);
        bgColorR = (uint8_t) ((bgColor & 0xff0000) >> 16);
        bgColorG = (uint8_t) ((bgColor & 0x00ff00) >> 8);
        bgColorB = (uint8_t) ((bgColor & 0x0000ff));
        transparent = ((bgColor >> 24) < 0x80);
    }
    ~GifBuilder()
    {
        GifEnd(&handle);
    }
    void addFrame(rlottie::Surface &s, uint32_t delay = 2)
    {
        argbTorgba(s);
        GifWriteFrame(&handle,
                      reinterpret_cast<uint8_t *>(s.buffer()),
                      s.width(),
                      s.height(),
                      delay,
                      transparent);
    }
    void argbTorgba(rlottie::Surface &s)
    {
        uint8_t *buffer = reinterpret_cast<uint8_t *>(s.buffer());
        uint32_t totalBytes = s.height() * s.bytesPerLine();

        for (uint32_t i = 0; i < totalBytes; i += 4) {
           unsigned char a = buffer[i+3];
           // A GIF pixel is transparent or not at all: those more than half transparent become wholly so
           if (transparent && (a < 0x80)) {
               buffer[i+3] = 0;
               continue;
           }
           // compute only if alpha is non zero
           if (a) {
               unsigned char r = buffer[i+2];
               unsigned char g = buffer[i+1];
               unsigned char b = buffer[i];

               if (transparent && (a != 255)) {
                   // The rest are wholly opaque, in their own colour, which rlottie gives premultiplied
                   buffer[i]   = (unsigned char) std::min(255, r * 255 / a);
                   buffer[i+1] = (unsigned char) std::min(255, g * 255 / a);
                   buffer[i+2] = (unsigned char) std::min(255, b * 255 / a);
                   buffer[i+3] = 255;
               } else if (!transparent && (a != 255)) { //un premultiply
                   unsigned char r2 = (unsigned char) ((float) bgColorR * ((float) (255 - a) / 255));
                   unsigned char g2 = (unsigned char) ((float) bgColorG * ((float) (255 - a) / 255));
                   unsigned char b2 = (unsigned char) ((float) bgColorB * ((float) (255 - a) / 255));
                   buffer[i] = r + r2;
                   buffer[i+1] = g + g2;
                   buffer[i+2] = b + b2;

               } else {
                 // only swizzle r and b
                 buffer[i] = r;
                 buffer[i+2] = b;
               }
           } else {
               buffer[i+2] = bgColorB;
               buffer[i+1] = bgColorG;
               buffer[i] = bgColorR;
           }
        }
    }

private:
    GifWriter      handle;
    uint8_t bgColorR, bgColorG, bgColorB;
    bool    transparent;
};

void StickerConversionThread::run()
{
    gchar  *compressedData = NULL;
    gsize   compressedSize = 0;
    GError *error = NULL;

    g_file_get_contents(inputFileName.c_str(), &compressedData, &compressedSize, &error);
    if (error) {
        m_errorMessage = error->message;
        g_error_free(error);
        return;
    }

    std::string lottieData;
    bool gunzipSuccess = gunzip(compressedData, compressedSize, lottieData, m_errorMessage);
    g_free(compressedData);
    if (!gunzipSuccess)
        return;

    std::unique_ptr<rlottie::Animation> player = rlottie::Animation::loadFromData(lottieData, "");
    if (!player) {
        // Unlikely error message not worth translating
        m_errorMessage = "Could not render animation";
        return;
    }

    char *tempFileName = NULL;
    int fd = g_file_open_tmp("tdlib_sticker_XXXXXX", &tempFileName, NULL);
    if (fd < 0) {
        // Unlikely error message not worth translating
        m_errorMessage = "Could not create temporary file";
        return;
    }
    m_outputFileName = tempFileName;
    g_free(tempFileName);

    unsigned w = m_size;
    unsigned h = m_size;
    auto buffer = std::unique_ptr<uint32_t[]>(new uint32_t[w * h]);
    size_t frameCount = player->totalFrame();

    // Transparent where the animation is, so that it shows on whatever background the conversation has
    GifBuilder builder(fd, w, h, 0);
    for (size_t i = 0; i < frameCount ; i++) {
        rlottie::Surface surface(buffer.get(), w, h, w * 4);
        player->renderSync(i, surface);
        builder.addFrame(surface);
    }
}

#else

void StickerConversionThread::run()
{
    m_errorMessage = "Not supported";
}

#endif

StickerConversionThread::Callback StickerConversionThread::g_callback = nullptr;

void StickerConversionThread::setCallback(AccountThread::Callback callback)
{
    g_callback = callback;
}

void StickerConversionThread::callback(PurpleTdClient* tdClient)
{
    if (g_callback)
        (tdClient->*g_callback)(this);
}

// The one function of GdkPixbuf the plugin needs; the name is there for tests to pretend it is missing
const char *gdkPixbufGetFileInfoName = "gdk_pixbuf_get_file_info";

// GdkPixbufFormat *gdk_pixbuf_get_file_info(const gchar *filename, gint *width, gint *height), from the
// front-end, Pidgin having GdkPixbuf loaded; the plugin does not link it
using GetFileInfo = void *(*)(const gchar *filename, gint *width, gint *height);

static GetFileInfo findGetFileInfo()
{
    return reinterpret_cast<GetFileInfo>(findFrontEndSymbol(gdkPixbufGetFileInfoName));
}

bool canShowImages()
{
    return findGetFileInfo() != nullptr;
}

ImageSupport probeImageFile(const std::string &path)
{
    GetFileInfo getFileInfo = findGetFileInfo();
    if (!getFileInfo)
        return ImageSupport::NoLoader;

    // Only as far as the size, which is where loaders tell what they make of a file
    gint width, height;
    return getFileInfo(path.c_str(), &width, &height) ? ImageSupport::Supported : ImageSupport::Unrecognized;
}

void warnMissingImageLoader(const std::string &path)
{
    // Named after its extension, which TDLib gives the files it downloads; once for each
    static std::set<std::string> warned;
    const char *dot    = strrchr(path.c_str(), '.');
    std::string format = (dot && !strchr(dot, G_DIR_SEPARATOR)) ? dot + 1 : "unknown";
    if (warned.insert(format).second)
        purple_debug_warning(config::pluginId, "Pictures in %s format are shown as text: the front-end has "
                             "no GdkPixbuf loader for them. Installing one would show them as pictures.\n",
                             format.c_str());
}

StickerConversionThread::Callback ReactionStickerThread::g_reactionCallback = nullptr;

void ReactionStickerThread::setCallback(AccountThread::Callback callback)
{
    g_reactionCallback = callback;
}

void ReactionStickerThread::callback(PurpleTdClient* tdClient)
{
    if (g_reactionCallback)
        (tdClient->*g_reactionCallback)(this);
}
