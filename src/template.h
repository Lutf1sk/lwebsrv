#ifndef TEMPLATE_H
#define TEMPLATE_H 1

#include <lt/io.h>

typedef struct connection connection_t;

typedef void (*stream_fn_t)(lt_write_fn_t callb, void* usr, connection_t* conn);

#define template_stream(name) void __template_stream_##name(lt_write_fn_t __callb, void* __usr, connection_t* conn)
#define echo(...) lt_io_printf(__callb, __usr, __VA_ARGS__)

isz template_render(lt_write_fn_t callb, void* usr, lstr_t template, connection_t* conn);
lstr_t template_render_str(lstr_t template, connection_t* conn);

#define template_render_child(path) template_render(__callb, __usr, (path), conn);
#define stream_invoke_child(name) __template_stream_##name(__callb, __usr, conn);

#endif
