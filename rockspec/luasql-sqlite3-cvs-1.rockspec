package = "LuaSQL-SQLite3"
version = "cvs-1"
source = {
  url = "git://github.com/keplerproject/luasql.git"
}
description = {
   summary = "Database connectivity for Lua (SQLite3 driver)",
   detailed = [[
      LuaSQL is a simple interface from Lua to a DBMS. It enables a
      Lua program to connect to databases, execute arbitrary SQL statements
      and retrieve results in a row-by-row cursor fashion.
   ]],
   license = "MIT/X11",
   homepage = "http://www.keplerproject.org/luasql/"
}
dependencies = {
   "lua >= 5.1"
}
external_dependencies = {
   SQLITE = {
      header = "sqlite3.h"
   }
}
build = {
   type = "make",
   variables = {
      T="sqlite3",
      LIB_OPTION = "$(LIBFLAG) -L$(SQLITE_LIBDIR) -lsqlite3",
      CFLAGS = "$(CFLAGS) -I$(LUA_INCDIR) -I$(SQLITE_INCDIR)"
   },
   build_variables = {
      DRIVER_LIBS="",
   },
   install_variables = {
      LUA_LIBDIR = "$(LIBDIR)",
   },
   platforms = {
     win32 = {
       variables = {
         LIB_OPTION = "$(SQLITE_LIB) $(LUA_LIBDIR)\\lua5.1.lib",
         CFLAGS = "$(CFLAGS) /I$(LUA_INCDIR) /I$(SQLITE_INCDIR)"
       }
     }
   }
}
