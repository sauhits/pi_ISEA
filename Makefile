CC=gcc
CXX=g++
LD=g++
CFLAGS=-c -Wall -pedantic-errors -O3 -std=gnu11 -D_GNU_SOURCE -I../ -g -O0 
LDFLAGS=-pthread 
OBJECTS=main.o ./exp1lib.o
EXECUTABLE=main
# OBJECTS=server.o ./exp1lib.o
# EXECUTABLE=server

# OBJECTS=bench.o ./exp1lib.o
# EXECUTABLE=bench

# OBJECTS=select_main.o ./exp1lib.o
# EXECUTABLE=select_main

# OBJECTS=multi_process_main.o ./exp1lib.o
# EXECUTABLE=multi_process_main

# OBJECTS=multi_thread_main.o ./exp1lib.o
# EXECUTABLE=multi_thread_main

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

.cpp.o:
	$(CXX) $(CFLAGS) $< -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	-rm -f ${EXECUTABLE} ${EXECUTABLE}.exe ${OBJECTS} core