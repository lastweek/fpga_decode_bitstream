SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)

all: $(OBJS)

clean:
	rm -f *.o

%.o: %.c
	gcc -g -o $@ $(CFLAGS) $(LEGO_INCLUDE) $< -lm -pthread

format: $(SRCS)
	astyle --options=_astylerc $(SRCS)
