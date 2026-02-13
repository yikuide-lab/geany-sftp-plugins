# Geany SFTP Plugin Makefile

CC = gcc
PKG_CONFIG = pkg-config
VERSION = 1.0.0

# Detect OS
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  PLUGIN = sftp.dylib
  SHARED_FLAG = -dynamiclib
  INSTALL_CMD = install
else ifeq ($(OS),Windows_NT)
  PLUGIN = sftp.dll
  SHARED_FLAG = -shared
  EXTRA_LIBS = -lws2_32
  INSTALL_CMD = install
else
  PLUGIN = sftp.so
  SHARED_FLAG = -shared
  INSTALL_CMD = install
endif

# Directories
PREFIX = $(shell $(PKG_CONFIG) --variable=prefix geany)
LIBDIR = $(shell $(PKG_CONFIG) --variable=libdir geany)
PLUGINDIR = $(LIBDIR)/geany

# Flags
CFLAGS = -Wall -Wextra -fPIC
CFLAGS += $(shell $(PKG_CONFIG) --cflags geany gtk+-3.0 libssh2 json-glib-1.0)
CFLAGS += -I.

LDFLAGS = $(SHARED_FLAG)
LDFLAGS += $(shell $(PKG_CONFIG) --libs geany gtk+-3.0 libssh2 glib-2.0 json-glib-1.0)
LDFLAGS += $(EXTRA_LIBS)

SOURCES = sftp-plugin.c connection.c config.c ui.c sync.c
OBJECTS = $(SOURCES:.c=.o)

DEBUG =

ifdef DEBUG
CFLAGS += -g -DDEBUG
else
CFLAGS += -O2
endif

.PHONY: all clean install uninstall check help info dist

all: $(PLUGIN)

$(PLUGIN): $(OBJECTS)
	@echo "Linking $@..."
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)
	@echo "Build complete: $@"

%.o: %.c sftp-plugin.h compat.h
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

install: $(PLUGIN)
	@echo "Installing plugin..."
	$(INSTALL_CMD) -d $(DESTDIR)$(PLUGINDIR)
	$(INSTALL_CMD) -m 755 $(PLUGIN) $(DESTDIR)$(PLUGINDIR)/$(PLUGIN)
	@echo "Installed to: $(DESTDIR)$(PLUGINDIR)/$(PLUGIN)"

uninstall:
	$(RM) $(DESTDIR)$(PLUGINDIR)/$(PLUGIN)

clean:
	$(RM) $(OBJECTS) sftp.so sftp.dylib sftp.dll
	$(RM) *~ *.bak

check:
	@echo "Checking dependencies..."
	@which $(CC) > /dev/null || (echo "Error: $(CC) not found"; exit 1)
	@$(PKG_CONFIG) --exists geany || (echo "Error: geany dev package not found"; exit 1)
	@$(PKG_CONFIG) --exists gtk+-3.0 || (echo "Error: GTK+3 dev package not found"; exit 1)
	@$(PKG_CONFIG) --exists libssh2 || (echo "Error: libssh2 dev package not found"; exit 1)
	@$(PKG_CONFIG) --exists json-glib-1.0 || (echo "Error: json-glib dev package not found"; exit 1)
	@echo "All dependencies satisfied"

help:
	@echo "Geany SFTP Plugin ($(UNAME_S))"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build plugin (default)"
	@echo "  clean      - Remove build files"
	@echo "  install    - Install plugin"
	@echo "  uninstall  - Remove plugin"
	@echo "  check      - Verify dependencies"
	@echo ""
	@echo "Options:"
	@echo "  make DEBUG=1              - Debug build"
	@echo "  make install DESTDIR=/tmp - Custom install root"

info:
	@echo "OS: $(UNAME_S)"
	@echo "Plugin: $(PLUGIN)"
	@echo "Prefix: $(PREFIX)"
	@echo "Plugin dir: $(PLUGINDIR)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"

dist:
	@DIR=geany-sftp-plugin-$(VERSION); \
	mkdir -p $$DIR; \
	cp *.c *.h Makefile LICENSE README*.md install.sh $$DIR/; \
	tar czf $$DIR.tar.gz $$DIR; \
	rm -rf $$DIR; \
	echo "Created: $$DIR.tar.gz"
