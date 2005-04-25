#T= mysql
T= postgres
#T= oci8
#T= odbc
#T= sqlite

# Installation directory
LUA_LIBDIR= /usr/local/lib/lua/5.0
# Lua includes directory
LUA_INC= /usr/local/include/lua5
COMPAT_DIR= ../compat/src
LIB_EXT= .so
#LIB_EXT= .dylib
LIB_OPTION= -shared
#LIB_OPTION= -dynamiclib
LUA_LIBS= -llua-5.0 -llualib-5.0 -lm
DLLIB= -ldl

VERSION= 2.0.1

OBJS= $(COMPAT_DIR)/compat-5.1.o src/luasql.o src/ls_$T.o
LIBNAME= lib$T.$(VERSION)$(LIB_EXT)
LOADLIB= $T$(LIB_EXT)

# Driver specific
# SQLite
#DRIVER_LIBS= -lsqlite
#DRIVER_INCS=

# PostgreSQL
DRIVER_LIBS= -L/usr/local/pgsql/lib -lpq
DRIVER_INCS= -I/usr/local/pgsql/include

# Oracle OCI8
#DRIVER_LIBS= -L/home/oracle/OraHome1/lib -lz -lclntsh
#DRIVER_INCS= -Itomas/dblua_oci8/linux/include -I/home/oracle/OraHome1/rdbms/demo -I/home/oracle/OraHome1/rdbms/public

# MySQL
#DRIVER_LIBS= -L/usr/local/mysql/lib -lmysqlclient -lz
#DRIVER_INCS= -I/usr/local/mysql/include

WARN= -Wall -Wmissing-prototypes -Wmissing-declarations -ansi
INCS= -I$(LUA_INC)
LIBS= $(LUA_LIBS) $(DLLIB)
CFLAGS= -O2 $(WARN) -I$(COMPAT_DIR) $(DRIVER_INCS) $(INCS) $(DEFS)
CC= gcc

PKG= luasql-$(VERSION)
DIST_DIR= $(PKG)
TAR_FILE= $(PKG).tar.gz
ZIP_FILE= $(PKG).zip
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
	$(CC) $(CFLAGS) -o src/$(LIBNAME) $(LIB_OPTION) $(OBJS) $(DRIVER_LIBS) $(LIBS)

$(COMPAT_DIR)/compat-5.1.o: $(COMPAT_DIR)/compat-5.1.c
	$(CC) -c $(CFLAGS) -o $@ $(COMPAT_DIR)/compat-5.1.c

dist: dist_dir
	tar -czf $(TAR_FILE) $(DIST_DIR)
	zip -rq $(ZIP_FILE) $(DIST_DIR)/*
	rm -rf $(DIST_DIR)

dist_dir:
	mkdir $(DIST_DIR)
	mkdir -p $(DIST_DIR)/src/jdbc/src/java/org/keplerproject/luasql/jdbc
	mkdir -p $(DIST_DIR)/src/jdbc/src/lua
	mkdir -p $(DIST_DIR)/src/ado
	mkdir -p $(DIST_DIR)/doc/us
	mkdir -p $(DIST_DIR)/tests
	cp README Makefile $(DIST_DIR)
	cp $(SRCS) $(DIST_DIR)/src
	cp tests/test.lua tests/performance.lua tests/example.lua $(DIST_DIR)/tests
	cp doc/us/*.html $(DIST_DIR)/doc/us
	cp doc/us/*.png $(DIST_DIR)/doc/us
	cp src/jdbc/Makefile $(DIST_DIR)/src/jdbc
	cp src/jdbc/build.xml $(DIST_DIR)/src/jdbc
	cp src/jdbc/src/java/org/keplerproject/luasql/jdbc/LuaSQLCursor.java $(DIST_DIR)/src/jdbc/src/java/org/keplerproject/luasql/jdbc
	cp src/jdbc/src/lua/jdbc.lua $(DIST_DIR)/src/jdbc/src/lua
	cp src/ado/ado.lua $(DIST_DIR)/src/ado

install:
	mkdir -p $(LUA_LIBDIR)/luasql
	cp src/$(LIBNAME) $(LUA_LIBDIR)/luasql
	cd $(LUA_LIBDIR)/luasql; ln -f -s $(LIBNAME) $(LOADLIB)

jdbc_driver:
	cd src/jdbc; make $@

clean:
	rm -f $(TAR_FILE) $(ZIP_FILE) src/$(LIBNAME) *.o

# $Id: Makefile,v 1.43 2005/04/25 17:09:11 tomas Exp $
