CC = gcc
CFLAGS = -std=gnu11 -O2 -Wall -Wextra -pipe -g

all: inotify

OBJS = main.o
OBJS += poller.o

inotify: $(OBJS)
	$(CC) -o $@ $(OBJS)

clean:
	@rm -f *.o
