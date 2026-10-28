#include "fixture.h"
#include "receiving.h"

// Telegram's formatting, as markup Pidgin draws
TEST(FormattedText, Entities)
{
    auto format = [](const std::string &text, std::vector<object_ptr<textEntity>> entities) {
        return getMessageText(*make_object<formattedText>(text, std::move(entities)));
    };
    std::vector<object_ptr<textEntity>> list;

    // Nested, and ending together; markup characters escaped
    list.push_back(makeTextEntity(0, 8, make_object<textEntityTypeBold>()));
    list.push_back(makeTextEntity(5, 3, make_object<textEntityTypeItalic>()));
    ASSERT_EQ(std::string("<b>bold <i>&lt;it</i></b> plain"), format("bold <it plain", std::move(list)));

    // Offsets in UTF-16 units: the cat is two of them
    list.clear();
    list.push_back(makeTextEntity(3, 4, make_object<textEntityTypeStrikethrough>()));
    ASSERT_EQ(std::string("\U0001F63A <s>gone</s>"), format("\U0001F63A gone", std::move(list)));

    // A link with text of its own, code, a spoiler
    list.clear();
    list.push_back(makeTextEntity(0, 4, make_object<textEntityTypeTextUrl>("https://x.org/?a=<1>")));
    list.push_back(makeTextEntity(5, 2, make_object<textEntityTypeCode>()));
    list.push_back(makeTextEntity(8, 6, make_object<textEntityTypeSpoiler>()));
    ASSERT_EQ(std::string("<a href=\"https://x.org/?a=&lt;1&gt;\">site</a> <font face=\"monospace\">ls</font>"
                          " <font color=\"#808080\" back=\"#808080\">secret</font>"),
              format("site ls secret", std::move(list)));

    // A quote, each of its lines after a bar
    list.clear();
    list.push_back(makeTextEntity(5, 7, make_object<textEntityTypeBlockQuote>()));
    std::string bar = "<font color=\"#808080\">\xe2\x96\x8e</font> ";
    ASSERT_EQ("said\n" + bar + "one\n" + bar + "two\nend", format("said\none\ntwo\nend", std::move(list)));

    // Mentions and the like stay plain
    list.clear();
    list.push_back(makeTextEntity(0, 5, make_object<textEntityTypeMention>()));
    ASSERT_EQ(std::string("@user hi"), format("@user hi", std::move(list)));
}
