#ifndef RESOURCE_H
#define RESOURCE_H 1

#include <lt/str.h>

#include "fwd.h"

lstr_t load_template(lstr_t path, connection_t* conn);
lstr_t load_text(lstr_t path);
lstr_t load_raw(lstr_t path);

static LT_INLINE
lstr_t load_template_cstr(char* path, connection_t* conn) {
	return load_template(lt_lsfroms(path), conn);
}

static LT_INLINE
lstr_t load_text_cstr(char* path) {
	return load_text(lt_lsfroms(path));
}

static LT_INLINE
lstr_t load_raw_cstr(char* path) {
	return load_raw(lt_lsfroms(path));
}

#define load_template(path, conn) (_Generic((path), \
		char*: load_template_cstr, \
		lstr_t: load_template \
		)((path), (conn))) \

#define load_text(path) (_Generic((path), \
		char*: load_text_cstr, \
		lstr_t: load_text \
		)((path))) \

#define load_raw(path) (_Generic((path), \
		char*: load_raw_cstr, \
		lstr_t: load_raw \
		)((path))) \

b8 is_route(connection_t* conn, lstr_t cmp);

static LT_INLINE
b8 is_route_cstr(connection_t* conn, char* cmp) {
	return is_route(conn, lt_lsfroms(cmp));
}

#define is_route(conn, cmp) (_Generic((cmp), \
		char*: is_route_cstr, \
		lstr_t: is_route\
		)((conn), (cmp)))

#endif