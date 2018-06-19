CC		= g++
CFLAGS		= -Wall
CFLAGS		+= -g -fPIC
H_DIR		= -I../inc
SOURCES		= MESA_field_stat.c
OBJS		= $(SOURCES:.c=.o)
TARGET_LIB	= libMESA_field_stat2.a
TARGET_SO	= libMESA_field_stat2.so

.PHONY: all clean deps
.c.o:
	$(CC) -c $(CFLAGS) -I. $(H_DIR) $<
all: $(TARGET_LIB) $(TARGET_SO)

deps:
	@$(CC) -MM $(SOURCES) $(CFLAGS)

$(TARGET_LIB): MESA_field_stat.o
	ar -r $@ $^
	cp $(TARGET_LIB) ../lib/
$(TARGET_SO): MESA_field_stat.o
	$(CC) -o $(TARGET_SO) $(CFLAGS) $^ -shared -lrt
	cp $(TARGET_SO) ../lib/

clean:
	rm -f $(TARGET_LIB) $(TARGET_SO) *.o core core.*
