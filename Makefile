CC = gcc
OBJC = clang
CFLAGS = -O3 -march=native -pipe -std=gnu11 -Wall -Wextra -fno-common
LDFLAGS = -pthread

SRCS = server.c mempool.c
OBJS = $(SRCS:.c=.o)

all: bedrock_core libbedrock.a manager

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

libbedrock.a: $(OBJS)
	ar rcs $@ $^

bedrock_core: libbedrock.a server_stub.c
	$(CC) $(CFLAGS) server_stub.c -L. -lbedrock -o bedrock_core $(LDFLAGS)

manager: libbedrock.a main_objc.m
	$(OBJC) -ObjC -framework Foundation $(CFLAGS) main_objc.m -L. -lbedrock -o manager $(LDFLAGS)

clean:
	rm -f *.o *.a bedrock_core manager
