# Part 0
# load common stuff
TOPDIR = ..
include $(TOPDIR)/Makefile.common

# backslashify slashes to avoid problems with sed
BPREFIX := $(subst /,\/,$(PREFIX))

CFG := $(wildcard *.cfg)
RES := $(CFG:%.cfg=%)

all: $(RES)

%: %.cfg
	sed 's/PREFIX/$(BPREFIX)/g' < $< > $@

clean:
	$(RM) $(RES) *~

install: 
	install -d  $(PREFIX)/share/fbxkb
#	install -m 644 $(RES) $(PREFIX)/share/fbxkb
	install -d  $(PREFIX)/share/fbxkb/images
	install -m 644 *.png $(PREFIX)/share/fbxkb/images/

uninstall:
	rm -rf $(PREFIX)/share/fbxkb
