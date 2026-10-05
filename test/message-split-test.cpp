#include "fixture.h"
#include "libpurple-mock.h"
#include <fmt/format.h>

class MessageSplitTest: public CommTest {};

TEST_F(MessageSplitTest, SplitCaption)
{
    loginWithOneContact();
    tgl.update(make_object<updateOption>(
        "message_caption_length_max",
        make_object<optionValueInteger>(9)
    ));

    uint8_t data1[] = {1, 2, 3, 4, 5};
    const int id1 = purple_imgstore_add_with_id(arrayDup(data1, sizeof(data1)), sizeof(data1), "filename1");
    std::string messageText = fmt::format("<img id=\"{}\">\n\n234567890", id1);
    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), messageText.c_str(), PURPLE_MESSAGE_SEND));

    tgl.verifyRequests({
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, makeInputMessagePhoto(
                make_object<inputFileLocal>(),
                nullptr, std::vector<std::int32_t>(), 0, 0,
                make_object<formattedText>("", std::vector<object_ptr<textEntity>>()),
                false, nullptr, false
            )
        ),
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, make_object<inputMessageText>(
                make_object<formattedText>("234567890", std::vector<object_ptr<textEntity>>()),
                nullptr, false
            )
        )
    });

    messageText = fmt::format("<img id=\"{}\">1\n123456789", id1);
    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), messageText.c_str(), PURPLE_MESSAGE_SEND));
    tgl.verifyRequests({
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, makeInputMessagePhoto(
                make_object<inputFileLocal>(),
                nullptr, std::vector<std::int32_t>(), 0, 0,
                make_object<formattedText>("1", std::vector<object_ptr<textEntity>>()),
                false, nullptr, false
            )
        ),
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, make_object<inputMessageText>(
                make_object<formattedText>("123456789", std::vector<object_ptr<textEntity>>()),
                nullptr, false
            )
        )
    });
}

TEST_F(MessageSplitTest, SplitCaptionAndText)
{
    loginWithOneContact();
    tgl.update(make_object<updateOption>(
        "message_caption_length_max",
        make_object<optionValueInteger>(9)
    ));
    tgl.update(make_object<updateOption>(
        "message_text_length_max",
        make_object<optionValueInteger>(19)
    ));

    uint8_t data1[] = {1, 2, 3, 4, 5};
    const int id1 = purple_imgstore_add_with_id(arrayDup(data1, sizeof(data1)), sizeof(data1), "filename1");
    std::string messageText = fmt::format("<img id=\"{}\">12345678912345678901234567890", id1);
    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), messageText.c_str(), PURPLE_MESSAGE_SEND));

    tgl.verifyRequests({
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, makeInputMessagePhoto(
                make_object<inputFileLocal>(),
                nullptr, std::vector<std::int32_t>(), 0, 0,
                make_object<formattedText>("123456789", std::vector<object_ptr<textEntity>>()),
                false, nullptr, false
            )
        ),
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, make_object<inputMessageText>(
                make_object<formattedText>("1234567890123456789", std::vector<object_ptr<textEntity>>()),
                nullptr, false
            )
        ),
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, make_object<inputMessageText>(
                make_object<formattedText>("0", std::vector<object_ptr<textEntity>>()),
                nullptr, false
            )
        )
    });
}

TEST_F(MessageSplitTest, SplitCaption_Utf8)
{
    loginWithOneContact();
    tgl.update(make_object<updateOption>(
        "message_caption_length_max",
        make_object<optionValueInteger>(10)
    ));

    uint8_t data1[] = {1, 2, 3, 4, 5};
    const int id1 = purple_imgstore_add_with_id(arrayDup(data1, sizeof(data1)), sizeof(data1), "filename1");
    std::string messageText = fmt::format("<img id=\"{}\">😃😃😃", id1);
    ASSERT_EQ(0, pluginInfo().send_im(connection, purpleUserName(0).c_str(), messageText.c_str(), PURPLE_MESSAGE_SEND));

    tgl.verifyRequests({
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, makeInputMessagePhoto(
                make_object<inputFileLocal>(),
                nullptr, std::vector<std::int32_t>(), 0, 0,
                // 8 bytes (limit is 9)
                make_object<formattedText>("😃😃", std::vector<object_ptr<textEntity>>()),
                false, nullptr, false
            )
        ),
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, make_object<inputMessageText>(
                make_object<formattedText>("😃", std::vector<object_ptr<textEntity>>()),
                nullptr, false
            )
        )
    });
}

TEST_F(MessageSplitTest, SplitText_Utf8)
{
    loginWithOneContact();
    tgl.update(make_object<updateOption>(
        "message_text_length_max",
        make_object<optionValueInteger>(10)
    ));

    ASSERT_EQ(0, pluginInfo().send_im(
        connection,
        purpleUserName(0).c_str(),
        "😃😃😃😃😃",
        PURPLE_MESSAGE_SEND
    ));

    tgl.verifyRequests({
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, make_object<inputMessageText>(
                make_object<formattedText>("😃😃", std::vector<object_ptr<textEntity>>()),
                nullptr, false
            )
        ),
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, make_object<inputMessageText>(
                make_object<formattedText>("😃😃", std::vector<object_ptr<textEntity>>()),
                nullptr, false
            )
        ),
        makeSendMessage(
            chatIds[0],
            0,
            nullptr,
            nullptr,
            nullptr, make_object<inputMessageText>(
                make_object<formattedText>("😃", std::vector<object_ptr<textEntity>>()),
                nullptr, false
            )
        )
    });
}
