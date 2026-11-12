// Validates a zones.txt on the Mac before copying it to the Vita (UI-SPEC.md 4.4).
// With a file: prints the parsed table in the same grammar, or the first error and its line (exit 0 or 1).
// Without: self-test on the default table text.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zones.h"
#include "padbits.h"

static const char default_text[] =
	"# Default zones: the centre ellipse cuts a hole in the four quadrants\n"
	"Stats ellipse 960 544 240 200\n"
	"L1 rect 0 0 960 544\n"
	"R1 rect 960 0 1920 544\n"
	"L3 rect 0 544 960 1088\n"
	"R3 rect 960 544 1920 1088\n";

static const char *kind_name(const Zone *z){
	if (z->action == ZONE_ACTION_STATS) return "Stats";
	if (z->action == ZONE_ACTION_NONE) return "None";
	if (z->buttons == PAD_L1) return "L1";
	if (z->buttons == PAD_R1) return "R1";
	if (z->buttons == PAD_L3) return "L3";
	return "R3";
}

static void print_table(const ZoneTable *t){
	for (int i = 0; i < t->count; i++){
		const Zone *z = &t->zones[i];
		printf("%s %s %d %d %d %d\n", kind_name(z), z->shape == ZONE_SHAPE_RECT ? "rect" : "ellipse", z->a, z->b, z->c, z->d);
	}
}

static int same_table(const ZoneTable *a, const ZoneTable *b){
	if (a->count != b->count) return 0;
	for (int i = 0; i < a->count; i++){
		const Zone *x = &a->zones[i], *y = &b->zones[i];
		if (x->shape != y->shape || x->action != y->action || x->a != y->a || x->b != y->b
			|| x->c != y->c || x->d != y->d || x->buttons != y->buttons) return 0;
	}
	return 1;
}

// zones_parse only says "rejected", so each line is parsed alone (a one-line file is a valid file)
// to find the first bad one
static int first_bad_line(const char *text, char *bad, size_t bad_size){
	int line_no = 0, zones = 0;
	const char *p = text;
	while (*p){
		line_no++;
		const char *end = p;
		while (*end && *end != '\n') end++;
		char buf[2048];
		size_t len = (size_t)(end - p);
		if (len >= sizeof(buf)) len = sizeof(buf) - 1;
		memcpy(buf, p, len);
		buf[len] = 0;
		char *hash = strchr(buf, '#');
		if (hash) *hash = 0;
		int blank = 1;
		for (char *c = buf; *c; c++) if (*c != ' ' && *c != '\t' && *c != '\r') blank = 0;
		if (!blank){
			ZoneTable one;
			if (zones_parse(buf, &one) < 0){
				snprintf(bad, bad_size, "%s", buf);
				return line_no;
			}
			if (++zones > ZONE_MAX){
				snprintf(bad, bad_size, "%s", buf);
				return line_no;
			}
		}
		p = *end ? end + 1 : end;
	}
	return 0;
}

int main(int argc, char **argv){
	if (argc < 2){
		ZoneTable parsed, def;
		zones_default(&def);
		int n = zones_parse(default_text, &parsed);
		if (n != 5 || !same_table(&parsed, &def)){
			fprintf(stderr, "zones_lint self-test FAILED: the default table text does not match zones_default\n");
			return 1;
		}
		printf("zones_lint self-test: ok (%d zones)\n", n);
		return 0;
	}

	FILE *f = fopen(argv[1], "rb");
	if (!f){
		fprintf(stderr, "%s: cannot open\n", argv[1]);
		return 1;
	}
	static char text[4096];
	size_t len = fread(text, 1, sizeof(text) - 1, f);
	fclose(f);
	text[len] = 0;
	if (len > 2047){
		fprintf(stderr, "%s: error: file is longer than 2047 bytes\n", argv[1]);
		return 1;
	}
	if (strlen(text) != len){
		fprintf(stderr, "%s: error: file contains a NUL byte\n", argv[1]);
		return 1;
	}

	ZoneTable table;
	if (zones_parse(text, &table) < 0){
		char bad[2048];
		int line = first_bad_line(text, bad, sizeof(bad));
		if (line) fprintf(stderr, "%s:%d: error: expected \"<kind> <shape> <a> <b> <c> <d>\" within range, or more than %d zones: %s\n", argv[1], line, ZONE_MAX, bad);
		else fprintf(stderr, "%s: error: no zone in the file\n", argv[1]);
		return 1;
	}
	print_table(&table);
	return 0;
}
