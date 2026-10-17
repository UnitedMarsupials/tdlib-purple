#include "fixture.h"
#include "libpurple-mock.h"
#include "buildopt.h"
#include <unistd.h>

// Custom emoji as custom smileys of the conversation, as Pidgin has them
class CustomEmojiTest: public CommTest {
protected:
    const int64_t     messageId     = 1;
    const int32_t     date          = 10001;
    const int64_t     customEmojiId = 5000;
    const int32_t     fileId        = 1234;
    const int32_t     thumbnailId   = 1235;
    const std::string emoji         = "\U0001F63A";
    // The cat, with 5000 in invisible tag characters
    const std::string code          = "\U0001F63A\U000E0035\U000E0030\U000E0030\U000E0030\U000E007F";

    void SetUp() override
    {
        CommTest::SetUp();
        setCustomSmileys(true);
    }

    object_ptr<message> makeEmojiMessage(int64_t id, const std::string &text, int32_t offset);
    object_ptr<sticker> makeEmojiSticker(object_ptr<StickerFormat> format, const std::string &path,
                                         const std::string &thumbnailPath = "");
    size_t              fileSize(const char *path);
};

object_ptr<message> CustomEmojiTest::makeEmojiMessage(int64_t id, const std::string &text, int32_t offset)
{
    object_ptr<messageText> content = makeTextMessage(text);
    // The cat is two UTF-16 code units long
    content->text_->entities_.push_back(make_object<textEntity>(
        offset, 2, make_object<textEntityTypeCustomEmoji>(customEmojiId)));
    return makeMessage(id, userIds[0], chatIds[0], false, date, std::move(content));
}

object_ptr<sticker> CustomEmojiTest::makeEmojiSticker(object_ptr<StickerFormat> format, const std::string &path,
                                                      const std::string &thumbnailPath)
{
    object_ptr<thumbnail> thumb;
    if (!thumbnailPath.empty())
        thumb = make_object<thumbnail>(make_object<thumbnailFormatWebp>(), 100, 100, make_object<file>(
            thumbnailId, 1000, 1000,
            make_object<localFile>(thumbnailPath, true, true, false, true, 0, 1000, 1000),
            make_object<remoteFile>("beh", "bleh", false, true, 1000)
        ));
    object_ptr<sticker> result = makeSticker(100, 100, emoji, make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(path, true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ), std::move(thumb));
    result->format_    = std::move(format);
    result->full_type_ = make_object<stickerFullTypeCustomEmoji>(customEmojiId, false);
    return result;
}

size_t CustomEmojiTest::fileSize(const char *path)
{
    gchar *data = NULL;
    gsize  size = 0;
    EXPECT_TRUE(g_file_get_contents(path, &data, &size, NULL));
    g_free(data);
    return size;
}

static object_ptr<stickers> makeStickers(object_ptr<sticker> item)
{
    std::vector<object_ptr<sticker>> list;
    list.push_back(std::move(item));
    return make_object<stickers>(std::move(list));
}

TEST_F(CustomEmojiTest, InText)
{
    loginWithOneContact();

    // Shown at once, with a code where the custom emoji goes
    tgl.update(make_object<updateNewMessage>(makeEmojiMessage(messageId, "hi " + emoji, 3)));
    tgl.verifyRequests({
        make_object<getCustomEmojiStickers>(std::vector<int64_t>{customEmojiId}),
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, messageId), nullptr, true)
    });
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        CustomSmileyAddEvent(purpleUserName(0), code),
        ServGotImEvent(connection, purpleUserName(0), "hi " + code, PURPLE_MESSAGE_RECV, date)
    );

    // ...which the picture fills when it is here
    tgl.reply(makeStickers(makeEmojiSticker(make_object<stickerFormatWebp>(), TEST_SOURCE_DIR "/test.webp")));
    prpl.verifyEvents(
        CustomSmileyWriteEvent(purpleUserName(0), code, fileSize(TEST_SOURCE_DIR "/test.webp")),
        CustomSmileyCloseEvent(purpleUserName(0), code)
    );

    // Known to the conversation now
    tgl.update(make_object<updateNewMessage>(makeEmojiMessage(messageId + 1, emoji + " again", 0)));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId + 1}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0), code + " again", PURPLE_MESSAGE_RECV, date));
}

TEST_F(CustomEmojiTest, NoCustomSmileys)
{
    setCustomSmileys(false);
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeEmojiMessage(messageId, "hi " + emoji, 3)));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ServGotImEvent(connection, purpleUserName(0), "hi " + emoji, PURPLE_MESSAGE_RECV, date)
    );
}

TEST_F(CustomEmojiTest, Folded)
{
    purple_account_set_bool(account, "fold-custom-emoji", TRUE);
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeEmojiMessage(messageId, "hi " + emoji, 3)));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ServGotImEvent(connection, purpleUserName(0), "hi " + emoji, PURPLE_MESSAGE_RECV, date)
    );
}

TEST_F(CustomEmojiTest, WebmFallsBackOnThumbnail)
{
    // Nothing that would play, but it starts the way WebM does
    char *path = NULL;
    int   fd   = g_file_open_tmp("tdlib_test_XXXXXX", &path, NULL);
    ASSERT_TRUE(fd >= 0);
    ASSERT_EQ(8, write(fd, "\x1A\x45\xDF\xA3webm", 8));
    ::close(fd);
    std::string webm = path;
    g_free(path);

    loginWithOneContact();
    tgl.update(make_object<updateNewMessage>(makeEmojiMessage(messageId, emoji, 0)));
    tgl.verifyRequests({
        make_object<getCustomEmojiStickers>(std::vector<int64_t>{customEmojiId}),
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, messageId), nullptr, true)
    });
    prpl.discardEvents();

    tgl.reply(makeStickers(makeEmojiSticker(make_object<stickerFormatWebm>(), webm, TEST_SOURCE_DIR "/test.webp")));
    prpl.verifyEvents(
        CustomSmileyWriteEvent(purpleUserName(0), code, fileSize(TEST_SOURCE_DIR "/test.webp")),
        CustomSmileyCloseEvent(purpleUserName(0), code)
    );
    remove(webm.c_str());
}

TEST_F(CustomEmojiTest, Reaction)
{
    loginWithOneContact();
    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId, userIds[0], chatIds[0], false, date, makeTextMessage("hi"))));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.discardEvents();

    std::vector<object_ptr<messageReaction>> list;
    std::vector<object_ptr<MessageSender>>   senders;
    senders.push_back(make_object<messageSenderUser>(userIds[0]));
    list.push_back(make_object<messageReaction>(make_object<reactionTypeCustomEmoji>(customEmojiId), 1,
                                                false, nullptr, std::move(senders)));
    tgl.update(make_object<updateMessageInteractionInfo>(chatIds[0], messageId,
        make_object<messageInteractionInfo>(0, 0, nullptr, make_object<messageReactions>(
            std::move(list), false, std::vector<object_ptr<paidReactor>>(), false
        ))));
    tgl.verifyRequest(getCustomEmojiStickers({customEmojiId}));

    // The picture already here goes to the conversation before the reaction that shows it
    tgl.reply(makeStickers(makeEmojiSticker(make_object<stickerFormatWebp>(), TEST_SOURCE_DIR "/test.webp")));
    prpl.verifyEvents(
        CustomSmileyAddEvent(purpleUserName(0), code),
        CustomSmileyWriteEvent(purpleUserName(0), code, fileSize(TEST_SOURCE_DIR "/test.webp")),
        CustomSmileyCloseEvent(purpleUserName(0), code),
        ServGotImEvent(connection, purpleUserName(0), "reacted with " + code + " to message „hi“.",
                       PURPLE_MESSAGE_RECV, 0)
    );
    tgl.verifyNoRequests();
}
