CC=clang
# linenoise-ng requires stdc++, classic linenoise does not
LUALIBS=-llua -lreadline
# -lncurses
#-llinenoise -lstdc++
SDLLIBS=`sdl-config --libs --cflags` -lSDL_image
LIBS=-lm $(SDLLIBS) $(LUALIBS)

all: vzgpt

vzgpt: main.o loader.o model.o tokens.o glyphgen.o ui_sdl.o ui_tty.o ui_http.o lua.o
	$(CC) -O2 main.o loader.o tokens.o glyphgen.o model.o ui_sdl.o ui_tty.o ui_http.o lua.o -o vzgpt $(LIBS)

main.o: main.c common.h config.h
	$(CC) -O2 -c main.c -D__MAIN__

tokens.o: tokens.c common.h config.h
	$(CC) -O3 -funsafe-math-optimizations -c tokens.c

model.o: model.c common.h config.h
	$(CC) -O3 -funsafe-math-optimizations -c model.c

loader.o: loader.c common.h config.h
	$(CC) -O2 -c loader.c

ui_sdl.o: ui_sdl.c common.h config.h
	$(CC) -O2 -c ui_sdl.c

ui_tty.o: ui_tty.c common.h config.h
	$(CC) -O2 -c ui_tty.c

ui_http.o: ui_http.c common.h config.h
	$(CC) -c ui_http.c

glyphgen.o: glyphgen.c common.h config.h
	$(CC) -O3 -c glyphgen.c

lua.o: lua.c common.h config.h
	$(CC) -O2 -c lua.c

clean:
	rm -f *~ *.o vzgpt DEADJOE
