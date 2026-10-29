#ifndef _CLIPBOARD_H
#define _CLIPBOARD_H

#include <string>
#include <vector>

// What /paste sends: files copied, as a file manager copies them, or else a picture
struct ClipboardContent {
    std::vector<std::string> files;       // local paths
    int                      imageId = 0; // in the image store, the reference the holder's
};

// The local paths of the URIs, those of other kinds left out
std::vector<std::string> getLocalPaths(char **uris);
// The same of a text/uri-list
std::vector<std::string> parseUriList(const std::string &list);

// The X clipboard, read with xclip for front-ends that cannot read it themselves; false, with what
// failed, if it holds neither files nor a picture
bool readXclip(ClipboardContent &content, std::string &error);

#endif
