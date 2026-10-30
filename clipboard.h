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

// What was pasted into a message being written, shown there as its placeholder, which the message,
// once sent, has the attachment in place of
enum class AttachmentKind { Photo, Video, Document };
struct PastedAttachment {
    std::string    placeholder; // brackets and all: [Image#1], [Video#2], [report.pdf]
    std::string    asFile;      // of a picture or video, its name, as [holiday.jpg], to send it as a document
    AttachmentKind kind;
    std::string    path;
    bool           temporary;   // a picture of the clipboard's, saved for it, removed with it

    PastedAttachment(std::string placeholder, std::string asFile, AttachmentKind kind, std::string path,
                     bool temporary)
    : placeholder(std::move(placeholder)), asFile(std::move(asFile)), kind(kind), path(std::move(path)),
      temporary(temporary) {}
    ~PastedAttachment();
    PastedAttachment(const PastedAttachment &) = delete;
    PastedAttachment &operator=(const PastedAttachment &) = delete;
};
using PastedAttachments = std::list<PastedAttachment>;

// By the file's content, as shared-mime-info tells, never its name: pictures Telegram takes as photos,
// and videos, as such, anything else, or what cannot be told, as a document
AttachmentKind guessAttachmentKind(const std::string &path);
// The placeholder of the file, which goes with it from then on, a file pasted again keeping its own
const std::string &addPastedFile(PastedAttachments &attachments, const std::string &path);
// The placeholder of a picture of the image store, saved to a file of its own; empty if it cannot be
const std::string &addPastedPicture(PastedAttachments &attachments, int imageId);
// What the placeholder, of either form, stands for, if anything; whether it is the form for a document
const PastedAttachment *findPasted(const PastedAttachments &attachments, const std::string &placeholder,
                                   bool &asFile);
// The placeholders of what is on the clipboard: its files, or its picture, the image store's reference
// let go
std::vector<std::string> addPasted(PastedAttachments &attachments, const ClipboardContent &content);

// The local paths of the URIs, those of other kinds left out
std::vector<std::string> getLocalPaths(char **uris);
// The same of a text/uri-list
std::vector<std::string> parseUriList(const std::string &list);

// The X clipboard, read with xclip for front-ends that cannot read it themselves; false, with what
// failed, if it holds neither files nor a picture
bool readXclip(ClipboardContent &content, std::string &error);

#endif
