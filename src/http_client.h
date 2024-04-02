#ifndef LT_HTTP_CLIENT

#include <lt/err.h>

typedef
struct lt_http_client {
	b8 use_https;
	lstr_t host;
	lt_socket_t* socket;
	lt_ssl_connection_t* conn;
} lt_http_client_t;

lt_err_t lt_http_client_connect(lt_http_client_t* out_client, const lt_sockaddr_t* addr, b8 use_https, lstr_t host, lt_alloc_t* alloc);
void lt_http_client_destroy(const lt_http_client_t* client, lt_alloc_t* alloc);

lt_err_t lt_http_client_get(const lt_http_client_t* client, lt_http_msg_t* out_response, lstr_t endpoint, lt_alloc_t* alloc);

#endif