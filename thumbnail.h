#ifndef _THUMBNAIL_H
#define _THUMBNAIL_H

#include <functional>
#include <string>
#include <vector>

// A thumbnail of a file, as a PNG file, for a placeholder to be shown by: the one file managers
// keep, or one made by a thumbnailer registered for the file's type, as freedesktop.org has them.
// Told once there is one, the path empty if there is none; one made for it is removed after. One
// made is of the size, in pixels; one kept may be of another, as close as can be.
using ThumbnailFound = std::function<void(const std::string &png)>;
void findThumbnail(const std::string &path, const std::string &mimeType, unsigned size, ThumbnailFound found);

// Where thumbnailers are looked for, in place of the XDG data directories' "thumbnailers", for tests
void setThumbnailerDirectories(const std::vector<std::string> &directories);

#endif
