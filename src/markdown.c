#include <lt/ctype.h>
#include <lt/io.h>
#include <lt/str.h>
#include <lt/html.h>

static LT_INLINE
b8 str_pending(char* it, char* end, lstr_t str) {
	if (it + str.len > end) {
		return 0;
	}
	return memcmp(it, str.str, str.len) == 0;
}

static LT_INLINE
b8 char_pending(char* it, char* end, char c) {
	if (it >= end) {
		return 0;
	}
	return *it == c;
}

static
b8 consume_if_str_pending(char** it, char* end, lstr_t str) {
	if (*it + str.len > end || memcmp(*it, str.str, str.len) != 0) {
		return 0;
	}
	*it += str.len;
	return 1;
}

static
b8 consume_if_char_pending(char** it, char* end, char c) {
	if (*it >= end || **it != c) {
		return 0;
	}
	++*it;
	return 1;
}

static
lstr_t consume_until_char(char** it, char* end, char c) {
	char* start = *it;
	while (*it < end && **it != c) {
		++*it;
	}
	return lt_lsfrom_range(start, (*it)++);
}

static
lstr_t consume_until_str(char** it, char* end, lstr_t str) {
	char* start = *it;
	while (*it < end && !str_pending(*it, end, str)) {
		++*it;
	}
	lstr_t consumed = lt_lsfrom_range(start, *it);
	*it += str.len;
	return consumed;
}

static
void skip_space(char** it, char* end) {
	while (*it < end && ((**it == ' ') || (**it == '\t'))) {
		++*it;
	}
}

#include <lt/time.h>

static b8 special_char_tab[256] = {
	['<'] = 1,
	['>'] = 1,
	['&'] = 1,
	['\''] = 1,
	['"'] = 1,
	['\n'] = 1,
	['`'] = 1,
	['!'] = 1,
	['['] = 1,
	['\\'] = 1,
	['*'] = 1,
	['_'] = 1,
	['~'] = 1,
};

void lt_md_render(lstr_t markdown, lstr_t link_base, lt_write_fn_t callb, void* usr) {
	b8 quote = 0;

	lt_writes(callb, usr, "<div>");
	char* it = markdown.str, *end = it + markdown.len;
	for (;;) {
		skip_space(&it, end);

		if (consume_if_char_pending(&it, end, '>')) {
			if (!quote) {
				quote = 1;
				lt_writes(callb, usr, "<blockquote>");
			}
		}
		else if (quote) {
			quote = 0;
			lt_writes(callb, usr, "</blockquote>");
		}

		if (consume_if_char_pending(&it, end, '\n')) {
			lt_writes(callb, usr, "<br>\n");
			if (it >= end) {
				break;
			}
			continue;
		}

		usz heading = 0;
		while (consume_if_char_pending(&it, end, '#')) {
			++heading;
		}
		if (heading) {
			lstr_t line = consume_until_char(&it, end, '\n');
			lt_io_printf(callb, usr, "<h%uz>%r#", heading, heading);
			lt_write_htmlencoded(callb, usr, line);
			lt_io_printf(callb, usr, "</h%uz>\n", heading);
			continue;
		}

		if (consume_if_char_pending(&it, end, '|')) {
			lstr_t line = consume_until_char(&it, end, '\n');
 			lt_writes(callb, usr, "<pre style='font-family: monospace'>|");
 			lt_write_htmlencoded(callb, usr, line);
 			lt_writes(callb, usr, "</pre>\n");
 			continue;
		}
		else if (consume_if_char_pending(&it, end, '-')) {
			skip_space(&it, end);

			if (consume_if_str_pending(&it, end, CLSTR("[X]"))) {
				lt_writes(callb, usr, "<input type='checkbox' disabled checked>");
			}
			else if (consume_if_str_pending(&it, end, CLSTR("[ ]"))) {
				lt_writes(callb, usr, "<input type='checkbox' disabled>");
			}
			else {
				lt_writes(callb, usr, "<span style='font-family: monospace'> - </span>");
			}
		}

		while (it < end && *it != '\n') {
			b8 image = 0;

			char c = *it++;
			switch (c) {
			case '\\':
				if (it < end) {
					lt_write_htmlencoded_char8(callb, usr, *it++);
				}
				break;

			case '!':
				if (!consume_if_char_pending(&it, end, '[')) {
					callb(usr, "!", 1);
					break;
				}
				image = 1;
				// fall through

			case '[': {
				lstr_t name = lt_lstrim(consume_until_char(&it, end, ']'));
				lstr_t link = name;

				if (consume_if_char_pending(&it, end, '(')) {
					link = lt_lstrim(consume_until_char(&it, end, ')'));;
				}

				lstr_t link_pfx = link_base;
				char* pfx_slash = "/";
				if (lt_lsprefix(link, CLSTR("http://")) || lt_lsprefix(link, CLSTR("https://"))) {
					link_pfx = NLSTR();
					pfx_slash = "";
				}

				if (image) {
					lt_io_printf(callb, usr, "<span><img src='%S%s", link_pfx, pfx_slash);
					lt_write_htmlencoded(callb, usr, link);
					lt_writes(callb, usr, "'><span class='tooltip'>");
					lt_write_htmlencoded(callb, usr, name);
					lt_writes(callb, usr, "</span></span>");
				}
				else {
					lt_io_printf(callb, usr, "<a href='%S%s", link_pfx, pfx_slash);
					lt_write_htmlencoded(callb, usr, link);
					callb(usr, "'>", 2);
					lt_write_htmlencoded(callb, usr, name);
					callb(usr, "</a>", 4);
				}
			}	break;

			case '`': {
				usz tilde_count = 1;
				if (consume_if_char_pending(&it, end, '`')) {
					++tilde_count;
				}
				if (consume_if_char_pending(&it, end, '`')) {
					++tilde_count;
				}

				char* add_attribs = "";
				if (char_pending(it, end, '\n')) {
					add_attribs = "block ";
				}

				lstr_t code = consume_until_str(&it, end, LSTR("```", tilde_count));
				lt_io_printf(callb, usr, "<pre class='%scode'>", add_attribs);
				lt_write_htmlencoded(callb, usr, code);
				callb(usr, "</pre>", 6);
			}	break;

			case '*':
				if (consume_if_str_pending(&it, end, CLSTR("**"))) {
					lstr_t text = consume_until_str(&it, end, CLSTR("***"));
					callb(usr, "<i><b>", 6);
					lt_write_htmlencoded(callb, usr, text);
					callb(usr, "</b></i>", 8);
				}
				else if (consume_if_char_pending(&it, end, '*')) {
					lstr_t text = consume_until_str(&it, end, CLSTR("**"));
					callb(usr, "<b>", 3);
					lt_write_htmlencoded(callb, usr, text);
					callb(usr, "</b>", 4);
				}
				else {
					lstr_t text = consume_until_char(&it, end, '*');
					callb(usr, "<i>", 3);
					lt_write_htmlencoded(callb, usr, text);
					callb(usr, "</i>", 4);
				}
				break;

// 			case '_': break;

			case '~':
				if (consume_if_char_pending(&it, end, '~')) {
					lstr_t text = consume_until_str(&it, end, CLSTR("~~"));
					callb(usr, "<s>", 3);
					lt_write_htmlencoded(callb, usr, text);
					callb(usr, "</s>", 4);
				}
				else {
					callb(usr, "~", 1);
				}
				break;

			case '<': callb(usr, "&lt;", 4); break;
			case '>': callb(usr, "&gt;", 4); break;
			case '&': callb(usr, "&amp;", 5); break;
			case '\'': callb(usr, "&apos;", 6); break;
			case '"': callb(usr, "&quot;", 6); break;
			default:
				char* start = it - 1;
				while (it < end && !special_char_tab[(u8)*it]) {
					++it;
				}
				callb(usr, start, it - start);
				break;
			}
		}

		lt_writes(callb, usr, "<br>\n");

		if (it++ >= end) {
			break;
		}
	}
	lt_writes(callb, usr, "</div>");
}