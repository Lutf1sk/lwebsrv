#include <lt/net.h>
#include <lt/ssl.h>
#include <lt/http.h>

#include "http_client.h"

lt_err_t lt_http_client_connect(lt_http_client_t* out_client, const lt_sockaddr_t* addr, b8 use_https, lstr_t host, lt_alloc_t* alloc) {
	lt_err_t err;

	lt_socket_t* socket = lt_socket_create(LT_SOCKTYPE_TCP, alloc);
	if (!socket) {
		return LT_ERR_UNKNOWN;
	}

	if ((err = lt_socket_connect(socket, addr))) {
		lt_socket_destroy(socket, alloc);
		return err;
	}

	if (use_https) {
#ifdef SSL
		lt_ssl_connection_t* ssl = lt_ssl_connect(socket, host);
		if (!ssl) {
			lt_socket_destroy(socket, alloc);
			return LT_ERR_UNKNOWN;
		}

		out_client->conn = ssl;
#else
		lt_werrf("lt_http_client_connect called with use_http=1, but -DLT_SSL was not set, falling back to http.\n");
#endif
	}
	out_client->host = host;
	out_client->socket = socket;
#ifdef SSL
	out_client->use_https = use_https;
#else
	out_client->use_https = 0;
#endif
	return LT_SUCCESS;
}

void lt_http_client_destroy(const lt_http_client_t* client, lt_alloc_t* alloc) {
#ifdef SSL
	lt_ssl_connection_destroy(client->conn);
#endif
	lt_socket_destroy(client->socket, alloc);
}

lt_err_t lt_http_client_get(const lt_http_client_t* client, lt_http_msg_t* out_response, lstr_t endpoint, lt_alloc_t* alloc) {
	lt_err_t err;

	lt_http_msg_t request;
	if ((err = lt_http_msg_create(&request, alloc))) {
		return err;
	}
	request.version = LT_HTTP_1_1;
	request.request_method = LT_HTTP_GET;
	request.request_file = endpoint;
	lt_http_add_header(&request, CLSTR("Host"), client->host);
	lt_http_add_header(&request, CLSTR("Connection"), CLSTR("keep-alive"));
	lt_http_add_header(&request, CLSTR("Accept"), CLSTR("*/*"));
	lt_http_add_header(&request, CLSTR("Accept-Language"), CLSTR("*/*"));
	request.body = NLSTR();

	lt_io_callback_t write_callb = (lt_io_callback_t)lt_socket_send;
	lt_io_callback_t read_callb = (lt_io_callback_t)lt_socket_recv;
	void* usr = client->socket;

#ifdef SSL
	if (client->use_https) {
		write_callb = (lt_io_callback_t)lt_ssl_send_fixed;
		read_callb = (lt_io_callback_t)lt_ssl_recv_fixed;
		usr = client->conn;
	}
#endif

	if ((err = lt_http_write_request(&request, write_callb, usr))) {
		lt_http_msg_destroy(&request, alloc);
		return err;
	}

	if ((err = lt_http_parse_response(out_response, read_callb, usr, alloc))) {
		lt_http_msg_destroy(&request, alloc);
		return err;
	}

	lt_http_msg_destroy(&request, alloc);
	return LT_SUCCESS;
}