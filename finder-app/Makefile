DEFAULT_CC = gcc 
CC = $(DEFAULT_CC)
LDFLAGS = 
SRCS = writer.c
TARGET = writer
CFLAGS = -Wall -Werror

ifdef CROSS_COMPILE
	CC = $(CROSS_COMPILE)$(DEFAULT_CC)
endif

# a list of object files that correspond to the source files.
# It changes the .c extension to .o for each item in SRCS
OBJS = $(SRCS:.c=.o)

# When you type 'make' with no arguments, it builds the all target, which in turn builds $(TARGET)
all: $(TARGET)

# gcc [flags] -o [output name] [input files]
# a rule for how to build .o files from .c files.	
# $< the dependencies of the rule (in this case, the source file), 
# $@ the target of the rule (in this case, the object file)
# -c compile the source file, but do not link
# -o specify the output file name

# %.o: %.c
# 	$(CC) $(CFLAGS) -c $< -o $@

# a rule for building $(TARGET) (writer) from $(OBJS)
# $@ represents the target $(TARGET)
# $^ represents all of the dependencies $(OBJS)
# link the object files into the final executable

# $(TARGET): $(OBJS)
# 	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET): writer.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) $< -o $@ $(LDFLAGS)

# It removes the object files and the final executable.
# The -f option makes rm ignore nonexistent files and not prompt the user
clean:
	rm -f $(OBJS) $(TARGET)
