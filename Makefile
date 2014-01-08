#!/usr/bin/env make

PREFIX?=/opt/geany

geany_cflags:=`PKG_CONFIG_PATH="$(PREFIX)/lib/pkgconfig" pkg-config --cflags geany`
geany_ldflags:=`PKG_CONFIG_PATH="$(PREFIX)/lib/pkgconfig" pkg-config --libs geany`

cflags:=$(CFLAGS) -g -Wall -std=c99 $(geany_cflags)
ldflags:=$(LDFLAGS) $(geany_ldflags)

hdrdetect.so: plugin.o
	$(CC) -shared $(cflags) -o $@ $^ $(ldflags)

plugin.o: plugin.c
	$(CC) -c -fPIC $(cflags) -o $@ $<

install:
	cp hdrdetect.so $(PREFIX)/lib/geany

clean:
	$(RM) *.o *.so
