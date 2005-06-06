V= 2.0.1
CONFIG= ./config

include $(CONFIG)

OBJS= $(COMPAT_DIR)/compat-5.1.o src/luasql.o src/ls_$T.o


SRCS= src/luasql.h src/luasql.c \
	src/ls_postgres.c \
	src/ls_odbc.c \
	src/ls_oci8.c \
	src/ls_mysql.c \
	src/ls_sqlite.c

AR= ar rcu
RANLIB= ranlib


lib: src/$(LIBNAME)

src/$(LIBNAME): $(OBJS)
	export MACOSX_DEPLOYMENT_TARGET="10.3"; $(CC) $(CFLAGS) -o $@ $(LIB_OPTION) $(OBJS) $(DRIVER_LIBS)

$(COMPAT_DIR)/compat-5.1.o: $(COMPAT_DIR)/compat-5.1.c
	$(CC) -c $(CFLAGS) -o $@ $(COMPAT_DIR)/compat-5.1.c

install:
	mkdir -p $(LUA_LIBDIR)/luasql
	cp src/$(LIBNAME) $(LUA_LIBDIR)/luasql
	ln -f -s $(LUA_LIBDIR)/luasql/$(LIBNAME) $(LUA_LIBDIR)/luasql/$T.so

jdbc_driver:
	cd src/jdbc; make $@

clean:
	rm -f src/$(LIBNAME) src/*.o $(COMPAT_DIR)/compat-5.1.o

# $Id: Makefile,v 1.47 2005/06/06 22:24:36 tomas Exp $
