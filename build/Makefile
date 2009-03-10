 #
##
## Makefile for Samurai library
## Copyright (c) 2001-2008, Jan Vidar Krey
##
 #

# BUILDDIR ?= $(CURDIR:%/build/=%)
ROOTDIR  ?= ..
BUILDDIR ?= $(ROOTDIR)/build
COREDIR  ?= $(ROOTDIR)/src
INCDIR   ?= $(ROOTDIR)/include
TESTDIR  ?= $(ROOTDIR)/test

.PHONY: targets
all: targets

include Makefile.common
include Makefile.sources
include Makefile.toolchain

lib_OBJECTS := $(lib_SOURCES:.cpp=$(OBJ_EXT))
lib_DEPENDS := $(lib_SOURCES:.cpp=.d)
lib_ARCHIVE := libsamurai.a
lib_DYNAMIC := libsamurai$(LIB_EXT)

### tests
include Makefile.tests

ALL=$(lib_DYNAMIC) tests
targets: $(ALL)

###### TARGETS - creating binaries

$(lib_DYNAMIC): $(lib_OBJECTS)

$(lib_ARCHIVE): $(lib_OBJECTS)

TARBALL=libsamurai-svn-$(shell date +%Y%m%d)

# this only works on unix
tarball:
	@svn -q export $(COREDIR) $(TARBALL)
	@tar czf $(TARBALL).tar.gz $(TARBALL)
	@rm -rf $(TARBALL)

clean:
	@$(RM) $(lib_OBJECTS)
	@$(RM) $(ALL)
	@$(RM) $(TESTS) $(test_OBJECTS)

autotest: $(lib_ARCHIVE) all
	@cd $(TESTDIR)/exotic && $(MAKE) run


