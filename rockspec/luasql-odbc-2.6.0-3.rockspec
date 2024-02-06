package = "LuaSQL-ODBC"
version = "2.6.0-3"
source = {
  url = "git+https://github.com/lunarmodules/luasql.git",
  branch = "2.6.0",
}
description = {
   summary = "Database connectivity for Lua (ODBC driver)",
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
   ODBC = {
      header = "sql.h"
   }
}
build = {
   type = "builtin",
   platforms = {
     windows = { 
       modules = {
         ["luasql.odbc"] = {
           sources = { "src/luasql.c", "src/ls_odbc.c" },  
           incdirs = { "$(ODBC_INCDIR)" },
           libdirs = { "$(ODBC_LIBDIR)" },
           libraries = { "odbc32" }
         }
       }
     },
     unix = { 
       modules = {
         ["luasql.odbc"] = {
           sources = { "src/luasql.c", "src/ls_odbc.c" },  
           incdirs = { "$(ODBC_INCDIR)" },
           libdirs = { "$(ODBC_LIBDIR)" },
           libraries = { "odbc" }
         }
       }
     }
   }
}
