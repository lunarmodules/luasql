package = "LuaSQL-ODBC"
version = "2.3.0-1"
source = {
  url = "git://github.com/keplerproject/luasql.git",
  branch = "v2.3.0",
}
description = {
   summary = "Database connectivity for Lua (ODBC driver)",
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
   ODBC = {
      header = "sql.h"
   }
}
build = {
   type = "builtin",
   modules = {
     ["luasql.odbc"] = {
       sources = { "src/luasql.c", "src/ls_odbc.c" },
       libraries = { "odbc" },
       incdirs = { "$(ODBC_INCDIR)" },
       libdirs = { "$(ODBC_LIBDIR)" }
     }
   }
}
