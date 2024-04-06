#include <lt/io.h>
#include <lt/term.h>
#include <lt/debug.h>

#include "server.h"
#include "resource.h"

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
