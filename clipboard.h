#ifndef _CLIPBOARD_H
#define _CLIPBOARD_H

#include <string>

// The picture on the X clipboard, read with xclip for front-ends that cannot read it themselves,
// put into the image store, its reference the caller's; 0, with what failed, if there is none
int storeXclipPicture(std::string &error);

#endif
