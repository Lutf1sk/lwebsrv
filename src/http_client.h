#ifndef LT_HTTP_CLIENT

#include <lt/err.h>
#include <lt/net.h>

typedef
struct lt_http_client {
	b8 use_https;
	lstr_t host;
	lt_socket_t* socket;
	lt_ssl_connection_t* conn;
} lt_http_client_t;

lt_err_t lt_http_client_connect(lt_http_client_t out_client[static 1], const lt_sockaddr_t addr[static 1], b8 use_https, lstr_t host, lt_alloc_t alloc[static 1]);
void lt_http_client_destroy(const lt_http_client_t client[static 1], lt_alloc_t alloc[static 1]);

lt_err_t lt_http_client_get(const lt_http_client_t client[static 1], lt_http_msg_t out_response[static 1], lstr_t endpoint, lt_alloc_t alloc[static 1]);

#endif