WARN= -Wall -Wmissing-prototypes -Wmissing-declarations
INCS= -I/usr/local/include/lua5 -I/usr/local/pgsql/include
LIBS_DIR= -L/usr/local/pgsql/lib
LIBS= -llua.5.0 -llualib.5.0 -lm -lpq -lz -ldl
CFLAGS= $(MYCFLAGS) $(WARN) $(INCS) $(DEFS)

ODBC_OBJ= ls_odbc.o
PG_OBJ= ls_pg.o
LS_OBJ= luasql.o

LS_DIR= luasql-2.0/
SRCS= $(LS_DIR)README $(LS_DIR)Makefile \
	$(LS_DIR)luasql.h $(LS_DIR)luasql.c \
	$(LS_DIR)ls_pg.h $(LS_DIR)ls_pg.c \
	$(LS_DIR)ls_pg.def $(LS_DIR)postgres.tmpl \
	$(LS_DIR)ls_odbc.h $(LS_DIR)ls_odbc.c \
	$(LS_DIR)ls_odbc.def $(LS_DIR)odbc.tmpl \
	$(LS_DIR)test.lua $(LS_DIR)performance.lua \
	$(LS_DIR)index.html $(LS_DIR)manual.html $(LS_DIR)lua.png 

AR= ar rcu
RANLIB= ranlib

ODBC_LIB= libluasqlodbc.2.0.a
PG_LIB= libluasqlpostgres.2.0.a
PG_SO= libluasqlpostgres.2.0.so
PG_DYLIB= libluasqlpostgres.2.0.dylib
ODBC_DLL= luasqlodbc.2.0.dll

dist:
	cd ..; tar -czf luasql-2.0.tar.gz $(SRCS)

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
