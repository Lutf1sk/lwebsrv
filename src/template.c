#include <lt/strstream.h>
#include <lt/mem.h>
#include <lt/ctype.h>
#include <lt/elf.h>
#include <lt/str.h>
#include <lt/io.h>
#include <lt/debug.h>
#include <lt/html.h>

#include "server.h"
#include "template.h"

static
void skip_space(char** it, char* end) {
	while (*it < end && lt_is_space(**it)) {
		++*it;
	}
}

static
lstr_t consume_name(char** it, char* end) {
	char* start = *it;
	while (*it < end && (lt_is_ident_body(**it) || **it == '-')) {
		++*it;
	}
	return lt_lsfrom_range(start, *it);
}

static
lstr_t consume_identifier(char** it, char* end) {
	char* start = *it;
	while (*it < end && lt_is_ident_body(**it)) {
		++*it;
	}
	return lt_lsfrom_range(start, *it);
}

static
lstr_t consume_attrib_val(char** it, char* end) {
	if (*it >= end) {
		lt_werrf("unexpected end-of-file\n");
		return NLSTR();
	}
	if (**it != '"') {
		lt_werrf("expected quotes around attribute value\n");
		return NLSTR();
	}

	char* start = ++*it;
	while (*it < end && **it != '"') {
		++*it;
	}
	return lt_lsfrom_range(start, (*it)++);
}

static
lstr_t consume_string(char** it, char* end) {
	if (*it >= end) {
		lt_werrf("unexpected end-of-file\n");
		return NLSTR();
	}
	if (**it != '"') {
		lt_werrf("expected quotes around string value\n");
		return NLSTR();
	}

	char* start = ++*it;
	while (*it < end && **it != '"') {
		++*it;
	}
	return lt_lsfrom_range(start, (*it)++);
}

static
void consume_and_write_text(lt_io_callback_t callb, void* usr, char** it, char* end) {
	if (*it >= end) {
		lt_werrf("unexpected end-of-file\n");
		return;
	}
	if (**it != '[') {
		lt_werrf("expected '[' around text body\n");
		return;
	}
	++*it;

	while (*it < end && **it != ']') {
		char c = *(*it)++;
		if (c == '\n') {
			lt_writes(callb, usr, "<br>");
		}
		else if (c == '\t') {
			while (*it < end && **it == '\t') {
				++*it;
			}
			if (*it >= end || lt_is_space(**it)) {
				lt_writes(callb, usr, " ");
			}
		}
		else {
			lt_write_htmlencoded_char8(callb, usr, c);
		}
	}
	++*it;
}

isz template_render(lt_io_callback_t callb, void* usr, lstr_t template, connection_t* conn) {
	for (char* it = template.str, *end = it + template.len; it < end;) {
		skip_space(&it, end);
		if (it >= end) {
			return it - template.str;
		}

		if (*it == '[') {
			consume_and_write_text(callb, usr, &it, end);
			continue;
		}
		else if (*it == '}') {
			return it - template.str;
		}
		else if (!lt_is_ident_body(*it)) {
			lt_werrf("unexpected character '%c'\n", *it);
			return -LT_ERR_INVALID_SYNTAX;
		}

		lstr_t elem_name = consume_name(&it, end);

		if (lt_lseq(elem_name, CLSTR("call"))) {
			skip_space(&it, end);
			lstr_t name = consume_identifier(&it, end);
			skip_space(&it, end);
			if (it >= end || *it++ != ';') {
				lt_werrf("expected ';' after stream name\n");
				return -LT_ERR_INVALID_SYNTAX;
			}

			lstr_t symname = lt_lsbuild(lt_libc_heap, "__template_stream_%S", name);
			lt_elf64_sym_t* sym = lt_elf64_sym_by_name(lt_debug_executable, symname);
			if (!sym) {
				lt_werrf("unknown stream function '%S'\n", name);
				return -LT_ERR_INVALID_SYNTAX;
			}
			else {
				usz sym_addr = (usz)sym->value + lt_debug_load_addr;
				((stream_fn_t)sym_addr)(callb, usr, conn);
			}
			lt_mfree(lt_libc_heap, symname.str);
			continue;
		}
		else if (lt_lseq(elem_name, CLSTR("include"))) {
			skip_space(&it, end);
			lstr_t path = consume_string(&it, end);
			skip_space(&it, end);
			if (it >= end || *it++ != ';') {
				lt_werrf("expected ';' after include path\n");
				return -LT_ERR_INVALID_SYNTAX;
			}

			lstr_t subtemplate;
			if (lt_freadallp_utf8(path, &subtemplate, lt_libc_heap)) {
				lt_werrf("failed to include template file '%S'\n", path);
				continue;
			}

			template_render(callb, usr, subtemplate, conn);
			lt_mfree(lt_libc_heap, subtemplate.str);
			continue;
		}
		else if (lt_lseq(elem_name, CLSTR("write"))) {
			skip_space(&it, end);
			lstr_t text = consume_string(&it, end);
			skip_space(&it, end);
			if (it >= end || *it++ != ';') {
				lt_werrf("expected ';' after write argument\n");
				return -LT_ERR_INVALID_SYNTAX;
			}
			lt_writels(callb, usr, text);
			continue;
		}
		else if (lt_lseq(elem_name, CLSTR("read"))) {
			skip_space(&it, end);
			lstr_t name = consume_string(&it, end);
			skip_space(&it, end);
			lstr_t def = NLSTR();
			if (it < end && *it == ',') {
				++it;
				skip_space(&it, end);
				def = consume_string(&it, end);
				skip_space(&it, end);
			}
			if (it >= end || *it++ != ';') {
				lt_werrf("expected ';' after read key\n");
				return -LT_ERR_INVALID_SYNTAX;
			}
			lstr_t* val = srv_get_var(conn, name);
			lt_write_htmlencoded(callb, usr, val ? *val : def);
			continue;
		}
		else if (lt_lseq(elem_name, CLSTR("param"))) {
			skip_space(&it, end);
			lstr_t name = consume_string(&it, end);
			skip_space(&it, end);
			lstr_t def = NLSTR();
			if (it < end && *it == ',') {
				++it;
				skip_space(&it, end);
				def = consume_string(&it, end);
				skip_space(&it, end);
			}
			if (it >= end || *it++ != ';') {
				lt_werrf("expected ';' after param name\n");
				return -LT_ERR_INVALID_SYNTAX;
			}
			lstr_t* val = uri_find_param(&conn->uri, name);
			lt_write_htmlencoded(callb, usr, val ? *val : def);
			continue;
		}

		lt_io_printf(callb, usr, "<%S", elem_name);

		skip_space(&it, end);
		while (it < end && lt_is_ident_body(*it)) {
			lstr_t attrib = consume_name(&it, end);
			skip_space(&it, end);

			lt_io_printf(callb, usr, " %S", attrib);

			if (it >= end) {
				lt_werrf("unexpected end-of-file\n");
				return -LT_ERR_INVALID_SYNTAX;
			}
			if (*it != '=') {
				continue;
			}
			++it;
			skip_space(&it, end);

			lstr_t val = consume_attrib_val(&it, end);
			skip_space(&it, end);

			lt_writes(callb, usr, "=\"");
			lt_write_htmlencoded(callb, usr, val);
			lt_writes(callb, usr, "\"");
		}

		if (it >= end) {
			lt_werrf("unexpected end-of-file\n");
			return -LT_ERR_INVALID_SYNTAX;
		}

		lt_writes(callb, usr, ">");

		char c = *it;
		if (c == ';') {
			if (!lt_lseq(elem_name, CLSTR("link")) && !lt_lseq(elem_name, CLSTR("br")) && !lt_lseq(elem_name, CLSTR("meta")) && !lt_lseq(elem_name, CLSTR("input"))) {
				lt_io_printf(callb, usr, "</%S>", elem_name);
			}
			++it;
			continue;
		}
		else if (c == '{') {
			++it;
			it += template_render(callb, usr, lt_lsfrom_range(it, end), conn);
			skip_space(&it, end);
			if (it >= end || *it != '}') {
				lt_werrf("expected '}' after element body\n", *it);
				return -LT_ERR_INVALID_SYNTAX;
			}
			++it;
			lt_io_printf(callb, usr, "</%S>", elem_name);
		}
		else if (c == '[') {
			consume_and_write_text(callb, usr, &it, end);
			lt_io_printf(callb, usr, "</%S>", elem_name);
		}
	}

	return template.len;
}

lstr_t template_render_str(lstr_t template, connection_t* conn) {
	lt_strstream_t ss;
	LT_ASSERT(lt_strstream_create(&ss, lt_libc_heap) == LT_SUCCESS);
	template_render((lt_io_callback_t)lt_strstream_write, &ss, template, conn);
	return ss.str;
}

