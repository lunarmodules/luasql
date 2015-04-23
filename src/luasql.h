/*
** $Id: luasql.h,v 1.12 2009/02/07 23:16:23 tomas Exp $
** See Copyright Notice in license.html
*/

#ifndef _LUASQL_
#define _LUASQL_

#ifndef LUASQL_API
#define LUASQL_API
#endif

#if !defined LUA_VERSION_NUM
/* Lua 5.0 */
#define luaL_Reg luaL_reg
#endif

#if LUA_VERSION_NUM>=503
#define luasql_pushinteger lua_pushinteger
#define luasql_isinteger lua_isinteger
#else
#define luasql_pushinteger lua_pushnumber
#define luasql_isinteger lua_isnumber
#endif

#define LUASQL_PREFIX "LuaSQL: "
#define LUASQL_TABLENAME "luasql"
#define LUASQL_ENVIRONMENT "Each driver must have an environment metatable"
#define LUASQL_CONNECTION "Each driver must have a connection metatable"
#define LUASQL_CURSOR "Each driver must have a cursor metatable"

LUASQL_API int luasql_faildirect (lua_State *L, const char *err);
LUASQL_API int luasql_failmsg (lua_State *L, const char *err, const char *m);
LUASQL_API int luasql_createmeta (lua_State *L, const char *name, const luaL_Reg *methods);
LUASQL_API void luasql_setmeta (lua_State *L, const char *name);
LUASQL_API void luasql_set_info (lua_State *L);

LUASQL_API const char* luasql_table_optstring(lua_State *L, int idx, const char* name, const char* def);
LUASQL_API lua_Number luasql_table_optnumber(lua_State *L, int idx, const char* name, lua_Number def);

LUASQL_API void luasql_find_driver_table (lua_State *L);

void luasql_registerobj(lua_State *L, int index, void *obj);
void luasql_unregisterobj(lua_State *L, void *obj);

#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM==501
void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup);
#endif

#endif
