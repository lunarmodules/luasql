WARN= -Wall -Wmissing-prototypes -Wmissing-declarations
INCS= -I/usr/local/include/lua5 -I/usr/local/pgsql/include
LIBS_DIR= -L/usr/local/pgsql/lib
LIBS= -llua.5.0 -llualib.5.0 -lm -lpq -lz -ldl
CFLAGS= $(MYCFLAGS) $(WARN) $(INCS) $(DEFS)

ODBC_OBJ= ls_odbc.o
PG_OBJ= ls_pg.o
LS_OBJ= luasql.o

VERSION= 2.0a
PKG= luasql-$(VERSION)
TAR_FILE= $(PKG).tar.gz
ZIP_FILE= $(PKG).zip
SRCS= README Makefile \
	luasql.h luasql.c \
	ls_pg.h ls_pg.c \
	ls_pg.def postgres.tmpl \
	ls_odbc.h ls_odbc.c \
	ls_odbc.def odbc.tmpl \
	test.lua performance.lua \
	index.html manual.html lua.png 

AR= ar rcu
RANLIB= ranlib

ODBC_LIB= libluasqlodbc.$(VERSION).a
PG_LIB= libluasqlpostgres.$(VERSION).a
PG_SO= libluasqlpostgres.$(VERSION).so
PG_DYLIB= libluasqlpostgres.$(VERSION).dylib
ODBC_DLL= luasqlodbc.$(VERSION).dll

dist:
	#cd ..; tar -czf luasql-2.0.tar.gz $(SRCS)
	mkdir $(PKG);
	cp $(SRCS) $(PKG);
	tar -czf $(TAR_FILE) $(PKG);
	zip -lq $(ZIP_FILE) $(PKG)/*
	rm -rf $(PKG)

pglinux: $(PG_LIB) $(PG_SO)
	sed "s/LIB_NAME/$(PG_SO)/" postgres.tmpl > postgres.lua

pgmac: $(PG_LIB) $(PG_DYLIB)
	sed "s/LIB_NAME/$(PG_DYLIB)/" postgres.tmpl > postgres.lua

odbcwin:
	sed "s/LIB_NAME/$(ODBC_DLL)/" odbc.tmpl > odbc.lua

$(PG_LIB): $(LS_OBJ) $(PG_OBJ)
	$(AR) $@ $(LS_OBJ) $(PG_OBJ)
	$(RANLIB) $@

$(PG_SO): $(LS_OBJ) $(PG_OBJ)
	gcc -o $@ -shared $(LS_OBJ) $(PG_OBJ) $(LIBS_DIR) $(LIBS)

$(PG_DYLIB): $(LS_OBJ) $(PG_OBJ)
	gcc -o $@ -dynamiclib $(LS_OBJ) $(PG_OBJ) $(LIBS_DIR) $(LIBS)

clean:
	rm -f $(LS_OBJ) $(ODBC_OBJ) $(PG_OBJ) $(PG_LIB) $(ODBC_LIB) $(PG_DYLIB) postgres.lua odbc.lua
