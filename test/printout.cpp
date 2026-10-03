#include "printout.h"

// These used to be two switch statements enumerating, by hand, every class name in the TDLib
// API -- some 1100 of them, over 1200 lines. Every TDLib release added, renamed and removed
// entries, and the tables silently rotted: by 1.8.48 they no longer compiled at all.
//
// TDLib can already name its own objects. to_string() writes "<className> {" followed by the
// fields, so the class name is simply the first word of it, and nothing here needs touching
// when the API moves again.
static std::string typeName(const td::TlObject &object)
{
    std::string dump = td::td_api::to_string(object);
    size_t      end  = dump.find_first_of(" \n");

    return (end == std::string::npos) ? dump : dump.substr(0, end);
}

std::string requestToString(const td::TlObject &req)
{
    return typeName(req);
}

std::string responseToString(const td::TlObject &object)
{
    return typeName(object);
}
