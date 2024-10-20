#include <lt/term.h>
#include <lt/thread.h>
#include <lt/mem.h>
#include <lt/io.h>
#include <lt/darr.h>
#include <lt/str.h>
#include <lt/ssl.h>

#define LT_ANSI_SHORTEN_NAMES 1
#include <lt/ansi.h>

#include "server.h"
#include "mime.h"
#include "template.h"

static
void on_client_connected(server_t server[static 1], u32 cid, lt_arena_t arena[static 1]) {
	lt_err_t err;

	connection_t* conn        = &server->connections[cid];
	lt_write_fn_t write_callb = (lt_write_fn_t)lt_socket_send;
	lt_read_fn_t  read_callb  = (lt_read_fn_t) lt_socket_recv;
	void*         callb_usr   = conn->socket;

#ifdef SSL
	if (server->use_https) {
		callb_usr = server->ssl_conn = lt_ssl_accept(client_socket);
		if (!callb_usr) {
			lt_werrf("ssl handshake failed\n");
			return;
		}
		write_callb = (lt_write_fn_t)lt_ssl_send_fixed;
		read_callb  = (lt_read_fn_t)lt_ssl_recv_fixed;
	}
#endif

	lt_alloc_t* alloc = &arena->interf;

	do {
		lt_amreset(arena);

		// read and parse request
		lt_mzero(&conn->request, sizeof(conn->request));
		if ((err = lt_http_parse_request(&conn->request, read_callb, callb_usr, alloc))) {
			if (err != LT_ERR_CLOSED) {
				lt_werrf("failed to parse http request: %S\n", lt_err_str(err));
			}
			conn->keep_alive = 0;
			return;
		}
		u32 addr = lt_sockaddr_ipv4_addr(&conn->addr);
		lt_ierrf("["FG_BYELLOW"C_%hd"RESET"] HTTP/%uw.%uw %S %S\n", addr,
				LT_HTTP_VERSION_MAJOR(conn->request.version),
				LT_HTTP_VERSION_MINOR(conn->request.version),
				lt_http_method_str(conn->request.request_method),
				conn->request.request_file);

		conn->uri = parse_uri(conn->request.request_file);

		lstr_t* conn_header = lt_http_find_header(&conn->request, CLSTR("Connection"));
		conn->keep_alive = conn_header && lt_lseq_nocase(*conn_header, CLSTR("keep-alive"));

		// create response
		lt_mzero(&conn->response, sizeof(conn->response));
		if ((err = lt_http_msg_create(&conn->response, alloc))) {
			lt_werrf("failed to create response message: %S\n", lt_err_str(err));
			break;
		}
		conn->response.version              = LT_HTTP_1_1;
		conn->response.response_status_code = 200;
		conn->response.response_status_msg  = CLSTR("OK");

		conn->response_mime_type = NLSTR();
		lt_hashtab_init(&conn->vars);

		// route parsed request
		if (server->on_request && server->on_request(conn))
			; // noop
		else if (srv_handle_mapped_request(server, conn))
			; // noop
		else if (server->on_unmapped_request) {
			server->on_unmapped_request(conn);
		}
		else {
			LT_ASSERT(server->on_404);
			server->on_404(conn);
		}

		if (conn->keep_alive) {
			lt_http_add_header(&conn->response, CLSTR("Connection"), CLSTR("keep-alive"));
			lt_http_add_header(&conn->response, CLSTR("Keep-Alive"), CLSTR("timeout=5, max=99"));
		}
		else {
			lt_http_add_header(&conn->response, CLSTR("Connection"), CLSTR("close"));
		}
		lt_http_add_header(&conn->response, CLSTR("Content-Type"), conn->response_mime_type);

		if ((err = lt_http_write_response(&conn->response, write_callb, callb_usr))) {
			lt_werrf("failed to send response message: %S\n", lt_err_str(err));
		}

		// cleanup
		free_uri(&conn->uri);
	} while (conn->keep_alive);
}

static
void connection_proc(connection_t* conn) {
	server_t* server = conn->server;
	u32 cid = conn - conn->server->connections;

	while (!server->done) {
		lt_mutex_lock(conn->mutex);

		on_client_connected(server, cid, conn->arena);

#ifdef SSL
		if (server->use_https && conn->ssl_conn) { // !! ssl_conn can be null
			lt_ssl_connection_destroy(conn->ssl_conn);
		}
#endif
		lt_socket_destroy(conn->socket, lt_libc_heap);

		// !! race condition
		conn->next_free    = server->first_free;
		server->first_free = cid;
	}
}

static
void listen_proc(server_t* server) {
	while (!server->done) {
		lt_sockaddr_t client_addr;

		lt_socket_t* client_socket = lt_socket_accept(server->socket, &client_addr, lt_libc_heap);
		if (!client_socket) {
			lt_werrf("failed to accept client: %S\n", lt_err_str(lt_errno()));
			goto err;
		}

		u32 ipv4_addr = lt_sockaddr_ipv4_addr(&client_addr);
		u16 ipv4_port = lt_sockaddr_ipv4_port(&client_addr);

		lt_ierrf("["FG_BYELLOW"C_%hd"RESET"] accepted incoming connection from %ub.%ub.%ub.%ub:%uw\n", ipv4_addr,
				(ipv4_addr >> 24), (ipv4_addr >> 16) & 0xFF, (ipv4_addr >> 8) & 0xFF, ipv4_addr & 0xFF, ipv4_port);

		u32 cid = server->first_free;
		if (cid == ~0) {
			lt_printf("pool is full, dropping connection...\n");
			goto err;
		}
		connection_t* client = &server->connections[cid];
		server->first_free = client->next_free; // !! race condition
		
		client->socket = client_socket;
		client->addr   = client_addr;
		lt_mutex_release(client->mutex);
		continue;

	err:
		lt_socket_destroy(client_socket, lt_libc_heap);
	}
}

void srv_set_var_moved(connection_t* conn, lstr_t key, lstr_t val) {
	variable_t* var = lt_amalloc(conn->arena, sizeof(*var));
	LT_ASSERT(var);
	*var = (variable_t) {
			.key = key,
			.val = val };

	lt_hashtab_insert(&conn->vars, lt_hashls(key), var, &conn->arena->interf);
}

void srv_set_var(connection_t* conn, lstr_t key, lstr_t val) {
	srv_set_var_moved(conn, key, lt_strdup(&conn->arena->interf, val));
}

#define var_is_equal(key_, var) (lt_lseq((key_), (var)->key))

LT_DEFINE_HASHTAB_FIND_FUNC(variable_t, lstr_t, htab_find_variable, var_is_equal)

lstr_t* srv_get_var(connection_t* conn, lstr_t key) {
	variable_t* var = htab_find_variable(&conn->vars, lt_hashls(key), key);
	return var ? &var->val : NULL;
}

#include <signal.h>

void srv_start(server_t* server) {
	lt_err_t err;

	if (!server->mappings) {
		server->mappings = lt_darr_create(route_mapping_t, 16, lt_libc_heap);
		LT_ASSERT(server->mappings != NULL);
	}

	signal(SIGPIPE, SIG_IGN);

	u16 default_port = 80;

#ifdef SSL
	if (server->use_https) {
		if (!server->cert_path.len) {
			lt_werrf("server->use_https was set, but no certificate was specified, falling back to http\n");
			server->use_https = 0;
			goto https_canceled;
		}
		else if (!server->key_path.len) {
			lt_werrf("server->use_https was set, but no private key was specified, falling back to http\n");
			server->use_https = 0;
			goto https_canceled;
		}

		if ((err = lt_ssl_init(LT_SSL_SERVER | LT_SSL_CLIENT))) {
			lt_werrf("failed to initialize ssl, falling back to http\n");
			server->use_https = 0;
			goto https_canceled;
		}
		if ((err = lt_ssl_configure_server(server->cert_path, server->key_path, server->cert_chain_path))) {
			lt_werrf("failed to set ssl certificates, falling back to http\n");
			lt_ssl_terminate(LT_SSL_SERVER | LT_SSL_CLIENT);
			server->use_https = 0;
			goto https_canceled;
		}

		default_port = 443;
		lt_ierrf("ssl successfully initialized\n");
	}
https_canceled:
#endif

	if (server->port == 0) {
		lt_ierrf("server->port not set, defaulting to port %uw\n", default_port);
		server->port = default_port;
	}

	if (server->max_connections == 0) {
		server->max_connections = SRV_DEFAULT_MAX_CONNECTIONS;
	}

	if (server->max_request_memory == 0) {
		server->max_request_memory = SRV_DEFAULT_MAX_REQUEST_MEMORY;
	}

	server->socket = lt_socket_create(LT_SOCKTYPE_TCP, lt_libc_heap);
	if (!server->socket) {
		lt_ferrf("failed to create socket: %S\n", lt_err_str(lt_errno()));
	}

	if ((err = lt_socket_server(server->socket, server->port))) {
		lt_ferrf("failed to bind server port: %S\n", lt_err_str(err));
	}

	usz connections_size = server->max_connections * sizeof(*server->connections);
	server->connections = lt_malloc(lt_libc_heap, connections_size);
	if (!server->connections) {
		lt_ferrf("failed to allocate connection array\n");
	}

	for (usz i = 0; i < server->max_connections; ++i) {
		connection_t* conn = &server->connections[i];
		conn->server = server;
		conn->next_free = i + 1;
		conn->arena = lt_amcreate(NULL, server->max_request_memory, 0);
		conn->mutex = lt_mutex_create(lt_libc_heap);
		lt_mutex_lock(conn->mutex);
		conn->thread = lt_thread_create((lt_thread_fn_t)connection_proc, conn, lt_libc_heap);
	}
	server->connections[server->max_connections - 1].next_free = ~0;

	server->listen_thread = lt_thread_create((lt_thread_fn_t)listen_proc, server, lt_libc_heap);
	if (!server->listen_thread) {
		lt_ferrf("failed to create message thread\n");
	}
}

void srv_stop(server_t* server) {
	server->done = 1;

	lt_thread_cancel(server->listen_thread);
	lt_thread_join(server->listen_thread, lt_libc_heap);

	lt_socket_destroy(server->socket, lt_libc_heap);

	for (usz i = 0; i < server->max_connections; ++i) {
		lt_amdestroy(server->connections[i].arena);
		lt_mutex_destroy(server->connections[i].mutex, lt_libc_heap);
		//lt_thread_join(server->connections[i].thread, lt_libc_heap);
	}
	lt_mfree(lt_libc_heap, server->connections);
	lt_darr_destroy(server->mappings);

#ifdef SSL
	lt_ssl_terminate(LT_SSL_SERVER | LT_SSL_CLIENT);
#endif
}

b8 srv_handle_mapped_request(server_t* server, connection_t* conn) {
	lt_err_t err;

	if (!server->mappings) {
		return 0;
	}

	for (usz i = 0; i < lt_darr_count(server->mappings); ++i) {
		route_mapping_t* m = &server->mappings[i];

		if (m->type == RMAP_FILE && lt_lseq(conn->uri.page, m->route)) {
			lstr_t data = NLSTR();
			if ((err = lt_freadallp(m->target, &data, &conn->arena->interf))) {
				lt_werrf("failed to read '%S': %S\n", m->target, lt_err_str(err));
			}

			conn->response_mime_type = m->mime_type;
			conn->response.body = data;
			return 1;
		}

		else if (m->type == RMAP_TEMPLATE && lt_lseq(conn->uri.page, m->route)) {
			lstr_t data = NLSTR();
			if ((err = lt_freadallp(m->target, &data, &conn->arena->interf))) {
				lt_werrf("failed to read '%S': %S\n", m->target, lt_err_str(err));
			}

			conn->response_mime_type = m->mime_type;
			conn->response.body = template_render_str(data, conn);
			return 1;
		}

		else if (m->type == RMAP_DIR && lt_lsprefix(conn->uri.page, m->route)) {
			srv_handle_dir_mapping(conn, m->route, m->target, m->mime_type);
			return 1;
		}
	}

	return 0;
}

void srv_handle_dir_mapping(connection_t* conn, lstr_t route, lstr_t target, lstr_t mime_type_override) {
	lstr_t file = LSTR(conn->uri.page.str + route.len, conn->uri.page.len - route.len);
	lstr_t load_path = lt_lsbuild(&conn->arena->interf, "%S/%S", target, file);

	lt_err_t err;

	lt_stat_t stat;
	if ((err = lt_lstatp(load_path, &stat))) {
		lt_werrf("failed to stat '%S': %S\n", load_path, lt_err_str(err));
		goto on_404;
	}
	if (stat.type != LT_DIRENT_FILE) {
		lt_werrf("ignoring request for '%S': IS_DIRECTORY\n", load_path);
		goto on_404;
	}

	lstr_t data = NLSTR();
	if ((err = lt_freadallp(load_path, &data, &conn->arena->interf))) {
		lt_werrf("failed to read '%S': %S\n", load_path, lt_err_str(err));
		goto on_404;
	}

	if (mime_type_override.len) {
		conn->response_mime_type = mime_type_override;
	}
	else {
		conn->response_mime_type = mime_type(conn->uri.page);
	}
	conn->response.body = data;
	return;

on_404:
	conn->server->on_404(conn);
}

void srv_map_(server_t* server, route_mapping_t mapping) {
	lt_err_t err;

	if (mapping.route.len > 1) {
		mapping.route = lt_lstrim_trailing_slash(mapping.route);
	}
	if (mapping.target.len > 1) {
		mapping.target = lt_lstrim_trailing_slash(mapping.target);
	}

	lt_ierrf("mapping '%S' to '%S'...\n", mapping.route, mapping.target);

	if (mapping.type == RMAP_AUTO) {
		lt_stat_t stat;
		if ((err = lt_lstatp(mapping.target, &stat))) {
			lt_werrf("stat failed for '%S', mapping ignored: %S\n", mapping.target, lt_err_str(err));
			return;
		}

		if (stat.type == LT_DIRENT_DIR) {
			mapping.type = RMAP_DIR;
			lt_ierrf("selected mapping type DIR\n");
		}
		else if (stat.type == LT_DIRENT_FILE) {
			if (lt_lssuffix(mapping.target, CLSTR(".tmpl"))) {
				mapping.type = RMAP_TEMPLATE;
				lt_ierrf("selected mapping type TEMPLATE\n");
			}
			else {
				mapping.type = RMAP_FILE;
				lt_ierrf("selected mapping type FILE\n");
			}
		}
		else {
			lt_werrf("invalid file type for '%S', mapping ignored\n", mapping.target);
			return;
		}
	}

	if (!mapping.mime_type.len) {
		switch (mapping.type) {
		case RMAP_AUTO:		LT_ASSERT_NOT_REACHED();
		case RMAP_DIR:		break;
		case RMAP_TEMPLATE:	mapping.mime_type = mime_type_or_default(mapping.route, CLSTR("text/html")); break;
		case RMAP_FILE:		mapping.mime_type = mime_type(mapping.target); break;
		}
	}

	if (!server->mappings) {
		server->mappings = lt_darr_create(route_mapping_t, 16, lt_libc_heap);
		LT_ASSERT(server->mappings != NULL);
	}

	lt_darr_push(server->mappings, mapping);
}
