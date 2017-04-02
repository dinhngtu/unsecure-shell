CFLAGS+=-Wall -Wextra -std=c99 -g

all: unshd unsh

unshd: readcmd.o sockdata.o unshd.o
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@

.PHONY: clean

clean:
	$(RM) *.o unshd unsh
