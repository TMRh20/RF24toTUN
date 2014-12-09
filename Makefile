#############################################################################
#
# Makefile for RF24toTUN
#
# License: MIT
# Author:  Rei <devel@reixd.net>
# Date:    22.08.2014
#
# Description:
# ------------
# use make all and make install to install the examples
# You can change the install directory by editing the prefix line
#

#**************************************************************************
#**** Use optional 'MESH=1' condition to compile with RF24Mesh support ****

prefix := /usr/local

# The recommended compiler flags for the Raspberry Pi
CCFLAGS=-Ofast -mfpu=vfp -mfloat-abi=hard -march=armv6zk -mtune=arm1176jzf-s -std=c++0x

# The needed libraries
LIBS=-lrf24-bcm -lrf24network -lboost_thread -lboost_system

# Optionally include the RF24Mesh library, and define USE_RF24MESH so its functions are included
ifeq "$(MESH)" "1"
LIBS+=-lrf24mesh
CCFLAGS+=-DUSE_RF24MESH
endif

# define all programs
PROGRAMS = rf24totun
SOURCES = rf24totun.cpp

all: ${PROGRAMS}

${PROGRAMS}: ${SOURCES}
	g++ ${CCFLAGS} -W -pedantic -Wall ${LIBS} $@.cpp -o $@

clean:
	rm -rf $(PROGRAMS)

install: all
	test -d $(prefix) || mkdir $(prefix)
	test -d $(prefix)/bin || mkdir $(prefix)/bin
	for prog in $(PROGRAMS); do \
	 install -m 0755 $$prog $(prefix)/bin; \
	done


.PHONY: install
