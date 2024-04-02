#include <lt/io.h>
#include <lt/mem.h>
#include <lt/str.h>
#include <lt/time.h>

#include "server.h"
#include "template.h"

lstr_t load_template(lstr_t path, connection_t* conn) {
	lt_err_t err;

	lstr_t template;
	if ((err = lt_freadallp_utf8(path, &template, lt_libc_heap))) {
		lt_werrf("failed to read template file '%S'\n", path);
		return NLSTR();
	}

	lstr_t generated = template_render_str(template, conn);
	lt_mfree(lt_libc_heap, template.str);
	return generated;
}

lstr_t load_text(lstr_t path) {
	lt_err_t err;
	lstr_t data;
	if ((err = lt_freadallp_utf8(path, &data, lt_libc_heap))) {
		lt_werrf("failed to read '%S'\n", path);
		return NLSTR();
	}
	return data;
}

lstr_t load_raw(lstr_t path) {
	lt_err_t err;

	lstr_t data;
	if ((err = lt_freadallp(path, &data, lt_libc_heap))) {
		lt_werrf("failed to read '%S'\n", path);
		return NLSTR();
	}
	return data;
}

b8 is_route(connection_t* conn, lstr_t cmp) {
	return lt_lseq(conn->uri.page, cmp);
}
