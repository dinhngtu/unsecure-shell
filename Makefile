CFLAGS+=-Wall -Wextra -std=c99 -g
TARGETS=unshd unsh slowpipe

all: $(TARGETS)

unshd: readcmd.o sockdata.o unshd.o
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@

.PHONY: clean

clean:
	$(RM) *.o $(TARGETS)
