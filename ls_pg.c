/*
** LuaSQL, PostgreSQL driver
** Authors: Pedro Rabinovitch, Roberto Ierusalimschy, Carlos Cassino
** Tomas Guisasola, Eduardo Quintao
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libpq-fe.h>

#include <lua.h>
#include <lauxlib.h>

#include "luasql.h"
#include "ls_pg.h"

#define LUASQL_ENVIRONMENT_PG "PostgreSQL environment"
#define LUASQL_CONNECTION_PG "PostgreSQL connection"
#define LUASQL_CURSOR_PG "PostgreSQL cursor"

typedef struct {
	short closed;
	unsigned conn_counter;  /* active connections counter */
} env_data;


typedef struct {
	short closed;
	unsigned cur_counter;   /* active cursors counter */
	int env;                /* reference to environment */
	int auto_commit;        /* 0 for manual commit */
	PGconn *pg_conn;
} conn_data;


typedef struct {
	short closed;
	int conn;               /* reference to connection */
	int numcols;            /* number of columns */
	int curr_tuple;         /* next tuple to be read */
	PGresult *pg_res;
} cur_data;


/*
** Check for valid environment.
*/
static env_data *getenvironment (lua_State *L) {
	env_data *env = (env_data *)luaL_checkudata (L, 1, LUASQL_ENVIRONMENT_PG);
	luaL_argcheck (L, env != NULL, 1, "environment expected");
	luaL_argcheck (L, !env->closed, 1, "environment is closed");
	return env;
}


/*
** Check for valid connection.
*/
static conn_data *getconnection (lua_State *L) {
	conn_data *conn = (conn_data *)luaL_checkudata (L, 1, LUASQL_CONNECTION_PG);
	luaL_argcheck (L, conn != NULL, 1, "connection expected");
	luaL_argcheck (L, !conn->closed, 1, "connection is closed");
	return conn;
}


/*
** Check for valid cursor.
*/
static cur_data *getcursor (lua_State *L) {
	cur_data *cur = (cur_data *)luaL_checkudata (L, 1, LUASQL_CURSOR_PG);
	luaL_argcheck (L, cur != NULL, 1, "cursor expected");
	luaL_argcheck (L, !cur->closed, 1, "cursor is closed");
	return cur;
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
static int cur_fetch (lua_State *L) {
	cur_data *cur = (cur_data *)getcursor (L);
	PGresult *res = cur->pg_res;
	int tuple = cur->curr_tuple;

	if (tuple >= PQntuples(cur->pg_res)) {
		lua_pushnil(L);  /* no more results */
		return 1;
	}

	cur->curr_tuple++;
	if (lua_istable (L, 2)) {
		int i;
		const char *opts = luaL_optstring (L, 3, "n");
		if (strchr (opts, 'n') != NULL)
			/* Copy values to numerical indices */
			for (i = 1; i <= cur->numcols; i++) {
				pushvalue (L, res, tuple, i);
				lua_rawseti (L, 2, i);
			}
		if (strchr (opts, 'a') != NULL)
			/* Copy values to alphanumerical indices */
			for (i = 1; i <= cur->numcols; i++) {
				lua_pushstring (L, PQfname (res, i-1));
				pushvalue (L, res, tuple, i);
				lua_rawset (L, 2);
			}
		lua_pushvalue(L, 2);
		return 1; /* return table */
	}
	else {
		int i;
		for (i = 1; i <= cur->numcols; i++)
			pushvalue (L, res, tuple, i);
		return cur->numcols; /* return #numcols values */
	}
}


/*
** Close the cursor on top of the stack.
** Return 1
*/
static int cur_close (lua_State *L) {
	conn_data *conn;
	cur_data *cur = (cur_data *)luaL_checkudata (L, 1, LUASQL_CURSOR_PG);
	if (cur->closed)
		return 0;

	/* Decrement the parent's cursor counter. */
	lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
	conn = (conn_data *)lua_touserdata (L, -1);
	conn->cur_counter--;
	/* Nullify structure fields. */
	cur->closed = 1;
	PQclear(cur->pg_res);
	cur->pg_res = NULL;
	luaL_unref (L, LUA_REGISTRYINDEX, cur->conn);
	cur->conn = LUA_NOREF;
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
static int cur_colinfo (lua_State *L) {
	cur_data *cur = (cur_data *)getcursor (L);
	PGresult *result = cur->pg_res;
	conn_data *conn;
	char typename[100];
	int i;

	lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
	if (!lua_isuserdata (L, -1))
		luaL_error (L, LUASQL_PREFIX"unexpected error (ColInfo)");
	conn = (conn_data *)lua_touserdata (L, -1);
	lua_newtable (L);
	for (i = 1; i <= cur->numcols; i++) {
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
static int cur_numrows (lua_State *L) {
	cur_data *cur = (cur_data *)getcursor (L);
	lua_pushnumber (L, PQntuples (cur->pg_res));
	return 1;
}


/*
** Create a new Cursor object and push it on top of the stack.
*/
static int create_cursor  (lua_State *L, conn_data *conn, PGresult *result) {
	cur_data *cur = (cur_data *)lua_newuserdata(L, sizeof(cur_data));
	luasql_setmeta (L, LUASQL_CURSOR_PG);

	/* fill in structure */
	cur->closed = 0;
	cur->pg_res = result;
	cur->numcols = PQnfields(result);
	cur->curr_tuple = 0;
	lua_pushvalue (L, 1);
	cur->conn = luaL_ref (L, LUA_REGISTRYINDEX);

	return 1;
}


static void sql_commit(conn_data *conn) {
	PQexec(conn->pg_conn, "COMMIT");
}


static void sql_begin(conn_data *conn) {
	PQexec(conn->pg_conn, "BEGIN"); 
}


static void sql_rollback(conn_data *conn) {
	PQexec(conn->pg_conn, "ROLLBACK");
}


/*
** Close a Connection object.
*/
static int conn_close (lua_State *L) {
	env_data *env;
	conn_data *conn = (conn_data *)luaL_checkudata (L, 1, LUASQL_CONNECTION_PG);
	if (conn->closed)
		return 0;
	if (conn->cur_counter > 0)
		luaL_error (L, LUASQL_PREFIX"unexpected error (ConnClose)");

	/* Decrement parent's connection counter. */
	lua_rawgeti (L, LUA_REGISTRYINDEX, conn->env);
	env = (env_data *)lua_touserdata (L, -1);
	env->conn_counter--;
	/* Nullify structure fields. */
	conn->closed = 1;
	luaL_unref (L, LUA_REGISTRYINDEX, conn->env);
	conn->env = LUA_NOREF;
	PQfinish(conn->pg_conn);
	conn->pg_conn = NULL;
	lua_pushnumber(L, 1);
	return 1;
}


/*
** Execute an SQL statement.
** Return a Cursor object if the statement is a query, otherwise
** return the number of tuples affected by the statement.
*/
static int conn_execute (lua_State *L) {
	conn_data *conn = (conn_data *)getconnection (L);
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
		return luasql_faildirect(L, PQerrorMessage(conn->pg_conn));
	}
}


/*
** Creates a table with the names of all database tables.
*/
/*
static int sqlConnTableList (lua_State *L) {
	conn_data *conn = (conn_data *)getconnection (L);
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
*/


/*
** Commit the current transaction.
*/
static int conn_commit (lua_State *L) {
	conn_data *conn = (conn_data *)getconnection (L);
	sql_commit(conn);
	if (conn->auto_commit == 0) 
		sql_begin(conn); 
	return 0;
}


/*
** Rollback the current transaction.
*/
static int conn_rollback (lua_State *L) {
	conn_data *conn = (conn_data *)getconnection (L);
	sql_rollback(conn);
	if (conn->auto_commit == 0) 
		sql_begin(conn); 
	return 0;
}


/*
** Set "auto commit" property of the connection.
** If 'true', then rollback current transaction.
** If 'false', then start a new transaction.
*/
static int conn_setautocommit (lua_State *L) {
	conn_data *conn = (conn_data *)getconnection (L);
	if (lua_toboolean (L, 2)) {
		conn->auto_commit = 1;
		sql_rollback(conn); /* Undo active transaction. */
	}
	else {
		conn->auto_commit = 0;
		sql_begin(conn);
	}
	return 0;
}


/*
** Create a new Connection object and push it on top of the stack.
*/
static int create_connection (lua_State *L, env_data *env, PGconn *const pg_conn) {
	conn_data *conn = (conn_data *)lua_newuserdata(L, sizeof(conn_data));
	luasql_setmeta (L, LUASQL_CONNECTION_PG);

	/* fill in structure */
	conn->closed = 0;
	conn->cur_counter = 0;
	conn->pg_conn = pg_conn;
	conn->auto_commit = 1;
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
** This driver provides two ways to connect to a data source:
** (1) giving the connection parameters as a set of pairs separated
**     by whitespaces in a string (first method parameter)
** (2) giving one string for each connection parameter, said
**     datasource, username, password, host and port.
*/
static int env_connect (lua_State *L) {
	env_data *env = (env_data *) getenvironment (L);
	const char *sourcename = luaL_checkstring(L, 2);
	PGconn *conn;

	if ((lua_gettop (L) == 2) && (strchr (sourcename, '=') != NULL))
		conn = PQconnectdb (luaL_check_string(L, 2));
	else {
		const char *username = luaL_optstring(L, 3, NULL);
		const char *password = luaL_optstring(L, 4, NULL);
		const char *pghost = luaL_optstring(L, 5, NULL);
		const char *pgport = luaL_optstring(L, 6, NULL);

		conn = PQsetdbLogin(pghost, pgport, NULL, NULL,
			sourcename, username, password);
	}

	if (PQstatus(conn) == CONNECTION_BAD)
		return luasql_faildirect(L, LUASQL_PREFIX"Error connecting to database.");
	PQsetNoticeProcessor(conn, notice_processor, NULL);
	env->conn_counter++;
	return create_connection(L, env, conn);
}


/*
** Close environment object.
*/
static int env_close (lua_State *L) {
	env_data *env = (env_data *)luaL_checkudata (L, 1, LUASQL_ENVIRONMENT_PG);
	if (env->closed)
		return 0;
	if (env->conn_counter > 0)
		luaL_error (L, LUASQL_PREFIX"unexpected error (EnvClose)");

	env->closed = 1;
	lua_pushnumber (L, 1);
	return 1;
}



/*
** Create metatables for each class of object.
*/
static void create_metatables (lua_State *L) {
    struct luaL_reg environment_methods[] = {
        {"close", env_close},
        {"connect", env_connect},
		{NULL, NULL},
	};
    struct luaL_reg connection_methods[] = {
        {"close", conn_close},
        /*{"TableList", sqlConnTableList},*/
        {"execute", conn_execute},
        {"commit", conn_commit},
        {"rollback", conn_rollback},
        {"setautocommit", conn_setautocommit},
		{NULL, NULL},
    };
    struct luaL_reg cursor_methods[] = {
        {"close", cur_close},
        {"fetch", cur_fetch},
        {"colinfo", cur_colinfo},
		{"numrows", cur_numrows},
		{NULL, NULL},
    };
	luasql_createmeta (L, LUASQL_ENVIRONMENT_PG, environment_methods);
	luasql_createmeta (L, LUASQL_CONNECTION_PG, connection_methods);
	luasql_createmeta (L, LUASQL_CURSOR_PG, cursor_methods);
}

/*
** Creates an Environment and returns it.
*/
static int create_environment (lua_State *L) {
	env_data *env = (env_data *)lua_newuserdata(L, sizeof(env_data));
	luasql_setmeta (L, LUASQL_ENVIRONMENT_PG);

	/* fill in structure */
	env->closed = 0;
	env->conn_counter = 0;
	return 1;
}


/*
** Creates the metatables for the objects and registers the
** driver open method.
*/
LUASQL_API int luasql_libopen_postgres (lua_State *L) { 
	lua_getglobal(L, LUASQL_TABLENAME);
	if (lua_isnil (L, -1)) {
		lua_newtable (L);
		lua_pushvalue (L, -1);
		lua_setglobal (L, LUASQL_TABLENAME);
	}
	lua_pushstring(L, "postgres");
	lua_pushcfunction(L, create_environment);
	lua_settable(L, -3);

	create_metatables (L);

	return 0;
}
