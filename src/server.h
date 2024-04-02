#ifndef SERVER_H
#define SERVER_H 1

#include <lt/net.h>
#include <lt/http.h>
#include <lt/hashtab.h>

#include "fwd.h"

// uri.c

#define MAX_URI_PARAMS 32

typedef
struct uri {
	lstr_t page;
	lstr_t query;

	lstr_t params[MAX_URI_PARAMS];
	lstr_t param_vals[MAX_URI_PARAMS];
	usz param_count;
} uri_t;

lstr_t urlencode(lstr_t str);
lstr_t urldecode(lstr_t str);

uri_t parse_uri(lstr_t str);
void free_uri(const uri_t* uri);

lstr_t* uri_find_param(uri_t* uri, lstr_t param);

// server.c

typedef
struct connection {
	volatile b8 active;
	b8 keep_alive;

	lt_thread_t* thread;

#ifdef SSL
	lt_ssl_connection_t* ssl_conn;
#endif
	lt_socket_t* socket;
	lt_sockaddr_t addr;
	lt_http_msg_t request;
	lt_http_msg_t response;

	uri_t uri;

	lstr_t response_mime_type;

	lt_hashtab_t vars;

	server_t* server;

	void* usr;
} connection_t;

typedef
enum route_mapping_type {
	RMAP_AUTO = 0,
	RMAP_DIR,
	RMAP_FILE,
	RMAP_TEMPLATE,
} route_mapping_type_t;

typedef
struct route_mapping {
	route_mapping_type_t type;
	lstr_t route;
	lstr_t target;
	lstr_t mime_type;
} route_mapping_t;

typedef
struct server {
	u16 port;
	usz max_connections;
	b8 (*on_request)(connection_t* c);
	void (*on_unmapped_request)(connection_t* c);
	void (*on_404)(connection_t* c);

#ifdef SSL
	b8 use_https;
	lstr_t cert_path;
	lstr_t key_path;
	lstr_t cert_chain_path;
#endif

	volatile b8 done;
	lt_socket_t* socket;
	lt_thread_t* listen_thread;
	connection_t* connections;

	usz total_connection_count;

	lt_darr(route_mapping_t) mappings;
} server_t;

#define SRV_DEFAULT_MAX_CONNECTIONS 128

typedef
struct variable {
	lstr_t key;
	lstr_t val;
} variable_t;

void srv_set_var(connection_t* conn, lstr_t key, lstr_t val);
void srv_set_var_moved(connection_t* conn, lstr_t key, lstr_t val);
lstr_t* srv_get_var(connection_t* conn, lstr_t key);

void srv_start(server_t* server);
void srv_stop(server_t* server);

b8 srv_handle_mapped_request(server_t* server, connection_t* conn);

void srv_handle_dir_mapping(connection_t* conn, lstr_t route, lstr_t target, lstr_t mime_type_override);

void srv_map_(server_t* server, route_mapping_t mapping);

#define srv_map(server, route_, target_, args...) srv_map_((server), (route_mapping_t){ .route = CLSTR(route_), .target = CLSTR(target_), args })

void srv_map_file(server_t* server, lstr_t route, lstr_t target);
void srv_map_dir(server_t* server, lstr_t route, lstr_t target);
void srv_map_page(server_t* server, lstr_t route, lstr_t target);

#endif
