CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -pedantic
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

.PHONY: all clean install uninstall

BINARIES := iwgrep iwtable

all: $(BINARIES)

iwgrep: iwgrep.c
	$(CC) $(CFLAGS) -o $@ $<

iwtable: iwtable.c
	$(CC) $(CFLAGS) -o $@ $<

install: $(BINARIES)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(BINARIES) $(DESTDIR)$(BINDIR)

uninstall:
	rm -f $(addprefix $(DESTDIR)$(BINDIR)/,$(BINARIES))

clean:
	rm -f $(BINARIES)
