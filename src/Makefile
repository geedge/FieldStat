MAJOR_VER=2
MINOR_VER=8
RELEASE_DATE=20181118
CC		= g++
GCC			= gcc
CFLAGS		= -Wall
CFLAGS		+= -g -fPIC
LDFLAGS		=  -shared 
LDFLAGS		+= -Wl,--version-script=version.map
H_DIR		= -I../inc
SOURCES		= MESA_field_stat.cpp hdr_histogram.c
OBJS		= $(SOURCES:.c=.o ,.cc=.o)
TARGET_LIB	= libMESA_field_stat2.a
TARGET_SO	= libMESA_field_stat2.so.$(MAJOR_VER).$(MINOR_VER).$(RELEASE_DATE)
.PHONY: all clean deps
.cpp.o:
	$(CC) -c $(CFLAGS) -I. $(H_DIR) $<
.c.o:
	$(GCC) -c $(CFLAGS) -I. $(H_DIR) $<
all: $(TARGET_LIB) $(TARGET_SO)

deps:
	@$(CC) -MM $(SOURCES) $(CFLAGS)

$(TARGET_LIB): MESA_field_stat.o hdr_histogram.o
	ar -r $@ $^
	cp $(TARGET_LIB) ../lib/
$(TARGET_SO): MESA_field_stat.o hdr_histogram.o
	$(CC) $(LDFLAGS) -o $(TARGET_SO) $^ -lrt 
	cp $(TARGET_SO) ../lib/

clean:
	rm -f $(TARGET_LIB) $(TARGET_SO) *.o core core.*
