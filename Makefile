VERSION= 2.0a

ODBC_OBJ= ls_odbc.o
ODBC_LIB= libluasqlodbc.$(VERSION).a
ODBC_DLL= luasqlodbc.$(VERSION).dll

PG_OBJ= ls_pg.o
PG_LIB= libluasqlpostgres.$(VERSION).a
PG_SO= libluasqlpostgres.$(VERSION).so
PG_DYLIB= libluasqlpostgres.$(VERSION).dylib
PG_LIBS= -L/usr/local/pgsql/lib -lpq
PG_INCS= -I/usr/local/pgsql/include

OCI_OBJ= ls_oci8.o
OCI_LIB= libluasqloci8.$(VERSION).a
OCI_SO= libluasqloci8.$(VERSION).so
OCI_DLL= luasqloracle.$(VERSION).dll
OCI_LIBS= -L/home/oracle/OraHome1/lib -lz -lclntsh
OCI_INCS= -Itomas/dblua_oci8/linux/include -I/home/oracle/OraHome1/rdbms/demo -I/home/oracle/OraHome1/rdbms/public

MYSQL_OBJ= ls_mysql.o
MYSQL_LIB= libluasqlmysql.$(VERSION).a
MYSQL_SO= libluasqlmysql.$(VERSION).so
MYSQL_DLL= luasqlmysql.$(VERSION).dll
MYSQL_LIBS= -L/usr/local/mysql/lib -lmysqlclient
MYSQL_INCS= -I/usr/local/mysql/include

WARN= -Wall -Wmissing-prototypes -Wmissing-declarations -ansi
INCS= -I/usr/local/include/lua5 $(PG_INCS) $(OCI_INCS) $(MYSQL_INCS)
#LIBS_DIR= -L../lua-5.0/lib
LIBS= -llua.5.0 -llualib.5.0 -lm -ldl
CFLAGS= -O2 $(WARN) $(INCS) $(DEFS)

PKG= luasql-$(VERSION)
TAR_FILE= $(PKG).tar.gz
ZIP_FILE= $(PKG).zip
SRCS= README Makefile \
	luasql.h luasql.c def.tmpl loader.tmpl \
	ls_pg.c \
	ls_odbc.c \
	ls_oci8.c \
	ls_mysql.c \
	test.lua performance.lua \
	index.html manual.html license.html authors.html lua.png 

AR= ar rcu
RANLIB= ranlib

LS_OBJ= luasql.o

dist:
	mkdir $(PKG);
	cp $(SRCS) $(PKG);
	tar -czf $(TAR_FILE) $(PKG);
	zip -lq $(ZIP_FILE) $(PKG)/*
	rm -rf $(PKG)

pglinux: $(PG_LIB) $(PG_SO)
	sed -e "s/LIB_NAME/$(PG_SO)/" -e "s/DRIVER/postgres/" loader.tmpl > postgres.lua

pgmac: $(PG_LIB) $(PG_DYLIB)
	sed -e "s/LIB_NAME/$(PG_DYLIB)/" -e "s/DRIVER/postgres/" loader.tmpl > postgres.lua

odbcwin:
	sed -e "s/LIB_NAME/$(ODBC_DLL)/" -e "s/DRIVER/odbc/" loader.tmpl > odbc.lua
	sed -e "s/VERSION_NUMBER/$(VERSION)/" -e "s/DRIVER/odbc/" def.tmpl > odbc.def

ocilinux: $(OCI_LIB) $(OCI_SO)
	sed -e "s/LIB_NAME/$(OCI_SO)/" -e "s/DRIVER/oracle/" loader.tmpl > oracle.lua

ociwin:
	sed -e "s/LIB_NAME/$(OCI_SO)/" -e "s/DRIVER/oracle/" loader.tmpl > oracle.lua
	sed -e "s/VERSION_NUMBER/$(VERSION)/" -e "s/DRIVER/oracle/" def.tmpl > oracle.def

mysqllinux: $(MYSQL_LIB) $(MYSQL_SO)
	sed -e "s/LIB_NAME/$(MYSQL_SO)/" -e "s/DRIVER/mysql/" loader.tmpl > mysql.lua

mysqlwin:
	sed -e "s/LIB_NAME/$(MYSQL_SO)/" -e "s/DRIVER/mysql/" loader.tmpl > mysql.lua
	sed -e "s/VERSION_NUMBER/$(VERSION)/" -e "s/DRIVER/mysql/" def.tmpl > mysql.def

$(PG_LIB): $(LS_OBJ) $(PG_OBJ)
	$(AR) $@ $(LS_OBJ) $(PG_OBJ)
	$(RANLIB) $@

$(PG_SO): $(LS_OBJ) $(PG_OBJ)
	gcc -o $@ -shared $(LS_OBJ) $(PG_OBJ) $(LIBS_DIR) $(PG_LIBS) $(LIBS)

$(PG_DYLIB): $(LS_OBJ) $(PG_OBJ)
	gcc -o $@ -dynamiclib $(LS_OBJ) $(PG_OBJ) $(LIBS_DIR) $(PG_LIBS) $(LIBS)

$(OCI_LIB): $(LS_OBJ) $(OCI_OBJ)
	$(AR) $@ $(LS_OBJ) $(OCI_OBJ)
	$(RANLIB) $@

$(OCI_SO): $(LS_OBJ) $(OCI_OBJ)
	gcc -o $@ -shared $(LS_OBJ) $(OCI_OBJ) $(LIBS_DIR) $(OCI_LIBS) $(LIBS)

$(MYSQL_LIB): $(LS_OBJ) $(MYSQL_OBJ)
	$(AR) $@ $(LS_OBJ) $(MYSQL_OBJ)
	$(RANLIB) $@

$(MYSQL_SO): $(LS_OBJ) $(MYSQL_OBJ)
	gcc -o $@ -shared $(LS_OBJ) $(MYSQL_OBJ) $(LIBS_DIR) $(MYSQL_LIBS) $(LIBS)

clean:
	rm -f $(TAR_FILE) $(ZIP_FILE) $(LS_OBJ) $(ODBC_OBJ) $(PG_OBJ) $(OCI_OBJ) $(MY_OBJ) $(ODBC_LIB) $(ODBC_DLL) $(PG_LIB) $(PG_SO) $(PG_DYLIB) $(OCI_LIB) $(OCI_SO) $(MYSQL_LIB) $(MYSQL_SO) postgres.lua odbc.lua oracle.lua mysql.lua postgres.def odbc.def oracle.def mysql.def
