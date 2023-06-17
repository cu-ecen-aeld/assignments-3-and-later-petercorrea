DEFAULT_CC = gcc 
CC = $(DEFAULT_CC)

ifdef CROSS_COMPILE
	CC = $(CROSS_COMPILE)$(DEFAULT_CC)
endif

CFLAGS = -Wall -Werror
LDFLAGS = 

SRCS = writer.c

OBJS = $(SRCS:.c=.o)

TARGET = writer

all: $(TARGET)
		
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f $(OBJS) $(TARGET)
