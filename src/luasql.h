#ifndef _LUASQL_
#define _LUASQL_

#ifndef LUASQL_API
#define LUASQL_API
#endif

#define LUASQL_PREFIX "SQL: "
#define LUASQL_TABLENAME "sql"
#define LUASQL_ENVIRONMENT "Each driver must have an environment metatable"
#define LUASQL_CONNECTION "Each driver must have a connection metatable"
#define LUASQL_CURSOR "Each driver must have a cursor metatable"

LUASQL_API int luasql_createmeta (lua_State *L, const char *name, const luaL_reg *methods);
LUASQL_API void luasql_setmeta (lua_State *L, const char *name);
LUASQL_API void *luasql_getuserdata (lua_State *L, const char *type);
LUASQL_API int lua_sqllibopen (lua_State *L);

#endif
