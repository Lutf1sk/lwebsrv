#include <lt/io.h>
#include <lt/term.h>
#include <lt/debug.h>
#include <lt/mem.h>
#include <lt/ctype.h>

#include "server.h"
#include "resource.h"
#include "template.h"
#include "mime.h"
#include "markdown.h"

#define MAX_DEPTH 16

template_stream(file_tree) {
	lstr_t map_route = *srv_get_var(conn, CLSTR("map_route"));
	lstr_t map_target = *srv_get_var(conn, CLSTR("map_target"));

	lt_dir_t* dirs[MAX_DEPTH];
	isz i = 0;
	dirs[i] = lt_dopenp(map_target, lt_libc_heap);
	if (!dirs[i]) {
		lt_werrf("failed to open directory '%S'\n", map_target);
		return;
	}

	char path[LT_PATH_MAX];
	memcpy(path, map_target.str, map_target.len);
	char* path_it = path + map_target.len;

	b8 empty = 1;

	for (;;) {
		lt_dirent_t* ent;
		while ((ent = lt_dread(dirs[i]))) {
			if (lt_lseq(ent->name, CLSTR(".")) || lt_lseq(ent->name, CLSTR(".."))) {
				continue;
			}

			if (path - (path_it + ent->name.len + 1) > LT_PATH_MAX) {
				lt_werrf("path too long, ignoring '%S/%S'\n", lt_lsfrom_range(path, path_it), ent->name);
				continue;
			}

			char* new_it = path_it + lt_sprintf(path_it, "/%S", ent->name);
			lstr_t full_path = lt_lsfrom_range(path, new_it);

			switch (ent->type) {
			case LT_DIRENT_DIR: {
				if (i + 1 == MAX_DEPTH) {
					lt_werrf("max recursion depth reached, ignoring directory '%S'\n", ent->name);
					break;
				}

				lt_dir_t* dir = lt_dopenp(full_path, lt_libc_heap);
				if (!dir) {
					lt_werrf("failed to open directory '%S'\n", full_path);
					break;
				}

				echo("<details><summary class=\"dir text-cyan\" style=\"padding-left: %uzpx\">%S</summary>\n", 16 * i + 2, ent->name);

				dirs[++i] = dir;
				path_it = new_it;
				empty = 0;
			}	break;

			case LT_DIRENT_FILE: {
				lt_stat_t stat;
				lt_err_t err = lt_lstatp(full_path, &stat);
				if (err != LT_SUCCESS) {
					lt_werrf("failed to stat '%S'\n", full_path);
					break;
				}

				lstr_t link = lt_lsfrom_range(path + map_target.len + 1, new_it);
				echo("<p class='file' style='padding-left: %uzpx'><a href='%S/%S'>%S</a><span>%mz</span><p>\n", 16 * i + 2, map_route, link, ent->name, stat.size);
				empty = 0;
			}	break;

			default:
				break;
			}
		}

		lt_dclose(dirs[i], lt_libc_heap);

		if (i-- == 0) {
			break;
		}

		path_it = path + lt_lsdirname(lt_lsfrom_range(path, path_it)).len;

		echo("</details>");
	}

	if (empty) {
		echo("<p class='bg-normal text-center'>No files available</p>");
	}
}

void on_404(connection_t* conn) {
	conn->response_mime_type = CLSTR("text/html; charset=UTF-8");
	conn->response.body = load_template("./pages/404.tmpl", conn);
	conn->response.response_status_code = 404;
	conn->response.response_status_msg = CLSTR("Not found");
}

b8 on_request(connection_t* conn) {

	// filetree for ./public
	if (is_route(conn, "/public")) {
		srv_set_var(conn, CLSTR("map_route"), CLSTR("/public"));
		srv_set_var(conn, CLSTR("map_target"), CLSTR("./public"));
		conn->response.body = load_template("./pages/public.tmpl", conn);
		return 1;
	}

	return 0;
}

int main(int argc, char** argv) {
	lt_debug_init();

	server_t server = (server_t){
// 			.use_https = 1,
// 			.cert_path = CLSTR("MY_CERT_DOT_PEM"),
// 			.key_path = CLSTR("MY_PRIVKEY_DOT_PEM"),
// 			.cert_chain_path = CLSTR("MY_CERT_CHAIN_DOT_PEM"),
			.port = 8000,
			.on_request = on_request,
			.on_404 = on_404 };

	srv_map(&server, "/favicon.ico", "./public/favicon.png");

	srv_map(&server, "/", "./pages/index.tmpl");
	srv_map(&server, "/", "./public");

	srv_start(&server);

	lt_term_init(0);

	for (;;) {
		u32 key = lt_term_getkey();
		if (key == (LT_TERM_MOD_CTRL | 'D')) {
			lt_printf("terminating...\n");
			srv_stop(&server);
			lt_term_restore();
			break;
		}

		if ((key & LT_TERM_KEY_MASK) == 's' || (key & LT_TERM_KEY_MASK) == 'S') {
			usz open_count = 0;
			for (usz i = 0; i < server.max_connections; ++i) {
				if (server.connections[i].active) {
					++open_count;
				}
			}

			lt_printf("%uz open connections, %uz total\n", open_count, server.total_connection_count);
		}
	}
	return 0;
}
