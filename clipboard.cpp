#include "clipboard.h"
#include "config.h"
#include "format.h"
#include "file-transfer.h"
#include "translate.h"
#include <purple.h>
#include <gio/gio.h>
#include <string.h>
#include <vector>
#ifndef _WIN32
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static const char XCLIP[] = "xclip";

PastedAttachment::~PastedAttachment()
{
    if (temporary)
        remove(path.c_str());
}

std::string guessMimeType(const std::string &path)
{
    enum { SNIFFED_LENGTH = 4096 };
    guchar  data[SNIFFED_LENGTH];
    gsize   length = 0;
    FILE   *file   = fopen(path.c_str(), "rb");
    if (file) {
        length = fread(data, 1, sizeof(data), file);
        fclose(file);
    }
    // By its content alone, its magic as shared-mime-info knows it: GIO, given the name too, would go by
    // that whenever it fits one type
    gboolean    uncertain = TRUE;
    gchar      *type      = length ? g_content_type_guess(nullptr, data, length, &uncertain) : nullptr;
    gchar      *mime      = (type && !uncertain) ? g_content_type_get_mime_type(type) : nullptr;
    std::string result    = mime ? mime : "";
    purple_debug_misc(config::pluginId, "%s is %s\n", path.c_str(), mime ? mime : "of no known type");
    g_free(mime);
    g_free(type);
    return result;
}

AttachmentKind guessAttachmentKind(const std::string &path)
{
    std::string mime = guessMimeType(path);
    // Pictures Telegram takes as photos; others, such as GIF or SVG, go as they are
    static const char *const photos[] = {"image/jpeg", "image/png", "image/webp", "image/bmp"};
    AttachmentKind kind = AttachmentKind::Document;
    for (const char *photo: photos)
        if (mime == photo)
            kind = AttachmentKind::Photo;
    if (g_str_has_prefix(mime.c_str(), "video/"))
        kind = AttachmentKind::Video;
    return kind;
}

static const char IMAGE_LABEL[] = "Image";
static const char VIDEO_LABEL[] = "Video";

std::string PastedAttachment::placeholder() const
{
    if (kind == AttachmentKind::Photo)
        return std::string("[") + IMAGE_LABEL + "#" + std::to_string(number) + "]";
    if (kind == AttachmentKind::Video)
        return std::string("[") + VIDEO_LABEL + "#" + std::to_string(number) + "]";
    return filePlaceholder();
}

std::string PastedAttachment::filePlaceholder() const
{
    return "[" + name + "#" + std::to_string(number) + "]";
}

PastedAttachments::~PastedAttachments()
{
    for (const std::string &directory: linkDirectories) {
        GDir *dir = g_dir_open(directory.c_str(), 0, nullptr);
        if (dir) {
            while (const gchar *entry = g_dir_read_name(dir))
                remove((directory + G_DIR_SEPARATOR_S + entry).c_str());
            g_dir_close(dir);
        }
        remove(directory.c_str());
    }
}

std::string makeAttachmentName(AttachmentKind kind, unsigned number, const std::string &mimeType)
{
    std::string name = (kind == AttachmentKind::Photo) ? "image" : (kind == AttachmentKind::Video) ? "video"
                                                                                                  : "document";
    name += "-" + std::to_string(number);
    // GIO tells no extension for a type: the subtype gives one, as image/svg+xml does svg, unless it
    // is a vendor's, or the like
    size_t slash = mimeType.find('/');
    if (slash == std::string::npos)
        return name;
    std::string subtype = mimeType.substr(slash + 1);
    subtype = subtype.substr(0, subtype.find('+'));
    if (g_str_has_prefix(subtype.c_str(), "x-"))
        subtype.erase(0, 2);
    if (!subtype.empty() && (subtype.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789") == std::string::npos))
        name += "." + subtype;
    return name;
}

const PastedAttachment &addPastedFile(PastedAttachments &attachments, const std::string &path)
{
    for (const PastedAttachment &attachment: attachments.items)
        if (!attachment.temporary && (attachment.path == path))
            return attachment;
    gchar *name = g_path_get_basename(path.c_str());
    attachments.items.emplace_back(++attachments.lastNumber, guessAttachmentKind(path), path, name, false);
    g_free(name);
    return attachments.items.back();
}

const PastedAttachment *addPastedPicture(PastedAttachments &attachments, int imageId)
{
    char *fileName = nullptr;
    if (!saveImage(imageId, &fileName))
        return nullptr;
    unsigned number = ++attachments.lastNumber;
    // Pictures are put on the clipboard as PNG
    attachments.items.emplace_back(number, AttachmentKind::Photo, fileName,
                                   makeAttachmentName(AttachmentKind::Photo, number, "image/png"), true);
    g_free(fileName);
    return &attachments.items.back();
}

std::vector<const PastedAttachment *> addPasted(PastedAttachments &attachments, const ClipboardContent &content)
{
    std::vector<const PastedAttachment *> pasted;
    for (const std::string &file: content.files)
        pasted.push_back(&addPastedFile(attachments, file));
    if (content.imageId) {
        const PastedAttachment *picture = addPastedPicture(attachments, content.imageId);
        if (picture)
            pasted.push_back(picture);
        purple_imgstore_unref_by_id(content.imageId);
    }
    return pasted;
}

PastedAttachment &addSentMedia(PastedAttachments &attachments, int64_t messageId, int32_t fileId,
                               AttachmentKind kind, const std::string &path, const std::string &name,
                               const std::string &mimeType)
{
    for (PastedAttachment &attachment: attachments.items)
        if ((attachment.sentIn == messageId) && (attachment.fileId == fileId)) {
            if (!path.empty())
                attachment.path = path;
            return attachment;
        }
    unsigned number = ++attachments.lastNumber;
    attachments.items.emplace_back(number, kind, path, name.empty() ? makeAttachmentName(kind, number, mimeType) : name,
                                   false);
    attachments.items.back().sentIn = messageId;
    attachments.items.back().fileId = fileId;
    return attachments.items.back();
}

PlaceholderUse findPasted(const PastedAttachments &attachments, const std::string &placeholder)
{
    PlaceholderUse use;
    size_t         hash = placeholder.rfind('#');
    if ((placeholder.size() < 4) || (placeholder.front() != '[') || (placeholder.back() != ']') ||
        (hash == std::string::npos) || (hash < 2))
        return use;
    std::string digits = placeholder.substr(hash + 1, placeholder.size() - hash - 2);
    if (digits.empty() || (digits.find_first_not_of("0123456789") != std::string::npos))
        return use;
    unsigned long number = strtoul(digits.c_str(), nullptr, 10);
    for (const PastedAttachment &attachment: attachments.items)
        if (attachment.number == number)
            use.attachment = &attachment;
    if (!use.attachment)
        return use;

    std::string label = placeholder.substr(1, hash - 1);
    bool        media = (use.attachment->kind != AttachmentKind::Document);
    if (media && (!g_ascii_strcasecmp(label.c_str(), IMAGE_LABEL) || !g_ascii_strcasecmp(label.c_str(), VIDEO_LABEL)))
        return use;
    use.asFile = true;
    // A name it cannot go under, one of a directory, is its own
    use.name   = (label.find('/') == std::string::npos) && (label != ".") && (label != "..") ? label
                                                                                           : use.attachment->name;
    return use;
}

static bool copyFile(const std::string &from, const std::string &to)
{
    FILE *in  = fopen(from.c_str(), "rb");
    FILE *out = in ? fopen(to.c_str(), "wb") : nullptr;
    bool  ok  = out;
    char  buffer[65536];
    for (size_t length; ok && (length = fread(buffer, 1, sizeof(buffer), in)); )
        ok = (fwrite(buffer, 1, length, out) == length);
    ok = ok && !ferror(in);
    if (out && fclose(out))
        ok = false;
    if (in)
        fclose(in);
    if (!ok && out)
        remove(to.c_str());
    return ok;
}

std::string getUploadPath(PastedAttachments &attachments, const PlaceholderUse &use)
{
    const PastedAttachment &attachment = *use.attachment;
    if (!g_file_test(attachment.path.c_str(), G_FILE_TEST_IS_REGULAR))
        return "";
    gchar *own = g_path_get_basename(attachment.path.c_str());
    bool   same = !use.asFile || (use.name == own);
    g_free(own);
    if (same)
        return attachment.path;

    // TDLib names the document after the file it uploads
    gchar *directory = g_dir_make_tmp("tdlib_attachment_XXXXXX", nullptr);
    if (!directory)
        return "";
    attachments.linkDirectories.push_back(directory);
    std::string linkPath = std::string(directory) + G_DIR_SEPARATOR_S + use.name;
    g_free(directory);
    // Not a symbolic link, which TDLib follows to the file's own name: a hard one, or else, on
    // another file system, a copy
#ifndef _WIN32
    if (!link(attachment.path.c_str(), linkPath.c_str()))
        return linkPath;
#endif
    return copyFile(attachment.path, linkPath) ? linkPath : "";
}

std::vector<std::string> getLocalPaths(char **uris)
{
    std::vector<std::string> paths;
    for (gchar **uri = uris; uri && *uri; uri++) {
        gchar *path = g_filename_from_uri(*uri, nullptr, nullptr);
        if (path)
            paths.push_back(path);
        g_free(path);
    }
    return paths;
}

std::vector<std::string> parseUriList(const std::string &list)
{
    gchar                  **uris  = g_uri_list_extract_uris(list.c_str());
    std::vector<std::string> paths = getLocalPaths(uris);
    g_strfreev(uris);
    return paths;
}

#ifndef _WIN32
// Runs the command, taking what it writes; false, with the error, if it cannot be started
static bool runCommand(const std::vector<const char *> &args, std::string &out, std::string &err, int &status,
                       GError **error)
{
    std::vector<gchar *> argv;
    for (const char *arg: args)
        argv.push_back(const_cast<gchar *>(arg));
    argv.push_back(nullptr);

    GPid pid;
    int  outFd, errFd;
    if (!g_spawn_async_with_pipes(nullptr, argv.data(), nullptr,
                                  GSpawnFlags(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD), nullptr, nullptr,
                                  &pid, nullptr, &outFd, &errFd, error))
        return false;

    struct pollfd fds[2] = {{outFd, POLLIN, 0}, {errFd, POLLIN, 0}};
    std::string  *into[2]   = {&out, &err};
    int           remaining = 2;
    char          buffer[65536];
    while (remaining) {
        if (poll(fds, 2, -1) < 0)
            break;
        for (int i = 0; i < 2; i++)
            if (fds[i].fd >= 0 && fds[i].revents) {
                ssize_t len = read(fds[i].fd, buffer, sizeof(buffer));
                if (len > 0)
                    into[i]->append(buffer, len);
                else {
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    remaining--;
                }
            }
    }
    for (struct pollfd &fd: fds)
        if (fd.fd >= 0)
            close(fd.fd);
    if (waitpid(pid, &status, 0) < 0)
        status = -1;
    g_spawn_close_pid(pid);
    return true;
}

// xclip's output of the type; false, with what failed, if it gives none
static bool runXclip(const char *type, std::string &data, std::string &error)
{
    std::string err;
    int         status     = 0;
    GError     *spawnError = nullptr;
    data.clear();
    if (!runCommand({XCLIP, "-selection", "clipboard", "-t", type, "-o"}, data, err, status, &spawnError)) {
        error = spawnError->message;
        g_error_free(spawnError);
        return false;
    }
    if (WIFEXITED(status) && !WEXITSTATUS(status) && !data.empty())
        return true;
    while (!err.empty() && strchr(" \t\r\n", err.back()))
        err.pop_back();
    // TRANSLATOR: Error after the /paste command, when xclip fails without saying why.
    error = !err.empty() ? err
                         : formatMessage(_("xclip failed, with status {}"),
                                         std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : status));
    return false;
}

bool readXclip(ClipboardContent &content, std::string &error)
{
    std::string data;
    if (!runXclip("TARGETS", data, error))
        return false;
    bool    haveFiles = false;
    gchar **targets   = g_strsplit(data.c_str(), "\n", -1);
    for (gchar **target = targets; *target; target++)
        if (!strcmp(g_strstrip(*target), "text/uri-list"))
            haveFiles = true;
    g_strfreev(targets);

    if (haveFiles) {
        if (!runXclip("text/uri-list", data, error))
            return false;
        content.files = parseUriList(data);
        if (!content.files.empty())
            return true;
    }
    if (!runXclip("image/png", data, error))
        return false;
    purple_debug_misc(config::pluginId, "Read a picture of %zu bytes with xclip\n", data.size());
    // The store takes the copy
    gpointer copy = g_malloc(data.size());
    memcpy(copy, data.data(), data.size());
    content.imageId = purple_imgstore_add_with_id(copy, data.size(), "clipboard.png");
    return true;
}
#else
bool readXclip(ClipboardContent &content, std::string &error)
{
    error = "xclip cannot be run on Windows";
    return false;
}
#endif
