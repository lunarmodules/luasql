local release = "2.8.0"
package = "LuaSQL-Firebird"
version = release.."-1"
source = {
  url = "git+https://github.com/lunarmodules/luasql.git",
  branch = release,
}
description = {
   summary = "Database connectivity for Lua (Firebird driver)",
   detailed = [[
      LuaSQL is a simple interface from Lua to a DBMS. It enables a
      Lua program to connect to databases, execute arbitrary SQL statements
      and retrieve results in a row-by-row cursor fashion.
   ]],
   license = "MIT/X11",
   homepage = "https://lunarmodules.github.io/luasql/"
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
       defines = { "LUASQL_VERSION_NUMBER=\"" .. release .. "\"" },
       libraries = { "fbclient" },
       incdirs = { "$(FB_INCDIR)" },
       libdirs = { "$(FB_LIBDIR)" }
     }
   }
}
