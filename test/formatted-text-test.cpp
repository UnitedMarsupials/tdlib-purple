#include "fixture.h"
#include "receiving.h"
#include "client-utils.h"
#include "log-history.h"

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

// Which message a logged line is
TEST(LogMarker, RoundTrip)
{
    std::string html = "Meow" + makeLogMarker(MessageId::fromString("35577135104"));
    ASSERT_EQ(std::string("Meow<!--tdlib-message 35577135104-->"), html);
    MessageId id;
    ASSERT_TRUE(takeLogMarker(html, &id));
    ASSERT_EQ(std::string("Meow"), html);
    ASSERT_EQ(35577135104, id.value());
    ASSERT_FALSE(takeLogMarker(html, &id));
}

TEST(LogOnly, Taken)
{
    std::string html = makeLogOnly("Meow: <b>new</b>");
    ASSERT_EQ(std::string("<!--tdlib-log-only-->Meow: <b>new</b><!--/tdlib-log-only-->"), html);
    ASSERT_TRUE(takeLogOnly(html));
    ASSERT_EQ(std::string(), html);

    html = "Meow" + makeLogOnly(": new") + " and" + makeLogOnly(" more");
    ASSERT_TRUE(takeLogOnly(html));
    ASSERT_EQ(std::string("Meow and"), html);
    ASSERT_FALSE(takeLogOnly(html));
}

TEST(LoggedLines, Parse)
{
    // As Pidgin's HTML logger writes them, after the line of the document's head, which reading
    // leaves out
    std::string content =
        "<span style=\"color: #A82F2F\"><span style=\"font-size: smaller\">(23:58:10)</span> <b>Ann:</b></span> Hi<!--tdlib-message 35577135104--><br>\n"
        "<span style=\"font-size: smaller\">(23:59:00)</span><b> <!--tdlib-log-only-->Ann edited the message from 23:58:10: Hello<!--/tdlib-log-only--></b><br>\n"
        "<span style=\"color: #16569E\"><span style=\"font-size: smaller\">(00:01:02)</span> <b>Me:</b></span> Bye<br>\n"
        "</p>\n"
        "</body>\n"
        "</html>\n";
    struct tm start = {};
    start.tm_year  = 2026 - 1900;
    start.tm_mon   = 9;
    start.tm_mday  = 28;
    start.tm_hour  = 23;
    start.tm_min   = 50;
    start.tm_isdst = -1;
    time_t logStart = mktime(&start);

    std::vector<LoggedLine> lines = parseLoggedLines(content, logStart);
    ASSERT_EQ(3u, lines.size());

    ASSERT_TRUE(lines[0].message);
    ASSERT_EQ(35577135104, lines[0].messageId.value());
    ASSERT_EQ(logStart + 8 * 60 + 10, lines[0].when);
    ASSERT_EQ(std::string("<span style=\"color: #A82F2F\"><span style=\"font-size: smaller\">(23:58:10)</span> <b>Ann:</b></span> Hi"),
              lines[0].html);
    ASSERT_EQ(std::string("Hi"), lines[0].html.substr(lines[0].bodyStart));

    // A notice, which the tab did not show but the log keeps, all of it
    ASSERT_FALSE(lines[1].message);
    ASSERT_FALSE(lines[1].messageId.valid());
    ASSERT_EQ(std::string("<span style=\"font-size: smaller\">(23:59:00)</span><b> Ann edited the message from 23:58:10: Hello</b>"),
              lines[1].html);

    // Past midnight, on the next day
    ASSERT_TRUE(lines[2].message);
    ASSERT_EQ(logStart + 11 * 60 + 2, lines[2].when);
    ASSERT_EQ(std::string("Bye"), lines[2].html.substr(lines[2].bodyStart));
}
