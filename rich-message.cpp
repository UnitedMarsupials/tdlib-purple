#include "rich-message.h"

#if TDLIB_API_RICH_MESSAGE

#include "receiving.h"
#include "translate.h"
#include <purple.h>
#include <algorithm>

// A rich message is laid out as an instant view page is: blocks of rich text, media among them.
// TDLib keeps adding kinds of either; one not known here shows as nothing, or its text if it has
// some to be found.

static std::string formatRichText(const td::td_api::RichText *text);

static std::string wrap(const char *open, const td::td_api::RichText *text, const char *close)
{
    return open + formatRichText(text) + close;
}

static std::string linkTo(const std::string &url, const td::td_api::RichText *text)
{
    std::string shown = formatRichText(text);
    return "<a href=\"" + escapeForDisplay(url) + "\">" + (shown.empty() ? escapeForDisplay(url) : shown) + "</a>";
}

static std::string formatRichText(const td::td_api::RichText *text)
{
    if (!text)
        return std::string();
    switch (text->get_id()) {
    case td::td_api::richTextPlain::ID:
        return escapeForDisplay(static_cast<const td::td_api::richTextPlain &>(*text).text_);
    case td::td_api::richTexts::ID: {
        std::string result;
        for (const auto &part: static_cast<const td::td_api::richTexts &>(*text).texts_)
            result += formatRichText(part.get());
        return result;
    }
    case td::td_api::richTextBold::ID:
        return wrap("<b>", static_cast<const td::td_api::richTextBold &>(*text).text_.get(), "</b>");
    case td::td_api::richTextItalic::ID:
        return wrap("<i>", static_cast<const td::td_api::richTextItalic &>(*text).text_.get(), "</i>");
    case td::td_api::richTextUnderline::ID:
        return wrap("<u>", static_cast<const td::td_api::richTextUnderline &>(*text).text_.get(), "</u>");
    case td::td_api::richTextStrikethrough::ID:
        return wrap("<s>", static_cast<const td::td_api::richTextStrikethrough &>(*text).text_.get(), "</s>");
    case td::td_api::richTextSubscript::ID:
        return wrap("<sub>", static_cast<const td::td_api::richTextSubscript &>(*text).text_.get(), "</sub>");
    case td::td_api::richTextSuperscript::ID:
        return wrap("<sup>", static_cast<const td::td_api::richTextSuperscript &>(*text).text_.get(), "</sup>");
    case td::td_api::richTextFixed::ID:
        return wrap("<font face=\"monospace\">", static_cast<const td::td_api::richTextFixed &>(*text).text_.get(),
                    "</font>");
    case td::td_api::richTextMarked::ID:
        return wrap("<u>", static_cast<const td::td_api::richTextMarked &>(*text).text_.get(), "</u>");
    case td::td_api::richTextSpoiler::ID:
        return formatRichText(static_cast<const td::td_api::richTextSpoiler &>(*text).text_.get());
    case td::td_api::richTextDateTime::ID:
        return formatRichText(static_cast<const td::td_api::richTextDateTime &>(*text).text_.get());
    case td::td_api::richTextMention::ID: {
        const auto &mention = static_cast<const td::td_api::richTextMention &>(*text);
        return linkTo("https://t.me/" + mention.username_, mention.text_.get());
    }
    case td::td_api::richTextMentionName::ID:
        return formatRichText(static_cast<const td::td_api::richTextMentionName &>(*text).text_.get());
    case td::td_api::richTextHashtag::ID:
        return formatRichText(static_cast<const td::td_api::richTextHashtag &>(*text).text_.get());
    case td::td_api::richTextCashtag::ID:
        return formatRichText(static_cast<const td::td_api::richTextCashtag &>(*text).text_.get());
    case td::td_api::richTextBankCardNumber::ID:
        return formatRichText(static_cast<const td::td_api::richTextBankCardNumber &>(*text).text_.get());
    case td::td_api::richTextBotCommand::ID:
        return formatRichText(static_cast<const td::td_api::richTextBotCommand &>(*text).text_.get());
    case td::td_api::richTextUrl::ID: {
        const auto &url = static_cast<const td::td_api::richTextUrl &>(*text);
        return linkTo(url.url_, url.text_.get());
    }
    case td::td_api::richTextEmailAddress::ID: {
        const auto &email = static_cast<const td::td_api::richTextEmailAddress &>(*text);
        return linkTo("mailto:" + email.email_address_, email.text_.get());
    }
    case td::td_api::richTextPhoneNumber::ID: {
        const auto &phone = static_cast<const td::td_api::richTextPhoneNumber &>(*text);
        return linkTo("tel:" + phone.phone_number_, phone.text_.get());
    }
    case td::td_api::richTextCustomEmoji::ID:
        return escapeForDisplay(static_cast<const td::td_api::richTextCustomEmoji &>(*text).alternative_text_);
    case td::td_api::richTextMathematicalExpression::ID:
        return "<font face=\"monospace\">" +
               escapeForDisplay(static_cast<const td::td_api::richTextMathematicalExpression &>(*text).expression_) +
               "</font>";
    case td::td_api::richTextReference::ID:
        return formatRichText(static_cast<const td::td_api::richTextReference &>(*text).text_.get());
    case td::td_api::richTextReferenceLink::ID:
        return formatRichText(static_cast<const td::td_api::richTextReferenceLink &>(*text).text_.get());
    case td::td_api::richTextAnchorLink::ID:
        return formatRichText(static_cast<const td::td_api::richTextAnchorLink &>(*text).text_.get());
    default:
        // An anchor or an icon, which show as nothing; or a kind newer than this
        return std::string();
    }
}

// In brackets, as in a quote of such a message
static std::string mediaPlaceholder(const char *kind)
{
    return std::string("[") + kind + "]";
}

static std::string formatCaption(const td::td_api::pageBlockCaption *caption)
{
    if (!caption)
        return std::string();
    std::string text   = formatRichText(caption->text_.get());
    std::string credit = formatRichText(caption->credit_.get());
    if (!credit.empty())
        text += (text.empty() ? "" : " ") + std::string("<i>") + credit + "</i>";
    return text;
}

static std::string withCaption(const std::string &media, const td::td_api::pageBlockCaption *caption)
{
    std::string text = formatCaption(caption);
    return text.empty() ? media : media + " " + text;
}

static void formatBlocks(const std::vector<td::td_api::object_ptr<td::td_api::PageBlock>> &blocks,
                         std::vector<std::string> &lines);

static std::string formatHeading(const td::td_api::RichText *text, int size)
{
    std::string html = formatRichText(text);
    return html.empty() ? html : "<b><font size=\"" + std::to_string(size) + "\">" + html + "</font></b>";
}

static void formatBlock(const td::td_api::PageBlock &block, std::vector<std::string> &lines)
{
    switch (block.get_id()) {
    case td::td_api::pageBlockTitle::ID:
        lines.push_back(formatHeading(static_cast<const td::td_api::pageBlockTitle &>(block).title_.get(), 5));
        break;
    case td::td_api::pageBlockHeader::ID:
        lines.push_back(formatHeading(static_cast<const td::td_api::pageBlockHeader &>(block).header_.get(), 4));
        break;
    case td::td_api::pageBlockSubheader::ID:
        lines.push_back(formatHeading(static_cast<const td::td_api::pageBlockSubheader &>(block).subheader_.get(), 3));
        break;
    case td::td_api::pageBlockSectionHeading::ID: {
        // The bigger the size, the lesser the heading, as with HTML's
        const auto &heading = static_cast<const td::td_api::pageBlockSectionHeading &>(block);
        lines.push_back(formatHeading(heading.text_.get(), std::max(3, 5 - heading.size_)));
        break;
    }
    case td::td_api::pageBlockSubtitle::ID:
        lines.push_back(wrap("<i>", static_cast<const td::td_api::pageBlockSubtitle &>(block).subtitle_.get(),
                             "</i>"));
        break;
    case td::td_api::pageBlockKicker::ID:
        lines.push_back(wrap("<font size=\"2\">", static_cast<const td::td_api::pageBlockKicker &>(block).kicker_.get(),
                             "</font>"));
        break;
    case td::td_api::pageBlockAuthorDate::ID: {
        const auto &authorDate = static_cast<const td::td_api::pageBlockAuthorDate &>(block);
        std::string text = formatRichText(authorDate.author_.get());
        if (authorDate.publish_date_)
            text += (text.empty() ? "" : ", ") + escapeForDisplay(formatMessageTime(authorDate.publish_date_));
        lines.push_back("<font size=\"2\">" + text + "</font>");
        break;
    }
    case td::td_api::pageBlockParagraph::ID:
        lines.push_back(formatRichText(static_cast<const td::td_api::pageBlockParagraph &>(block).text_.get()));
        break;
    case td::td_api::pageBlockThinking::ID:
        lines.push_back(wrap("<i>", static_cast<const td::td_api::pageBlockThinking &>(block).text_.get(), "</i>"));
        break;
    case td::td_api::pageBlockFooter::ID:
        lines.push_back(wrap("<font size=\"2\">", static_cast<const td::td_api::pageBlockFooter &>(block).footer_.get(),
                             "</font>"));
        break;
    case td::td_api::pageBlockPreformatted::ID:
        lines.push_back(wrap("<pre>", static_cast<const td::td_api::pageBlockPreformatted &>(block).text_.get(),
                             "</pre>"));
        break;
    case td::td_api::pageBlockMathematicalExpression::ID:
        lines.push_back("<font face=\"monospace\">" +
                        escapeForDisplay(static_cast<const td::td_api::pageBlockMathematicalExpression &>(block).expression_) +
                        "</font>");
        break;
    case td::td_api::pageBlockDivider::ID:
        lines.push_back("<hr>");
        break;
    case td::td_api::pageBlockList::ID:
        for (const auto &item: static_cast<const td::td_api::pageBlockList &>(block).items_) {
            if (!item)
                continue;
            std::vector<std::string> itemLines;
            formatBlocks(item->blocks_, itemLines);
            // Its label, a number or a bullet, or a box ticked or not
            std::string label = item->has_checkbox_ ? (item->is_checked_ ? "\xe2\x98\x91" : "\xe2\x98\x90")
                                                    : escapeForDisplay(item->label_);
            for (size_t i = 0; i < itemLines.size(); i++)
                lines.push_back((i == 0) ? label + " " + itemLines[i] : "&nbsp;&nbsp;&nbsp;&nbsp;" + itemLines[i]);
        }
        break;
    case td::td_api::pageBlockBlockQuote::ID: {
        const auto              &quote = static_cast<const td::td_api::pageBlockBlockQuote &>(block);
        std::vector<std::string> quoted;
        formatBlocks(quote.blocks_, quoted);
        for (const std::string &line: quoted)
            lines.push_back("&nbsp;&nbsp;&nbsp;&nbsp;<i>" + line + "</i>");
        std::string credit = formatRichText(quote.credit_.get());
        if (!credit.empty())
            lines.push_back("&nbsp;&nbsp;&nbsp;&nbsp;\xe2\x80\x94 " + credit);
        break;
    }
    case td::td_api::pageBlockPullQuote::ID: {
        const auto &quote = static_cast<const td::td_api::pageBlockPullQuote &>(block);
        lines.push_back("&nbsp;&nbsp;&nbsp;&nbsp;<i>" + formatRichText(quote.text_.get()) + "</i>");
        std::string credit = formatRichText(quote.credit_.get());
        if (!credit.empty())
            lines.push_back("&nbsp;&nbsp;&nbsp;&nbsp;\xe2\x80\x94 " + credit);
        break;
    }
    case td::td_api::pageBlockPhoto::ID: {
        const auto &photo = static_cast<const td::td_api::pageBlockPhoto &>(block);
        std::string media = mediaPlaceholder(_("photo"));
        if (!photo.url_.empty())
            media = "<a href=\"" + escapeForDisplay(photo.url_) + "\">" + media + "</a>";
        lines.push_back(withCaption(media, photo.caption_.get()));
        break;
    }
    case td::td_api::pageBlockVideo::ID:
        lines.push_back(withCaption(mediaPlaceholder(_("video")),
                                    static_cast<const td::td_api::pageBlockVideo &>(block).caption_.get()));
        break;
    case td::td_api::pageBlockAnimation::ID:
        lines.push_back(withCaption(mediaPlaceholder(_("animation")),
                                    static_cast<const td::td_api::pageBlockAnimation &>(block).caption_.get()));
        break;
    case td::td_api::pageBlockAudio::ID:
        lines.push_back(withCaption(mediaPlaceholder(_("audio")),
                                    static_cast<const td::td_api::pageBlockAudio &>(block).caption_.get()));
        break;
    case td::td_api::pageBlockVoiceNote::ID:
        lines.push_back(withCaption(mediaPlaceholder(_("voice message")),
                                    static_cast<const td::td_api::pageBlockVoiceNote &>(block).caption_.get()));
        break;
    case td::td_api::pageBlockCollage::ID:
    case td::td_api::pageBlockSlideshow::ID: {
        // Its pictures in a row, then its caption
        const bool collage = (block.get_id() == td::td_api::pageBlockCollage::ID);
        const auto &blocks = collage ? static_cast<const td::td_api::pageBlockCollage &>(block).blocks_
                                     : static_cast<const td::td_api::pageBlockSlideshow &>(block).blocks_;
        const auto *caption = collage ? static_cast<const td::td_api::pageBlockCollage &>(block).caption_.get()
                                      : static_cast<const td::td_api::pageBlockSlideshow &>(block).caption_.get();
        std::vector<std::string> items;
        formatBlocks(blocks, items);
        std::string row;
        for (const std::string &item: items)
            row += (row.empty() ? "" : " ") + item;
        lines.push_back(withCaption(row, caption));
        break;
    }
    case td::td_api::pageBlockCover::ID: {
        const auto &cover = static_cast<const td::td_api::pageBlockCover &>(block);
        if (cover.cover_)
            formatBlock(*cover.cover_, lines);
        break;
    }
    case td::td_api::pageBlockEmbedded::ID: {
        const auto &embedded = static_cast<const td::td_api::pageBlockEmbedded &>(block);
        std::string link     = embedded.url_.empty() ? std::string()
                                                     : "<a href=\"" + escapeForDisplay(embedded.url_) + "\">" +
                                                       escapeForDisplay(embedded.url_) + "</a>";
        std::string text     = withCaption(link, embedded.caption_.get());
        if (!text.empty())
            lines.push_back(text);
        break;
    }
    case td::td_api::pageBlockEmbeddedPost::ID: {
        const auto &post = static_cast<const td::td_api::pageBlockEmbeddedPost &>(block);
        std::string author = escapeForDisplay(post.author_);
        if (!post.url_.empty())
            author = "<a href=\"" + escapeForDisplay(post.url_) + "\">" + (author.empty() ? escapeForDisplay(post.url_)
                                                                                         : author) + "</a>";
        if (!author.empty())
            lines.push_back("<b>" + author + "</b>");
        std::vector<std::string> posted;
        formatBlocks(post.blocks_, posted);
        for (const std::string &line: posted)
            lines.push_back("&nbsp;&nbsp;&nbsp;&nbsp;" + line);
        std::string caption = formatCaption(post.caption_.get());
        if (!caption.empty())
            lines.push_back(caption);
        break;
    }
    case td::td_api::pageBlockMap::ID:
        lines.push_back(withCaption(_("[location]"), static_cast<const td::td_api::pageBlockMap &>(block).caption_.get()));
        break;
    case td::td_api::pageBlockChatLink::ID: {
        const auto &chatLink = static_cast<const td::td_api::pageBlockChatLink &>(block);
        std::string title    = escapeForDisplay(chatLink.title_);
        lines.push_back(chatLink.username_.empty() ? title
                                                   : "<a href=\"https://t.me/" + escapeForDisplay(chatLink.username_) +
                                                     "\">" + (title.empty() ? "@" + escapeForDisplay(chatLink.username_)
                                                                           : title) + "</a>");
        break;
    }
    case td::td_api::pageBlockDetails::ID: {
        const auto &details = static_cast<const td::td_api::pageBlockDetails &>(block);
        std::string header  = formatRichText(details.header_.get());
        if (!header.empty())
            lines.push_back("<b>" + header + "</b>");
        formatBlocks(details.blocks_, lines);
        break;
    }
    case td::td_api::pageBlockTable::ID: {
        // A row a line, its cells apart; a header's bold
        const auto &table   = static_cast<const td::td_api::pageBlockTable &>(block);
        std::string caption = formatRichText(table.caption_.get());
        if (!caption.empty())
            lines.push_back("<b>" + caption + "</b>");
        for (const auto &row: table.cells_) {
            std::string line;
            for (size_t i = 0; i < row.size(); i++) {
                std::string cell = row[i] ? formatRichText(row[i]->text_.get()) : std::string();
                if (row[i] && row[i]->is_header_ && !cell.empty())
                    cell = "<b>" + cell + "</b>";
                line += (i ? " | " : "") + cell;
            }
            lines.push_back(line);
        }
        break;
    }
    case td::td_api::pageBlockRelatedArticles::ID: {
        const auto &related = static_cast<const td::td_api::pageBlockRelatedArticles &>(block);
        std::string header  = formatRichText(related.header_.get());
        if (!header.empty())
            lines.push_back("<b>" + header + "</b>");
        for (const auto &article: related.articles_)
            if (article && !article->url_.empty())
                lines.push_back("<a href=\"" + escapeForDisplay(article->url_) + "\">" +
                                escapeForDisplay(article->title_.empty() ? article->url_ : article->title_) + "</a>");
        break;
    }
    default:
        // An anchor, which shows as nothing; or a kind newer than this
        break;
    }
}

static void formatBlocks(const std::vector<td::td_api::object_ptr<td::td_api::PageBlock>> &blocks,
                         std::vector<std::string> &lines)
{
    for (const auto &block: blocks)
        if (block)
            formatBlock(*block, lines);
}

std::string formatRichMessage(const td::td_api::richMessage &message)
{
    std::vector<std::string> lines;
    formatBlocks(message.blocks_, lines);
    std::string html;
    for (const std::string &line: lines)
        if (!line.empty())
            html += (html.empty() ? "" : "<br>") + line;
    return html;
}

std::string getRichMessageSummary(const td::td_api::richMessage &message)
{
    // The first block with words in it, as they are, without markup
    std::vector<std::string> lines;
    for (const auto &block: message.blocks_) {
        if (!block)
            continue;
        lines.clear();
        formatBlock(*block, lines);
        for (const std::string &line: lines) {
            gchar      *plain = purple_markup_strip_html(line.c_str());
            gchar      *text  = purple_unescape_html(plain);
            std::string result = text ? text : "";
            g_free(text);
            g_free(plain);
            if (!result.empty())
                return result;
        }
    }
    return std::string();
}

#endif
