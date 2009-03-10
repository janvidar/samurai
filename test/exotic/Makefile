 #
##
## Makefile for the Samurai library library
## Copyright (c) 2001-2008, Jan Vidar Krey
##
 #

CXXFLAGS += -I../../include/ -g
LDFLAGS  += -lssl -lz
libsamurai = ../../build/libsamurai.a

SOURCES := \
	file.tcc \
	hash_tiger.tcc \
	inetaddress.tcc \
	sockets.tcc \
	url.tcc

.PHONY: all clear

all: autotest

generated_tests.o: generated_tests.cpp

autotest: generated_tests.o
	g++ -o $@ $^ $(libsamurai) $(LDFLAGS) 
	rm generated_tests.o generated_tests.cpp

generated_tests.cpp:
	$(shell exotic --standalone $(SOURCES) > generated_tests.cpp )

clean:
	@$(RM) *.o 
	@$(RM) autotest generated_tests.cpp


run: clean all
	@gdb -x autotest.gdb --args ./autotest -s -f
