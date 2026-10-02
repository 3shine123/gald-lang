# Terminal::Ncurses Demo
# Requires: ncurses (brew install ncurses)
# Run: make run
# Debug memory corruption: make asan && ./ncurses_demo_asan

GALDC = ../../target/debug/galdc
NCURSES ?= /opt/homebrew/opt/ncurses
SAN = -fsanitize=address

all: ncurses_demo

ncurses_demo.c: ncurses_demo.gm
	$(GALDC) -rewrite-gald ncurses_demo.gm -o ncurses_demo.c -fno-gald-arc

ncurses_demo: ncurses_demo.c
	clang -std=c99 -fblocks \
		-I ../../include -I ../../include/Foundation -I ../../include/Terminal \
		-I $(NCURSES)/include \
		ncurses_demo.c \
		../../include/gald/runtime.c \
		../../include/Terminal/ncurses_mouse.c \
		$(NCURSES)/lib/libncurses.dylib \
		-Wl,-rpath,$(NCURSES)/lib \
		-o ncurses_demo -w

asan: ncurses_demo.c
	clang -std=c99 -fblocks $(SAN) \
		-I ../../include -I ../../include/Foundation -I ../../include/Terminal \
		-I $(NCURSES)/include \
		ncurses_demo.c \
		../../include/gald/runtime.c \
		../../include/Terminal/ncurses_mouse.c \
		$(NCURSES)/lib/libncurses.dylib \
		-Wl,-rpath,$(NCURSES)/lib \
		-o ncurses_demo_asan -w -g

run: ncurses_demo
	./ncurses_demo

clean:
	rm -f ncurses_demo.c ncurses_demo ncurses_demo_asan

.PHONY: all asan run clean