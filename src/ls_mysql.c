/*
** LuaSQL, MySQL driver
** Authors:  Eduardo Quintao
** $Id: ls_mysql.c,v 1.3 2003/07/23 18:32:38 eduquintao Exp $
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <mysql.h>

#include <lua.h>
#include <lauxlib.h>

#include "luasql.h"

#define LUASQL_ENVIRONMENT_MYSQL "MySQL environment"
#define LUASQL_CONNECTION_MYSQL "MySQL connection"
#define LUASQL_CURSOR_MYSQL "MySQL cursor"

typedef struct {
	short      closed;
} env_data;

typedef struct {
	short      closed;
	int        env;                /* reference to environment */
	int        auto_commit;        /* 0 for manual commit */
	MYSQL     *my_conn;
} conn_data;


typedef struct {
	short      closed;
	int        conn;               /* reference to connection */
	int        numcols;            /* number of columns */
	int        numrows;			   /* number of rows returned or affected */
	int        colnames, coltypes; /* reference to column information tables */
	int        curr_tuple;         /* current tuple to be read by fetch */
	MYSQL_RES *my_res;
} cur_data;


typedef void (*creator) (lua_State *L, cur_data *cur);


LUASQL_API int luasql_libopen_mysql (lua_State *L);


/*
** Check for valid environment.
*/
static env_data *getenvironment (lua_State *L) {
	env_data *env = (env_data *)luaL_checkudata (L, 1, LUASQL_ENVIRONMENT_MYSQL);
	luaL_argcheck (L, env != NULL, 1, "environment expected");
	luaL_argcheck (L, !env->closed, 1, "environment is closed");
	return env;
}


/*
** Check for valid connection.
*/
static conn_data *getconnection (lua_State *L) {
	conn_data *conn = (conn_data *)luaL_checkudata (L, 1, LUASQL_CONNECTION_MYSQL);
	luaL_argcheck (L, conn != NULL, 1, "connection expected");
	luaL_argcheck (L, !conn->closed, 1, "connection is closed");
	return conn;
}


/*
** Check for valid cursor.
*/
static cur_data *getcursor (lua_State *L) {
	cur_data *cur = (cur_data *)luaL_checkudata (L, 1, LUASQL_CURSOR_MYSQL);
	luaL_argcheck (L, cur != NULL, 1, "cursor expected");
	luaL_argcheck (L, !cur->closed, 1, "cursor is closed");
	return cur;
}


/*
** Push the value of #i field of #tuple row.
*/
static void pushvalue (lua_State *L, void *row) {
/* Levar em consideracao os tipos mais proximos a lua */
	if (row == NULL)
		lua_pushnil (L);
	else
		lua_pushstring (L, row);
}


/*
** Get another row of the given cursor.
*/
static int cur_fetch (lua_State *L) {
	cur_data *cur = getcursor (L);
	MYSQL_RES *res = cur->my_res;
	MYSQL_ROW row;
	MYSQL_FIELD *fields;
	int tuple = cur->curr_tuple;
	fields = mysql_fetch_fields(res);

	if (tuple >= cur->numrows) {
		lua_pushnil(L);  /* no more results */
		return 1;
	}

	cur->curr_tuple++;
	row = mysql_fetch_row(res);
	if (lua_istable (L, 2)) {
		int i;
		const char *opts = luaL_optstring (L, 3, "n");
		if (strchr (opts, 'n') != NULL)
			/* Copy values to numerical indices */
			for (i = 0; i < cur->numcols; i++) {
				pushvalue (L, row[i]);
				lua_rawseti (L, 2, i+1);
			}
		if (strchr (opts, 'a') != NULL)
			/* Copy values to alphanumerical indices */
			for (i = 0; i < cur->numcols; i++) {
				lua_pushstring (L, fields[i].name);
				pushvalue (L, row[i]);
				lua_rawset (L, 2);
			}
		lua_pushvalue(L, 2);
		return 1; /* return table */
	}
	else {
		int i;
		for (i = 0; i < cur->numcols; i++)
			pushvalue (L, row[i]);
		return cur->numcols; /* return #numcols values */
	}
}


/*
** Close the cursor on top of the stack.
** Return 1
*/
static int cur_close (lua_State *L) {
	cur_data *cur = (cur_data *)luaL_checkudata (L, 1, LUASQL_CURSOR_MYSQL);
	luaL_argcheck (L, cur != NULL, 1, LUASQL_PREFIX"cursor expected");
	if (cur->closed)
		return 0;

	/* Nullify structure fields. */
	cur->closed = 1;
/* Checar o my_res antes */
	mysql_free_result(cur->my_res);
	luaL_unref (L, LUA_REGISTRYINDEX, cur->conn);
	luaL_unref (L, LUA_REGISTRYINDEX, cur->colnames);
	luaL_unref (L, LUA_REGISTRYINDEX, cur->coltypes);

	lua_pushnumber(L, 1);
	return 1;
}


/*
** Get the internal database type of the given column.
*/
static char *getcolumntype (enum enum_field_types type) {

	switch (type) {
		case MYSQL_TYPE_VAR_STRING: case MYSQL_TYPE_STRING:
			return "string";
		case MYSQL_TYPE_DECIMAL: case MYSQL_TYPE_SHORT: case MYSQL_TYPE_LONG:
		case MYSQL_TYPE_FLOAT: case MYSQL_TYPE_DOUBLE: case MYSQL_TYPE_LONGLONG:
		case MYSQL_TYPE_INT24: case MYSQL_TYPE_YEAR: case MYSQL_TYPE_TINY: 
			return "number";
		case MYSQL_TYPE_TINY_BLOB: case MYSQL_TYPE_MEDIUM_BLOB:
		case MYSQL_TYPE_LONG_BLOB: case MYSQL_TYPE_BLOB:
			return "binary";
		case MYSQL_TYPE_DATE: case MYSQL_TYPE_NEWDATE:
			return "date";
		case MYSQL_TYPE_DATETIME:
			return "datetime";
		case MYSQL_TYPE_TIME:
			return "time";
		case MYSQL_TYPE_ENUM: case MYSQL_TYPE_SET:
			return "set";
		case MYSQL_TYPE_TIMESTAMP:
			return "timestamp";
		case MYSQL_TYPE_NULL:
			return "null";
		default:
			return "undefined";
	}
}


/*
** Creates the list of fields names and pushes it on top of the stack.
*/
static void create_colnames (lua_State *L, cur_data *cur) {
/* Posso usar o proprio cur->my_res ???? */
	MYSQL_RES *result = cur->my_res;
	MYSQL_FIELD *fields;
	int i;
	fields = mysql_fetch_fields(result);
	lua_newtable (L);
	for (i = 1; i <= cur->numcols; i++) {
		lua_pushstring (L, fields[i-1].name);
		lua_rawseti (L, -2, i);
	}
}


/*
** Creates the list of fields types and pushes it on top of the stack.
*/
static void create_coltypes (lua_State *L, cur_data *cur) {
	/* conn_data *conn; */
	char typename[100];
	MYSQL_RES *result = cur->my_res;
	MYSQL_FIELD *fields;
	int i;
	fields = mysql_fetch_fields(result);
/*	lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
	if (!lua_isuserdata (L, -1))
		luaL_error (L, LUASQL_PREFIX"invalid connection");
	conn = (conn_data *)lua_touserdata (L, -1);*/
	lua_newtable (L);
	for (i = 1; i <= cur->numcols; i++) {
		sprintf (typename, "%.20s (%d)", getcolumntype (fields[i-1].type), fields[i-1].length);
		lua_pushstring(L, typename);
		lua_rawseti (L, -2, i);
	}
}


/*
** Pushes a column information table on top of the stack.
** If the table isn't built yet, call the creator function and stores
** a reference to it on the cursor structure.
*/
static void _pushtable (lua_State *L, cur_data *cur, size_t off, creator func) {
	int *ref = (int *)((char *)cur + off);
	if (*ref != LUA_NOREF)
		lua_rawgeti (L, LUA_REGISTRYINDEX, *ref);
	else {
		func (L, cur);
		/* Stores a reference to it on the cursor structure */
		lua_pushvalue (L, -1);
		*ref = luaL_ref (L, LUA_REGISTRYINDEX);
	}
}
#define pushtable(L,c,m,f) (_pushtable(L,c,offsetof(cur_data,m),f))


/*
** Return the list of field names.
*/
static int cur_getcolnames (lua_State *L) {
	pushtable (L, getcursor(L), colnames, create_colnames);
	return 1;
}


/*
** Return the list of field types.
*/
static int cur_getcoltypes (lua_State *L) {
	pushtable (L, getcursor(L), coltypes, create_coltypes);
	return 1;
}


/*
** Push the number of rows.
*/
static int cur_numrows (lua_State *L) {
	lua_pushnumber (L, getcursor(L)->numrows );
	return 1;
}


/*
** Create a new Cursor object and push it on top of the stack.
*/
static int create_cursor (lua_State *L, int conn, MYSQL_RES *result, int rows, int cols) {
	cur_data *cur = (cur_data *)lua_newuserdata(L, sizeof(cur_data));
	luasql_setmeta (L, LUASQL_CURSOR_MYSQL);

	/* fill in structure */
	cur->closed = 0;
	cur->conn = LUA_NOREF;
	cur->numcols = cols;
	cur->numrows = rows;
	cur->colnames = LUA_NOREF;
	cur->coltypes = LUA_NOREF;
	cur->curr_tuple = 0;
	cur->my_res = result;
	lua_pushvalue (L, conn);
	cur->conn = luaL_ref (L, LUA_REGISTRYINDEX);

	return 1;
}


/*
** Close a Connection object.
*/
static int conn_close (lua_State *L) {
	conn_data *conn=(conn_data *)luaL_checkudata(L, 1, LUASQL_CONNECTION_MYSQL);
	luaL_argcheck (L, conn != NULL, 1, LUASQL_PREFIX"connection expected");
	if (conn->closed)
		return 0;

	/* Nullify structure fields. */
	conn->closed = 1;
	luaL_unref (L, LUA_REGISTRYINDEX, conn->env);
/* Testar o my_conn ?????? */
	mysql_close(conn->my_conn);
	lua_pushnumber(L, 1);
	return 1;
}


/*
** Execute an SQL statement.
** Return a Cursor object if the statement is a query, otherwise
** return the number of tuples affected by the statement.
*/
static int conn_execute (lua_State *L) {
	conn_data *conn = getconnection (L);
	const char *statement = luaL_checkstring (L, 2);
	unsigned long st_len = strlen(statement);
	if (!mysql_real_query(conn->my_conn, statement, st_len)) {
		unsigned int num_rows, num_cols;
		MYSQL_RES *res;
		res = mysql_store_result(conn->my_conn);
		num_rows = mysql_affected_rows(conn->my_conn);
		num_cols = mysql_field_count(conn->my_conn);
		if (res) { /* tuples returned */
			return create_cursor (L, 1, res, num_rows, num_cols);
		}
		else { /* mysql_store_result() returned nothing; should it have? */
			if(num_cols == 0) { /* no tuples returned */
            	/* query does not return data (it was not a SELECT) */
				lua_pushnumber(L, num_rows);
				return 1;
        	}
			else /* mysql_store_result() should have returned data */
				return luasql_faildirect(L, mysql_error(conn->my_conn));
		}
	}
	else  /* error executing query */
		return luasql_faildirect(L, mysql_error(conn->my_conn));
}


/*
** Commit the current transaction.
*/
static int conn_commit (lua_State *L) {
	conn_data *conn = getconnection (L);
	mysql_commit(conn->my_conn);
	return 0;
}


/*
** Rollback the current transaction.
*/
static int conn_rollback (lua_State *L) {
	conn_data *conn = getconnection (L);
	mysql_rollback(conn->my_conn);
	return 0;
}


/*
** Set "auto commit" property of the connection. Modes ON/OFF
*/
static int conn_setautocommit (lua_State *L) {
	conn_data *conn = getconnection (L);
	if (lua_toboolean (L, 2)) {
		conn->auto_commit = 1;
		mysql_autocommit(conn->my_conn, 1); /* Set it ON */
	}
	else {
		conn->auto_commit = 0;
		mysql_autocommit(conn->my_conn, 0);
	}
	return 0;
}


/*
** Create a new Connection object and push it on top of the stack.
*/
static int create_connection (lua_State *L, int env, MYSQL *const my_conn) {
	conn_data *conn = (conn_data *)lua_newuserdata(L, sizeof(conn_data));
	luasql_setmeta (L, LUASQL_CONNECTION_MYSQL);

	/* fill in structure */
	conn->closed = 0;
	conn->env = LUA_NOREF;
	conn->auto_commit = 1;
	conn->my_conn = my_conn;
	lua_pushvalue (L, env);
	conn->env = luaL_ref (L, LUA_REGISTRYINDEX);
	return 1;
}


/*
** Connects to a data source.
**     param: one string for each connection parameter, said
**     datasource, username, password, host and port.
*/
static int env_connect (lua_State *L) {
	MYSQL *conn;
	env_data *env = getenvironment(L);

	const char *sourcename = luaL_checkstring(L, 2);
	const char *username = luaL_optstring(L, 3, NULL);
	const char *password = luaL_optstring(L, 4, NULL);
	const char *host = luaL_optstring(L, 5, NULL);
	const char *port = luaL_optstring(L, 6, 0);

	/* Inicializa o ponteiro da conexao e testa se conseguiu  */
	conn = mysql_init(NULL);
	if (conn == NULL)
		return luasql_faildirect(L, LUASQL_PREFIX"Error connecting: Out of memory.");

	if (!mysql_real_connect(conn, host, username, password, 
		sourcename, port, NULL, 0))
	{
		mysql_close(conn);
		return luasql_faildirect(L, LUASQL_PREFIX"Error connecting to database.");
	}
		/*mysql_error(&conn));  */
	return create_connection(L, 1, conn);
}


/*
** Close environment object.
*/
static int env_close (lua_State *L) {
	env_data *env= (env_data *)luaL_checkudata (L, 1, LUASQL_ENVIRONMENT_MYSQL);
	luaL_argcheck (L, env != NULL, 1, LUASQL_PREFIX"environment expected");
	if (env->closed)
		return 0;

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
        {"execute", conn_execute},
        {"commit", conn_commit},
        {"rollback", conn_rollback},
        {"setautocommit", conn_setautocommit},
		{NULL, NULL},
    };
    struct luaL_reg cursor_methods[] = {
        {"close", cur_close},
        {"getcolnames", cur_getcolnames},
        {"getcoltypes", cur_getcoltypes},
        {"fetch", cur_fetch},
		{"numrows", cur_numrows},
		{NULL, NULL},
    };
	luasql_createmeta (L, LUASQL_ENVIRONMENT_MYSQL, environment_methods);
	luasql_createmeta (L, LUASQL_CONNECTION_MYSQL, connection_methods);
	luasql_createmeta (L, LUASQL_CURSOR_MYSQL, cursor_methods);
}

/*
** Creates an Environment and returns it.
*/
static int create_environment (lua_State *L) {
	env_data *env = (env_data *)lua_newuserdata(L, sizeof(env_data));
	luasql_setmeta (L, LUASQL_ENVIRONMENT_MYSQL);

	/* fill in structure */
	env->closed = 0;
	return 1;
}


/*
** Creates the metatables for the objects and registers the
** driver open method.
*/
LUASQL_API int luasql_libopen_mysql (lua_State *L) { 
	luasql_getlibtable (L);
	lua_pushstring(L, "mysql");
	lua_pushcfunction(L, create_environment);
	lua_settable(L, -3);

	create_metatables (L);

	return 0;
}
