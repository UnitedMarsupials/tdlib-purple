#ifndef _CLIPBOARD_H
#define _CLIPBOARD_H

#include <list>
#include <string>
#include <vector>

// What is pasted: files copied, as a file manager copies them, or else a picture
struct ClipboardContent {
    std::vector<std::string> files;       // local paths
    int                      imageId = 0; // in the image store, the reference the holder's
};

// What was pasted into a message being written: its content, fixed then, though a file is only
// referenced, numbered in one sequence, and written into the message by placeholders. [Image#N]
// and [Video#N] send a picture or video as such, nameless; [name#N] sends what is numbered N as a
// file of that name, its own to begin with, or one made for it.
enum class AttachmentKind { Photo, Video, Document };
struct PastedAttachment {
    unsigned       number;
    AttachmentKind kind;      // of its content: a picture or video can go as such
    std::string    path;
    std::string    name;      // as a file
    bool           temporary; // a picture of the clipboard's, saved for it, removed with it

    PastedAttachment(unsigned number, AttachmentKind kind, std::string path, std::string name, bool temporary)
    : number(number), kind(kind), path(std::move(path)), name(std::move(name)), temporary(temporary) {}
    ~PastedAttachment();
    PastedAttachment(const PastedAttachment &) = delete;
    PastedAttachment &operator=(const PastedAttachment &) = delete;

    // As pasted: a picture or video as such, anything else as a file
    std::string placeholder() const;
    std::string filePlaceholder() const;
};

struct PastedAttachments {
    std::list<PastedAttachment> items;
    unsigned                    lastNumber = 0;
    // Of links to files, by the names they are sent under, removed with the rest
    std::vector<std::string>    linkDirectories;

    PastedAttachments() = default;
    ~PastedAttachments();
    PastedAttachments(const PastedAttachments &) = delete;
    PastedAttachments &operator=(const PastedAttachments &) = delete;
};

// What a placeholder in a message stands for, and how it is to go
struct PlaceholderUse {
    const PastedAttachment *attachment = nullptr;
    bool                    asFile     = false;
    std::string             name;      // as a file
};

// By the file's content, as shared-mime-info tells, never its name: pictures Telegram takes as photos,
// and videos, as such, anything else, or what cannot be told, as a document
AttachmentKind guessAttachmentKind(const std::string &path);
// A name for content that has none: its kind, number and, from its MIME type, an extension, such as
// image-1.png
std::string makeAttachmentName(AttachmentKind kind, unsigned number, const std::string &mimeType);
// The file, numbered, a file pasted again keeping its number
const PastedAttachment &addPastedFile(PastedAttachments &attachments, const std::string &path);
// A picture of the image store, saved to a file of its own; none if it cannot be
const PastedAttachment *addPastedPicture(PastedAttachments &attachments, int imageId);
// What is on the clipboard: its files, or its picture, the image store's reference let go
std::vector<const PastedAttachment *> addPasted(PastedAttachments &attachments, const ClipboardContent &content);
// The bracketed text as a placeholder, label and number, the label Image or Video in any letter
// case, anything else a file's name; nothing if it is none
PlaceholderUse findPasted(const PastedAttachments &attachments, const std::string &placeholder);
// The file to upload: the pasted one, or a link to it, of the name it is to go under; empty if it
// is gone, or the link cannot be made
std::string getUploadPath(PastedAttachments &attachments, const PlaceholderUse &use);

// The local paths of the URIs, those of other kinds left out
std::vector<std::string> getLocalPaths(char **uris);
// The same of a text/uri-list
std::vector<std::string> parseUriList(const std::string &list);

// The X clipboard, read with xclip for front-ends that cannot read it themselves; false, with what
// failed, if it holds neither files nor a picture
bool readXclip(ClipboardContent &content, std::string &error);

#endif
