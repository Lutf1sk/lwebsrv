#ifndef MIME_H
#define MIME_H 1

#include <lt/lt.h>

lstr_t mime_type_or_default(lstr_t path, lstr_t def);
lstr_t mime_type(lstr_t path);

#endif
