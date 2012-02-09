OBJS = channel.o channeluser.o handlers.o main.o server.o simclist.o utils.o parser.o
DEPS = $(OBJS:.o=.d)
CC = gcc
CFLAGS = -I../../include -g3 -Wall -fpic -std=gnu99 -MMD -MP -DDEBUG
BIN = ../chirc
LDLIBS = -pthread

all: $(BIN)
	
$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) $(LDLIBS) $(OBJS) -o $(BIN)
	
%.d: %.c

clean:
	-rm -f $(OBJS) $(BIN) *.d
