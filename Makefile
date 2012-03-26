V= 2.3.0
CONFIG= ./config

include $(CONFIG)

OBJS= src/luasql.o src/ls_$T.o


SRCS= src/luasql.h src/luasql.c \
	src/ls_firebird.c \
	src/ls_postgres.c \
	src/ls_odbc.c \
	src/ls_oci8.c \
	src/ls_mysql.c \
	src/ls_sqlite.c \
	src/ls_sqlite3.c

AR= ar rcu
RANLIB= ranlib


lib: src/$(LIBNAME)

src/$(LIBNAME): $(OBJS)
	export MACOSX_DEPLOYMENT_TARGET="10.3"; $(CC) $(CFLAGS) -o $@ $(LIB_OPTION) $(OBJS) $(DRIVER_LIBS)

install:
	mkdir -p $(LUA_LIBDIR)/luasql
	cp src/$(LIBNAME) $(LUA_LIBDIR)/luasql

jdbc_driver:
	cd src/jdbc; make $@

clean:
	rm -f src/$(LIBNAME) src/*.o

# $Id: Makefile,v 1.56 2008/05/30 17:21:18 tomas Exp $
