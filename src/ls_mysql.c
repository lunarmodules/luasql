/*
** LuaSQL, MySQL driver
** Authors:  Eduardo Quintao
** See Copyright Notice in license.html
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#ifdef WIN32
#include <winsock2.h>
#define NO_CLIENT_LONG_LONG
#endif

#include "mysql.h"
#include "errmsg.h"

#include "lua.h"
#include "lauxlib.h"

#include "luasql.h"

#define LUASQL_ENVIRONMENT_MYSQL "MySQL environment"
#define LUASQL_CONNECTION_MYSQL "MySQL connection"
#define LUASQL_CURSOR_MYSQL "MySQL cursor"
#define LUASQL_STATEMENT_MYSQL "MySQL statement"

/* For compat with old version 4.0 */
#if (MYSQL_VERSION_ID < 40100)
#define MYSQL_TYPE_VAR_STRING   FIELD_TYPE_VAR_STRING
#define MYSQL_TYPE_STRING       FIELD_TYPE_STRING
#define MYSQL_TYPE_DECIMAL      FIELD_TYPE_DECIMAL
#define MYSQL_TYPE_SHORT        FIELD_TYPE_SHORT
#define MYSQL_TYPE_LONG         FIELD_TYPE_LONG
#define MYSQL_TYPE_FLOAT        FIELD_TYPE_FLOAT
#define MYSQL_TYPE_DOUBLE       FIELD_TYPE_DOUBLE
#define MYSQL_TYPE_LONGLONG     FIELD_TYPE_LONGLONG
#define MYSQL_TYPE_INT24        FIELD_TYPE_INT24
#define MYSQL_TYPE_YEAR         FIELD_TYPE_YEAR
#define MYSQL_TYPE_TINY         FIELD_TYPE_TINY
#define MYSQL_TYPE_TINY_BLOB    FIELD_TYPE_TINY_BLOB
#define MYSQL_TYPE_MEDIUM_BLOB  FIELD_TYPE_MEDIUM_BLOB
#define MYSQL_TYPE_LONG_BLOB    FIELD_TYPE_LONG_BLOB
#define MYSQL_TYPE_BLOB         FIELD_TYPE_BLOB
#define MYSQL_TYPE_DATE         FIELD_TYPE_DATE
#define MYSQL_TYPE_NEWDATE      FIELD_TYPE_NEWDATE
#define MYSQL_TYPE_DATETIME     FIELD_TYPE_DATETIME
#define MYSQL_TYPE_TIME         FIELD_TYPE_TIME
#define MYSQL_TYPE_TIMESTAMP    FIELD_TYPE_TIMESTAMP
#define MYSQL_TYPE_ENUM         FIELD_TYPE_ENUM
#define MYSQL_TYPE_SET          FIELD_TYPE_SET
#define MYSQL_TYPE_NULL         FIELD_TYPE_NULL

#define mysql_commit(_) ((void)_)
#define mysql_rollback(_) ((void)_)
#define mysql_autocommit(_,__) ((void)_)

#endif

/* For MySQL 8.0 or later, my_bool is replaced by bool */
#if !defined(MARIADB_BASE_VERSION) && MYSQL_VERSION_ID >= 80000
typedef bool my_bool;
#endif

typedef struct {
	short      closed;
} env_data;

typedef struct {
	short      closed;
	int        env;                /* reference to environment */
	MYSQL     *my_conn;
} conn_data;

typedef struct {
	short      closed;
	int        conn;               /* reference to connection */
	int        numcols;            /* number of columns */
	int        colnames, coltypes; /* reference to column information tables */
	MYSQL_RES *my_res;
	MYSQL 	  *my_conn;
	/* Prepared statement support */
	short      is_prepared;
	int        stmt;               /* reference to statement */
	MYSQL_BIND *res_binds;
} cur_data;


typedef struct {
	short      closed;
	int        conn;               /* reference to connection */
	MYSQL_STMT *my_stmt;
	unsigned long param_count;
	MYSQL_BIND *binds;
} stmt_data;



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
static void pushvalue (lua_State *L, void *row, long int len) {
	if (row == NULL)
		lua_pushnil (L);
	else
		lua_pushlstring (L, row, len);
}

/*
** Push a value from a prepared statement buffer.
*/
static void push_stmt_value (lua_State *L, MYSQL_BIND *bind) {
	if (*bind->is_null) {
		lua_pushnil (L);
		return;
	}
	switch (bind->buffer_type) {
		case MYSQL_TYPE_TINY:
			lua_pushinteger (L, *(char *)bind->buffer);
			break;
		case MYSQL_TYPE_SHORT:
			lua_pushinteger (L, *(short *)bind->buffer);
			break;
		case MYSQL_TYPE_LONG:
			lua_pushinteger (L, *(int *)bind->buffer);
			break;
		case MYSQL_TYPE_LONGLONG:
			lua_pushinteger (L, *(long long *)bind->buffer);
			break;
		case MYSQL_TYPE_FLOAT:
			lua_pushnumber (L, *(float *)bind->buffer);
			break;
		case MYSQL_TYPE_DOUBLE:
			lua_pushnumber (L, *(double *)bind->buffer);
			break;
		case MYSQL_TYPE_DECIMAL:
		case MYSQL_TYPE_NEWDECIMAL:
		case MYSQL_TYPE_STRING:
		case MYSQL_TYPE_VAR_STRING:
		case MYSQL_TYPE_TINY_BLOB:
		case MYSQL_TYPE_MEDIUM_BLOB:
		case MYSQL_TYPE_LONG_BLOB:
		case MYSQL_TYPE_BLOB:
			lua_pushlstring (L, bind->buffer, *bind->length);
			break;
		default:
			lua_pushlstring (L, bind->buffer, *bind->length);
			break;
	}
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
		case MYSQL_TYPE_TIMESTAMP:
			return "timestamp";
		case MYSQL_TYPE_ENUM: case MYSQL_TYPE_SET:
			return "set";
		case MYSQL_TYPE_NULL:
			return "null";
		default:
			return "undefined";
	}
}


/*
** Creates the lists of fields names and fields types.
*/
static void create_colinfo (lua_State *L, cur_data *cur) {
	MYSQL_FIELD *fields;
	char typename[50];
	int i;
	fields = mysql_fetch_fields(cur->my_res);
	lua_newtable (L); /* names */
	lua_newtable (L); /* types */
	for (i = 1; i <= cur->numcols; i++) {
		lua_pushstring (L, fields[i-1].name);
		lua_rawseti (L, -3, i);
		sprintf (typename, "%.20s(%ld)", getcolumntype (fields[i-1].type), fields[i-1].length);
		lua_pushstring(L, typename);
		lua_rawseti (L, -2, i);
	}
	/* Stores the references in the cursor structure */
	cur->coltypes = luaL_ref (L, LUA_REGISTRYINDEX);
	cur->colnames = luaL_ref (L, LUA_REGISTRYINDEX);
}


/*
** Closes the cursor and nullify all structure fields.
*/
static void cur_nullify (lua_State *L, cur_data *cur) {
	/* Nullify structure fields. */
	cur->closed = 1;
	mysql_free_result(cur->my_res);
	luaL_unref (L, LUA_REGISTRYINDEX, cur->conn);
	luaL_unref (L, LUA_REGISTRYINDEX, cur->colnames);
	luaL_unref (L, LUA_REGISTRYINDEX, cur->coltypes);
	if (cur->is_prepared) {
		luaL_unref (L, LUA_REGISTRYINDEX, cur->stmt);
		if (cur->res_binds) {
			int i;
			for (i = 0; i < cur->numcols; i++) {
				if (cur->res_binds[i].buffer) {
					free(cur->res_binds[i].buffer);
				}
				if (cur->res_binds[i].is_null) {
					free(cur->res_binds[i].is_null);
				}
				if (cur->res_binds[i].length) {
					free(cur->res_binds[i].length);
				}
				if (cur->res_binds[i].error) {
					free(cur->res_binds[i].error);
				}
			}
			free(cur->res_binds);
			cur->res_binds = NULL;
		}
	}
}


/*
** Get another row of the given cursor.
*/
static int cur_fetch (lua_State *L) {
	cur_data *cur = getcursor (L);
	MYSQL_RES *res = cur->my_res;
	unsigned long *lengths;
	MYSQL_ROW row = NULL;
	stmt_data *stmt = NULL;

	if (cur->is_prepared) {
		int res;
		lua_rawgeti (L, LUA_REGISTRYINDEX, cur->stmt);
		stmt = (stmt_data *)lua_touserdata (L, -1);
		lua_pop (L, 1);
		res = mysql_stmt_fetch (stmt->my_stmt);
		if (res == MYSQL_NO_DATA) {
			cur_nullify (L, cur);
			lua_pushnil (L);
			return 1;
		} else if (res != 0 && res != MYSQL_DATA_TRUNCATED) {
			return luasql_failmsg (L, "error fetching statement result. MySQL: ", mysql_stmt_error (stmt->my_stmt));
		}
	} else {
		row = mysql_fetch_row(res);
		if (row == NULL) {
			cur_nullify (L, cur);
			lua_pushnil(L);  /* no more results */
			return 1;
		}
		lengths = mysql_fetch_lengths(res);
	}

	if (lua_istable (L, 2)) {
		const char *opts = luaL_optstring (L, 3, "n");
		if (strchr (opts, 'n') != NULL) {
			/* Copy values to numerical indices */
			int i;
			for (i = 0; i < cur->numcols; i++) {
				if (cur->is_prepared) {
					push_stmt_value (L, &cur->res_binds[i]);
				} else {
					pushvalue (L, row[i], lengths[i]);
				}
				lua_rawseti (L, 2, i+1);
			}
		}
		if (strchr (opts, 'a') != NULL) {
			int i;
			/* Check if colnames exists */
			if (cur->colnames == LUA_NOREF)
		        create_colinfo(L, cur);
			lua_rawgeti (L, LUA_REGISTRYINDEX, cur->colnames);/* Push colnames*/

			/* Copy values to alphanumerical indices */
			for (i = 0; i < cur->numcols; i++) {
				lua_rawgeti(L, -1, i+1); /* push the field name */

				/* Actually push the value */
				if (cur->is_prepared) {
					push_stmt_value (L, &cur->res_binds[i]);
				} else {
					pushvalue (L, row[i], lengths[i]);
				}
				lua_rawset (L, 2);
			}
		}
		lua_pushvalue(L, 2);
		return 1; /* return table */
	}
	else {
		int i;
		luaL_checkstack (L, cur->numcols, LUASQL_PREFIX"too many columns");
		for (i = 0; i < cur->numcols; i++) {
			if (cur->is_prepared) {
				push_stmt_value (L, &cur->res_binds[i]);
			} else {
				pushvalue (L, row[i], lengths[i]);
			}
		}
		return cur->numcols; /* return #numcols values */
	}
}

/*
** Get the next result from multiple statements
*/
static int cur_next_result (lua_State *L) {
	cur_data *cur = getcursor (L);
	MYSQL* con = cur->my_conn;
	int status;
	if(mysql_more_results(con)){
		status = mysql_next_result(con);
		if(status == 0){
			mysql_free_result(cur->my_res);
			cur->my_res = mysql_store_result(con);
			if(cur->my_res != NULL){
				lua_pushboolean(L, 1);
				return 1;
			}else{
				lua_pushboolean(L, 0);
				lua_pushinteger(L, mysql_errno(con));
				lua_pushstring(L, mysql_error(con));
				return 3;
			}
		}else{
			lua_pushboolean(L, 0);
			lua_pushinteger(L, status);
			switch(status){
				case CR_COMMANDS_OUT_OF_SYNC:
					lua_pushliteral(L, "CR_COMMANDS_OUT_OF_SYNC");
				break;
				case CR_SERVER_GONE_ERROR:
					lua_pushliteral(L, "CR_SERVER_GONE_ERROR");
				break;
				case CR_SERVER_LOST:
					lua_pushliteral(L, "CR_SERVER_LOST");
				break;
				case CR_UNKNOWN_ERROR:
					lua_pushliteral(L, "CR_UNKNOWN_ERROR");
				break;
				default:
					lua_pushliteral(L, "Unknown");
			}
			return 3;
		}
	}else{
		lua_pushboolean(L, 0);
		lua_pushinteger(L, -1);
		return 2;
	}
}

/*
** Check if next result is available
*/
static int cur_has_next_result (lua_State *L) {
	cur_data *cur = getcursor (L);
	lua_pushboolean(L, mysql_more_results(cur->my_conn));
	return 1;
}


/*
** Cursor object collector function
*/
static int cur_gc (lua_State *L) {
	cur_data *cur = (cur_data *)luaL_checkudata (L, 1, LUASQL_CURSOR_MYSQL);
	if (cur != NULL && !(cur->closed))
		cur_nullify (L, cur);
	return 0;
}


/*
** Close the cursor on top of the stack.
** Return 1
*/
static int cur_close (lua_State *L) {
	cur_data *cur = (cur_data *)luaL_checkudata (L, 1, LUASQL_CURSOR_MYSQL);
	luaL_argcheck (L, cur != NULL, 1, LUASQL_PREFIX"cursor expected");
	if (cur->closed) {
		lua_pushboolean (L, 0);
		lua_pushstring (L, "Cursor is already closed");
		return 2;
	}
	cur_nullify (L, cur);
	lua_pushboolean (L, 1);
	return 1;
}


/*
** Pushes a column information table on top of the stack.
** If the table isn't built yet, call the creator function and stores
** a reference to it on the cursor structure.
*/
static void _pushtable (lua_State *L, cur_data *cur, size_t off) {
	int *ref = (int *)cur + off / sizeof(int);

	/* If colnames or coltypes do not exist, create both. */
	if (*ref == LUA_NOREF)
		create_colinfo(L, cur);

	/* Pushes the right table (colnames or coltypes) */
	lua_rawgeti (L, LUA_REGISTRYINDEX, *ref);
}
#define pushtable(L,c,m) (_pushtable(L,c,offsetof(cur_data,m)))


/*
** Return the list of field names.
*/
static int cur_getcolnames (lua_State *L) {
	pushtable (L, getcursor(L), colnames);
	return 1;
}


/*
** Return the list of field types.
*/
static int cur_getcoltypes (lua_State *L) {
	pushtable (L, getcursor(L), coltypes);
	return 1;
}


/*
** Push the number of rows.
*/
static int cur_numrows (lua_State *L) {
	lua_pushinteger (L, (lua_Number)mysql_num_rows (getcursor(L)->my_res));
	return 1;
}


/*
** Seeks to an arbitrary row in a query result set.
*/
static int cur_seek (lua_State *L) {
	cur_data *cur = getcursor (L);
	lua_Integer rownum = luaL_checkinteger (L, 2);
	mysql_data_seek (cur->my_res, rownum);
	return 0;
}


/*
** Create a new Cursor object and push it on top of the stack.
*/
static int create_cursor (lua_State *L, int conn, MYSQL_RES *result, int cols, int stmt) {
	cur_data *cur = (cur_data *)LUASQL_NEWUD(L, sizeof(cur_data));
	luasql_setmeta (L, LUASQL_CURSOR_MYSQL);

	/* fill in structure */
	cur->closed = 0;
	cur->conn = LUA_NOREF;
	cur->numcols = cols;
	cur->colnames = LUA_NOREF;
	cur->coltypes = LUA_NOREF;
	cur->my_res = result;
	cur->my_conn = NULL;
	cur->is_prepared = (stmt != LUA_NOREF);
	cur->stmt = LUA_NOREF;
	cur->res_binds = NULL;

	lua_pushvalue (L, conn);
	cur->conn = luaL_ref (L, LUA_REGISTRYINDEX);

	if (cur->is_prepared) {
		lua_pushvalue (L, stmt);
		cur->stmt = luaL_ref (L, LUA_REGISTRYINDEX);
	}

	return 1;
}


static int conn_gc (lua_State *L) {
	conn_data *conn=(conn_data *)luaL_checkudata(L, 1, LUASQL_CONNECTION_MYSQL);
	if (conn != NULL && !(conn->closed)) {
		/* Nullify structure fields. */
		conn->closed = 1;
		luaL_unref (L, LUA_REGISTRYINDEX, conn->env);
		mysql_close (conn->my_conn);
	}
	return 0;
}


/*
** Close a Connection object.
*/
static int conn_close (lua_State *L) {
	conn_data *conn=(conn_data *)luaL_checkudata(L, 1, LUASQL_CONNECTION_MYSQL);
	luaL_argcheck (L, conn != NULL, 1, LUASQL_PREFIX"connection expected");
	if (conn->closed) {
		lua_pushboolean (L, 0);
		lua_pushstring (L, "Connection is already closed");
		return 2;
	}
	/* Nullify structure fields. */
	conn->closed = 1;
	luaL_unref (L, LUA_REGISTRYINDEX, conn->env);
	mysql_close (conn->my_conn);
	lua_pushboolean (L, 1);
	return 1;
}

/*
** Ping connection.
*/
static int conn_ping (lua_State *L) {
	conn_data *conn=(conn_data *)luaL_checkudata(L, 1, LUASQL_CONNECTION_MYSQL);
	luaL_argcheck (L, conn != NULL, 1, LUASQL_PREFIX"connection expected");
	if (conn->closed) {
		lua_pushboolean (L, 0);
		return 1;
	}
	if (mysql_ping (conn->my_conn) == 0) {
		lua_pushboolean (L, 1);
		return 1;
	} else if (mysql_errno (conn->my_conn) == CR_SERVER_GONE_ERROR) {
		lua_pushboolean (L, 0);
		return 1;
	}
	luaL_error(L, mysql_error(conn->my_conn));
	return 0;
}


static int escape_string (lua_State *L) {
  size_t size, new_size;
  conn_data *conn = getconnection (L);
  const char *from = luaL_checklstring(L, 2, &size);
  char *to;
  to = (char*)malloc(sizeof(char) * (2 * size + 1));
  if(to) {
    new_size = mysql_real_escape_string(conn->my_conn, to, from, size);
    lua_pushlstring(L, to, new_size);
    free(to);
    return 1;
  }
  luaL_error(L, "could not allocate escaped string");
  return 0;
}

/*
** Execute an SQL statement.
** Return a Cursor object if the statement is a query, otherwise
** return the number of tuples affected by the statement.
*/
static int conn_execute (lua_State *L) {
	conn_data *conn = getconnection (L);
	size_t st_len;
	const char *statement = luaL_checklstring (L, 2, &st_len);
	if (mysql_real_query(conn->my_conn, statement, st_len))
		/* error executing query */
		return luasql_failmsg(L, "error executing query. MySQL: ", mysql_error(conn->my_conn));
	else
	{
		MYSQL_RES *res = mysql_store_result(conn->my_conn);
		unsigned int num_cols = mysql_field_count(conn->my_conn);

		if (res) { /* tuples returned */
			return create_cursor (L, 1, res, num_cols, LUA_NOREF);
		}
		else { /* mysql_use_result() returned nothing; should it have? */
			if(num_cols == 0) { /* no tuples returned */
            	/* query does not return data (it was not a SELECT) */
				lua_pushinteger(L, mysql_affected_rows(conn->my_conn));
				return 1;
        	}
			else /* mysql_use_result() should have returned data */
				return luasql_failmsg(L, "error retrieving result. MySQL: ", mysql_error(conn->my_conn));
		}
	}
}


/*
** Commit the current transaction.
*/
static int conn_commit (lua_State *L) {
	conn_data *conn = getconnection (L);
	lua_pushboolean(L, !mysql_commit(conn->my_conn));
	return 1;
}



/*
** Rollback the current transaction.
*/
static int conn_rollback (lua_State *L) {
	conn_data *conn = getconnection (L);
	lua_pushboolean(L, !mysql_rollback(conn->my_conn));
	return 1;
}


/*
** Set "auto commit" property of the connection. Modes ON/OFF
*/
static int conn_setautocommit (lua_State *L) {
	conn_data *conn = getconnection (L);
	if (lua_toboolean (L, 2)) {
		mysql_autocommit(conn->my_conn, 1); /* Set it ON */
	}
	else {
		mysql_autocommit(conn->my_conn, 0);
	}
	lua_pushboolean(L, 1);
	return 1;
}


/*
** Get Last auto-increment id generated
*/
static int conn_getlastautoid (lua_State *L) {
  conn_data *conn = getconnection(L);
  lua_pushinteger(L, mysql_insert_id(conn->my_conn));
  return 1;
}

/*
** Create a new Connection object and push it on top of the stack.
*/
static int create_connection (lua_State *L, int env, MYSQL *const my_conn) {
	conn_data *conn = (conn_data *)LUASQL_NEWUD(L, sizeof(conn_data));
	luasql_setmeta (L, LUASQL_CONNECTION_MYSQL);

	/* fill in structure */
	conn->closed = 0;
	conn->env = LUA_NOREF;
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
	const char *sourcename = luaL_checkstring(L, 2);
	const char *username = luaL_optstring(L, 3, NULL);
	const char *password = luaL_optstring(L, 4, NULL);
	const char *host = luaL_optstring(L, 5, NULL);
	const int port = luaL_optinteger(L, 6, 0);
	const char *unix_socket = luaL_optstring(L, 7, NULL);
	const long client_flag = (long)luaL_optinteger(L, 8, 0);
	MYSQL *conn;
	getenvironment(L); /* validate environment */

	/* Try to init the connection object. */
	conn = mysql_init(NULL);
	if (conn == NULL)
		return luasql_faildirect(L, "error connecting: Out of memory.");

	mysql_options(conn, MYSQL_READ_DEFAULT_GROUP, "client-lua");	
	
	if (!mysql_real_connect(conn, host, username, password,
		sourcename, port, unix_socket, client_flag))
	{
		char error_msg[100];
		strncpy (error_msg,  mysql_error(conn), 99);
		mysql_close (conn); /* Close conn if connect failed */
		return luasql_failmsg (L, "error connecting to database. MySQL: ", error_msg);
	}
	return create_connection(L, 1, conn);
}


/*
**
*/
static int env_gc (lua_State *L) {
	env_data *env= (env_data *)luaL_checkudata (L, 1, LUASQL_ENVIRONMENT_MYSQL);
	if (env != NULL && !(env->closed)) {
		env->closed = 1;
		mysql_library_end();
	}
	return 0;
}


/*
** Close environment object.
*/
static int env_close (lua_State *L) {
	env_data *env= (env_data *)luaL_checkudata (L, 1, LUASQL_ENVIRONMENT_MYSQL);
	luaL_argcheck (L, env != NULL, 1, LUASQL_PREFIX"environment expected");
	if (env->closed) {
		lua_pushboolean (L, 0);
		lua_pushstring(L, "Environment is already closed");
		return 2;
	}
	env->closed = 1;
	mysql_library_end();
	lua_pushboolean (L, 1);
	return 1;
}



/*
** Check for valid statement.
*/
static stmt_data *getstatement (lua_State *L) {
	stmt_data *stmt = (stmt_data *)luaL_checkudata (L, 1, LUASQL_STATEMENT_MYSQL);
	luaL_argcheck (L, stmt != NULL, 1, "statement expected");
	luaL_argcheck (L, !stmt->closed, 1, "statement is closed");
	return stmt;
}

/*
** Closes the statement and nullify all structure fields.
*/
static void stmt_nullify (lua_State *L, stmt_data *stmt) {
	stmt->closed = 1;
	luaL_unref (L, LUA_REGISTRYINDEX, stmt->conn);
	if (stmt->binds) {
		unsigned int i;
		for (i = 0; i < stmt->param_count; i++) {
			if (stmt->binds[i].buffer) {
				free(stmt->binds[i].buffer);
			}
			if (stmt->binds[i].is_null) {
				free(stmt->binds[i].is_null);
			}
			if (stmt->binds[i].length) {
				free(stmt->binds[i].length);
			}
		}
		free(stmt->binds);
		stmt->binds = NULL;
	}
	mysql_stmt_close (stmt->my_stmt);
}

/*
** Close the statement on top of the stack.
*/
static int stmt_close (lua_State *L) {
	stmt_data *stmt = (stmt_data *)luaL_checkudata (L, 1, LUASQL_STATEMENT_MYSQL);
	luaL_argcheck (L, stmt != NULL, 1, LUASQL_PREFIX"statement expected");
	if (stmt->closed) {
		lua_pushboolean (L, 0);
		lua_pushstring (L, "Statement is already closed");
		return 2;
	}
	stmt_nullify (L, stmt);
	lua_pushboolean (L, 1);
	return 1;
}

/*
** Statement object collector function
*/
static int stmt_gc (lua_State *L) {
	stmt_data *stmt = (stmt_data *)luaL_checkudata (L, 1, LUASQL_STATEMENT_MYSQL);
	if (stmt != NULL && !(stmt->closed)) {
		stmt_nullify (L, stmt);
	}
	return 0;
}

/*
** Bind parameters to a statement.
*/
static int stmt_bind (lua_State *L) {
	stmt_data *stmt = getstatement (L);
	int lua_args = lua_gettop (L) - 1; /* exclude stmt object */
	int i;

	if (lua_args == 2 && lua_isnumber (L, 2)) {
		int index = lua_tointeger (L, 2);
		int arg_idx = 3;
		int type = lua_type (L, arg_idx);
		MYSQL_BIND *bind;

		luaL_argcheck (L, index > 0 && index <= (int)stmt->param_count, 2, "index out of range");
		bind = &(stmt->binds[index - 1]);

		/* Free previous buffer if any */
		if (bind->buffer) {
			free (bind->buffer);
			bind->buffer = NULL;
		}

		if (type == LUA_TNIL) {
			bind->buffer_type = MYSQL_TYPE_NULL;
			*bind->is_null = 1;
		} else {
			*bind->is_null = 0;
			if (type == LUA_TNUMBER) {
#if LUA_VERSION_NUM >= 503
				if (lua_isinteger (L, arg_idx)) {
					bind->buffer_type = MYSQL_TYPE_LONGLONG;
					bind->buffer = malloc (sizeof (long long));
					*(long long *)bind->buffer = lua_tointeger (L, arg_idx);
				} else {
					bind->buffer_type = MYSQL_TYPE_DOUBLE;
					bind->buffer = malloc (sizeof (double));
					*(double *)bind->buffer = lua_tonumber (L, arg_idx);
				}
#else
				bind->buffer_type = MYSQL_TYPE_DOUBLE;
				bind->buffer = malloc (sizeof (double));
				*(double *)bind->buffer = lua_tonumber (L, arg_idx);
#endif
			} else if (type == LUA_TBOOLEAN) {
				bind->buffer_type = MYSQL_TYPE_TINY;
				bind->buffer = malloc (sizeof (char));
				*(char *)bind->buffer = lua_toboolean (L, arg_idx) ? 1 : 0;
			} else if (type == LUA_TSTRING) {
				size_t len;
				const char *str = lua_tolstring (L, arg_idx, &len);
				bind->buffer_type = MYSQL_TYPE_STRING;
				bind->buffer = malloc (len);
				memcpy (bind->buffer, str, len);
				bind->buffer_length = len;
				*bind->length = len;
			} else {
				return luasql_failmsg (L, "invalid parameter type", "");
			}
		}
	} else if (lua_args == (int)stmt->param_count) {
		for (i = 0; i < (int)stmt->param_count; i++) {
			int arg_idx = i + 2;
			int type = lua_type (L, arg_idx);
			MYSQL_BIND *bind = &(stmt->binds[i]);

			/* Free previous buffer if any */
			if (bind->buffer) {
				free (bind->buffer);
				bind->buffer = NULL;
			}

			if (type == LUA_TNIL) {
				bind->buffer_type = MYSQL_TYPE_NULL;
				*bind->is_null = 1;
			} else {
				*bind->is_null = 0;
				if (type == LUA_TNUMBER) {
#if LUA_VERSION_NUM >= 503
					if (lua_isinteger (L, arg_idx)) {
						bind->buffer_type = MYSQL_TYPE_LONGLONG;
						bind->buffer = malloc (sizeof (long long));
						*(long long *)bind->buffer = lua_tointeger (L, arg_idx);
					} else {
						bind->buffer_type = MYSQL_TYPE_DOUBLE;
						bind->buffer = malloc (sizeof (double));
						*(double *)bind->buffer = lua_tonumber (L, arg_idx);
					}
#else
					bind->buffer_type = MYSQL_TYPE_DOUBLE;
					bind->buffer = malloc (sizeof (double));
					*(double *)bind->buffer = lua_tonumber (L, arg_idx);
#endif
				} else if (type == LUA_TBOOLEAN) {
					bind->buffer_type = MYSQL_TYPE_TINY;
					bind->buffer = malloc (sizeof (char));
					*(char *)bind->buffer = lua_toboolean (L, arg_idx) ? 1 : 0;
				} else if (type == LUA_TSTRING) {
					size_t len;
					const char *str = lua_tolstring (L, arg_idx, &len);
					bind->buffer_type = MYSQL_TYPE_STRING;
					bind->buffer = malloc (len);
					memcpy (bind->buffer, str, len);
					bind->buffer_length = len;
					*bind->length = len;
				} else {
					return luasql_failmsg (L, "invalid parameter type", "");
				}
			}
		}
	} else {
		return luasql_failmsg (L, "wrong number of parameters", "");
	}

	if (stmt->param_count > 0) {
		if (mysql_stmt_bind_param (stmt->my_stmt, stmt->binds)) {
			return luasql_failmsg (L, "error binding parameters. MySQL: ", mysql_stmt_error (stmt->my_stmt));
		}
	}

	lua_pushboolean (L, 1);
	return 1;
}

/*
** Bind result buffers for a prepared statement.
*/
static int bind_result (lua_State *L, cur_data *cur, stmt_data *stmt) {
	MYSQL_RES *meta = cur->my_res;
	unsigned int num_fields = cur->numcols;
	MYSQL_FIELD *fields = mysql_fetch_fields(meta);
	unsigned int i;

	cur->res_binds = malloc (sizeof (MYSQL_BIND) * num_fields);
	memset (cur->res_binds, 0, sizeof (MYSQL_BIND) * num_fields);

	for (i = 0; i < num_fields; i++) {
		unsigned long buffer_length = fields[i].length + 1;
		cur->res_binds[i].buffer_type = fields[i].type;
		cur->res_binds[i].buffer = malloc (buffer_length);
		cur->res_binds[i].buffer_length = buffer_length;
		cur->res_binds[i].is_null = malloc (sizeof (my_bool));
		cur->res_binds[i].length = malloc (sizeof (unsigned long));
		cur->res_binds[i].error = malloc (sizeof (my_bool));
	}

	if (mysql_stmt_bind_result (stmt->my_stmt, cur->res_binds)) {
		return luasql_failmsg (L, "error binding result. MySQL: ", mysql_stmt_error (stmt->my_stmt));
	}
	return 0;
}

/*
** Execute a prepared statement.
*/
static int stmt_execute (lua_State *L) {
	stmt_data *stmt = getstatement (L);

	if (mysql_stmt_execute (stmt->my_stmt)) {
		return luasql_failmsg (L, "error executing statement. MySQL: ", mysql_stmt_error (stmt->my_stmt));
	}

	/* Check for result set vs affected rows */
	MYSQL_RES *meta = mysql_stmt_result_metadata (stmt->my_stmt);
	if (meta) {
		int cols = mysql_num_fields(meta);
		if (mysql_stmt_store_result (stmt->my_stmt)) {
			mysql_free_result (meta);
			return luasql_failmsg (L, "error storing statement result. MySQL: ", mysql_stmt_error (stmt->my_stmt));
		}

		create_cursor (L, stmt->conn, meta, cols, 1);
		cur_data *cur = (cur_data *)lua_touserdata(L, -1);
		if (bind_result(L, cur, stmt)) {
			lua_pop(L, 1);
			return 2; /* bind_result should have pushed error */
		}
		return 1;
	} else {
		lua_pushinteger (L, mysql_stmt_affected_rows (stmt->my_stmt));
		return 1;
	}
}

/*
** Create a prepared statement.
*/
static int conn_prepare (lua_State *L) {
	conn_data *conn = getconnection (L);
	size_t st_len;
	const char *statement = luaL_checklstring (L, 2, &st_len);
	MYSQL_STMT *my_stmt = mysql_stmt_init (conn->my_conn);
	unsigned long param_count;

	if (!my_stmt) {
		return luasql_failmsg (L, "error initializing statement", "");
	}

	if (mysql_stmt_prepare (my_stmt, statement, st_len)) {
		mysql_stmt_close (my_stmt);
		return luasql_failmsg (L, "error preparing statement. MySQL: ", mysql_stmt_error (my_stmt));
	}

	param_count = mysql_stmt_param_count (my_stmt);

	stmt_data *stmt = (stmt_data *)LUASQL_NEWUD (L, sizeof (stmt_data));
	luasql_setmeta (L, LUASQL_STATEMENT_MYSQL);

	/* fill in structure */
	stmt->closed = 0;
	stmt->conn = LUA_NOREF;
	stmt->my_stmt = my_stmt;
	stmt->param_count = param_count;
	stmt->binds = NULL;

	if (param_count > 0) {
		unsigned int i;
		stmt->binds = malloc (sizeof (MYSQL_BIND) * param_count);
		memset (stmt->binds, 0, sizeof (MYSQL_BIND) * param_count);
		for (i = 0; i < param_count; i++) {
			stmt->binds[i].is_null = malloc (sizeof (my_bool));
			stmt->binds[i].length = malloc (sizeof (unsigned long));
		}
	}

	lua_pushvalue (L, 1);
	stmt->conn = luaL_ref (L, LUA_REGISTRYINDEX);

	return 1;
}


/*
** Create metatables for each class of object.
*/
static void create_metatables (lua_State *L) {
    struct luaL_Reg environment_methods[] = {
		{"__gc", env_gc},
		{"__close", env_gc},
		{"close", env_close},
		{"connect", env_connect},
		{NULL, NULL},
	};
    struct luaL_Reg connection_methods[] = {
		{"prepare", conn_prepare},
		{"__gc", conn_gc},
		{"__close", conn_gc},
		{"close", conn_close},
		{"ping", conn_ping},
		{"escape", escape_string},
		{"execute", conn_execute},
		{"commit", conn_commit},
		{"rollback", conn_rollback},
		{"setautocommit", conn_setautocommit},
		{"getlastautoid", conn_getlastautoid},
		{NULL, NULL},
    };
    struct luaL_Reg statement_methods[] = {
		{"__gc", stmt_gc},
		{"__close", stmt_gc},
		{"close", stmt_close},
		{"bind", stmt_bind},
		{"execute", stmt_execute},
		{NULL, NULL},
    };
    struct luaL_Reg cursor_methods[] = {
        {"__gc", cur_gc},
		{"__close", cur_gc},
        {"close", cur_close},
        {"getcolnames", cur_getcolnames},
        {"getcoltypes", cur_getcoltypes},
        {"fetch", cur_fetch},
        {"numrows", cur_numrows},
        {"seek", cur_seek},
		{"nextresult", cur_next_result},
		{"hasnextresult", cur_has_next_result},
		{NULL, NULL},
    };
	luasql_createmeta (L, LUASQL_ENVIRONMENT_MYSQL, environment_methods);
	luasql_createmeta (L, LUASQL_CONNECTION_MYSQL, connection_methods);
	luasql_createmeta (L, LUASQL_STATEMENT_MYSQL, statement_methods);
	luasql_createmeta (L, LUASQL_CURSOR_MYSQL, cursor_methods);
	lua_pop (L, 4);
}


/*
** Creates an Environment and returns it.
*/
static int create_environment (lua_State *L) {
	env_data *env = (env_data *)LUASQL_NEWUD(L, sizeof(env_data));
	luasql_setmeta (L, LUASQL_ENVIRONMENT_MYSQL);

	/* fill in structure */
	env->closed = 0;
	return 1;
}


/*
** Creates the metatables for the objects and registers the
** driver open method.
*/
LUASQL_API int luaopen_luasql_mysql (lua_State *L) {
	struct luaL_Reg driver[] = {
		{"mysql", create_environment},
		{NULL, NULL},
	};
	create_metatables (L);
	lua_newtable(L);
	luaL_setfuncs(L, driver, 0);
	luasql_set_info (L);
    lua_pushliteral (L, "_CLIENTVERSION");
#ifdef MARIADB_CLIENT_VERSION_STR
lua_pushliteral (L, MARIADB_CLIENT_VERSION_STR);
#else
lua_pushliteral (L, MYSQL_SERVER_VERSION);
#endif
    lua_settable (L, -3);
	return 1;
}
