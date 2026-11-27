#include "rich-message.h"

#if TDLIB_API_RICH_MESSAGE

#include "receiving.h"
#include "client-utils.h"
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

// A link to where it may not go is shown as its text alone
static std::string linkTo(const std::string &url, const std::string &shown)
{
    std::string text = shown.empty() ? escapeForDisplay(url) : shown;
    return isLinkSchemeAllowed(url) ? "<a href=\"" + escapeForDisplay(url) + "\">" + text + "</a>" : text;
}

static std::string linkTo(const std::string &url, const td::td_api::RichText *text)
{
    return linkTo(url, formatRichText(text));
}

// The rich text inside one of type T
template<typename T>
static const td::td_api::RichText *getInnerText(const td::td_api::RichText &text)
{
    return static_cast<const T &>(text).text_.get();
}

// Those that are their text, in tags or as it is
namespace {
struct RichTextWrap {
    int32_t                       id;
    const td::td_api::RichText *(*inner)(const td::td_api::RichText &);
    const char                   *open;
    const char                   *close;
};
}
static const RichTextWrap RichTextWraps[] = {
    {td::td_api::richTextBold::ID,           getInnerText<td::td_api::richTextBold>,           "<b>",   "</b>"},
    {td::td_api::richTextItalic::ID,         getInnerText<td::td_api::richTextItalic>,         "<i>",   "</i>"},
    {td::td_api::richTextUnderline::ID,      getInnerText<td::td_api::richTextUnderline>,      "<u>",   "</u>"},
    {td::td_api::richTextStrikethrough::ID,  getInnerText<td::td_api::richTextStrikethrough>,  "<s>",   "</s>"},
    {td::td_api::richTextSubscript::ID,      getInnerText<td::td_api::richTextSubscript>,      "<sub>", "</sub>"},
    {td::td_api::richTextSuperscript::ID,    getInnerText<td::td_api::richTextSuperscript>,    "<sup>", "</sup>"},
    {td::td_api::richTextFixed::ID, getInnerText<td::td_api::richTextFixed>, "<font face=\"monospace\">", "</font>"},
    // Underlined: a background color of our choosing could be unreadable in the user's theme
    {td::td_api::richTextMarked::ID,         getInnerText<td::td_api::richTextMarked>,         "<u>",   "</u>"},
    // Hidden until selected, as a spoiler in an ordinary message is
    {td::td_api::richTextSpoiler::ID, getInnerText<td::td_api::richTextSpoiler>,
     "<font color=\"#808080\" back=\"#808080\">", "</font>"},
    {td::td_api::richTextDateTime::ID,       getInnerText<td::td_api::richTextDateTime>,       "", ""},
    {td::td_api::richTextMentionName::ID,    getInnerText<td::td_api::richTextMentionName>,    "", ""},
    {td::td_api::richTextHashtag::ID,        getInnerText<td::td_api::richTextHashtag>,        "", ""},
    {td::td_api::richTextCashtag::ID,        getInnerText<td::td_api::richTextCashtag>,        "", ""},
    {td::td_api::richTextBankCardNumber::ID, getInnerText<td::td_api::richTextBankCardNumber>, "", ""},
    {td::td_api::richTextBotCommand::ID,     getInnerText<td::td_api::richTextBotCommand>,     "", ""},
    {td::td_api::richTextReference::ID,      getInnerText<td::td_api::richTextReference>,      "", ""},
    {td::td_api::richTextReferenceLink::ID,  getInnerText<td::td_api::richTextReferenceLink>,  "", ""},
    {td::td_api::richTextAnchorLink::ID,     getInnerText<td::td_api::richTextAnchorLink>,     "", ""},
};

static std::string formatRichText(const td::td_api::RichText *text)
{
    if (!text)
        return std::string();
    for (const RichTextWrap &wrapped: RichTextWraps)
        if (wrapped.id == text->get_id())
            return wrap(wrapped.open, wrapped.inner(*text), wrapped.close);

    switch (text->get_id()) {
    case td::td_api::richTextPlain::ID:
        return escapeForDisplay(static_cast<const td::td_api::richTextPlain &>(*text).text_);
    case td::td_api::richTexts::ID: {
        std::string result;
        for (const auto &part: static_cast<const td::td_api::richTexts &>(*text).texts_)
            result += formatRichText(part.get());
        return result;
    }
    case td::td_api::richTextMention::ID: {
        const auto &mention = static_cast<const td::td_api::richTextMention &>(*text);
        return linkTo("https://t.me/" + mention.username_, mention.text_.get());
    }
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
    default:
        // An anchor or an icon, which show as nothing; or a kind newer than this
        return std::string();
    }
}

// Before a line within a list item, a quote or the like
static const char INDENT[] = "&nbsp;&nbsp;&nbsp;&nbsp;";

// In brackets, as in a quote of such a message
static std::string mediaPlaceholder(const char *kind)
{
    return escapeForDisplay(std::string("[") + kind + "]");
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

// Links to its media, numbered in the order they are shown, for the plugin to open when clicked
struct MediaLinks {
    const RichMediaLink  *link;
    const RichMediaImage *image = nullptr;
    size_t                next  = 0;
};

static void formatBlocks(const std::vector<td::td_api::object_ptr<td::td_api::PageBlock>> &blocks,
                         std::vector<std::string> &lines, MediaLinks &links);

static std::string formatHeading(const td::td_api::RichText *text, int size)
{
    std::string html = formatRichText(text);
    return html.empty() ? html : "<b><font size=\"" + std::to_string(size) + "\">" + html + "</font></b>";
}

// A medium's placeholder, linked to its file, where it can be
static std::string mediaItem(const char *kind, MediaLinks &links)
{
    // The picture itself, where it has been fetched
    std::string media = (links.image && *links.image) ? (*links.image)(links.next) : std::string();
    if (media.empty())
        media = mediaPlaceholder(kind);
    std::string url   = (links.link && *links.link) ? (*links.link)(links.next) : std::string();
    links.next++;
    return url.empty() ? media : "<a href=\"" + escapeForDisplay(url) + "\">" + media + "</a>";
}

static void formatBlock(const td::td_api::PageBlock &block, std::vector<std::string> &lines, MediaLinks &links)
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
            formatBlocks(item->blocks_, itemLines, links);
            // Its label, a number or a bullet, or a box ticked or not
            std::string label = item->has_checkbox_ ? (item->is_checked_ ? "\xe2\x98\x91" : "\xe2\x98\x90")
                                                    : escapeForDisplay(item->label_);
            for (size_t i = 0; i < itemLines.size(); i++)
                lines.push_back((i == 0) ? label + " " + itemLines[i] : INDENT + itemLines[i]);
        }
        break;
    case td::td_api::pageBlockBlockQuote::ID: {
        const auto              &quote = static_cast<const td::td_api::pageBlockBlockQuote &>(block);
        std::vector<std::string> quoted;
        formatBlocks(quote.blocks_, quoted, links);
        for (const std::string &line: quoted)
            lines.push_back(std::string(INDENT) + "<i>" + line + "</i>");
        std::string credit = formatRichText(quote.credit_.get());
        if (!credit.empty())
            lines.push_back(std::string(INDENT) + "\xe2\x80\x94 " + credit);
        break;
    }
    case td::td_api::pageBlockPullQuote::ID: {
        const auto &quote = static_cast<const td::td_api::pageBlockPullQuote &>(block);
        lines.push_back(std::string(INDENT) + "<i>" + formatRichText(quote.text_.get()) + "</i>");
        std::string credit = formatRichText(quote.credit_.get());
        if (!credit.empty())
            lines.push_back(std::string(INDENT) + "\xe2\x80\x94 " + credit);
        break;
    }
    case td::td_api::pageBlockPhoto::ID: {
        const auto &photo = static_cast<const td::td_api::pageBlockPhoto &>(block);
        // The picture, linked to; or where the sender links it to, if it cannot be
        std::string media = mediaItem(_("photo"), links);
        if ((media.compare(0, 2, "<a") != 0) && !photo.url_.empty())
            media = linkTo(photo.url_, media);
        lines.push_back(withCaption(media, photo.caption_.get()));
        break;
    }
    case td::td_api::pageBlockVideo::ID:
        lines.push_back(withCaption(mediaItem(_("video"), links),
                                    static_cast<const td::td_api::pageBlockVideo &>(block).caption_.get()));
        break;
    case td::td_api::pageBlockAnimation::ID:
        lines.push_back(withCaption(mediaItem(_("animation"), links),
                                    static_cast<const td::td_api::pageBlockAnimation &>(block).caption_.get()));
        break;
    case td::td_api::pageBlockAudio::ID:
        lines.push_back(withCaption(mediaItem(_("audio"), links),
                                    static_cast<const td::td_api::pageBlockAudio &>(block).caption_.get()));
        break;
    case td::td_api::pageBlockVoiceNote::ID:
        lines.push_back(withCaption(mediaItem(_("voice message"), links),
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
        formatBlocks(blocks, items, links);
        std::string row;
        for (const std::string &item: items)
            row += (row.empty() ? "" : " ") + item;
        lines.push_back(withCaption(row, caption));
        break;
    }
    case td::td_api::pageBlockCover::ID: {
        const auto &cover = static_cast<const td::td_api::pageBlockCover &>(block);
        if (cover.cover_)
            formatBlock(*cover.cover_, lines, links);
        break;
    }
    case td::td_api::pageBlockEmbedded::ID: {
        const auto &embedded = static_cast<const td::td_api::pageBlockEmbedded &>(block);
        std::string link     = embedded.url_.empty() ? std::string() : linkTo(embedded.url_, std::string());
        std::string text     = withCaption(link, embedded.caption_.get());
        if (!text.empty())
            lines.push_back(text);
        break;
    }
    case td::td_api::pageBlockEmbeddedPost::ID: {
        const auto &post = static_cast<const td::td_api::pageBlockEmbeddedPost &>(block);
        std::string author = escapeForDisplay(post.author_);
        if (!post.url_.empty())
            author = linkTo(post.url_, author);
        if (!author.empty())
            lines.push_back("<b>" + author + "</b>");
        std::vector<std::string> posted;
        formatBlocks(post.blocks_, posted, links);
        for (const std::string &line: posted)
            lines.push_back(INDENT + line);
        std::string caption = formatCaption(post.caption_.get());
        if (!caption.empty())
            lines.push_back(caption);
        break;
    }
    case td::td_api::pageBlockMap::ID:
        // TRANSLATOR: What a message is, as in "reacted with 👍 to location from 12:00".
        lines.push_back(withCaption(mediaPlaceholder(_("location")),
                                    static_cast<const td::td_api::pageBlockMap &>(block).caption_.get()));
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
        formatBlocks(details.blocks_, lines, links);
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
                lines.push_back(linkTo(article->url_, escapeForDisplay(article->title_)));
        break;
    }
    default:
        // An anchor, which shows as nothing; or a kind newer than this
        break;
    }
}

static void formatBlocks(const std::vector<td::td_api::object_ptr<td::td_api::PageBlock>> &blocks,
                         std::vector<std::string> &lines, MediaLinks &links)
{
    for (const auto &block: blocks)
        if (block)
            formatBlock(*block, lines, links);
}

std::string formatRichMessage(const td::td_api::richMessage &message, const RichMediaLink &link,
                              const RichMediaImage &image)
{
    std::vector<std::string> lines;
    MediaLinks               links{&link, &image};
    formatBlocks(message.blocks_, lines, links);
    std::string html;
    for (const std::string &line: lines)
        if (!line.empty())
            html += (html.empty() ? "" : "<br>") + line;
    return html;
}

// Its media in the order they are shown, as formatBlock numbers them
static const td::td_api::file *findMedia(const std::vector<td::td_api::object_ptr<td::td_api::PageBlock>> &blocks,
                                         size_t &index, std::string &description, const td::td_api::photo **photo,
                                         unsigned sizeLimitKb);

static const td::td_api::file *findMedia(const td::td_api::PageBlock &block, size_t &index, std::string &description,
                                         const td::td_api::photo **photo, unsigned sizeLimitKb)
{
    auto counted = [&index, &description](const td::td_api::file *file, const char *kind) -> const td::td_api::file * {
        if (index-- != 0)
            return nullptr;
        description = kind;
        return file;
    };
    switch (block.get_id()) {
    case td::td_api::pageBlockPhoto::ID: {
        const td::td_api::photo *shown = static_cast<const td::td_api::pageBlockPhoto &>(block).photo_.get();
        const td::td_api::file  *file  = counted(chooseShownPhotoSize(shown, 320, sizeLimitKb), _("photo"));
        if (file && photo)
            *photo = shown;
        return file;
    }
    case td::td_api::pageBlockVideo::ID: {
        const auto &video = static_cast<const td::td_api::pageBlockVideo &>(block).video_;
        return counted(video ? video->video_.get() : nullptr, _("video"));
    }
    case td::td_api::pageBlockAnimation::ID: {
        const auto &animation = static_cast<const td::td_api::pageBlockAnimation &>(block).animation_;
        return counted(animation ? animation->animation_.get() : nullptr, _("animation"));
    }
    case td::td_api::pageBlockAudio::ID: {
        const auto &audio = static_cast<const td::td_api::pageBlockAudio &>(block).audio_;
        return counted(audio ? audio->audio_.get() : nullptr, _("audio"));
    }
    case td::td_api::pageBlockVoiceNote::ID: {
        const auto &voiceNote = static_cast<const td::td_api::pageBlockVoiceNote &>(block).voice_note_;
        return counted(voiceNote ? voiceNote->voice_.get() : nullptr, _("voice message"));
    }
    case td::td_api::pageBlockList::ID:
        for (const auto &item: static_cast<const td::td_api::pageBlockList &>(block).items_) {
            const td::td_api::file *file = item ? findMedia(item->blocks_, index, description, photo, sizeLimitKb)
                                                : nullptr;
            if (file || (index == size_t(-1)))
                return file;
        }
        return nullptr;
    case td::td_api::pageBlockBlockQuote::ID:
        return findMedia(static_cast<const td::td_api::pageBlockBlockQuote &>(block).blocks_, index, description, photo,
                         sizeLimitKb);
    case td::td_api::pageBlockCollage::ID:
        return findMedia(static_cast<const td::td_api::pageBlockCollage &>(block).blocks_, index, description, photo,
                         sizeLimitKb);
    case td::td_api::pageBlockSlideshow::ID:
        return findMedia(static_cast<const td::td_api::pageBlockSlideshow &>(block).blocks_, index, description, photo,
                         sizeLimitKb);
    case td::td_api::pageBlockCover::ID: {
        const auto &cover = static_cast<const td::td_api::pageBlockCover &>(block).cover_;
        return cover ? findMedia(*cover, index, description, photo, sizeLimitKb) : nullptr;
    }
    case td::td_api::pageBlockEmbeddedPost::ID:
        return findMedia(static_cast<const td::td_api::pageBlockEmbeddedPost &>(block).blocks_, index, description, photo,
                         sizeLimitKb);
    case td::td_api::pageBlockDetails::ID:
        return findMedia(static_cast<const td::td_api::pageBlockDetails &>(block).blocks_, index, description, photo,
                         sizeLimitKb);
    default:
        return nullptr;
    }
}

// Counting down to the one wanted, past it once found: size_t(-1) then
static const td::td_api::file *findMedia(const std::vector<td::td_api::object_ptr<td::td_api::PageBlock>> &blocks,
                                         size_t &index, std::string &description, const td::td_api::photo **photo,
                                         unsigned sizeLimitKb)
{
    for (const auto &block: blocks) {
        const td::td_api::file *file = block ? findMedia(*block, index, description, photo, sizeLimitKb) : nullptr;
        if (file || (index == size_t(-1)))
            return file;
    }
    return nullptr;
}

const td::td_api::file *findRichMessageMedia(const td::td_api::richMessage &message, size_t index,
                                             std::string &description, const td::td_api::photo **photo,
                                             unsigned sizeLimitKb)
{
    return findMedia(message.blocks_, index, description, photo, sizeLimitKb);
}

std::string getRichMessageSummary(const td::td_api::richMessage &message)
{
    // The first block with words in it, as they are, without markup
    std::vector<std::string> lines;
    for (const auto &block: message.blocks_) {
        if (!block)
            continue;
        lines.clear();
        MediaLinks none{nullptr};
        formatBlock(*block, lines, none);
        for (const std::string &line: lines) {
            gchar      *plain = purple_markup_strip_html(line.c_str());
            gchar      *text  = purple_unescape_html(plain);
            std::string result = text ? text : "";
            g_free(text);
            g_free(plain);
            // A line of it, as a quote has room for
            result = result.substr(0, result.find('\n'));
            if (!result.empty())
                return result;
        }
    }
    return std::string();
}

#endif
