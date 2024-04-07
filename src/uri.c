#include <lt/str.h>
#include <lt/io.h>
#include <lt/strstream.h>
#include <lt/mem.h>

#include "server.h"

lstr_t urlencode(lstr_t str) {
	lt_strstream_t ss;
	LT_ASSERT(lt_strstream_create(&ss, lt_libc_heap) == LT_SUCCESS);

	return ss.str;
}

lstr_t urldecode(lstr_t str) {
	lt_strstream_t ss;
	LT_ASSERT(lt_strstream_create(&ss, lt_libc_heap) == LT_SUCCESS);

	for (char* it = str.str, *end = it + str.len; it < end;) {
		char c = *it++;
		if (c != '%') {
			lt_strstream_writec(&ss, c);
			continue;
		}

		if (it + 2 > end) {
			lt_werrf("url encoded escape sequence broken by end-of-string\n");
			break;
		}

		lstr_t enc = LSTR(it, 2);
		u64 dec;
		if (lt_lshextou(enc, &dec) != LT_SUCCESS) {
			lt_werrf("invalid url encoded escape sequence '%%%S'\n", enc);
			lt_strstream_writec(&ss, '%');
			lt_strstream_writels(&ss, enc);
		}
		else {
			lt_strstream_writec(&ss, (u8)dec);
		}

		it += 2;
	}

	return ss.str;
}

lstr_t resolve(lstr_t str) {
	lt_strstream_t ss;
	LT_ASSERT(lt_strstream_create(&ss, lt_libc_heap) == LT_SUCCESS);

	char* it = str.str, *end = it + str.len;
	for (;;) {
		lstr_t dir = lt_lssplit(lt_lsfrom_range(it, end), '/');

		if (dir.len && !lt_lseq(dir, CLSTR(".")) && !lt_lseq(dir, CLSTR(".."))) {
			lt_strstream_writec(&ss, '/');
			lt_strstream_writels(&ss, dir);
		}
		it += dir.len + 1;

		if (it >= end) {
			break;
		}
	}

	if (!ss.str.len) {
		lt_strstream_writec(&ss, '/');
	}

	return ss.str;
}

uri_t parse_uri(lstr_t str) {
	lstr_t page = lt_lssplit(str, '?');
	lstr_t query = NLSTR();
	if (page.len != str.len) {
		query = LSTR(str.str + page.len + 1, str.len - page.len - 1);
	}
	page = urldecode(page);

	uri_t uri = {
			.page = resolve(page),
			.query = query,
			.param_count = 0 };

	lt_mfree(lt_libc_heap, page.str);

	// This does not handle ?asdf&fdsa=2
	for (char* it = query.str, *end = it + query.len; it < end;) {
		char* key_start = it;
		while (it < end && *it != '=') {
			++it;
		}
		lstr_t key = lt_lsfrom_range(key_start, it++);

		char* val_start = it;
		while (it < end && *it != '&') {
			++it;
		}
		lstr_t val = lt_lsfrom_range(val_start, it++);

		uri.params[uri.param_count] = urldecode(key);
		uri.param_vals[uri.param_count++] = urldecode(val);

		if (uri.param_count == MAX_URI_PARAMS) {
			lt_werrf("max uri param limit reached, dropped trailing data\n");
			break;
		}
	}

	return uri;
}

void free_uri(const uri_t* uri) {
	lt_mfree(lt_libc_heap, uri->page.str);
	for (usz i = 0; i < uri->param_count; ++i) {
		lt_mfree(lt_libc_heap, uri->params[i].str);
		lt_mfree(lt_libc_heap, uri->param_vals[i].str);
	}
}

lstr_t* uri_find_param(uri_t* uri, lstr_t param) {
	for (usz i = 0; i < uri->param_count; ++i) {
		if (lt_lseq_nocase(uri->params[i], param)) {
			return &uri->param_vals[i];
		}
	}

	return NULL;
}
