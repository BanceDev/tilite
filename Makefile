CC = cc

PREFIX = /usr/local
LIBS = -lX11 -lXinerama -lXcursor

CPPFLAGS = -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Os ${CPPFLAGS} -fdiagnostics-color=always -I/usr/X11R6/include
LDFLAGS = ${LIBS} -L/usr/X11R6/lib

SRC = src/tilite.c
OBJ = build/tilite.o

all: tilite

build/tilite.o: src/tilite.c
	mkdir -p build
	${CC} -c ${CFLAGS} src/tilite.c -o build/tilite.o

tilite: ${OBJ}
	${CC} -o tilite ${OBJ} ${LDFLAGS}

clean:
	rm -rf build tilite

install: all
	mkdir -p ${PREFIX}/bin
	cp -f tilite ${PREFIX}/bin/
	chmod 755 ${PREFIX}/bin/tilite

uninstall:
	rm -f ${PREFIX}/bin/tilite
