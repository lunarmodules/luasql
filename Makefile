#T= mysql
T= postgres
#T= oci8
#T= odbc
#T= sqlite

#LIB_EXT= .so
LIB_EXT= .dylib
#LIB_OPTION= -shared
LIB_OPTION= -dynamiclib
COMPAT_DIR= ../compat

VERSION= 2.0b2

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
#DRIVER_LIBS= -L/usr/local/mysql/lib -lmysqlclient
#DRIVER_INCS= -I/usr/local/mysql/include

WARN= -Wall -Wmissing-prototypes -Wmissing-declarations -ansi
INCS= -I/usr/local/include/lua5 -I$(COMPAT_DIR) $(DRIVER_INCS)
LIBS= $(DRIVER_LIBS) -llua-5.0 -llualib-5.0 -lm -ldl
CFLAGS= -O2 $(WARN) $(INCS) $(DEFS)
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


lib: $(OBJS)
	$(CC) $(CFLAGS) -o $(LIBNAME) $(LIB_OPTION) $(OBJS) $(LIBS)

compat-5.1.o: $(COMPAT_DIR)/compat-5.1.c
	$(CC) -c $(CFLAGS) -o $@ $(COMPAT_DIR)/compat-5.1.c

dist: dist_dir
	tar -czf $(TAR_FILE) $(DIST_DIR)
	zip -lq $(ZIP_FILE) $(DIST_DIR)/*
	rm -rf $(DIST_DIR)

dist_dir:
	mkdir $(DIST_DIR)
	cp $(SRCS) $(DIST_DIR)

#sqlitelinux: $(SQLITE_LIB) $(SQLITE_SO)

#pglinux: $(PG_LIB) $(PG_SO)

#pgmac: $(PG_LIB) $(PG_DYLIB)

#odbcwin:
	#sed -e "s/VERSION_NUMBER/$(VERSION)/" -e "s/DRIVER/odbc/" def.tmpl > odbc.def

#ocilinux: $(OCI_LIB) $(OCI_SO)

#ociwin:
	#sed -e "s/VERSION_NUMBER/$(VERSION)/" -e "s/DRIVER/oracle/" def.tmpl > oracle.def

#mysqllinux: $(MYSQL_LIB) $(MYSQL_SO)

#mysqlwin:
	#sed -e "s/VERSION_NUMBER/$(VERSION)/" -e "s/DRIVER/mysql/" def.tmpl > mysql.def

#$(SQLITE_LIB): $(LS_OBJ) $(SQLITE_OBJ)
	#$(AR) $@ $(LS_OBJ) $(SQLITE_OBJ)
	#$(RANLIB) $@

#$(SQLITE_SO): $(LS_OBJ) $(SQLITE_OBJ)
	#gcc -o $@ -shared $(LS_OBJ) $(SQLITE_OBJ) $(LIBS_DIR) $(SQLITE_LIBS) $(LIBS)
	#ln -f -s $@ $(SQLITE_LOADLIB)

#$(PG_LIB): $(LS_OBJ) $(PG_OBJ)
	#$(AR) $@ $(LS_OBJ) $(PG_OBJ)
	#$(RANLIB) $@

#$(PG_SO): $(LS_OBJ) $(PG_OBJ)
	#gcc -o $@ -shared $(LS_OBJ) $(PG_OBJ) $(LIBS_DIR) $(PG_LIBS) $(LIBS)
	#ln -f -s $@ $(PG_LOADLIB)

#$(PG_DYLIB): $(LS_OBJ) $(PG_OBJ)
	#gcc -o $@ -dynamiclib $(LS_OBJ) $(PG_OBJ) $(LIBS_DIR) $(PG_LIBS) $(LIBS)
	#ln -f -s $@ $(PG_LOADLIB)

#$(OCI_LIB): $(LS_OBJ) $(OCI_OBJ)
	#$(AR) $@ $(LS_OBJ) $(OCI_OBJ)
	#$(RANLIB) $@

#$(OCI_SO): $(LS_OBJ) $(OCI_OBJ)
	#gcc -o $@ -shared $(LS_OBJ) $(OCI_OBJ) $(LIBS_DIR) $(OCI_LIBS) $(LIBS)
	#ln -f -s $@ $(OCI_LOADLIB)

#$(MYSQL_LIB): $(LS_OBJ) $(MYSQL_OBJ)
	#$(AR) $@ $(LS_OBJ) $(MYSQL_OBJ)
	#$(RANLIB) $@

#$(MYSQL_SO): $(LS_OBJ) $(MYSQL_OBJ)
	#gcc -o $@ -shared $(LS_OBJ) $(MYSQL_OBJ) $(LIBS_DIR) $(MYSQL_LIBS) $(LIBS)
	#ln -f -s $@ $(MYSQL_LOADLIB)

install:
	mkdir -p $(LIB_DIR)
	cp $(LIBNAME) $(LOADLIB) $(LIB_DIR)

clean:
	rm -f $(TAR_FILE) $(ZIP_FILE) $(LIBNAME) *.o *.lua
	#rm -f $(TAR_FILE) $(ZIP_FILE) \
		$(LS_OBJ) $(ODBC_OBJ) $(PG_OBJ) $(OCI_OBJ) $(MYSQL_OBJ) $(SQLITE_OBJ)\
		$(ODBC_LIB) $(ODBC_DLL) $(SQLITE_LIB) $(SQLITE_SO) $(PG_LIB) $(PG_SO) $(PG_DYLIB) $(OCI_LIB) $(OCI_SO) $(MYSQL_LIB) $(MYSQL_SO) \
		sqlite.lua postgres.lua odbc.lua oracle.lua mysql.lua sqlite.def postgres.def odbc.def oracle.def mysql.def
