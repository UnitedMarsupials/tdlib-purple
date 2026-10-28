#include "fixture.h"
#include "receiving.h"
#include "client-utils.h"

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

// Pidgin's markup, as Telegram's formatting
TEST(FormattedText, FromMarkup)
{
    auto entities = [](const std::string &html) {
        object_ptr<formattedText> text = parseFormattedText(html);
        std::string result = text->text_;
        for (const auto &entity: text->entities_)
            result += " " + std::to_string(entity->offset_) + "+" + std::to_string(entity->length_) + ":" +
                      std::to_string(entity->type_->get_id());
        return result;
    };
    auto id = [](int32_t value) { return std::to_string(value); };

    // Nested and overlapping as Pidgin writes them; offsets in UTF-16 units, past the cat
    ASSERT_EQ("\U0001F63A bold italic 3+11:" + id(textEntityTypeBold::ID) + " 8+6:" + id(textEntityTypeItalic::ID),
              entities("\U0001F63A <b>bold <i>italic</i></b>"));
    // Line breaks, entities, styles of spans; font faces and colors dropped
    ASSERT_EQ("a < b\nc 6+1:" + id(textEntityTypeUnderline::ID) + " 6+1:" + id(textEntityTypeStrikethrough::ID),
              entities("<font color=\"#ff0000\">a &lt; b</font><br><span style=\"text-decoration: underline line-through;\">c</span>"));
    // A link with text of its own, and one that is its address
    object_ptr<formattedText> text = parseFormattedText("<a href=\"https://x.org/?a=1&amp;b=2\">site</a> "
                                                        "<a href=\"https://y.org\">https://y.org</a>");
    ASSERT_EQ("site https://y.org", text->text_);
    ASSERT_EQ(1u, text->entities_.size());
    ASSERT_EQ(textEntityTypeTextUrl::ID, text->entities_[0]->type_->get_id());
    ASSERT_EQ("https://x.org/?a=1&b=2", static_cast<const textEntityTypeTextUrl &>(*text->entities_[0]->type_).url_);

    // Pidgin writing it its own way is no change
    ASSERT_TRUE(isSameFormattedText(*parseFormattedText("<b>x</b> y"), *parseFormattedText("<B>x</B> y")));
    ASSERT_FALSE(isSameFormattedText(*parseFormattedText("<b>x</b> y"), *parseFormattedText("x y")));
}
