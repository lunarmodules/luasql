#include <lua.h>
#include <lauxlib.h>

#include "luasql.h"

/*
** Create a metatable
*/
LUASQL_API int luasql_createmeta (lua_State *L, const char *name, const luaL_reg *methods) {
	if (!luaL_newmetatable (L, name))
		return 0;

	/* define methods */
	luaL_openlib (L, NULL, methods, 0);

	/* define metamethods */
	lua_pushliteral (L, "__gc");
	lua_pushcfunction (L, methods->func);
	lua_settable (L, -3);

	lua_pushliteral (L, "__index");
	lua_pushvalue (L, -2);
	lua_settable (L, -3);

	lua_pushliteral (L, "__metatable");
	lua_pushnumber (L, 0); /* !!!!!!! o valor e' usado ? */
	lua_settable (L, -3);

	return 1;
}


/*
** Define the metatable for the object on top of the stack
*/
LUASQL_API void luasql_setmeta (lua_State *L, const char *name) {
	luaL_getmetatable (L, name);
	lua_setmetatable (L, -2);
}


/*
** Check the type (metatable) of #1 arg
** Do NOT pop the argument
*/
/*
LUASQL_API void *luasql_getuserdata (lua_State *L, const char *type) {
	return luaL_checkudata (L, 1, type);
}
*/


/*

static int sqlOpen(lua_State *L) {
    const char *driver = luaL_checkstring(L, 2);
    lua_gettable(L, 1);
    if (lua_isnil(L, -1))
		luaL_error(L, "driver '%s' not loaded.", driver);
    lua_call(L, 0, 2);
    return 2;
}

LUASQL_API int lua_sqllibopen(lua_State *L) {
    lua_newtable(L);
    lua_pushstring(L, "Open");
    lua_pushcfunction(L, sqlOpen);
    lua_settable(L, -3);
    lua_setglobal(L, "sql");
	return 1;
}
*/
