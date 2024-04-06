#include <lt/html.h>
#include <lt/mem.h>
#include <lt/str.h>

#include "server.h"
#include "template.h"

#define MAX_DEPTH 16

template_stream(file_tree) {
	lstr_t map_route = *srv_get_var(conn, CLSTR("map_route"));
	lstr_t map_target = *srv_get_var(conn, CLSTR("map_target"));

	lt_dir_t* dirs[MAX_DEPTH];
	isz i = 0;
	dirs[i] = lt_dopenp(map_target, lt_libc_heap);
	if (!dirs[i]) {
		lt_werrf("failed to open directory '%S'\n", map_target);
		return;
	}

	char path[LT_PATH_MAX];
	memcpy(path, map_target.str, map_target.len);
	char* path_it = path + map_target.len;

	b8 empty = 1;

	for (;;) {
		lt_dirent_t* ent;
		while ((ent = lt_dread(dirs[i]))) {
			if (lt_lseq(ent->name, CLSTR(".")) || lt_lseq(ent->name, CLSTR(".."))) {
				continue;
			}

			if (path - (path_it + ent->name.len + 1) > LT_PATH_MAX) {
				lt_werrf("path too long, ignoring '%S/%S'\n", lt_lsfrom_range(path, path_it), ent->name);
				continue;
			}

			char* new_it = path_it + lt_sprintf(path_it, "/%S", ent->name);
			lstr_t full_path = lt_lsfrom_range(path, new_it);

			switch (ent->type) {
			case LT_DIRENT_DIR: {
				if (i + 1 == MAX_DEPTH) {
					lt_werrf("max recursion depth reached, ignoring directory '%S'\n", ent->name);
					break;
				}

				lt_dir_t* dir = lt_dopenp(full_path, lt_libc_heap);
				if (!dir) {
					lt_werrf("failed to open directory '%S'\n", full_path);
					break;
				}

				echo("<details><summary class=\"dir text-cyan\" style=\"padding-left: %uzpx\">%S</summary>\n", 16 * i + 2, ent->name);

				dirs[++i] = dir;
				path_it = new_it;
				empty = 0;
			}	break;

			case LT_DIRENT_FILE: {
				lt_stat_t stat;
				lt_err_t err = lt_lstatp(full_path, &stat);
				if (err != LT_SUCCESS) {
					lt_werrf("failed to stat '%S'\n", full_path);
					break;
				}

				lstr_t link = lt_htmlencode(lt_lsfrom_range(path + map_target.len + 1, new_it), lt_libc_heap);
				echo("<p class='file' style='padding-left: %uzpx'><a href='%S/%S'>%S</a><span>%mz</span><p>\n", 16 * i + 2, map_route, link, ent->name, stat.size);
				lt_mfree(lt_libc_heap, link.str);
				empty = 0;
			}	break;

			default:
				break;
			}
		}

		lt_dclose(dirs[i], lt_libc_heap);

		if (i-- == 0) {
			break;
		}

		path_it = path + lt_lsdirname(lt_lsfrom_range(path, path_it)).len;

		echo("</details>");
	}

	if (empty) {
		echo("<p class='bg-normal text-center'>No files available</p>");
	}
}
