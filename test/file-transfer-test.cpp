#include "fixture.h"
#include "libpurple-mock.h"
#include "buildopt.h"
#include "receiving.h"
#include "td-client.h"
#include "client-utils.h"
#include <glib/gstdio.h>

class FileTransferTest: public CommTest {};

TEST_F(FileTransferTest, Document_AlreadyDownloaded)
{
    const int64_t messageId = 1;
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageDocument>(
            make_object<document>(
                "doc.file.name", "mime/type", nullptr, nullptr,
                make_object<file>(
                    fileId, 10000, 10000,
                    make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
                    make_object<remoteFile>("beh", "bleh", false, true, 10000)
                )
            ),
            make_object<formattedText>("caption", std::vector<object_ptr<textEntity>>())
        )
    )));
    tgl.verifyRequests({
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, messageId), nullptr, true)
    });
    prpl.verifyEvents(
        ServGotImEvent(
            connection,
            purpleUserName(0),
            "<a href=\"file://file:///path\">doc.file.name [mime/type]</a>\ncaption",
            PURPLE_MESSAGE_RECV,
            date
        )
    );
}

TEST_F(FileTransferTest, BigPhoto_RequestDownload)
{
    purple_account_set_string(account, "media-size-threshold", "0.5");

    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 600000, 600000,
            make_object<localFile>("", true, true, false, false, 0, 0, 0),
            make_object<remoteFile>("beh", "bleh", false, true, 600000)
        ), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            make_object<photo>(false, nullptr, std::move(sizes)),
            make_object<formattedText>("caption", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    )));
    tgl.verifyRequest(
        viewMessages(chatIds[0], std::vector<int64_t>(1, 1), nullptr, true)
    );
    prpl.verifyEvents(
        ServGotImEvent(connection, purpleUserName(0), "caption", PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + ": Requesting photo download",
            PURPLE_MESSAGE_SYSTEM, date
        ),
        RequestActionEvent(connection, account, NULL, NULL, 2)
    );

    prpl.requestedAction("_Yes");
    tgl.verifyRequest(downloadFile(fileId, 1, 0, 0, true));
    tgl.reply(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));

    prpl.verifyEvents(ServGotImEvent(
        connection,
        purpleUserName(0),
        "<img src=\"file:///path\">",
        (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES),
        date
    ));
}

TEST_F(FileTransferTest, BigPhoto_Ignore)
{
    purple_account_set_string(account, "media-size-threshold", "0.5");
    purple_account_set_string(account, "media-handling-behavior", "discard");

    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 655360, 655360,
            make_object<localFile>("", true, true, false, false, 0, 0, 0),
            make_object<remoteFile>("beh", "bleh", false, true, 655360)
        ), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            make_object<photo>(false, nullptr, std::move(sizes)),
            make_object<formattedText>("caption", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    )));
    tgl.verifyRequest(
        viewMessages(chatIds[0], std::vector<int64_t>(1, 1), nullptr, true)
    );
    prpl.verifyEvents(
        ServGotImEvent(connection, purpleUserName(0), "caption", PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + ": Ignoring photo download (purple_str_size_to_units)",
            PURPLE_MESSAGE_SYSTEM, date
        )
    );
}

TEST_F(FileTransferTest, SecretPhoto_AlreadyDownloaded)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    // This test is about the message being refused, so say so rather than leaning on whatever
    // ShowSelfDestructDefault happens to be: the FreeBSD port, for one, compiles it the other way.
    purple_account_set_bool(account, "show-self-destruct", FALSE);
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 10000, 10000,
            make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
            make_object<remoteFile>("beh", "bleh", false, true, 10000)
        ), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            make_object<photo>(false, nullptr, std::move(sizes)),
            make_object<formattedText>("caption", std::vector<object_ptr<textEntity>>()),
            false, false, true
        )
    )));
    tgl.verifyRequest(
        viewMessages(chatIds[0], std::vector<int64_t>(1, 1), nullptr, true)
    );

    // Secret photos are always ignored
    prpl.verifyEvents(
        ServGotImEvent(connection, purpleUserName(0), "caption", PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + ": Ignoring secret file (photo)",
            PURPLE_MESSAGE_SYSTEM, date
        )
    );
}

TEST_F(FileTransferTest, PhotoWithoutCaption)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 10000, 10000,
            make_object<localFile>("", true, true, false, false, 0, 0, 0),
            make_object<remoteFile>("beh", "bleh", false, true, 10000)
        ), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            make_object<photo>(false, nullptr, std::move(sizes)),
            make_object<formattedText>("", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    )));
    tgl.verifyRequest(downloadFile(fileId, 1, 0, 0, true));
    prpl.verifyNoEvents();

    tgl.reply(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));

    prpl.verifyEvents(ServGotImEvent(
        connection,
        purpleUserName(0),
        "<img src=\"file:///path\">",
        (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES),
        date
    ));
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));
}

TEST_F(FileTransferTest, SendFile_ErrorInUploadResponse)
{
    const char *const PATH = "/path";
    loginWithOneContact();

    setFakeFileSize(PATH, 9000);
    pluginInfo().send_file(connection, purpleUserName(0).c_str(), PATH);
    prpl.verifyEvents(XferAcceptedEvent(purpleUserName(0), PATH));
    tgl.verifyRequest(preliminaryUploadFile(
        make_object<inputFileLocal>(PATH),
        make_object<fileTypeDocument>(),
        1
    ));

    tgl.reply(make_object<error>(1, "error"));
    prpl.verifyEvents(XferRemoteCancelEvent(PATH));
}

TEST_F(FileTransferTest, SendFile_SendMessageResponseError)
{
    const char *const PATH   = "/path";
    const int32_t     fileId = 1234;
    loginWithOneContact();

    setFakeFileSize(PATH, 9000);
    pluginInfo().send_file(connection, purpleUserName(0).c_str(), PATH);
    prpl.verifyEvents(XferAcceptedEvent(purpleUserName(0), PATH));
    tgl.verifyRequest(preliminaryUploadFile(
        make_object<inputFileLocal>(PATH),
        make_object<fileTypeDocument>(),
        1
    ));

    tgl.reply(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(PATH, false, false, false, true, 0, 10000, 10000),
        make_object<remoteFile>("", "", true, false, 0)
    ));
    prpl.verifyEvents(
        XferStartEvent(PATH),
        XferProgressEvent(PATH, 0)
    );

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(PATH, false, false, false, true, 0, 10000, 10000),
        make_object<remoteFile>("", "", true, false, 5000)
    )));
    prpl.verifyEvents(XferProgressEvent(PATH, 5000));

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(PATH, false, false, false, true, 0, 10000, 10000),
        make_object<remoteFile>("", "", true, false, 9500)
    )));
    prpl.verifyEvents(XferProgressEvent(PATH, 9000));

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(PATH, false, false, false, true, 0, 10000, 10000),
        make_object<remoteFile>("", "", false, false, 10000)
    )));
    prpl.verifyEvents(
        XferCompletedEvent(PATH, TRUE, 9000),
        XferEndEvent(PATH)
    );
    tgl.verifyRequest(*makeSendMessage(
        chatIds[0],
        0,
        nullptr,
        nullptr,
        nullptr,
        makeInputMessageDocument(
            make_object<inputFileId>(fileId),
            nullptr,
            false,
            make_object<formattedText>()
        )
    ));

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(PATH, false, false, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));

    tgl.reply(make_object<error>(100, "error"));
    prpl.verifyEvents(
        NewConversationEvent(PURPLE_CONV_TYPE_IM, account, purpleUserName(0)),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            "Failed to send message: code 100 (error)",
            PURPLE_MESSAGE_SYSTEM, 0
        )
    );
}

TEST_F(FileTransferTest, SendFile_UnknownUser)
{
    const char *const PATH = "/path";
    login();

    setFakeFileSize(PATH, 9000);
    pluginInfo().send_file(connection, "Antonie van Leeuwenhoek", PATH);
    prpl.verifyEvents(
        XferAcceptedEvent("Antonie van Leeuwenhoek", PATH),
        XferLocalCancelEvent(PATH)
    );
}

TEST_F(FileTransferTest, WebpSticker)
{
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageSticker>(makeSticker(320, 200, "",
            makeFile(fileId, 10000, TEST_SOURCE_DIR "/test.webp")), false)
    )));
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(
        connection,
        purpleUserName(0),
        "\n<img id=\"" + std::to_string(getLastImgstoreId()) + "\">",
        (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES),
        date
    ));
    // As it is, for the front-end's GdkPixbuf to decode
    gchar *data = NULL;
    gsize  size = 0;
    ASSERT_TRUE(g_file_get_contents(TEST_SOURCE_DIR "/test.webp", &data, &size, NULL));
    g_free(data);
    ASSERT_EQ(size, purple_imgstore_get_size(purple_imgstore_find_by_id(getLastImgstoreId())));
}

TEST_F(FileTransferTest, WebpSticker_NoLoader)
{
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;
    setImageLoaders({});
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageSticker>(makeSticker(320, 200, "",
            makeFile(fileId, 10000, TEST_SOURCE_DIR "/test.webp")), false)
    )));
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(
        connection,
        purpleUserName(0),
        "<a href=\"file://file://" TEST_SOURCE_DIR "/test.webp\">sticker</a>",
        PURPLE_MESSAGE_RECV,
        date
    ));
}

TEST_F(FileTransferTest, WebpSticker_NoLoader_Emoji)
{
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;
    setImageLoaders({});
    loginWithOneContact();

    // Where it cannot be shown, the emoji it goes with stands in for it
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageSticker>(makeSticker(320, 200, "\U0001F63A",
            makeFile(fileId, 10000, TEST_SOURCE_DIR "/test.webp")), false)
    )));
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(
        connection,
        purpleUserName(0),
        "<a href=\"file://file://" TEST_SOURCE_DIR "/test.webp\">\U0001F63A</a>",
        PURPLE_MESSAGE_RECV,
        date
    ));
}

#ifndef NoLottie
TEST_F(FileTransferTest, AnimatedStickerDecode)
#else
TEST_F(FileTransferTest, DISABLED_AnimatedStickerDecode)
#endif
{
    const int32_t date    = 10001;
    const int32_t fileId  = 1234;
    loginWithOneContact();

    // No thumbnail, only .tgs
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageSticker>(makeSticker(320, 200, "",
            make_object<file>(
                fileId, 10000, 10000,
                make_object<localFile>(TEST_SOURCE_DIR "/test.tgs", true, true, false, true, 0, 10000, 10000),
                make_object<remoteFile>("beh", "bleh", false, true, 10000)
            )), false)
    )));
    tgl.verifyRequests({
        make_object<viewMessages>(chatIds[0], std::vector<int64_t>(1, 1), nullptr, true),
    });

    tgl.reply(make_object<ok>()); // reply to viewMessages

    prpl.verifyEvents(
        ServGotImEvent(
            connection,
            purpleUserName(0),
            // Sticker was converted to gif
            "\n<img id=\"" + std::to_string(getLastImgstoreId()) + "\">",
            (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES),
            date
        )
    );
}

TEST_F(FileTransferTest, Sticker_AnimatedDisabled_AlreadyDownloaded)
{
    const int32_t date      = 10001;
    const int32_t fileId[2] = {1234, 1235};
    const int32_t thumbId   = 1236;
    purple_account_set_bool(account, "animated-stickers", FALSE);
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageSticker>(makeSticker(320, 200, "",
            make_object<file>(
                fileId[0], 10000, 10000,
                nullptr,
                make_object<remoteFile>("beh", "bleh", false, true, 10000)
            )), false)
    )));
    tgl.verifyRequest(downloadFile(fileId[0], 1, 0, 0, true));
    prpl.verifyNoEvents();

    tgl.reply(make_object<file>(
        fileId[0], 10000, 10000,
        make_object<localFile>("/sticker", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));

    prpl.verifyEvents(ServGotImEvent(
        connection,
        purpleUserName(0),
        "<a href=\"file://file:///sticker\">sticker</a>",
        PURPLE_MESSAGE_RECV,
        date
    ));
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));

    // Now with thumbnail and main file, both already downloaded
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageSticker>(makeSticker(320, 200, "",
            make_object<file>(
                fileId[1], 10000, 10000,
                make_object<localFile>("/sticker2.tgs", true, true, false, true, 0, 10000, 10000),
                make_object<remoteFile>("beh", "bleh", false, true, 10000)
            )
        ,
            make_object<thumbnail>(
                make_object<thumbnailFormatJpeg>(),
                320, 200,
                make_object<file>(
                    thumbId, 10000, 10000,
                    make_object<localFile>("/thumb", true, true, false, true, 0, 10000, 10000),
                    make_object<remoteFile>("beh", "bleh", false, true, 10000)
                )
            )), false)
    )));

    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(
        connection,
        purpleUserName(0),
        // Sticker replaced with thumbnail because it's .tgs
        "<a href=\"file://file:///thumb\">sticker</a>",
        PURPLE_MESSAGE_RECV,
        date
    ));
}

TEST_F(FileTransferTest, Sticker_AnimatedDisabled_ThumbnailAboveLimit)
{
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;
    const int32_t thumbId   = 1236;
    purple_account_set_bool(account, "animated-stickers", FALSE);
    purple_account_set_string(account, "media-size-threshold", "0.1");
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageSticker>(makeSticker(320, 200, "",
            make_object<file>(
                fileId, 10000, 10000,
                make_object<localFile>("", true, true, false, false, 0, 0, 0),
                make_object<remoteFile>("beh", "bleh", false, true, 10000)
            )
        ,
            make_object<thumbnail>(
                make_object<thumbnailFormatJpeg>(),
                320, 200,
                make_object<file>(
                    thumbId, 100000000, 100000000,
                    make_object<localFile>("", true, true, false, false, 0, 0, 0),
                    make_object<remoteFile>("beh", "bleh", false, true, 100000000)
                )
            )), false)
    )));
    tgl.verifyRequest(downloadFile(fileId, 1, 0, 0, true));
    prpl.verifyNoEvents();

    tgl.reply(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/sticker.tgs", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));
    prpl.verifyNoEvents();
    tgl.verifyRequest(downloadFile(thumbId, 1, 0, 0, true));

    tgl.reply(make_object<file>(
        fileId, 100000000, 100000000,
        make_object<localFile>("/thumb", true, true, false, true, 0, 100000000, 100000000),
        make_object<remoteFile>("beh", "bleh", false, true, 100000000)
    ));
    prpl.verifyEvents(
        ServGotImEvent(
            connection, purpleUserName(0),
            // Sticker replaced with thumbnail because it's .tgs
            "<a href=\"file://file:///thumb\">sticker</a>",
            PURPLE_MESSAGE_RECV, date
        )
    );
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));
}

TEST_F(FileTransferTest, Sticker_AnimatedDisabled_LongDownloads_ThumbnailAboveLimit)
{
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;
    const int32_t thumbId   = 1236;
    purple_account_set_bool(account, "animated-stickers", FALSE);
    purple_account_set_string(account, "media-size-threshold", "0.1");
    loginWithOneContact();

    // Now with thumbnail and main file, both already downloaded
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageSticker>(makeSticker(320, 200, "",
            make_object<file>(
                fileId, 10000, 10000,
                make_object<localFile>("", true, true, false, false, 0, 0, 0),
                make_object<remoteFile>("beh", "bleh", false, true, 10000)
            )
        ,
            make_object<thumbnail>(
                make_object<thumbnailFormatJpeg>(),
                320, 200,
                make_object<file>(
                    thumbId, 100000000, 100000000,
                    make_object<localFile>("", true, true, false, false, 0, 0, 0),
                    make_object<remoteFile>("beh", "bleh", false, true, 100000000)
                )
            )), false)
    )));
    tgl.verifyRequest(downloadFile(fileId, 1, 0, 0, true));
    prpl.verifyNoEvents();

    runTimeouts();
    std::string tempFileName;
    prpl.verifyEvents(
        XferAcceptedEvent(purpleUserName(0), &tempFileName)
    );

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/sticker.tgs", true, true, true, false, 0, 0, 2000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));
    prpl.verifyEvents(
        XferStartEvent(tempFileName),
        XferProgressEvent(tempFileName, 2000)
    );

    tgl.reply(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/sticker.tgs", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));
    prpl.verifyEvents(
        XferCompletedEvent(tempFileName, TRUE, 10000),
        XferEndEvent(tempFileName)
    );
    ASSERT_FALSE(g_file_test(tempFileName.c_str(), G_FILE_TEST_EXISTS));
    tgl.verifyRequests({make_object<downloadFile>(thumbId, 1, 0, 0, true)});

    runTimeouts();
    prpl.verifyEvents(
        XferAcceptedEvent(purpleUserName(0), &tempFileName)
    );

    tgl.update(make_object<updateFile>(make_object<file>(
        thumbId, 100000000, 100000000,
        make_object<localFile>("/thumb", true, true, true, false, 0, 0, 2000),
        make_object<remoteFile>("beh", "bleh", false, true, 100000000)
    )));
    prpl.verifyEvents(
        XferStartEvent(tempFileName),
        XferProgressEvent(tempFileName, 2000)
    );

    tgl.reply(make_object<file>(
        fileId, 100000000, 100000000,
        make_object<localFile>("/thumb", true, true, false, true, 0, 100000000, 100000000),
        make_object<remoteFile>("beh", "bleh", false, true, 100000000)
    ));
    prpl.verifyEvents(
        XferCompletedEvent(tempFileName, TRUE, 100000000),
        XferEndEvent(tempFileName),
        ServGotImEvent(
            connection, purpleUserName(0),
            "<a href=\"file://file:///thumb\">sticker</a>",
            PURPLE_MESSAGE_RECV, date
        )
    );
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));

    ASSERT_FALSE(g_file_test(tempFileName.c_str(), G_FILE_TEST_EXISTS));
}

TEST_F(FileTransferTest, Photo_DownloadProgress_StuckAtStart)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 10000, 10000,
            make_object<localFile>("", true, true, false, false, 0, 0, 0),
            make_object<remoteFile>("beh", "bleh", false, true, 10000)
        ), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            make_object<photo>(false, nullptr, std::move(sizes)),
            make_object<formattedText>("photo", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    )));
    uint64_t downloadReqId = tgl.verifyRequest(
        downloadFile(fileId, 1, 0, 0, true)
    );
    prpl.verifyNoEvents();

    tgl.runTimeouts();
    std::string tempFileName;
    prpl.verifyEvents(
        XferAcceptedEvent(purpleUserName(0), &tempFileName),
        ServGotImEvent(connection, purpleUserName(0), "photo", PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + ": Downloading photo",
            PURPLE_MESSAGE_SYSTEM, date
        )
    );
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, true, false, 0, 0, 2000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));
    prpl.verifyEvents(
        XferStartEvent(tempFileName),
        XferProgressEvent(tempFileName, 2000)
    );

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, true, false, 0, 0, 5000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));
    prpl.verifyEvents(
        XferProgressEvent(tempFileName, 5000)
    );

    tgl.reply(downloadReqId, make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));
    ASSERT_FALSE(g_file_test(tempFileName.c_str(), G_FILE_TEST_EXISTS));
    prpl.verifyEvents(
        XferCompletedEvent(tempFileName, TRUE, 10000),
        XferEndEvent(tempFileName),
        ServGotImEvent(
            connection,
            purpleUserName(0),
            "<img src=\"file:///path\">",
            (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES),
            date
        )
    );

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));
}

TEST_F(FileTransferTest, Photo_DownloadProgress)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    const int32_t date2  = 10002;
    const int64_t messageId2 = 2;
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 10000, 10000,
            make_object<localFile>("", true, true, false, false, 0, 0, 0),
            make_object<remoteFile>("beh", "bleh", false, true, 10000)
        ), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            make_object<photo>(false, nullptr, std::move(sizes)),
            make_object<formattedText>("photo", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    )));
    uint64_t downloadReqId = tgl.verifyRequest(
        downloadFile(fileId, 1, 0, 0, true)
    );
    prpl.verifyNoEvents();

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, true, false, 0, 0, 2000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));

    tgl.runTimeouts();
    std::string tempFileName;
    prpl.verifyEvents(
        XferAcceptedEvent(purpleUserName(0), &tempFileName),
        XferStartEvent(&tempFileName),
        ServGotImEvent(connection, purpleUserName(0), "photo", PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + ": Downloading photo",
            PURPLE_MESSAGE_SYSTEM, date
        )
    );
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId2, userIds[0], chatIds[0], false, date2, makeTextMessage("followUp")
    )));
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId2}, nullptr, true));
    prpl.verifyEvents(ServGotImEvent(connection, purpleUserName(0), "followUp", PURPLE_MESSAGE_RECV, date2));

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, true, false, 0, 0, 5000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));

    prpl.verifyEvents(XferProgressEvent(tempFileName, 5000));

    tgl.reply(downloadReqId, make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));
    ASSERT_FALSE(g_file_test(tempFileName.c_str(), G_FILE_TEST_EXISTS));
    prpl.verifyEvents(
        XferCompletedEvent(tempFileName, TRUE, 10000),
        XferEndEvent(tempFileName),
        ServGotImEvent(
            connection,
            purpleUserName(0),
            "<img src=\"file:///path\">",
            (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES),
            date
        )
    );

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));
}

TEST_F(FileTransferTest, Photo_DownloadProgress_StuckAtStart_Cancel)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 10000, 10000,
            make_object<localFile>("", true, true, false, false, 0, 0, 0),
            make_object<remoteFile>("beh", "bleh", false, true, 10000)
        ), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            make_object<photo>(false, nullptr, std::move(sizes)),
            make_object<formattedText>("photo", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    )));
    uint64_t downloadFileReqId = tgl.verifyRequest(
        downloadFile(fileId, 1, 0, 0, true)
    );
    prpl.verifyNoEvents();

    tgl.runTimeouts();
    std::string tempFileName;
    prpl.verifyEvents(
        XferAcceptedEvent(purpleUserName(0), &tempFileName),
        ServGotImEvent(connection, purpleUserName(0), "photo", PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + ": Downloading photo",
            PURPLE_MESSAGE_SYSTEM, date
        )
    );
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));

    purple_xfer_cancel_local(prpl.getLastXfer());
    prpl.verifyEvents(XferLocalCancelEvent(tempFileName));
    tgl.reply(downloadFileReqId, make_object<error>(400, "Download cancelled"));
    ASSERT_FALSE(g_file_test(tempFileName.c_str(), G_FILE_TEST_EXISTS));
    tgl.verifyRequest(cancelDownloadFile(fileId, false));

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, false, false, 0, 1000, 1000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));
}

TEST_F(FileTransferTest, Photo_DownloadProgress_Cancel)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 10000, 10000,
            make_object<localFile>("", true, true, false, false, 0, 0, 0),
            make_object<remoteFile>("beh", "bleh", false, true, 10000)
        ), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            make_object<photo>(false, nullptr, std::move(sizes)),
            make_object<formattedText>("photo", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    )));
    auto downloadFileReqId = tgl.verifyRequest(
        downloadFile(fileId, 1, 0, 0, true)
    );
    prpl.verifyNoEvents();

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, true, false, 0, 0, 2000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));

    tgl.runTimeouts();
    std::string tempFileName;
    prpl.verifyEvents(
        XferAcceptedEvent(purpleUserName(0), &tempFileName),
        XferStartEvent(&tempFileName),
        ServGotImEvent(connection, purpleUserName(0), "photo", PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + ": Downloading photo",
            PURPLE_MESSAGE_SYSTEM, date
        )
    );
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, true, false, 0, 0, 5000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));

    prpl.verifyEvents(
        XferProgressEvent(tempFileName, 5000)
    );

    purple_xfer_cancel_local(prpl.getLastXfer());
    prpl.verifyEvents(XferLocalCancelEvent(tempFileName));
    tgl.reply(downloadFileReqId, make_object<error>(400, "Download cancelled"));
    ASSERT_FALSE(g_file_test(tempFileName.c_str(), G_FILE_TEST_EXISTS));
    tgl.verifyRequest(cancelDownloadFile(fileId, false));

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, false, false, 0, 6000, 6000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));
}

TEST_F(FileTransferTest, SendFileToNonContact)
{
    const char *const PATH   = "/path";
    const int32_t     fileId = 1234;
    setFakeFileSize(PATH, 10000);

    login();

    // Normaly users without chats have to be group chat members, but for the test it doesn't matter
    tgl.update(standardUpdateUser(0));

    // Send ﬁle successfully
    pluginInfo().send_file(
        connection,
        (userFirstNames[0] + " " + userLastNames[0]).c_str(),
        PATH
    );
    prpl.verifyEvents(
        XferAcceptedEvent(userFirstNames[0] + " " + userLastNames[0], PATH)
    );
    tgl.verifyRequest(createPrivateChat(userIds[0], false));

    tgl.update(standardPrivateChat(0));
    tgl.reply(makeChat(
        chatIds[0],
        make_object<chatTypePrivate>(userIds[0]),
        userFirstNames[0] + " " + userLastNames[0],
        nullptr, 0, 0, 0
    ));
    tgl.verifyRequest(preliminaryUploadFile(
        make_object<inputFileLocal>(PATH),
        make_object<fileTypeDocument>(),
        1
    ));

    tgl.reply(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(PATH, false, false, false, true, 0, 10000, 10000),
        make_object<remoteFile>("", "", true, false, 0)
    ));
    prpl.verifyEvents(
        XferStartEvent(PATH),
        XferProgressEvent(PATH, 0)
    );
    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(PATH, false, false, false, true, 0, 10000, 10000),
        make_object<remoteFile>("", "", false, false, 10000)
    )));
    prpl.verifyEvents(
        XferCompletedEvent(PATH, TRUE, 10000),
        XferEndEvent(PATH)
    );
    tgl.verifyRequest(*makeSendMessage(
        chatIds[0],
        0,
        nullptr,
        nullptr,
        nullptr,
        makeInputMessageDocument(
            make_object<inputFileId>(fileId),
            nullptr,
            false,
            make_object<formattedText>()
        )
    ));
    ASSERT_EQ(0, pluginInfo().send_im(
        connection,
        (userFirstNames[0] + " " + userLastNames[0]).c_str(),
        "message2",
        PURPLE_MESSAGE_SEND
    ));
    tgl.verifyRequest(*makeSendMessage(
        chatIds[0],
        0,
        nullptr,
        nullptr,
        nullptr,
        make_object<inputMessageText>(
            make_object<formattedText>("message2", std::vector<object_ptr<textEntity>>()),
            nullptr,
            false
        )
    ));
}

TEST_F(FileTransferTest, SendFileToNonContact_CreatePrivateChatFail)
{
    const char *const PATH   = "/path";
    setFakeFileSize(PATH, 10000);

    login();

    // Normaly users without chats have to be group chat members, but for the test it doesn't matter
    tgl.update(standardUpdateUser(1));

    pluginInfo().send_file(
        connection,
        (userFirstNames[1] + " " + userLastNames[1]).c_str(),
        PATH
    );
    prpl.verifyEvents(
        XferAcceptedEvent(userFirstNames[1] + " " + userLastNames[1], PATH)
    );
    tgl.verifyRequest(createPrivateChat(userIds[1], false));

    tgl.reply(make_object<error>(100, "error"));
    prpl.verifyEvents(XferLocalCancelEvent(PATH));
}

TEST_F(FileTransferTest, SendFileToNonContact_TurboCancel)
{
    const char *const PATH   = "/path";
    setFakeFileSize(PATH, 10000);

    login();

    // Normaly users without chats have to be group chat members, but for the test it doesn't matter
    tgl.update(standardUpdateUser(1));

    pluginInfo().send_file(
        connection,
        (userFirstNames[1] + " " + userLastNames[1]).c_str(),
        PATH
    );
    prpl.verifyEvents(
        XferAcceptedEvent(userFirstNames[1] + " " + userLastNames[1], PATH)
    );
    tgl.verifyRequest(createPrivateChat(userIds[1], false));

    purple_xfer_cancel_local(prpl.getLastXfer());
    prpl.discardEvents();

    tgl.update(standardPrivateChat(0));
    tgl.reply(makeChat(
        chatIds[0],
        make_object<chatTypePrivate>(userIds[0]),
        userFirstNames[0] + " " + userLastNames[0],
        nullptr, 0, 0, 0
    ));
}

TEST_F(FileTransferTest, ReceiveDocument_StandardTransfer_TinyFile)
{
    const int64_t messageId = 1;
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;
    uint8_t       data[]    = {1, 2, 3, 4, 5};
    const char *outputFileName = ".test_download";

    setUiName("spectrum"); // No longer pidgin - now downloads will use libpurple transfers
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageDocument>(
            make_object<document>(
                "doc.file.name", "mime/type", nullptr, nullptr,
                make_object<file>(
                    fileId, 10000, 10000,
                    make_object<localFile>("", true, true, false, false, 0, 0, 0),
                    make_object<remoteFile>("beh", "bleh", false, true, 10000)
                )
            ),
            make_object<formattedText>("document", std::vector<object_ptr<textEntity>>())
        )
    )));

    // Showing the caption creates the conversation, so the read receipt is sent now
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));

    prpl.verifyEvents(
        ServGotImEvent(connection, purpleUserName(0), "document", PURPLE_MESSAGE_RECV, date),
        XferRequestEvent(PURPLE_XFER_RECEIVE, purpleUserName(0).c_str(), "doc.file.name")
    );

    purple_xfer_request_accepted(prpl.getLastXfer(), outputFileName);
    prpl.verifyEvents(
        XferAcceptedEvent(purpleUserName(0), outputFileName),
        XferStartEvent(outputFileName)
    );

    tgl.verifyRequest(downloadFile(fileId, 1, 0, 0, true));

    char *tdlibFileName = NULL;
    int fd = g_file_open_tmp("tdlib_test_XXXXXX", &tdlibFileName, NULL);
    ASSERT_TRUE(fd >= 0);
    ASSERT_EQ((ssize_t)sizeof(data), write(fd, data, sizeof(data)));
    ::close(fd);

    tgl.reply(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(tdlibFileName, true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));
    tgl.update(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(tdlibFileName, true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));

    prpl.verifyEvents(
        XferWriteFileEvent(outputFileName, data, sizeof(data)),
        XferCompletedEvent(outputFileName, TRUE, sizeof(data)),
        XferEndEvent(outputFileName)
    );

    remove(tdlibFileName);
    g_free(tdlibFileName);
}

TEST_F(FileTransferTest, ReceiveDocument_StandardTransfer_Progress)
{
    const int64_t messageId = 1;
    const int32_t date      = 10001;
    const int32_t fileId    = 1234;
    uint8_t       data[]    = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const char *outputFileName = ".test_download";

    setUiName("spectrum"); // No longer pidgin - now downloads will use libpurple transfers
    loginWithOneContact();

    tgl.update(make_object<updateNewMessage>(makeMessage(
        messageId,
        userIds[0],
        chatIds[0],
        false,
        date,
        make_object<messageDocument>(
            make_object<document>(
                "doc.file.name", "mime/type", nullptr, nullptr,
                make_object<file>(
                    fileId, 10000, 10000,
                    make_object<localFile>("", true, true, false, false, 0, 0, 0),
                    make_object<remoteFile>("beh", "bleh", false, true, 10000)
                )
            ),
            make_object<formattedText>("document", std::vector<object_ptr<textEntity>>())
        )
    )));
    // Showing the caption creates the conversation, so the read receipt is sent now
    tgl.verifyRequest(viewMessages(chatIds[0], {messageId}, nullptr, true));
    prpl.verifyEvents(
        ServGotImEvent(connection, purpleUserName(0), "document", PURPLE_MESSAGE_RECV, date),
        XferRequestEvent(PURPLE_XFER_RECEIVE, purpleUserName(0).c_str(), "doc.file.name")
    );

    purple_xfer_request_accepted(prpl.getLastXfer(), outputFileName);
    prpl.verifyEvents(
        XferAcceptedEvent(purpleUserName(0), outputFileName),
        XferStartEvent(outputFileName)
    );

    tgl.verifyRequest(downloadFile(fileId, 1, 0, 0, true));

    char *tdlibFileName = NULL;
    int fd = g_file_open_tmp("tdlib_test_XXXXXX", &tdlibFileName, NULL);
    ASSERT_TRUE(fd >= 0);
    ASSERT_EQ((ssize_t)sizeof(data), write(fd, data, sizeof(data)));
    ::close(fd);

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(tdlibFileName, true, true, true, false, 0, 0, 2000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));
    prpl.verifyEvents(XferProgressEvent(outputFileName, 2000));

    tgl.reply(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(tdlibFileName, true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));
    tgl.update(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(tdlibFileName, true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));

    prpl.verifyEvents(
        XferWriteFileEvent(outputFileName, data, 10),
        XferWriteFileEvent(outputFileName, data+10, sizeof(data)-10),
        XferCompletedEvent(outputFileName, TRUE, sizeof(data)),
        XferEndEvent(outputFileName)
    );

    remove(tdlibFileName);
    g_free(tdlibFileName);
}

TEST_F(FileTransferTest, Photo_LongDownload_StartandDownloadsConfigured)
{
    purple_account_set_string(account, "download-behaviour", "file-transfer");
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 10000, 10000,
            make_object<localFile>("", true, true, false, false, 0, 0, 0),
            make_object<remoteFile>("beh", "bleh", false, true, 10000)
        ), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            make_object<photo>(false, nullptr, std::move(sizes)),
            make_object<formattedText>("photo", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    )));
    uint64_t downloadReqId = tgl.verifyRequest(
        downloadFile(fileId, 1, 0, 0, true)
    );
    prpl.verifyNoEvents();

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, true, false, 0, 0, 2000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));

    tgl.runTimeouts();
    prpl.verifyEvents(
        ServGotImEvent(connection, purpleUserName(0), "photo", PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + ": Downloading photo",
            PURPLE_MESSAGE_SYSTEM, date
        )
    );
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, true, false, 0, 0, 5000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));
    tgl.reply(downloadReqId, make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    ));
    prpl.verifyEvents(ServGotImEvent(
        connection,
        purpleUserName(0),
        "<img src=\"file:///path\">",
        (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_IMAGES),
        date
    ));
}

TEST_F(FileTransferTest, ActiveUploadAtLogout_BeforeUploadResponse)
{
    const char *const PATH = "/path";
    loginWithOneContact();

    setFakeFileSize(PATH, 9000);
    pluginInfo().send_file(connection, purpleUserName(0).c_str(), PATH);
    prpl.verifyEvents(XferAcceptedEvent(purpleUserName(0), PATH));
    tgl.verifyRequest(preliminaryUploadFile(
        make_object<inputFileLocal>(PATH),
        make_object<fileTypeDocument>(),
        1
    ));

    pluginInfo().close(connection);
    prpl.verifyEvents(XferLocalCancelEvent(PATH));
}

TEST_F(FileTransferTest, ActiveUploadAtLogout_AfterUploadResponse)
{
    const char *const PATH = "/path";
    const int32_t     fileId = 1234;
    loginWithOneContact();

    setFakeFileSize(PATH, 9000);
    pluginInfo().send_file(connection, purpleUserName(0).c_str(), PATH);
    prpl.verifyEvents(XferAcceptedEvent(purpleUserName(0), PATH));
    tgl.verifyRequest(preliminaryUploadFile(
        make_object<inputFileLocal>(PATH),
        make_object<fileTypeDocument>(),
        1
    ));

    tgl.reply(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>(PATH, false, false, false, true, 0, 10000, 10000),
        make_object<remoteFile>("", "", true, false, 0)
    ));
    prpl.verifyEvents(
        XferStartEvent(PATH),
        XferProgressEvent(PATH, 0)
    );
    pluginInfo().close(connection);
    prpl.verifyEvents(XferLocalCancelEvent(PATH));
}

TEST_F(FileTransferTest, ActiveUploadAtLogout_SendFileToNonContact_LogoutBeforeChatResponse)
{
    const char *const PATH   = "/path";
    setFakeFileSize(PATH, 10000);

    login();

    // Normaly users without chats have to be group chat members, but for the test it doesn't matter
    tgl.update(standardUpdateUser(0));

    // Send ﬁle successfully
    pluginInfo().send_file(
        connection,
        (userFirstNames[0] + " " + userLastNames[0]).c_str(),
        PATH
    );
    prpl.verifyEvents(
        XferAcceptedEvent(userFirstNames[0] + " " + userLastNames[0], PATH)
    );
    tgl.verifyRequest(createPrivateChat(userIds[0], false));

    pluginInfo().close(connection);
    prpl.verifyEvents(XferLocalCancelEvent(PATH));
}

TEST_F(FileTransferTest, ActiveDownloadAtLogout_StuckAtStart)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();

    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(make_object<file>(
            fileId, 10000, 10000,
            make_object<localFile>("", true, true, false, false, 0, 0, 0),
            make_object<remoteFile>("beh", "bleh", false, true, 10000)
        ), 640, 480));
    tgl.update(make_object<updateNewMessage>(makeMessage(
        1,
        userIds[0],
        chatIds[0],
        false,
        date,
        makeMessagePhoto(
            make_object<photo>(false, nullptr, std::move(sizes)),
            make_object<formattedText>("photo", std::vector<object_ptr<textEntity>>()),
            false, false, false
        )
    )));
    tgl.verifyRequest(downloadFile(fileId, 1, 0, 0, true));
    prpl.verifyNoEvents();

    tgl.update(make_object<updateFile>(make_object<file>(
        fileId, 10000, 10000,
        make_object<localFile>("/path", true, true, true, false, 0, 0, 2000),
        make_object<remoteFile>("beh", "bleh", false, true, 10000)
    )));

    tgl.runTimeouts();
    std::string tempFileName;
    prpl.verifyEvents(
        XferAcceptedEvent(purpleUserName(0), &tempFileName),
        XferStartEvent(&tempFileName),
        ServGotImEvent(connection, purpleUserName(0), "photo", PURPLE_MESSAGE_RECV, date),
        ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            userFirstNames[0] + " " + userLastNames[0] + ": Downloading photo",
            PURPLE_MESSAGE_SYSTEM, date
        )
    );
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));

    ASSERT_TRUE(g_file_test(tempFileName.c_str(), G_FILE_TEST_EXISTS));
    pluginInfo().close(connection);
    prpl.verifyEvents(XferLocalCancelEvent(tempFileName));
    ASSERT_FALSE(g_file_test(tempFileName.c_str(), G_FILE_TEST_EXISTS));
}

TEST_F(FileTransferTest, PhotoSizeWithinLimit)
{
    purple_account_set_string(account, "media-size-threshold", "0.5");
    login();

    // Listed with one above the limit first
    std::vector<object_ptr<photoSize>> sizes;
    sizes.push_back(makePhotoSize(makeFile(1, 700 * 1024, "", "big"), 1280, 960));
    sizes.push_back(makePhotoSize(makeFile(2, 100 * 1024, "", "small"), 320, 240));
    object_ptr<messagePhoto> content = makeMessagePhoto(makePhotoWithSizes(std::move(sizes)),
                                                        makeFormattedText(""), false, false, false);

    const file *selected = selectPhotoSize(account, *content);
    ASSERT_NE(nullptr, selected);
    ASSERT_EQ(2, selected->id_);
}

// What /paste reads with xclip outside Pidgin, attached to the message written after
class PasteTest: public CommTest {
protected:
    // xclip, answering for the clipboard's types as the script given for them does
    void paste(const char *targets, const char *uriList);
};

void PasteTest::paste(const char *targets, const char *uriList)
{
    gchar *dir = g_dir_make_tmp("paste-test-XXXXXX", nullptr);
    ASSERT_NE(nullptr, dir);
    std::string script = std::string(dir) + "/xclip";
    std::string text   = std::string("#!/bin/sh\n"
                                     "case \"$4\" in\n"
                                     "TARGETS) printf '") + targets + "';;\n"
                         "text/uri-list) printf '" + uriList + "';;\n"
                         "*) exit 1;;\n"
                         "esac\n";
    ASSERT_TRUE(g_file_set_contents(script.c_str(), text.c_str(), -1, nullptr));
    g_chmod(script.c_str(), 0755);
    std::string path = getenv("PATH") ? getenv("PATH") : "";
    setenv("PATH", (std::string(dir) + ":" + path).c_str(), 1);

    PurpleConversation *conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, purpleUserName(0).c_str(),
                                                                     account);
    if (!conv)
        conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, purpleUserName(0).c_str());
    prpl.discardEvents();
    prpl.runCommand("paste", conv, {});

    setenv("PATH", path.c_str(), 1);
    g_unlink(script.c_str());
    g_rmdir(dir);
    g_free(dir);
}

// Files copied, said by their placeholders; written into the message, they go as an album, captioned
// by what follows them, after the text before them
TEST_F(PasteTest, FilesAsAlbum)
{
    loginWithOneContact();
    gchar *dir = g_dir_make_tmp("paste-album-XXXXXX", nullptr);
    ASSERT_NE(nullptr, dir);
    std::string a = std::string(dir) + "/a.pdf";
    std::string b = std::string(dir) + "/b.pdf";
    for (const std::string &file: {a, b})
        ASSERT_TRUE(g_file_set_contents(file.c_str(), "%PDF-1.4\n", -1, nullptr));

    std::string list = "file://" + a + "\\r\\nfile://" + b + "\\r\\n";
    paste("TARGETS\\ntext/uri-list\\n", list.c_str());
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), "",
        "Pasted [a.pdf#1], [b.pdf#2]: write it into the message, where it goes, to send it there.",
        (PurpleMessageFlags)(PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG), 0
    ));

    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), "Here: [a.pdf#1]\n[b.pdf#2] Both",
                                      PURPLE_MESSAGE_SEND));
    auto album = make_object<sendMessageAlbum>();
    album->chat_id_ = chatIds[0];
    album->input_message_contents_.push_back(makeInputMessageDocument(make_object<inputFileLocal>(a),
                                                                      nullptr, false, make_object<formattedText>()));
    album->input_message_contents_.push_back(makeInputMessageDocument(
        make_object<inputFileLocal>(b), nullptr, false,
        make_object<formattedText>("Both", std::vector<object_ptr<textEntity>>())));
    tgl.verifyRequests({
        makeSendMessage(chatIds[0], 0, nullptr, nullptr, nullptr, make_object<inputMessageText>(
            make_object<formattedText>("Here: ", std::vector<object_ptr<textEntity>>()), nullptr, false)),
        std::move(album)
    });

    for (const std::string &file: {a, b})
        g_unlink(file.c_str());
    g_rmdir(dir);
    g_free(dir);
}

// A file pasted again keeps its number, and another of the same name gets one of its own
TEST_F(PasteTest, SameName)
{
    loginWithOneContact();
    paste("TARGETS\\ntext/uri-list\\n", "file:///path/a.pdf\\r\\n");
    prpl.discardEvents();
    paste("TARGETS\\ntext/uri-list\\n", "file:///path/a.pdf\\r\\nfile:///other/a.pdf\\r\\n");
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), "",
        "Pasted [a.pdf#1], [a.pdf#2]: write it into the message, where it goes, to send it there.",
        (PurpleMessageFlags)(PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG), 0
    ));
}

// Files go as what their content is, whatever their names say: a picture and a video together, as
// an album, and what is neither as a document, apart
TEST_F(PasteTest, KindsByContent)
{
    loginWithOneContact();
    gchar *dir = g_dir_make_tmp("paste-kinds-XXXXXX", nullptr);
    ASSERT_NE(nullptr, dir);
    std::string photo = std::string(dir) + "/photo.pdf";
    std::string video = std::string(dir) + "/movie.txt";
    std::string notes = std::string(dir) + "/notes.png";
    static const char png[] = "\x89PNG\r\n\x1a\n\0\0\0\rIHDR";
    static const char mp4[] = "\0\0\0\x20" "ftypisom\0\0\x02\0isomiso2avc1mp41";
    ASSERT_TRUE(g_file_set_contents(photo.c_str(), png, sizeof(png) - 1, nullptr));
    ASSERT_TRUE(g_file_set_contents(video.c_str(), mp4, sizeof(mp4) - 1, nullptr));
    ASSERT_TRUE(g_file_set_contents(notes.c_str(), "Notes, of no picture at all\n", -1, nullptr));

    std::string list = "file://" + photo + "\\r\\nfile://" + video + "\\r\\nfile://" + notes + "\\r\\n";
    paste("TARGETS\\ntext/uri-list\\n", list.c_str());
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), "",
        "Pasted [Image#1] (or [photo.pdf#1], as a file), [Video#2] (or [movie.txt#2], as a file), [notes.png#3]: "
        "write it into the message, where it goes, to send it there.",
        (PurpleMessageFlags)(PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG), 0
    ));

    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), "[image#1] [Video#2] [notes.png#3]",
                                      PURPLE_MESSAGE_SEND));
    auto album = make_object<sendMessageAlbum>();
    album->chat_id_ = chatIds[0];
    album->input_message_contents_.push_back(makeInputMessagePhoto(
        make_object<inputFileLocal>(photo), nullptr, std::vector<std::int32_t>(), 0, 0,
        make_object<formattedText>(), false, nullptr, false));
    album->input_message_contents_.push_back(makeInputMessageVideo(make_object<inputFileLocal>(video),
                                                                   make_object<formattedText>()));
    tgl.verifyRequests({
        std::move(album),
        makeSendMessage(chatIds[0], 0, nullptr, nullptr, nullptr,
                        makeInputMessageDocument(make_object<inputFileLocal>(notes), nullptr, false,
                                                 make_object<formattedText>()))
    });

    // By its name, the picture goes as a file, as it is
    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), "[photo.pdf#1]", PURPLE_MESSAGE_SEND));
    tgl.verifyRequest(*makeSendMessage(chatIds[0], 0, nullptr, nullptr, nullptr,
                                       makeInputMessageDocument(make_object<inputFileLocal>(photo), nullptr, false,
                                                                make_object<formattedText>())));

    // A file gone by the time the message is sent: nothing of it is
    g_unlink(notes.c_str());
    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), "Notes: [notes.png#3]",
                                      PURPLE_MESSAGE_SEND));
    tgl.verifyNoRequests();
    prpl.verifyEvents(ConversationWriteEvent(
        purpleUserName(0), purpleUserName(0),
        "The message is not sent: [notes.png#3] is gone from " + notes + ".",
        PURPLE_MESSAGE_SYSTEM, 0
    ));

    for (const std::string &file: {photo, video, notes})
        g_unlink(file.c_str());
    g_rmdir(dir);
    g_free(dir);
}

// A photo of ours, shown as it is sent, is uploaded: that is no edit, nor is it shown again
TEST_F(FileTransferTest, SentPhotoUploaded)
{
    const int32_t date   = 10001;
    const int32_t fileId = 1234;
    loginWithOneContact();

    auto makeContent = [fileId](const char *uniqueId, bool uploaded) {
        std::vector<object_ptr<photoSize>> sizes;
        sizes.push_back(makePhotoSize(make_object<file>(
                fileId, 10000, 10000,
                make_object<localFile>("/path", true, true, false, true, 0, 10000, 10000),
                make_object<remoteFile>(uploaded ? "beh" : "", uniqueId, !uploaded, uploaded, uploaded ? 10000 : 0)
            ), 640, 480));
        return makeMessagePhoto(make_object<photo>(false, nullptr, std::move(sizes)),
                                make_object<formattedText>("", std::vector<object_ptr<textEntity>>()),
                                false, false, false);
    };
    tgl.update(make_object<updateNewMessage>(makeMessage(1, selfId, chatIds[0], true, date, makeContent("", false))));
    prpl.discardEvents();
    tgl.verifyRequest(viewMessages(chatIds[0], {1}, nullptr, true));

    tgl.update(make_object<td::td_api::updateMessageContent>(chatIds[0], 1, makeContent("bleh", true)));
    prpl.verifyNoEvents();
    tgl.verifyNoRequests();
}

// A message's media, edited, goes by a placeholder, as pasted things do: kept, the caption alone is
// edited; another put in its place replaces it; taken out, or with more put in, the edit is refused
TEST_F(PasteTest, MediaEdited)
{
    loginWithOneContact();
    PurpleTdClient *client = PurpleTdClient::findConnected(("+" + selfPhoneNumber).c_str());
    ASSERT_NE(nullptr, client);
    const ChatId    chat    = ChatId::fromString(std::to_string(chatIds[0]).c_str());
    const MessageId message = MessageId::fromString("1");

    gchar *dir = g_dir_make_tmp("media-edit-XXXXXX", nullptr);
    ASSERT_NE(nullptr, dir);
    std::string photo = std::string(dir) + "/photo.jpg";
    std::string doc   = std::string(dir) + "/doc.pdf";
    std::string later = std::string(dir) + "/image-3.jpg";
    for (const std::string &file: {photo, doc, later})
        ASSERT_TRUE(g_file_set_contents(file.c_str(), "\xff\xd8\xff\xe0", 4, nullptr));
    std::string list = "file://" + doc + "\\r\\n";
    paste("TARGETS\\ntext/uri-list\\n", list.c_str());
    prpl.discardEvents();

    SentMedia media;
    media.present  = true;
    media.kind     = AttachmentKind::Photo;
    media.fileId   = 99;
    media.path     = photo;
    media.mimeType = "image/jpeg";
    auto edit = [&](const char *text) {
        client->askEditMessage(chat, message, "caption", "caption", true, 0, media);
        prpl.verifyEvents(RequestInputEvent(nullptr, account, NULL, NULL));
        prpl.inputEnter(text);
    };

    // The photo kept, which is #2, the document being #1
    edit("[Image#2] new caption");
    tgl.verifyRequest(editMessageCaption(chatIds[0], 1, nullptr, makeFormattedText("new caption"), false));
    tgl.reply(make_object<error>(400, "whatever"));
    prpl.discardEvents();

    // The document in its place
    edit("[doc.pdf#1] with the document");
    {
        auto request = make_object<editMessageMedia>();
        request->chat_id_    = chatIds[0];
        request->message_id_ = 1;
        request->input_message_content_ = makeInputMessageDocument(make_object<inputFileLocal>(doc), nullptr, false,
                                                                   makeFormattedText("with the document"));
        tgl.verifyRequest(*request);
    }

    // Neither the media taken out, nor more of it
    for (const char *text: {"just a caption", "[Image#2] [doc.pdf#1] both"}) {
        edit(text);
        tgl.verifyNoRequests();
        prpl.verifyEvents(ConversationWriteEvent(
            purpleUserName(0), purpleUserName(0),
            *text == 'j' ? "The message is left as it was: Telegram takes no picture, video or file out of a "
                           "message; it can be replaced, or the message deleted."
                         : "The message is left as it was: A message holds one picture, video or file, which can "
                           "be replaced, but no more.",
            PURPLE_MESSAGE_ERROR, 0));
    }

    // Media not here yet is fetched first: as a file of the name made for it, being the fetched one's
    SentMedia remote = media;
    remote.fileId = 100;
    remote.path   = "";
    client->askEditMessage(chat, MessageId::fromString("2"), "", "", true, 0, remote);
    prpl.verifyEvents(RequestInputEvent(nullptr, account, NULL, NULL));
    prpl.inputEnter("[image-3.jpg#3]");
    tgl.verifyRequest(downloadFile(100, 32, 0, 0, true));
    tgl.reply(::makeFile(100, 4, later));
    {
        auto request = make_object<editMessageMedia>();
        request->chat_id_    = chatIds[0];
        request->message_id_ = 2;
        request->input_message_content_ = makeInputMessageDocument(make_object<inputFileLocal>(later), nullptr, false,
                                                                   makeFormattedText(""));
        tgl.verifyRequest(*request);
    }

    for (const std::string &file: {photo, doc, later})
        g_unlink(file.c_str());
    g_rmdir(dir);
    g_free(dir);
}
