#ifndef _TDLIB_PURPLE_H
#define _TDLIB_PURPLE_H

#include "transceiver.h"
#include <purple.h>

extern "C" {
    gboolean purple_init_plugin(PurplePlugin *plugin);
};
void tgprpl_set_test_backend(ITransceiverBackend *backend);
void tgprpl_set_single_thread();
// What clicking a tdlib:// link does
void tgprpl_open_message_link(const char *link);

#endif
