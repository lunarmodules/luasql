V= 2.0.3
CONFIG= ./config

include $(CONFIG)

ifeq "$(LUA_VERSION_NUM)" "500"
COMPAT_O= $(COMPAT_DIR)/compat-5.1.o
endif

OBJS= src/luasql.o src/ls_$T.o $(COMPAT_O)


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
	cd $(LUA_LIBDIR)/luasql; ln -f -s $(LIBNAME) $T.so

jdbc_driver:
	cd src/jdbc; make $@

clean:
	rm -f src/$(LIBNAME) src/*.o $(COMPAT_O)

# $Id: Makefile,v 1.51 2006/08/22 14:42:59 tomas Exp $
