#include "config.h"

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <math.h> // min

#include "cmd.h"
#include "duc.h"
	

static bool opt_apparent = false;
static char *opt_database = NULL;
static double opt_min_size = 0;
static double opt_min_size_relative = 0;
static int opt_max_num_items = -1;
static bool opt_exclude_files = false;

#define FORMAT_DIR "\", \"size_apparent\": %jd, \"size_actual\": %jd, \"count\": %jd, \"children\": ["

static void indent(int n)
{
	int i;
	for(i=0; i<n; i++) {
		putchar('	');
	}
}


static void print_escaped(const char *s)
{
	while(*s) {
		switch(*s) {
			case '\\': printf("\\\\"); break;
			case '	': printf("\\t"); break;
			case '\n': printf("\\n"); break;
			case '"': printf("\\\""); break;
			default:
				if(*s >= 0 && *s < 32) {
					printf("#x%02x", *(uint8_t *)s);
				} else {
					putchar(*s); 
				}
				break;
		}
		s++;
	}
}


static int dump(duc *duc, duc_dir *dir, int depth, off_t min_size, int ex_files)
{
	// return num items inside
	struct duc_dirent *e;
	int num_items = 0;
	bool is_first = true;

	duc_size_type st = opt_apparent ? DUC_SIZE_TYPE_APPARENT : DUC_SIZE_TYPE_ACTUAL;

	while( (e = duc_dir_read(dir, DUC_SIZE_TYPE_ACTUAL, DUC_SORT_SIZE)) != NULL) {

		off_t size = duc_get_size(&e->size, st);

		if( size >= min_size) {

			if (is_first) {
				printf("\n");
				is_first = false;
			} else {
				printf(",\n");
			}

			if(e->type == DUC_FILE_TYPE_DIR ) {
				duc_dir *dir_child = duc_dir_openent(dir, e);
				if(dir_child) {
					num_items += 1;

					indent(depth);
					printf("{ \"name\": \"");
					print_escaped(e->name);
					printf(FORMAT_DIR, (intmax_t)e->size.apparent, (intmax_t)e->size.actual, (intmax_t)e->size.count);
					int num_child_items = dump(duc, dir_child, depth + 1, min_size, ex_files);
					num_items += num_child_items;
					if (num_child_items) {
						printf("\n");
						indent(depth);
					}
					printf("] }");
				}
			} else {
				if(!ex_files ) {
					num_items += 1;

					indent(depth);
					printf("{ \"name\": \"");
					print_escaped(e->name);
					printf("\", \"size_apparent\": %jd, \"size_actual\": %jd }", (intmax_t)e->size.apparent, (intmax_t)e->size.actual);				
				}
			}
		}
	}

	return num_items;
}


static int json_main(duc *duc, int argc, char **argv)
{
	char *path = ".";
	if(argc > 0) path = argv[0];

	int r = duc_open(duc, opt_database, DUC_OPEN_RO);
	if(r != DUC_OK) {
		duc_log(duc, DUC_LOG_FTL, "%s", duc_strerror(duc));
		return -1;
	}

	duc_dir *dir = duc_dir_open(duc, path);
	if(dir == NULL) {
		duc_log(duc, DUC_LOG_FTL, "%s", duc_strerror(duc));
		return -1;
	}

	// printf("{\n");
	// struct duc_index_report *report;
	// if( (report = duc_get_report(duc, 0)) != NULL) {

	// 	char ts_date[32];
	// 	char ts_time[32];
	// 	time_t t = report->time_start.tv_sec;
	// 	struct tm *tm = localtime(&t);
	// 	strftime(ts_date, sizeof ts_date, "%Y-%m-%d", tm);
	// 	strftime(ts_time, sizeof ts_time, "%H:%M:%S", tm);
	
	// 	printf("\"metadata\": {}")




	// 	printf("  <tr>\n");
	// 	printf("   <td><a href=\"%s&path=", url);
	// 	print_cgi(report->path);
	// 	printf("\">");
	// 	print_html(report->path);
	// 	printf("</a></td>\n");
	// 	printf("   <td>%s</td>\n", siz);
	// 	printf("   <td>%zu</td>\n", report->file_count);
	// 	printf("   <td>%zu</td>\n", report->dir_count);
	// 	printf("   <td>%s</td>\n", ts_date);
	// 	printf("   <td>%s</td>\n", ts_time);
	// 	printf("  </tr>\n");

	// 	duc_index_report_free(report);
		
	// 	printf("},\n")
	// }

	// printf("\"filesystem\": \n");

	struct duc_size size;
	duc_dir_get_size(dir, &size);
	off_t size_min = fmax(opt_min_size, opt_min_size_relative * size.actual);

	printf("{ \"name\": \"");
	print_escaped(path);
	printf(FORMAT_DIR, (intmax_t)size.apparent, (intmax_t)size.actual, (intmax_t)size.count);
	dump(duc, dir, 1, size_min, opt_exclude_files);
	printf("\n] }\n");

	// printf("}");

	duc_dir_close(dir);
	duc_close(duc);

	return 0;
}


static struct ducrc_option options[] = {
	{ &opt_apparent,      "apparent",      'a', DUCRC_TYPE_BOOL,   "interpret min_size/-s value as apparent size" },
	{ &opt_database,      "database",      'd', DUCRC_TYPE_STRING, "select database file to use [~/.duc.db]" },
	{ &opt_exclude_files, "exclude-files", 'x', DUCRC_TYPE_BOOL,   "exclude file from output, only include directories" },
	{ &opt_min_size,      "min_size",      's', DUCRC_TYPE_DOUBLE, "specify min size for files or directories" },
	{ &opt_min_size_relative,      "min_size_relative",      'r', DUCRC_TYPE_DOUBLE, "specify min size for files or directories, as a fraction of the top-level PATH's size" },
	{ &opt_max_num_items,      "max_num_items",      'i', DUCRC_TYPE_INT, "maximum number of items in the response, further items are discarded" },
	{ NULL }
};


struct cmd cmd_json = {
	.name = "json",
	.descr_short = "Dump JSON output",
	.usage = "[options] [PATH]",
	.main = json_main,
	.options = options,
};


/*
 * End
 */

//./duc json /net/ic1arch.epfl.ch/ic_cvlab_1_arch_nfs/cvlabsrc1 --database=../cvlabsrc1-scan.duc.db --min_size_relative=0.005 > scan_005.json   