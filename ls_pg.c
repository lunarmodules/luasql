/*
** LuaSQL, PostgreSQL driver
** Authors: Pedro Rabinovitch, Roberto Ierusalimschy, Carlos Cassino
** Tomas Guisasola, Eduardo Quintao
*/

#include <assert.h>
#include <ctype.h>
#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lua5/lua.h>
#include <lua5/lauxlib.h>

#include "luasql.h"
#include "ls_pg.h"


typedef struct env_data {
	unsigned conn_counter;  /* active connections counter */
} env_data;


typedef struct conn_data {
	PGconn *pg_conn;
	int env;                /* reference to environment */
	unsigned cur_counter;   /* active cursors counter */
	int auto_commit;        /* 0 for manual commit */
} conn_data;


typedef struct cur_data {
	PGresult *pg_res;
	int conn;               /* reference to connection */
	int numcols;            /* number of columns */
	int curr_tuple;         /* next tuple to be read */
} cur_data;


static int fail_direct (lua_State *L, const char *msg) {
  lua_pushnil(L);
  lua_pushstring(L, msg);
  return 2;
}


/*
** Push the value of #i field of #tuple row.
*/
static void pushvalue (lua_State *L, PGresult *res, int tuple, int i) {
	if (PQgetisnull (res, tuple, i-1))
		lua_pushnil (L);
	else
		lua_pushstring (L, PQgetvalue (res, tuple, i-1));
}


/*
** Get another row of the given cursor.
*/
static int sqlCurFetch (lua_State *L) {
	cur_data *cursor = (cur_data *)luasql_getuserdata(L, LUASQL_CURSOR_PG);
	PGresult *res = cursor->pg_res;
	int tuple = cursor->curr_tuple;

	if (tuple >= PQntuples(cursor->pg_res)) {
		lua_pushnil(L);  /* no more results */
		return 1;
	}

	cursor->curr_tuple++;
	if (lua_istable (L, 2)) {
		int i;
		const char *opts = luaL_optstring (L, 3, "n");
		if (strchr (opts, 'n') != NULL)
			/* Copy values to numerical indices */
			for (i = 1; i <= cursor->numcols; i++) {
				pushvalue (L, res, tuple, i);
				lua_rawseti (L, 2, i);
			}
		if (strchr (opts, 'a') != NULL)
			/* Copy values to alphanumerical indices */
			for (i = 1; i <= cursor->numcols; i++) {
				lua_pushstring (L, PQfname (res, i-1));
				pushvalue (L, res, tuple, i);
				lua_rawset (L, 2);
			}
		lua_pushvalue(L, 2);
		return 1; /* return table */
	}
	else {
		int i;
		for (i = 1; i <= cursor->numcols; i++)
			pushvalue (L, res, tuple, i);
		return cursor->numcols; /* return #numcols values */
	}
}


/*
** Close the cursor on top of the stack.
** Return 1
*/
static int sqlCurClose (lua_State *L) {
	conn_data *conn;
	cur_data *cursor = (cur_data *)luasql_getuserdata(L, LUASQL_CURSOR_PG);
	if (!cursor->pg_res)
		luaL_error (L, LUASQL_PREFIX"unexpected error (CurClose)");
	/* Remove metatable */
	lua_pushnil (L);
	lua_setmetatable (L, 1);
	/* Decrement the parent's cursor counter. */
	lua_rawgeti (L, LUA_REGISTRYINDEX, cursor->conn);
	conn = (conn_data *)lua_touserdata (L, -1);
	conn->cur_counter--;
	/* Nullify structure fields. */
	luaL_unref (L, LUA_REGISTRYINDEX, cursor->conn);
	cursor->conn = LUA_NOREF;
	PQclear(cursor->pg_res);
	cursor->pg_res = NULL;
	lua_pushnumber(L, 1);
	return 1;
}


/*
** Get the internal database type of the given column.
*/
static char *getcolumntype (PGconn *conn, PGresult *result, int i, char *buff) {
	Oid codigo = PQftype (result, i);
	char stmt[100];
  	PGresult *res;

 	sprintf (stmt, "select typname from pg_type where oid = %d", codigo);
  	res = PQexec(conn, stmt);
	strcpy (buff, "undefined");

	if (PQresultStatus (res) == PGRES_TUPLES_OK) {
		if (PQntuples(res) > 0) {
			char *name = PQgetvalue(res, 0, 0);
			if (strcmp (name, "bpchar")==0 || strcmp (name, "varchar")==0) {
				int modifier = PQfmod (result, i) - 4;
				sprintf (buff, "%.20s (%d)", name, modifier);
			}
			else
				strncpy (buff, name, 20);
		}
	}
	PQclear(res);
	return buff;
}


/*
** Create a table with the names and the types of the fields.
** The names are stored at the position they appear in the result;
** the types are stored in entries named by the corresponding field.
*/
static int sqlCurGetColInfo (lua_State *L) {
	cur_data *cursor = (cur_data *)luasql_getuserdata (L, LUASQL_CURSOR_PG);
	PGresult *result = cursor->pg_res;
	conn_data *conn;
	char typename[100];
	int i;

	lua_rawgeti (L, LUA_REGISTRYINDEX, cursor->conn);
	if (!lua_isuserdata (L, -1))
		luaL_error (L, LUASQL_PREFIX"unexpected error (ColInfo)");
	conn = (conn_data *)lua_touserdata (L, -1);
	lua_newtable (L);
	for (i = 1; i <= cursor->numcols; i++) {
		lua_pushstring(L, PQfname(result, i-1));
		lua_pushvalue(L, -1);
		lua_rawseti(L, -3, i);
		lua_pushstring(L, getcolumntype (conn->pg_conn, result, i-1, typename));
		lua_rawset(L, -3);
	}
	return 1;
}


/*
** Push the number of rows.
*/
static int sqlCurNumRows (lua_State *L) {
	cur_data *cursor = (cur_data *)luasql_getuserdata (L, LUASQL_CURSOR_PG);
	lua_pushnumber (L, PQntuples (cursor->pg_res));
	return 1;
}


/*
** Create a new Cursor object and push it on top of the stack.
*/
static int create_cursor  (lua_State *L, conn_data *conn, PGresult *result) {
	cur_data *cursor = (cur_data *)lua_newuserdata(L, sizeof(cur_data));
	luasql_setmeta (L, LUASQL_CURSOR_PG);

	/* fill in structure */
	cursor->pg_res = result;
	cursor->numcols = PQnfields(result);
	cursor->curr_tuple = 0;
	lua_pushvalue (L, 1);
	cursor->conn = luaL_ref (L, LUA_REGISTRYINDEX);

	return 1;
}


static void sqlCommit(conn_data *conn) {
	PQexec(conn->pg_conn, "COMMIT");
}


static void sqlBegin(conn_data *conn) {
	PQexec(conn->pg_conn, "BEGIN"); 
}


static void sqlRollback(conn_data *conn) {
	PQexec(conn->pg_conn, "ROLLBACK");
}


/*
** Execute an SQL statement.
** Return a Cursor object if the statement is a query, otherwise
** return the number of tuples affected by the statement.
*/
static int sqlConnExecute (lua_State *L) {
	conn_data *conn = (conn_data *)luasql_getuserdata(L, LUASQL_CONNECTION_PG);
	const char *statement = luaL_checkstring (L, 2);
	PGresult *res;
	res = PQexec(conn->pg_conn, statement);
	if (res && PQresultStatus(res)==PGRES_COMMAND_OK) {
		/* no tuples returned */
		lua_pushnumber(L, atof(PQcmdTuples(res)));
		return 1;
	}
	else if (res && PQresultStatus(res)==PGRES_TUPLES_OK) {
		/* tuples returned */
		conn->cur_counter++;
		return create_cursor (L, conn, res);
	}
	else {  /* error */
		return fail_direct(L, PQerrorMessage(conn->pg_conn));
	}
}


/*
** Creates a table with the names of all database tables.
*/
static int sqlConnTableList (lua_State *L) {
	conn_data *conn = (conn_data *)luasql_getuserdata(L, LUASQL_CONNECTION_PG);
	PGresult *pg_res = PQexec(conn->pg_conn,
		"SELECT relname FROM pg_class WHERE relkind = 'r' AND "
		"relowner != (SELECT relowner FROM pg_class WHERE relname = 'pg_class');");
	int numtables = PQntuples(pg_res);
	int i;
	lua_newtable(L);
	for (i=0; i < numtables; i++) {
		lua_pushstring(L, PQgetvalue(pg_res, i, 0));
		lua_rawseti(L, -2, i+1);
	}
	return 1;
}


/*
** Set "auto commit" property of the connection.
** If 'true', then rollback current transaction.
** If 'false', then start a new transaction.
*/
static int sqlConnSetAutoCommit (lua_State *L) {
	conn_data *conn = (conn_data *)luasql_getuserdata(L, LUASQL_CONNECTION_PG);
	if (lua_toboolean (L, 2)) {
		conn->auto_commit = 1;
		sqlRollback(conn); /* Undo active transaction. */
	}
	else {
		conn->auto_commit = 0;
		sqlBegin(conn);
	}
	return 0;
}


/*
** Commit the current transaction.
*/
static int sqlConnCommit (lua_State *L) {
	conn_data *conn = (conn_data *)luasql_getuserdata(L, LUASQL_CONNECTION_PG);
	sqlCommit(conn);
	if (conn->auto_commit == 0) 
		sqlBegin(conn); 

	return 0;
}


/*
** Rollback the current transaction.
*/
static int sqlConnRollback (lua_State *L) {
	conn_data *conn = (conn_data *)luasql_getuserdata(L, LUASQL_CONNECTION_PG);
	sqlRollback(conn);
	if (conn->auto_commit == 0) 
		sqlBegin(conn); 

	return 0;
}


/*
** Close a Connection object.
*/
static int sqlConnClose (lua_State *L) {
	env_data *env;
	conn_data *conn = (conn_data *)luasql_getuserdata(L, LUASQL_CONNECTION_PG);
	if (conn->cur_counter > 0)
		luaL_error (L, LUASQL_PREFIX"unexpected error (ConnClose)");
	lua_pushnil (L);
	lua_setmetatable (L, 1);
	/* Decrement parent's connection counter. */
	lua_rawgeti (L, LUA_REGISTRYINDEX, conn->env);
	env = (env_data *)lua_touserdata (L, -1);
	env->conn_counter--;
	/* Nullify structure fields. */
	luaL_unref (L, LUA_REGISTRYINDEX, conn->env);
	conn->env = LUA_NOREF;
	PQfinish(conn->pg_conn);
	conn->pg_conn = NULL;
	lua_pushnumber(L, 1);
	return 1;
}


/*
** Create a new Connection object and push it on top of the stack.
*/
static int create_connection (lua_State *L, env_data *env, PGconn *const pg_conn) {
	conn_data *conn = (conn_data *)lua_newuserdata(L, sizeof(conn_data));
	luasql_setmeta (L, LUASQL_CONNECTION_PG);
	/* fill in structure */
	conn->pg_conn = pg_conn;
	conn->auto_commit = 1;
	conn->cur_counter = 0;
	lua_pushvalue (L, 1);
	conn->env = luaL_ref (L, LUA_REGISTRYINDEX);
	return 1;
}


static void notice_processor (void *arg, const char *message) {
  (void)arg; (void)message;
  /* arg == NULL */
}


/*
** Connects to a data source.
*/
static int sqlEnvConnect (lua_State *L) {
	env_data *env = (env_data *) luasql_getuserdata(L, LUASQL_ENVIRONMENT_PG); 
	const char *sourcename = luaL_checkstring(L, 2);
	const char *username = luaL_optstring(L, 3, NULL);
	const char *password = luaL_optstring(L, 4, NULL);
	const char *pghost = luaL_optstring(L, 5, NULL);
	const char *pgport = luaL_optstring(L, 6, NULL);

	PGconn *conn = PQsetdbLogin(pghost, pgport, NULL, NULL,
                              sourcename, username, password);
/*	PGconn *conn = PQconnectdb (luaL_check_string(L, 2)); */

	if (PQstatus(conn) == CONNECTION_BAD) {
		const char *msg = LUASQL_PREFIX"Error connecting to database.";
		return fail_direct(L, msg);
	}
	PQsetNoticeProcessor(conn, notice_processor, NULL);
	env->conn_counter++;
	return create_connection(L, env, conn);
}


/*
** Close environment object.
*/
static int sqlEnvClose (lua_State *L) {
	env_data *env = (env_data *)luasql_getuserdata (L, LUASQL_ENVIRONMENT_PG);
	if (env->conn_counter > 0)
		lua_pushnil (L);
	else {
		lua_pushnil (L);
		lua_setmetatable (L, 1);
		lua_pushnumber (L, 1);
	}
	return 1;
}



/*
** Create metatables for each class of object.
*/
static void createmetatables (lua_State *L)
{
    struct luaL_reg environment_methods[] = {
        {"Close", sqlEnvClose},
        {"Connect", sqlEnvConnect},
		{NULL, NULL}
	};
    struct luaL_reg connection_methods[] = {
        {"Close", sqlConnClose},
        {"TableList", sqlConnTableList},
        {"Commit", sqlConnCommit},
        {"Rollback", sqlConnRollback},
        {"Execute", sqlConnExecute},
        {"SetAutoCommit", sqlConnSetAutoCommit},
		{NULL, NULL}
    };
    struct luaL_reg cursor_methods[] = {
        {"Close", sqlCurClose},
        {"Fetch", sqlCurFetch},
        {"ColInfo", sqlCurGetColInfo},
		{"NumRows", sqlCurNumRows},
		{NULL, NULL}
    };
	luasql_createmeta (L, LUASQL_ENVIRONMENT_PG, environment_methods);
	luasql_createmeta (L, LUASQL_CONNECTION_PG, connection_methods);
	luasql_createmeta (L, LUASQL_CURSOR_PG, cursor_methods);
}

/*
** Creates an Environment and returns.
*/
static int sqlOpen (lua_State *L) {
	env_data *env = (env_data *)lua_newuserdata(L, sizeof(env_data));
	env->conn_counter = 0;
	luasql_setmeta (L, LUASQL_ENVIRONMENT_PG);
	return 1;
}


/*
** Creates the global table, registers the Open method for the driver
** and creates the tags.
*/
LUASQL_API int luasql_libopen_postgres (lua_State *L) { 
	lua_getglobal(L, LUASQL_TABLENAME);
	if (lua_isnil (L, -1)) {
		lua_newtable (L);
		lua_pushvalue (L, -1);
		lua_setglobal (L, LUASQL_TABLENAME);
	}
	lua_pushstring(L, "postgres");
	lua_pushcfunction(L, sqlOpen);
	lua_settable(L, -3);

	createmetatables (L);

	return 0;
}
