package = "LuaSQL-DuckDB"
version = "2.6.0-3"
source = {
  url = "",
  branch = "2.6.0",
}
description = {
   summary = "Database connectivity for Lua (DuckDB driver)",
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
   DUCKDB = {
      header = "duckdb.h"
   }
}
build = {
   type = "builtin",
   modules = {
     ["luasql.duckdb"] = {
       sources = { "src/luasql.c", "src/ls_duckdb.c" },
       libraries = { "duckdb" },
       incdirs = { "$(DUCKDB_INCDIR)" },
       libdirs = { "$(DUCKDB_LIBDIR)" }
     }
   }
}
