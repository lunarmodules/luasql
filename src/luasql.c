/*
** See Copyright Notice in license.html
*/

#include <string.h>

#include "lua.h"
#include "lauxlib.h"


#include "luasql.h"

#if !defined(lua_pushliteral)
#define lua_pushliteral(L, s) \
	lua_pushstring(L, "" s, (sizeof(s)/sizeof(char))-1)
#endif

#if LUA_VERSION_NUM < 502
#define lua_rawlen lua_objlen
#endif


/*
** Typical database error situation
*/
LUASQL_API int luasql_faildirect(lua_State *L, const char *err) {
	lua_pushnil(L);
	lua_pushliteral(L, LUASQL_PREFIX);
	lua_pushstring(L, err);
	lua_concat(L, 2);
	return 2;
}


/*
** Database error with LuaSQL message
** @param err LuaSQL error message.
** @param m Driver error message.
*/
LUASQL_API int luasql_failmsg(lua_State *L, const char *err, const char *m) {
	lua_pushnil(L);
	lua_pushliteral(L, LUASQL_PREFIX);
	lua_pushstring(L, err);
	lua_pushstring(L, m);
	lua_concat(L, 3);
	return 2;
}


typedef struct { short  closed; } pseudo_data;

/*
** Return the name of the object's metatable.
** This function is used by `tostring'.
*/
static int luasql_tostring (lua_State *L) {
	char buff[100];
	pseudo_data *obj = (pseudo_data *)lua_touserdata (L, 1);
	if (obj->closed)
		strcpy (buff, "closed");
	else
		sprintf (buff, "%p", (void *)obj);
	lua_pushfstring (L, "%s (%s)", lua_tostring(L,lua_upvalueindex(1)), buff);
	return 1;
}


#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM==501
/*
** Adapted from Lua 5.2.0
*/
void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup) {
	luaL_checkstack(L, nup+1, "too many upvalues");
	for (; l->name != NULL; l++) {	/* fill the table with given functions */
		int i;
		lua_pushstring(L, l->name);
		for (i = 0; i < nup; i++)	/* copy upvalues to the top */
			lua_pushvalue(L, -(nup + 1));
		lua_pushcclosure(L, l->func, nup);	/* closure with those upvalues */
		lua_settable(L, -(nup + 3));
	}
	lua_pop(L, nup);	/* remove upvalues */
}
#endif

/*
** Create a metatable and leave it on top of the stack.
*/
LUASQL_API int luasql_createmeta (lua_State *L, const char *name, const luaL_Reg *methods) {
	if (!luaL_newmetatable (L, name))
		return 0;

	/* define methods */
	luaL_setfuncs (L, methods, 0);

	/* define metamethods */
	lua_pushliteral (L, "__index");
	lua_pushvalue (L, -2);
	lua_settable (L, -3);

	lua_pushliteral (L, "__tostring");
	lua_pushstring (L, name);
	lua_pushcclosure (L, luasql_tostring, 1);
	lua_settable (L, -3);

	lua_pushliteral (L, "__metatable");
	lua_pushliteral (L, LUASQL_PREFIX"you're not allowed to get this metatable");
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
** __index metamethod for the "type" table.
** Only called when a key is NOT found in the table, so valid lookups
** (e.g. luasql.type.int) have zero overhead.  On a miss it throws an
** informative error listing every valid type name.
*/
static int luasql_type_index (lua_State *L) {
	const char *key = luaL_checkstring (L, 2);
	luaL_Buffer b;
	int first = 1;

	luaL_buffinit (L, &b);
	luaL_addstring (&b, LUASQL_PREFIX "invalid type '");
	luaL_addstring (&b, key);
	luaL_addstring (&b, "', expected one of: ");

	/* iterate the type table (arg 1) to collect valid key names */
	lua_pushnil (L);
	while (lua_next (L, 1) != 0) {
		if (!first)
			luaL_addstring (&b, ", ");
		luaL_addstring (&b, lua_tostring (L, -2));
		first = 0;
		lua_pop (L, 1);  /* pop value, keep key for next iteration */
	}

	luaL_pushresult (&b);
	return lua_error (L);
}


/*
** Creates a "type" sub-table on the module table (assumed on top of stack).
** Registers driver-agnostic type constants: int, number, string, boolean,
** date, time, timestamp, null.
** Sets an __index metamethod that throws on unknown keys.
*/
LUASQL_API void luasql_set_types (lua_State *L) {
	lua_newtable (L);
	lua_pushinteger (L, LUASQL_TYPE_INT);
	lua_setfield (L, -2, "int");
	lua_pushinteger (L, LUASQL_TYPE_NUMBER);
	lua_setfield (L, -2, "number");
	lua_pushinteger (L, LUASQL_TYPE_STRING);
	lua_setfield (L, -2, "string");
	lua_pushinteger (L, LUASQL_TYPE_BOOLEAN);
	lua_setfield (L, -2, "boolean");
	lua_pushinteger (L, LUASQL_TYPE_DATE);
	lua_setfield (L, -2, "date");
	lua_pushinteger (L, LUASQL_TYPE_TIME);
	lua_setfield (L, -2, "time");
	lua_pushinteger (L, LUASQL_TYPE_TIMESTAMP);
	lua_setfield (L, -2, "timestamp");
	lua_pushinteger (L, LUASQL_TYPE_NULL);
	lua_setfield (L, -2, "null");

	/* attach metatable with __index that errors on unknown keys */
	lua_newtable (L);                                  /* metatable */
	lua_pushcfunction (L, luasql_type_index);
	lua_setfield (L, -2, "__index");
	lua_setmetatable (L, -2);              /* setmetatable(type_tbl, mt) */

	lua_setfield (L, -2, "type");
}

/*
** Validates the entire params table passed to stmt:execute().
** tbl_idx: absolute stack index of the params table.
** is_named_out: set to 1 if string keys (named), 0 if integer keys (positional).
**
** Returns 1 on success.
** Returns 0 on failure — pushes nil + errmsg onto the stack.
*/
LUASQL_API int luasql_validate_params (lua_State *L, int tbl_idx, int *is_named_out, int *num_params_out) {
    int is_named = -1;  /* -1=unknown, 1=named, 0=positional */
    int param_count = 0;
    
    /* Convert tbl_idx to absolute index to prevent it from changing when we push nil */
    int abs_tbl_idx = (tbl_idx < 0) ? lua_gettop(L) + tbl_idx + 1 : tbl_idx;

    lua_pushnil(L);
    while (lua_next(L, abs_tbl_idx) != 0) {
        param_count++;
        /* key at -2, value at -1 */

        /* check if keys are named or positional */
        int key_type = lua_type(L, -2);
        if (key_type != LUA_TSTRING && key_type != LUA_TNUMBER) {
            lua_pop(L, 2);
            lua_pushnil(L);
            lua_pushstring(L, LUASQL_PREFIX "parameter keys must be strings or integers");
            return 0;
        }

        if (key_type == LUA_TNUMBER) {
            lua_Number num = lua_tonumber(L, -2);
            lua_Integer integer = lua_tointeger(L, -2);
            if (num != (lua_Number)integer || integer <= 0) {
                lua_pop(L, 2);
                lua_pushnil(L);
                lua_pushstring(L, LUASQL_PREFIX "numeric parameter keys must be positive integers");
                return 0;
            }
        }

        int key_is_str = (key_type == LUA_TSTRING);
        if (is_named == -1) {
            is_named = key_is_str ? 1 : 0;
        } else if (is_named != (key_is_str ? 1 : 0)) {
            lua_pop(L, 2);
            lua_pushnil(L);
            lua_pushstring(L, LUASQL_PREFIX "cannot mix positional and named parameters");
            return 0;
        }

        /* validate value entry */
        if (lua_istable(L, -1)) {
            /* must be exactly {value, luasql.type.X} — length 2 */
            if (lua_rawlen(L, -1) != (size_t)2) {
                lua_pop(L, 2);
                lua_pushnil(L);
                lua_pushstring(L, LUASQL_PREFIX "parameter must be {value, luasql.type.X}");
                return 0;
            }

            /* get type (index 2) */
            lua_rawgeti(L, -1, 2);
            if (lua_type(L, -1) != LUA_TNUMBER) {
                lua_pop(L, 3);
                lua_pushnil(L);
                lua_pushstring(L, LUASQL_PREFIX "second element must be a luasql.type constant");
                return 0;
            }
            int luasql_type = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);  /* pop type */

            /* validate type constant range */
            if (luasql_type < 0 || luasql_type >= LUASQL_TYPE_COUNT) {
                lua_pop(L, 2);
                lua_pushnil(L);
                lua_pushfstring(L, LUASQL_PREFIX "unknown luasql.type constant: %d", luasql_type);
                return 0;
            }

            /* get value (index 1) */
            lua_rawgeti(L, -1, 1);

            /* validate value matches type */
            int ok = 1;
            switch (luasql_type) {
                case LUASQL_TYPE_INT:
                case LUASQL_TYPE_NUMBER:
                    ok = (lua_type(L, -1) == LUA_TNUMBER);
                    break;
                case LUASQL_TYPE_STRING:
                case LUASQL_TYPE_DATE:
                case LUASQL_TYPE_TIME:
                case LUASQL_TYPE_TIMESTAMP:
                    ok = (lua_type(L, -1) == LUA_TSTRING);
                    break;
                case LUASQL_TYPE_BOOLEAN:
                    ok = lua_isboolean(L, -1);
                    break;
                case LUASQL_TYPE_NULL:
                    ok = 1;  /* value doesn't matter when type is null */
                    break;
            }
            lua_pop(L, 1);  /* pop value */

            if (!ok) {
                lua_pop(L, 2);
                lua_pushnil(L);
                lua_pushstring(L, LUASQL_PREFIX "value does not match declared luasql.type");
                return 0;
            }

        } else if (lua_type(L, -1) == LUA_TNUMBER && (int)lua_tointeger(L, -1) == LUASQL_TYPE_NULL) {
            /* bare luasql.type.null constant — always valid */

        } else {
            /* anything else is an error */
            lua_pop(L, 2);
            lua_pushnil(L);
            lua_pushstring(L, LUASQL_PREFIX "parameter must be {value, luasql.type.X} " "or luasql.type.null");
            return 0;
        }

        lua_pop(L, 1);  /* pop value, keep key for next iteration */
    }

    *is_named_out = (is_named == 1) ? 1 : 0;
    if (num_params_out) *num_params_out = param_count;
    return 1;
}


/*
** Assumes the table is on top of the stack.
*/
LUASQL_API void luasql_set_info (lua_State *L) {
	lua_pushliteral (L, "_COPYRIGHT");
	lua_pushliteral (L, "Copyright (C) 2003-2026 Lunar Modules Project");
	lua_settable (L, -3);
	lua_pushliteral (L, "_DESCRIPTION");
	lua_pushliteral (L, "LuaSQL is a simple interface from Lua to a DBMS");
	lua_settable (L, -3);
	lua_pushliteral (L, "_VERSION");
	lua_pushliteral (L, "LuaSQL "LUASQL_VERSION_NUMBER" (for "LUA_VERSION")");
	lua_settable (L, -3);
}
