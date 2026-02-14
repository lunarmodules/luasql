local release = "2.8.0"
package = "LuaSQL-Postgres"
version = release.."-1"
source = {
  url = "git+https://github.com/lunarmodules/luasql.git",
  branch = release,
}
description = {
   summary = "Database connectivity for Lua (Postgres driver)",
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
   PGSQL = {
      header = "libpq-fe.h"
   }
}
build = {
   type = "builtin",
   modules = {
     ["luasql.postgres"] = {
       sources = { "src/luasql.c", "src/ls_postgres.c" },
       defines = { "LUASQL_VERSION_NUMBER=\"" .. release .. "\"" },
       libraries = { "pq" },
       incdirs = { "$(PGSQL_INCDIR)" },
       libdirs = { "$(PGSQL_LIBDIR)" }
     }
   }
}
