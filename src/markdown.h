#ifndef LT_MARKDOWN_H
#define LT_MARKDOWN_H 1

#include <lt/lt.h>

void lt_md_render(lstr_t markdown, lstr_t link_base, lt_write_fn_t callb, void* usr);

#endif
