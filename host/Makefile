# SPDX-License-Identifier: GPL-2.0-or-later
# isalpc: Linux binary, DOS EXE (Open Watcom 2: WATCOM=<dir>), self-test
WATCOM ?= $(HOME)/work/toolchain/openwatcom
WBIN   ?= $(firstword $(wildcard $(WATCOM)/armo64 $(WATCOM)/bino64 $(WATCOM)/binl64 $(WATCOM)/binl))

isalpc: isalpc.c
	$(CC) -O2 -Wall -o $@ $<

ISALPC.EXE: isalpc.c
	WATCOM=$(WATCOM) INCLUDE=$(WATCOM)/h PATH=$(WBIN):$$PATH wcl -q -bt=dos -ms -3 -os -k8192 -fe=$@ $<
	rm -f isalpc.o

test: test_isalpc.c isalpc.c ../fw/src/cfgport.c
	$(CC) -std=c90 -pedantic -Wall -Wextra -Wdeclaration-after-statement -D_POSIX_C_SOURCE=200809L -fsyntax-only isalpc.c   # Watcom wants C89
	$(CC) -Wall -o /tmp/test_isalpc test_isalpc.c && /tmp/test_isalpc >/tmp/test_isalpc.log && tail -1 /tmp/test_isalpc.log

.PHONY: test
