#include "config.h"

#include <limits.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <libgen.h>
#include <ctype.h>

#include "cmd.h"
#include "duc.h"
#include "duc-graph.h"


struct param {
	char *key;
	char *val;
	struct param *next;
};

#define DIV_HEADER 1
#define DIV_FOOTER 2

static int opt_apparent = 0;
static int opt_count = 0;
static char *opt_css_url = NULL;
static char *opt_header = NULL;
static char *opt_footer = NULL;
static char *opt_database = NULL;
static char *opt_dbdir = NULL;
static int opt_bytes = 0;
static int opt_list = 0;
static int opt_size = 800;
static int opt_gradient = 0;
static double opt_fuzz = 0.7;
static int opt_levels = 4;
static char *opt_palette = NULL;
static int opt_tooltip = 0;
static int opt_ring_gap = 4;

static struct param *param_list = NULL;

static void print_html(const char *s)
{
	while(*s) {
		switch(*s) {
			case '<': printf("&lt;"); break;
			case '>': printf("&gt;"); break;
			case '&': printf("&amp;"); break;
			case '"': printf("&quot;"); break;
			default: putchar(*s); break;
		}
		s++;
	}
}

static int isrfc1738(const char c)
{
	switch (c) {
		case '$':
		case '-':
		case '_':
		case '.':
		case '+':
		case '!':
		case '*':
		case '(':
		case ')':
			return 1;
		default:
			return 0;
	}
	return 0;
}

static void print_cgi(const char *s)
{
	while(*s) {
		if(*s == '/' || isrfc1738(*s) || isalnum(*s)) {
			putchar(*s);
		} else {
			printf("%%%02x", *(uint8_t *)s);
		}
		s++;
	}
}


static int hexdigit(char a)
{
	if (a >= 'a') {
		a -= 'a'-'A';
	}
	if (a >= 'A') {
		a -= ('A' - 10);
	} else {
		a -= '0';
	}
	return a;
}


void decode_uri(char *src, char *dst)
{       
	char a, b;
	while (*src) {
		if((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
			*dst++ = 16 * hexdigit(a) + hexdigit(b);
			src+=3;
		} else if (*src == '+') {
			*dst++ = ' ';
			src++;
		} else {
			*dst++ = *src++;
		}
	}
	*dst++ = '\0';
}


static int cgi_parse(void)
{
	char *qs = getenv("QUERY_STRING");
	if(qs == NULL) qs = "";

	char *p = qs;

	for(;;) {

		char *pe = strchr(p, '=');
		if(!pe) break;
		char *pn = strchr(pe, '&');
		if(!pn) pn = pe + strlen(pe);

		char *key = p;
		int keylen = pe-p;
		char *val = pe+1;
		int vallen = pn-pe-1;

		struct param *param = malloc(sizeof(struct param));
		assert(param);

		param->key = malloc(keylen+1);
		assert(param->key);
		strncpy(param->key, key, keylen);
		param->key[keylen] = '\0';
		decode_uri(param->key, param->key);

		param->val = malloc(vallen+1);
		assert(param->val);
		strncpy(param->val, val, vallen);
		param->val[vallen] = '\0';
		decode_uri(param->val, param->val);
		
		param->next = param_list;
		param_list = param;

		if(*pn == 0) break;
		p = pn+1;
	}

	return 0;
}


static char *cgi_get(const char *key)
{
	struct param *param = param_list;

	while(param) {
		if(strcmp(param->key, key) == 0) {
			return param->val;
		}
		param = param->next;
	}

	return NULL;
}


static void print_css(void)
{
	printf(
		"<style>\n"
		"body { font-family: \"arial\", \"sans-serif\"; font-size: 11px; }\n"
		"table, thead, tbody, tr, td, th { font-size: inherit; font-family: inherit; }\n"
		"#main { display:table-cell; }\n"
		"#index { border-bottom: solid 1px #777777; }\n"
		"#index table td { padding-left: 5px; }\n"
		"#graph { float: left; }\n"
		"#list { float: left; }\n"
		"#list table { margin-left: auto; margin-right: auto; }\n"
		"#list table td { padding-left: 5px; }\n"
		"#list table td.name, th.name { text-align: left; }\n"
		"#list table td.size, th.size { text-align: right; }\n"
		"#tooltip { display: none; position: absolute; background-color: white; border: solid 1px black; padding: 3px; }\n"
		"</style>\n"
	);
}

/* 
Read in the header or footer (or other file) and send to STDOUT, return NULL if 
successful, -1 for errors. 
*/

int print_html_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if(f == NULL) {
	  duc_log(NULL, DUC_LOG_DBG, "Error reading %s: %s", path, strerror(errno));
	return -1;
    }

    char buf[1024];
    size_t nbytes;

    while (fgets(buf, sizeof(buf) - 1, f) != NULL) {
	printf("%s",buf);
    }

    fclose(f);
    return 0;
}

static void print_div(const int type, const char *path)
{
    int ok = -1;
    if (path) {
	  ok = print_html_file(path);
    }
    
    if (ok == -1) {
	switch(type) {
	case DIV_HEADER:
	    printf(
		"<div id=\"header\">\n"
		"Welcome to <br>"
		"<hr>\n"
		"</div>\n\n"
		);
		break;
	case DIV_FOOTER:
	    printf(
		"<div id=\"footer\">\n"
		"<hr>\n"
		"You can find  duc at <A HREF=\"http://github.com/zevv/duc\">http://github.com/zevv/duc</A>"
		"</div>\n\n"
		);	       
		break;
	default:
		break;
	}
    }
}

static void print_script(const char *path)
{
	printf(
		"<script>\n"
		"  window.onload = function() {\n"
		"    var img = document.getElementById('duc_canvas');\n"
		"    var rect = img.getBoundingClientRect(img);\n"
		"    var tt = document.getElementById('tooltip');\n"
		"    var timer;\n"
		"    img.onmousedown = function(e) {\n"
		"      if(e.button == 0) {\n"
		"        var x = e.clientX - rect.left;\n"
		"        var y = e.clientY - rect.top;\n"
		"        window.location = '?x=' + x + '&y=' + y + '&path=");
	print_html(path);
	printf( "';\n"
		"      }\n"
		"    }\n");

	if(opt_tooltip) {
		printf(
		"    img.onmouseout = function() { tt.style.display = \"none\"; };\n"
		"    img.onmousemove = function(e) {\n"
		"      if(timer) clearTimeout(timer);\n"
		"      timer = setTimeout(function() {\n"
		"        var x = e.clientX - rect.left;\n"
		"        var y = e.clientY - rect.top;\n"
		"        var req = new XMLHttpRequest();\n"
		"        req.onreadystatechange = function() {\n"
		"          if(req.readyState == 4 && req.status == 200) {\n"
		"            tt.innerHTML = req.responseText;\n"
		"            tt.style.display = tt.innerHTML.length > 0 ? \"block\" : \"none\";\n"
		"            tt.style.left = (e.clientX - tt.offsetWidth / 2) + \"px\";\n"
		"            tt.style.top = (e.clientY - tt.offsetHeight - 5) + \"px\";\n"
		"          }\n"
		"        };\n"
		"        req.open(\"GET\", \"?cmd=tooltip&path=%s&x=\"+x+\"&y=\"+y , true);\n"
		"        req.send()\n"
		"      }, 100);\n"
		"    };\n", path);
	}

	printf(
		"  };\n"
		"</script>\n"
	      );
}


static void do_index(duc *duc, duc_graph *graph, duc_dir *dir)
{
    DIR *cgi_db_dir;
	struct dirent *e;
	struct stat sb;
	char name[128];
	int r;

	/* 
	   look for db= CGI parameter, which implies that we're using
	   index_dir mode to support multiple DBs in one CGI instance.
	*/
        char *cgi_db_name = cgi_get("db");
	if (opt_dbdir) {
	  cgi_db_dir = opendir(opt_dbdir);
	  if (!cgi_db_dir) {
		duc_log(NULL, DUC_LOG_DBG, "Error: opendir(%s): %s\n", opt_dbdir, strerror(errno));
		return;
	  }
	  
	  while(e = readdir(cgi_db_dir)) {
		lstat(e->d_name, &sb);
		/* skip all directory entries */
		if (S_ISDIR(sb.st_mode)) {
		  continue;
		}

		/* all passed in CGI string DBs do NOT have the .db extension, so add it
		   to compare to found file name. */
		strcpy(name,cgi_db_name);
		strncat(name,".db",3);
		if (strcmp(e->d_name,name)) {
		  if (duc) {
			duc_close(duc);
		  }

		  r = duc_open(duc, e->d_name, DUC_OPEN_RO);
		  if(r != DUC_OK) {
			printf("Content-Type: text/plain\n\n");
			printf("%s\n", duc_strerror(duc));
			return -1;
		  }
	
		  
		  /* reopen duc_dir *duc here to a new DB I think. */
		}
	  }
	}

	char *path = cgi_get("path");
	char *script = getenv("SCRIPT_NAME");
	if(!script) return;
		
	char url[DUC_PATH_MAX];
	snprintf(url, sizeof url, "%s?cmd=index", script);

	char *xs = cgi_get("x");
	char *ys = cgi_get("y");

	if(dir && xs && ys) {

		int x = atoi(xs);
		int y = atoi(ys);

		duc_dir *dir2 = duc_graph_find_spot(graph, dir, x, y, NULL);
		if(dir2) {
			dir = dir2;
			path = duc_dir_get_path(dir);
		}
	}

	struct duc_index_report *report;
	int i = 0;

	printf(
		"Content-Type: text/html\n"
		"\n"
		"<!DOCTYPE html>\n"
		"<head>\n"
		"  <meta charset=\"utf-8\" />\n"
	);

	if(opt_css_url) {
		printf("<link rel=\"stylesheet\" type=\"text/css\" href=\"%s\">\n", opt_css_url);
	} else {
		print_css();
	}

	if(path) {
		print_script(path);
	}

	printf("</head>\n");

	print_div(DIV_HEADER, opt_header);

	printf("<div id=main>\n");
	printf("<div id=index>\n");
	printf(" <table>\n");
	printf("  <tr>\n");
	printf("   <th>Path</th>\n");
	printf("   <th>Size</th>\n");
	printf("   <th>Files</th>\n");
	printf("   <th>Directories</th>\n");
	printf("   <th>Date</th>\n");
	printf("   <th>Time</th>\n");
	printf("  </tr>\n");

	while( (report = duc_get_report(duc, i)) != NULL) {

		char ts_date[32];
		char ts_time[32];
		time_t t = report->time_start.tv_sec;
		struct tm *tm = localtime(&t);
		strftime(ts_date, sizeof ts_date, "%Y-%m-%d",tm);
		strftime(ts_time, sizeof ts_time, "%H:%M:%S",tm);
	
		duc_size_type st = opt_apparent ? DUC_SIZE_TYPE_APPARENT : DUC_SIZE_TYPE_ACTUAL;

		char siz[32];
		duc_human_size(&report->size, st, 0, siz, sizeof siz);

		printf("  <tr>\n");
		printf("   <td><a href=\"%s&path=", url);
		print_cgi(report->path);
		printf("\">");
		print_html(report->path);
		printf("</a></td>\n");
		printf("   <td>%s</td>\n", siz);
		printf("   <td>%zu</td>\n", report->file_count);
		printf("   <td>%zu</td>\n", report->dir_count);
		printf("   <td>%s</td>\n", ts_date);
		printf("   <td>%s</td>\n", ts_time);
		printf("  </tr>\n");

		duc_index_report_free(report);
		i++;
	}
	printf(" </table>\n");

	if(path) {
		printf("<div id=graph>\n");
		duc_graph_draw(graph, dir);
		printf("</div>\n");
	}

	if(path && dir && opt_list) {

		duc_size_type st = opt_count ? DUC_SIZE_TYPE_COUNT : 
	                           opt_apparent ? DUC_SIZE_TYPE_APPARENT : DUC_SIZE_TYPE_ACTUAL;

		printf("<div id=list>\n");
		printf(" <table>\n");
		printf("  <tr>\n");
		printf("   <th class=name>Filename</th>\n");
		printf("   <th class=size>Size</th>\n");
		printf("  </tr>\n");

		duc_dir_rewind(dir);

		struct duc_dirent *e;
		int n = 0;
		while((n++ < 40) && (e = duc_dir_read(dir, st)) != NULL) {
			char siz[32];
			duc_human_size(&e->size, st, opt_bytes, siz, sizeof siz);
			printf("  <tr><td class=name>");

			if(e->type == DUC_FILE_TYPE_DIR) {
				printf("<a href=\"%s&path=", url);
				print_cgi(path);
				printf("/");
				print_cgi(e->name);
				printf("\">");
			}

			print_html(e->name);

			if(e->type == DUC_FILE_TYPE_DIR) 
				printf("</a>\n");

			printf("   <td class=size>%s</td>\n", siz);
			printf("  </tr>\n");
		}

		printf(" </table>\n");
		printf("</div>\n");
	}

	printf("</div>\n");

	if(opt_tooltip) {
		printf("<div id=\"tooltip\"></div>\n");
	}

	print_div(DIV_FOOTER,opt_footer);

	printf("</body>\n");
	printf("</html>\n");

	fflush(stdout);
}


void do_tooltip(duc *duc, duc_graph *graph, duc_dir *dir)
{
	duc_size_type st = opt_count ? DUC_SIZE_TYPE_COUNT : 
			   opt_apparent ? DUC_SIZE_TYPE_APPARENT : DUC_SIZE_TYPE_ACTUAL;

	printf("Content-Type: text/html\n");
	printf("\n");

	char *xs = cgi_get("x");
	char *ys = cgi_get("y");

	if(dir && xs && ys) {

		int x = atoi(xs);
		int y = atoi(ys);

		struct duc_dirent *ent = NULL;
		duc_dir *dir2 = duc_graph_find_spot(graph, dir, x, y, &ent);
		if(dir2) duc_dir_close(dir2);

		if(ent) {
			char siz_app[32], siz_act[32], siz_cnt[32];
			duc_human_size(&ent->size, DUC_SIZE_TYPE_APPARENT, opt_bytes, siz_app, sizeof siz_app);
			duc_human_size(&ent->size, DUC_SIZE_TYPE_ACTUAL, opt_bytes, siz_act, sizeof siz_act);
			duc_human_size(&ent->size, DUC_SIZE_TYPE_COUNT, opt_bytes, siz_cnt, sizeof siz_cnt);
			char *typ = duc_file_type_name(ent->type);
			printf("name: %s<br>\n"
			       "type: %s<br>\n"
			       "actual size: %s<br>\n"
			       "apparent size: %s<br>\n"
			       "file count: %s",
			       ent->name, typ, siz_act, siz_app, siz_cnt);

			free(ent->name);
			free(ent);
		}
	}
}


static int cgi_main(duc *duc, int argc, char **argv)
{
	int r;

	if(getenv("GATEWAY_INTERFACE") == NULL) {
		fprintf(stderr, 
			"The 'cgi' subcommand is used for integrating Duc into a web server.\n"
			"Please refer to the documentation for instructions how to install and configure.\n"
		);
		return(-1);
	}
	
	cgi_parse();

	char *cmd = cgi_get("cmd");
	if(cmd == NULL) cmd = "index";
	
	if (opt_database && opt_dbdir) {
	  fprintf(stderr,
			  "oops, both -d and -D options are used together...\n\n"
			  );
	  return(-1);
	}
	
	r = duc_open(duc, opt_database, DUC_OPEN_RO);
	if(r != DUC_OK) {
	  printf("Content-Type: text/plain\n\n");
	  printf("%s\n", duc_strerror(duc));
	  return -1;
	}
	
	duc_dir *dir = NULL;
	char *path = cgi_get("path");
	if(path) {
		dir = duc_dir_open(duc, path);
		if(dir == NULL) {
			printf("Content-Type: text/plain\n\n");
			printf("%s\n", duc_strerror(duc));
			print_html(path);
			return -1;
		}
	}

	static enum duc_graph_palette palette = 0;
	
	if(opt_palette) {
		char c = tolower(opt_palette[0]);
		if(c == 's') palette = DUC_GRAPH_PALETTE_SIZE;
		if(c == 'r') palette = DUC_GRAPH_PALETTE_RAINBOW;
		if(c == 'g') palette = DUC_GRAPH_PALETTE_GREYSCALE;
		if(c == 'm') palette = DUC_GRAPH_PALETTE_MONOCHROME;
		if(c == 'c') palette = DUC_GRAPH_PALETTE_CLASSIC;
	}

	duc_size_type st = opt_count ? DUC_SIZE_TYPE_COUNT : 
			   opt_apparent ? DUC_SIZE_TYPE_APPARENT : DUC_SIZE_TYPE_ACTUAL;

	duc_graph *graph = duc_graph_new_html(duc, stdout, 0);
	duc_graph_set_size(graph, opt_size, opt_size);
	duc_graph_set_max_level(graph, opt_levels);
	duc_graph_set_fuzz(graph, opt_fuzz);
	duc_graph_set_palette(graph, palette);
	duc_graph_set_exact_bytes(graph, opt_bytes);
	duc_graph_set_size_type(graph, st);
	duc_graph_set_ring_gap(graph, opt_ring_gap);
	duc_graph_set_gradient(graph, opt_gradient);

	if(strcmp(cmd, "index") == 0) do_index(duc, graph, dir);
	if(strcmp(cmd, "tooltip") == 0) do_tooltip(duc, graph, dir);

	duc_close(duc);

	return 0;
}


static struct ducrc_option options[] = {
	{ &opt_apparent,  "apparent",  'a', DUCRC_TYPE_BOOL,   "Show apparent instead of actual file size" },
	{ &opt_bytes,     "bytes",     'b', DUCRC_TYPE_BOOL,   "show file size in exact number of bytes" },
	{ &opt_count,     "count",      0,  DUCRC_TYPE_BOOL,   "show number of files instead of file size" },
	{ &opt_css_url,   "css-url",    0,  DUCRC_TYPE_STRING, "url of CSS style sheet to use instead of default CSS" },
	{ &opt_database,  "database",  'd', DUCRC_TYPE_STRING, "select database file to use [~/.duc.db]" },
	{ &opt_dbdir,     "dbdir",      0, DUCRC_TYPE_STRING,  "Directory to search for DBs.  Required for building index." },
	{ &opt_footer,    "footer",     0, DUCRC_TYPE_STRING, "select html file to include in footer div" },
	{ &opt_fuzz,      "fuzz",       0,  DUCRC_TYPE_DOUBLE, "use radius fuzz factor when drawing graph [0.7]" },
	{ &opt_gradient,  "gradient",   0,  DUCRC_TYPE_BOOL,   "draw graph with color gradient" },
	{ &opt_header,    "header",     0, DUCRC_TYPE_STRING, "select html file to include in header div" },
	{ &opt_levels,    "levels",    'l', DUCRC_TYPE_INT,    "draw up to ARG levels deep [4]" },
	{ &opt_list,      "list",       0,  DUCRC_TYPE_BOOL,   "generate table with file list" },
	{ &opt_palette,   "palette",    0,  DUCRC_TYPE_STRING, "select palette",
		"available palettes are: size, rainbow, greyscale, monochrome, classic" },
	{ &opt_ring_gap,  "ring-gap",   0,  DUCRC_TYPE_INT,    "leave a gap of VAL pixels between rings" },
	{ &opt_size,      "size",      's', DUCRC_TYPE_INT,    "image size [800]" },
	{ &opt_tooltip,   "tooltip",    0,  DUCRC_TYPE_BOOL,   "enable tooltip when hovering over the graph",
		"enabling the tooltip will cause an asynchronous HTTP request every time the mouse is moved and "
		"can greatly increase the HTTP traffic to the web server" },
	{ NULL }
};

struct cmd cmd_cgi = {
	.name = "cgi",
	.descr_short = "CGI interface wrapper",
	.usage = "[options] [PATH]",
	.main = cgi_main,
	.options = options,
		
};

/*
 * End
 */

