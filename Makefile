V= 2.6.0
CONFIG= ./config

include $(CONFIG)

OBJS= src/luasql.o
SRCS= src/luasql.h src/luasql.c

# list of all driver names
DRIVER_LIST= $(subst src/ls_,,$(basename $(wildcard src/ls_*.c)))

# used for help formatting
EMPTY=
SPACE= $(EMPTY) $(EMPTY)

all : 
	@echo "usage: make { $(subst $(SPACE),$(SPACE)|$(SPACE),$(DRIVER_LIST)) }"

# explicity matches against the list of avilable driver names
$(DRIVER_LIST) : % : src/%.so

# builds the specified driver
src/%.so : src/ls_%.c $(OBJS) 
	$(CC) $(CFLAGS) src/ls_$*.c -o $@ $(LIB_OPTION) $(OBJS) $(DRIVER_INCS_$*) $(DRIVER_LIBS_$*)

# builds the general LuaSQL functions
$(OBJS) : $(SRCS)
	$(CC) $(CFLAGS) -c src/luasql.c -o src/luasql.o

install:
	mkdir -p $(LUA_LIBDIR)/luasql
	cp src/*.so $(LUA_LIBDIR)/luasql

jdbc_driver:
	cd src/jdbc; make $@

clean:
	rm -f src/*.so src/*.o

