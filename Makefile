#T= mysql
T= postgres
#T= oci8
#T= odbc
#T= sqlite

# Installation directory
LUA_LIBDIR= /usr/local/lib/lua/5.0
# Lua includes directory
LUA_INC= /usr/local/include/lua5
COMPAT_DIR= ../compat
LIB_EXT= .so
#LIB_EXT= .dylib
LIB_OPTION= -shared
#LIB_OPTION= -dynamiclib
LUA_LIBS= -llua-5.0 -llualib-5.0 -lm
DLLIB= -ldl

VERSION= 2.0b3

OBJS= compat-5.1.o luasql.o ls_$T.o
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
SRCS= README Makefile \
	luasql.h luasql.c def.tmpl \
	ls_postgres.c \
	ls_odbc.c \
	ls_oci8.c \
	ls_mysql.c \
	ls_sqlite.c \
	test.lua performance.lua \
	index.html manual.html license.html authors.html luasql.png

AR= ar rcu
RANLIB= ranlib


lib: $(LIBNAME)

$(LIBNAME): $(OBJS)
	$(CC) $(CFLAGS) -o $(LIBNAME) $(LIB_OPTION) $(OBJS) $(DRIVER_LIBS) $(LIBS)

compat-5.1.o: $(COMPAT_DIR)/compat-5.1.c
	$(CC) -c $(CFLAGS) -o $@ $(COMPAT_DIR)/compat-5.1.c

dist: dist_dir
	tar -czf $(TAR_FILE) $(DIST_DIR)
	zip -rq $(ZIP_FILE) $(DIST_DIR)/*
	rm -rf $(DIST_DIR)

dist_dir:
	mkdir $(DIST_DIR)
	mkdir -p $(DIST_DIR)/jdbc/src/java/org/keplerproject/luasql/jdbc
	mkdir -p $(DIST_DIR)/jdbc/src/lua
	cp $(SRCS) $(DIST_DIR)
	cp jdbc/Makefile $(DIST_DIR)/jdbc
	cp jdbc/build.xml $(DIST_DIR)/jdbc
	cp jdbc/src/java/org/keplerproject/luasql/jdbc/LuaSQLCursor.java $(DIST_DIR)/jdbc/src/java/org/keplerproject/luasql/jdbc
	cp jdbc/src/lua/jdbc.lua $(DIST_DIR)/jdbc/src/lua

install:
	mkdir -p $(LUA_LIBDIR)/luasql
	cp $(LIBNAME) $(LUA_LIBDIR)/luasql
	cd $(LUA_LIBDIR)/luasql; ln -f -s $(LIBNAME) $(LOADLIB)

jdbc_driver:
	cd jdbc; make $@

clean:
	rm -f $(TAR_FILE) $(ZIP_FILE) $(LIBNAME) *.o
