# compilation flags
CXX_FLAGS=-std=c++11 -Ofast -Wall -Wextra -g -march=native
CFLAGS=-O3 -Wall -std=c99 -g
CC=gcc
CXX=g++

# main executables 
EXECS=pfbwt-f pfbwt-f64

# targets not producing a file declared phony
.PHONY: all clean tarfile

all: $(EXECS)

gsa/gsacak.o: gsa/gsacak.c gsa/gsacak.h
	$(CC) $(CFLAGS) -c -o $@ $<

gsa/gsacak64.o: gsa/gsacak.c gsa/gsacak.h
	$(CC) $(CFLAGS) -c -o $@ $< -DM64

pfbwt-f: pfbwt-f.cpp utils.o gsa/gsacak.o pfbwt-f.hpp file_wrappers.hpp
	$(CXX) $(CXX_FLAGS) -I./ -I./sdsl-lite/include -o $@ pfbwt-f.cpp utils.o gsa/gsacak.o -lz

pfbwt-f64: pfbwt-f.cpp utils.o gsa/gsacak64.o pfbwt-f.hpp file_wrappers.hpp
	$(CXX) $(CXX_FLAGS) -DM64 -I./ -I./sdsl-lite/include -o $@ pfbwt-f.cpp utils.o gsa/gsacak64.o -lz

simplebwt: simplebwt.o gsa/gsacak.o
	$(CC) $(CFLAGS) -o $@ $< gsa/gsacak.o

simplebwt64: simplebwt.o gsa/gsacak64.o
	$(CC) $(CFLAGS) -DM64 -o $@ $< gsa/gsacak64.o

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(EXECS) $(EXECS_NT) *.o gsa/*.o
