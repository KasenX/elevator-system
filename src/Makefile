CC=gcc
CFLAGS=-pthread -Wall -Wextra -Wfloat-equal -Wundef -Wcast-align -Wwrite-strings -pedantic -g

# Source files
SRCS = call.c car.c controller.c internal.c safety.c shared.c car_vector.c

# Object files
OBJS = $(SRCS:.c=.o)

# Executables
EXECS = call car controller internal safety

all: $(EXECS)

shared.o: shared.c shared.h
	$(CC) $(CFLAGS) -c $< -o $@

call: call.o shared.o
	$(CC) $(CFLAGS) $^ -o $@

car: car.o shared.o
	$(CC) $(CFLAGS) $^ -o $@

controller: controller.o shared.o car_vector.o
	$(CC) $(CFLAGS) $^ -o $@ -lm

internal: internal.o shared.o
	$(CC) $(CFLAGS) $^ -o $@

safety: safety.o
	$(CC) $(CFLAGS) $^ -o $@

controller.o: controller.c shared.h car_vector.h
	$(CC) $(CFLAGS) -c $< -o $@

car_vector.o: car_vector.c car_vector.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c shared.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(EXECS)

.PHONY: all clean call car controller internal safety