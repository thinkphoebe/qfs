CC := gcc
CFLAGS := -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast
LDFLAGS := -lpthread
NAME := qscmd
SRC := $(wildcard src/*.c)
 
$(NAME): $(SRC)
	mkdir -p out
	cp -ra res/default out/
	cp -ra res/qscmd.conf out/
	$(CC) -o out/$(NAME) $(SRC) $(CFLAGS) $(LDFLAGS)

install:
	cp -ra out /opt/qfs

clean:
	rm -rf out
