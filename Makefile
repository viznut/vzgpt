CC=clang

all: vzgpt

vzgpt: main.o tokens.o glyphgen.o model.o ui_sdl.o ui_tty.o
	$(CC) -O2 main.o tokens.o glyphgen.o model.o ui_sdl.o ui_tty.o -o vzgpt `sdl-config --libs --cflags` -lSDL_image -lm

main.o: main.c common.h config.h
	$(CC) -O2 -c main.c -D__MAIN__

tokens.o: tokens.c common.h config.h
	$(CC) -O3 -c tokens.c

model.o: model.c common.h config.h
	$(CC) -O3 -funsafe-math-optimizations -c model.c

ui_sdl.o: ui_sdl.c common.h config.h
	$(CC) -O2 -c ui_sdl.c

ui_tty.o: ui_tty.c common.h config.h
	$(CC) -O2 -c ui_tty.c

glyphgen.o: glyphgen.c common.h config.h
	$(CC) -O3 -c glyphgen.c

clean:
	rm -f *~ *.o vzgpt DEADJOE
