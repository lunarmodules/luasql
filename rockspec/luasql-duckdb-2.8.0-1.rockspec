local release = "2.8.0"
package = "LuaSQL-DuckDB"
version = release.."-1"
source = {
  url = "git+https://github.com/lunarmodules/luasql.git",
  branch = release,
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
       defines = { "LUASQL_VERSION_NUMBER=\"" .. release .. "\"" },
       libraries = { "duckdb" },
       incdirs = { "$(DUCKDB_INCDIR)" },
       libdirs = { "$(DUCKDB_LIBDIR)" }
     }
   }
}
