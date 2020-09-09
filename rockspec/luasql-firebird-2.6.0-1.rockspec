package = "LuaSQL-Firebird"
version = "2.6.0-1"
source = {
  url = "git://github.com/keplerproject/luasql.git",
  branch = "2.6.0",
}
description = {
   summary = "Database connectivity for Lua (Firebird driver)",
   detailed = [[
      LuaSQL is a simple interface from Lua to a DBMS. It enables a
      Lua program to connect to databases, execute arbitrary SQL statements
      and retrieve results in a row-by-row cursor fashion.
   ]],
   license = "MIT/X11",
   homepage = "http://keplerproject.github.io/luasql/"
}
dependencies = {
   "lua >= 5.1"
}
external_dependencies = {
   FB = {
      header = "ibase.h"
   }
}
build = {
   type = "builtin",
   modules = {
     ["luasql.firebird"] = {
       sources = { "src/luasql.c", "src/ls_firebird.c" },
       libraries = { "fbclient" },
       incdirs = { "$(FB_INCDIR)" },
       libdirs = { "$(FB_LIBDIR)" }
     }
   }
}
