#ifndef _LUASQLPG_
#define _LUASQLPG_

#define LUASQL_ENVIRONMENT_PG "PostgreSQL environment"
#define LUASQL_CONNECTION_PG "PostgreSQL connection"
#define LUASQL_CURSOR_PG "PostgreSQL cursor"

LUASQL_API int luasql_libopen_postgres(lua_State *L);

#endif
