#
# FileBrowser gadget class - OpenAmiga project FileBrowser
# http://openamiga.org/?function=gitproject&name=FileBrowser
#
# Builds FileBrowserDemo, which links FileBrowser.o. Applications add
# FileBrowser.c and FileBrowser.h to their own build.
#

CC      = ppc-amigaos-gcc
CFLAGS  = -O2 -Wall -Wextra -Wno-unused-parameter -g
LDFLAGS =
LIBS    =

all: FileBrowserDemo

FileBrowserDemo: FileBrowserDemo.o FileBrowser.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

FileBrowser.o: FileBrowser.c FileBrowser.h
	$(CC) $(CFLAGS) -c -o $@ $<

FileBrowserDemo.o: FileBrowserDemo.c FileBrowser.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o FileBrowserDemo

.PHONY: all clean
