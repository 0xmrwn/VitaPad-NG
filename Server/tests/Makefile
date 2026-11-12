# Host tests and tools for the pure UI modules (no Vita headers). Run: make -C Server/tests test
CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -Werror -I../source -I../kernel -I../../Common
PURE = ../source/zones.c ../source/mirror.c ../source/screen.c ../source/stats.c ../source/uiplan.c
HEADERS = $(wildcard ../source/*.h)

.PHONY: test clean

test: out/test_ui out/plan_dump out/zones_lint
	out/test_ui
	out/zones_lint

out:
	mkdir -p out

out/test_ui: test_ui.c $(PURE) $(HEADERS) | out
	$(CC) $(CFLAGS) test_ui.c $(PURE) -o $@ -lm

out/plan_dump: plan_dump.c $(PURE) $(HEADERS) | out
	$(CC) $(CFLAGS) plan_dump.c $(PURE) -o $@ -lm

out/zones_lint: zones_lint.c $(PURE) $(HEADERS) | out
	$(CC) $(CFLAGS) zones_lint.c $(PURE) -o $@ -lm

clean:
	rm -rf out
