CC       = gcc
LIBS     = 
CFLAGS   = -O2 -g -Wno-unused-result

OBJS     =

.PHONY: all clean

all:    dtbtool

clean: 
	rm *.o
	rm $(all)

dtbtool: dtbtool.o  $(OBJS)
	gcc $^ -o $@ $(LIBS)
