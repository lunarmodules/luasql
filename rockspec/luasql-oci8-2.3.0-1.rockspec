package = "LuaSQL-OCI8"
version = "2.3.0-1"
source = {
  url = "git://github.com/keplerproject/luasql.git",
  branch = "v2.3.0",
}
description = {
   summary = "Database connectivity for Lua (Oracle driver)",
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
   OCI8 = {
      header = "oci.h"
   }
}
build = {
   type = "builtin",
   modules = {
     ["luasql.oci8"] = {
       sources = { "src/luasql.c", "src/ls_oci8.c" },
       libraries = { "z", "clntsh", },
       incdirs = { "$(OCI8_INCDIR)" },
       libdirs = { "$(OCI8_LIBDIR)" }
     }
   }
}
