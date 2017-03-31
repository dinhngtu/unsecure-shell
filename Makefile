CFLAGS+=-Wall -Wextra -std=c99 -g

unshd: readcmd.o sockdata.o unshd.o
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@

.PHONY: clean

clean:
	$(RM) *.o unsh
