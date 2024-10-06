#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include <lualib.h>
#include "duckdb.h"
#include "luasql.h"

#define LUASQL_ENVIRONMENT_DUCKDB "DuckDB environment"
#define LUASQL_CONNECTION_DUCKDB "DuckDB connection"
#define LUASQL_CURSOR_DUCKDB "DuckDB cursor"

typedef struct {
    short closed;
} env_data;

typedef struct {
    short closed;
    int env;                /* reference to environment */
    int auto_commit;        /* 0 for manual commit */
    duckdb_database db;
    duckdb_connection con;
} conn_data;

typedef struct {
    short closed;
    int conn;               /* reference to connection */
    int numcols;            /* number of columns */
    int colnames, coltypes; /* reference to column information tables */
    int curr_tuple;         /* next tuple to be read */
    duckdb_result result;
} cur_data;

typedef void (*creator) (lua_State *L, cur_data *cur);

/*
** Check for valid environment.
*/
static env_data *getenvironment(lua_State *L) {
    env_data *env = (env_data *)luaL_checkudata(L, 1, LUASQL_ENVIRONMENT_DUCKDB);
    luaL_argcheck(L, env != NULL, 1, LUASQL_PREFIX "environment expected");
    luaL_argcheck(L, !env->closed, 1, LUASQL_PREFIX "environment is closed");
    return env;
}

/*
** Check for valid connection.
*/
static conn_data *getconnection(lua_State *L) {
    conn_data *conn = (conn_data *)luaL_checkudata(L, 1, LUASQL_CONNECTION_DUCKDB);
    luaL_argcheck(L, conn != NULL, 1, LUASQL_PREFIX "connection expected");
    luaL_argcheck(L, !conn->closed, 1, LUASQL_PREFIX "connection is closed");
    return conn;
}

static const char *duckdb_type_to_string(duckdb_type type) {
    switch (type) {
        case DUCKDB_TYPE_BOOLEAN: return "BOOLEAN";
        case DUCKDB_TYPE_TINYINT: return "TINYINT";
        case DUCKDB_TYPE_SMALLINT: return "SMALLINT";
        case DUCKDB_TYPE_INTEGER: return "INTEGER";
        case DUCKDB_TYPE_BIGINT: return "BIGINT";
        case DUCKDB_TYPE_FLOAT: return "FLOAT";
        case DUCKDB_TYPE_DOUBLE: return "DOUBLE";
        case DUCKDB_TYPE_VARCHAR: return "VARCHAR";
        default: return "UNKNOWN";
    }
}

/*
** Check for valid cursor.
*/
static cur_data *getcursor(lua_State *L) {
    cur_data *cur = (cur_data *)luaL_checkudata(L, 1, LUASQL_CURSOR_DUCKDB);
    luaL_argcheck(L, cur != NULL, 1, LUASQL_PREFIX "cursor expected");
    luaL_argcheck(L, !cur->closed, 1, LUASQL_PREFIX "cursor is closed");
    return cur;
}

/*
** Push the value of #i field of #tuple row.
*/
static void pushvalue(lua_State *L, duckdb_result *result, idx_t row, idx_t col) {
    if (duckdb_value_is_null(result, col, row)) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, duckdb_value_varchar(result, col, row));
    }
}

/*
** Closes the cursor and nullify all structure fields.
*/
static void cur_nullify(lua_State *L, cur_data *cur) {
    /* Nullify structure fields. */
    cur->closed = 1;
    duckdb_destroy_result(&cur->result);
    luaL_unref(L, LUA_REGISTRYINDEX, cur->conn);
    luaL_unref(L, LUA_REGISTRYINDEX, cur->colnames);
    luaL_unref(L, LUA_REGISTRYINDEX, cur->coltypes);
}

/*
** Get another row of the given cursor.
*/
static int cur_fetch(lua_State *L) {
    cur_data *cur = getcursor(L);
    idx_t row = cur->curr_tuple;

    if (row >= duckdb_row_count(&cur->result)) {
        cur_nullify(L, cur);
        lua_pushnil(L);  /* no more results */
        return 1;
    }

    cur->curr_tuple++;
    if (lua_istable(L, 2)) {
        int i;
        const char *opts = luaL_optstring(L, 3, "n");
        if (strchr(opts, 'n') != NULL) {
            /* Copy values to numerical indices */
            for (i = 0; i < cur->numcols; i++) {
                pushvalue(L, &cur->result, row, i);
                lua_rawseti(L, 2, i + 1);
            }
        }
        if (strchr(opts, 'a') != NULL) {
            /* Copy values to alphanumerical indices */
            for (i = 0; i < cur->numcols; i++) {
                lua_pushstring(L, duckdb_column_name(&cur->result, i));
                pushvalue(L, &cur->result, row, i);
                lua_rawset(L, 2);
            }
        }
        lua_pushvalue(L, 2);
        return 1; /* return table */
    } else {
        int i;
        luaL_checkstack(L, cur->numcols, LUASQL_PREFIX "too many columns");
        for (i = 0; i < cur->numcols; i++) {
            pushvalue(L, &cur->result, row, i);
        }
        return cur->numcols; /* return #numcols values */
    }
}

/*
** Cursor object collector function
*/
static int cur_gc(lua_State *L) {
    cur_data *cur = (cur_data *)luaL_checkudata(L, 1, LUASQL_CURSOR_DUCKDB);
    if (cur != NULL && !(cur->closed)) {
        cur_nullify(L, cur);
    }
    return 0;
}

/*
** Closes the cursor on top of the stack.
** Returns true in case of success, or false in case the cursor was
** already closed.
** Throws an error if the argument is not a cursor.
*/
static int cur_close(lua_State *L) {
    cur_data *cur = (cur_data *)luaL_checkudata(L, 1, LUASQL_CURSOR_DUCKDB);
    luaL_argcheck(L, cur != NULL, 1, LUASQL_PREFIX "cursor expected");
    if (cur->closed) {
        lua_pushboolean(L, 0);
        return 1;
    }
    cur_nullify(L, cur); /* == cur_gc(L); */
    lua_pushboolean(L, 1);
    return 1;
}

/*
** Creates the list of fields names and pushes it on top of the stack.
*/
static void create_colnames(lua_State *L, cur_data *cur) {
    idx_t numcols = duckdb_column_count(&cur->result);
    lua_newtable(L);
    for (idx_t i = 0; i < numcols; i++) {
        lua_pushstring(L, duckdb_column_name(&cur->result, i));
        lua_rawseti(L, -2, i + 1);
    }
}

/*
** Creates the list of fields types and pushes it on top of the stack.
*/
static void create_coltypes(lua_State *L, cur_data *cur) {
    idx_t numcols = duckdb_column_count(&cur->result);
    lua_newtable(L);
    for (idx_t i = 0; i < numcols; i++) {
        // lua_pushstring(L, duckdb_column_type(&cur->result, i));
        lua_pushstring(L, duckdb_type_to_string(duckdb_column_type(&cur->result, i)));
        lua_rawseti(L, -2, i + 1);
    }
}

/*
** Pushes a column information table on top of the stack.
** If the table isn't built yet, call the creator function and stores
** a reference to it on the cursor structure.
*/
static void _pushtable(lua_State *L, cur_data *cur, size_t off, creator func) {
    int *ref = (int *)cur + off / sizeof(int);
    if (*ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, *ref);
    } else {
        func(L, cur);
        /* Stores a reference to it on the cursor structure */
        lua_pushvalue(L, -1);
        *ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
}
#define pushtable(L, c, m, f) (_pushtable(L, c, offsetof(cur_data, m), f))

/*
** Return the list of field names.
*/
static int cur_getcolnames(lua_State *L) {
    pushtable(L, getcursor(L), colnames, create_colnames);
    return 1;
}

/*
** Return the list of field types.
*/
static int cur_getcoltypes(lua_State *L) {
    pushtable(L, getcursor(L), coltypes, create_coltypes);
    return 1;
}

/*
** Push the number of rows.
*/
static int cur_numrows(lua_State *L) {
    // lua_pushnumber(L, duckdb_row_count(getcursor(L)->result));
    lua_pushnumber(L, duckdb_row_count(&getcursor(L)->result));
    return 1;
}

/*
** Create a new Cursor object and push it on top of the stack.
*/
static int create_cursor(lua_State *L, int conn, duckdb_result *result) {
    cur_data *cur = (cur_data *)LUASQL_NEWUD(L, sizeof(cur_data));
    luasql_setmeta(L, LUASQL_CURSOR_DUCKDB);

    /* fill in structure */
    cur->closed = 0;
    cur->conn = LUA_NOREF;
    cur->numcols = duckdb_column_count(result);
    cur->colnames = LUA_NOREF;
    cur->coltypes = LUA_NOREF;
    cur->curr_tuple = 0;
    cur->result = *result;
    lua_pushvalue(L, conn);
    cur->conn = luaL_ref(L, LUA_REGISTRYINDEX);

    return 1;
}

/*
** Connection object collector function
*/
static int conn_gc(lua_State *L) {
    conn_data *conn = (conn_data *)luaL_checkudata(L, 1, LUASQL_CONNECTION_DUCKDB);
    if (conn != NULL && !(conn->closed)) {
        /* Nullify structure fields. */
        conn->closed = 1;
        luaL_unref(L, LUA_REGISTRYINDEX, conn->env);
        duckdb_disconnect(&conn->con);
        duckdb_close(&conn->db);
    }
    return 0;
}

/*
** Closes the connection on top of the stack.
** Returns true in case of success, or false in case the connection was
** already closed.
** Throws an error if the argument is not a connection.
*/
static int conn_close(lua_State *L) {
    conn_data *conn = (conn_data *)luaL_checkudata(L, 1, LUASQL_CONNECTION_DUCKDB);
    luaL_argcheck(L, conn != NULL, 1, LUASQL_PREFIX "connection expected");
    if (conn->closed) {
        lua_pushboolean(L, 0);
        return 1;
    }
    conn_gc(L);
    lua_pushboolean(L, 1);
    return 1;
}

/*
** Execute an SQL statement.
** Return a Cursor object if the statement is a query, otherwise
** return the number of tuples affected by the statement.
*/
static int conn_execute(lua_State *L) {
    conn_data *conn = getconnection(L);
    const char *statement = luaL_checkstring(L, 2);
    duckdb_result result;
    if (duckdb_query(conn->con, statement, &result) != DuckDBSuccess) {
        return luasql_failmsg(L, "error executing statement. DuckDB: ", duckdb_result_error(&result));
    }

    duckdb_result_type rt;
    rt = duckdb_result_return_type(result);
    if (rt == DUCKDB_RESULT_TYPE_QUERY_RESULT) {
        /* tuples returned */
        return create_cursor(L, 1, &result);
    } else {
        /* no tuples returned */
        lua_pushnumber(L, duckdb_rows_changed(&result));
        duckdb_destroy_result(&result);
        return 1;
    }
}


/*
** Commit the current transaction.
*/
static int conn_commit(lua_State *L)
{
  char *errmsg;
  conn_data *conn = getconnection(L);
  duckdb_result res;
  const char *sql = "COMMIT";

  if (conn->auto_commit == 0) sql = "COMMIT;BEGIN";
  if (duckdb_query(conn->con, sql, &res) != DuckDBSuccess)
    {
      lua_pushnil(L);
      lua_pushliteral(L, LUASQL_PREFIX);
      lua_pushstring(L, errmsg);
      lua_concat(L, 2);
      return 2;
    }
  lua_pushboolean(L, 1);
  return 1;
}


/*
** Rollback the current transaction.
*/
static int conn_rollback(lua_State *L)
{
  char *errmsg;
  conn_data *conn = getconnection(L);
  duckdb_result res;
  const char *sql = "ROLLBACK";

  if (conn->auto_commit == 0) sql = "ROLLBACK;BEGIN";

  if (duckdb_query(conn->con, sql, &res) != DuckDBSuccess)
    {
      lua_pushnil(L);
      lua_pushliteral(L, LUASQL_PREFIX);
      lua_pushstring(L, errmsg);
      lua_concat(L, 2);
      return 2;
    }
  lua_pushboolean(L, 1);
  return 1;
}

/*
** Set "auto commit" property of the connection.
** If 'true', then rollback current transaction.
** If 'false', then start a new transaction.
*/
static int conn_setautocommit(lua_State *L) {
    conn_data *conn = getconnection(L);
    if (lua_toboolean(L, 2)) {
        conn->auto_commit = 1;
    } else {
        conn->auto_commit = 0;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/*
** Create a new Connection object and push it on top of the stack.
*/
static int create_connection(lua_State *L, int env, duckdb_connection *const con) {
    conn_data *conn = (conn_data *)LUASQL_NEWUD(L, sizeof(conn_data));
    luasql_setmeta(L, LUASQL_CONNECTION_DUCKDB);

    /* fill in structure */
    conn->closed = 0;
    conn->env = LUA_NOREF;
    conn->auto_commit = 1;
    conn->con = con;
    lua_pushvalue(L, env);
    conn->env = luaL_ref(L, LUA_REGISTRYINDEX);
    return 1;
}

/*
** Connects to a DuckDB database.
*/
static int env_connect(lua_State *L) {
    const char *sourcename = luaL_checkstring(L, 2);
	  // const char *config = luaL_gettable(L, 7, NULL);
    duckdb_database db;
    duckdb_connection con;
    duckdb_config conf;
    duckdb_create_config(&conf);
    if (lua_gettop(L) >= 7 && !lua_isnil(L, 7)) {
        luaL_checktype(L, 7, LUA_TTABLE);
        lua_pushnil(L); // First key
        while (lua_next(L, 7) != 0) {
            // Get the key and value from the table
            const char *key = lua_tostring(L, -2);
            const char *val = lua_tostring(L, -1);

            // Set the configuration option
            duckdb_set_config(conf, key, val);

            lua_pop(L, 1); // Remove value, keep key for the next iteration
        }
    }

    char * error = NULL;

    getenvironment(L);	/* validate environment */

    // Needs to pass in db config in third parameter here
    if (duckdb_open_ext(sourcename, &db, conf, &error) != DuckDBSuccess) {
        return luasql_failmsg(L, "error connecting to database. DuckDB: ", error);
    }
    if (duckdb_connect(db, &con) != DuckDBSuccess) {
        duckdb_close(&db);
        return luasql_failmsg(L, "error connecting to database. DuckDB: ", "Unspecified driver error");
    }
    return create_connection(L, 1, con);
}

/*
** Environment object collector function.
*/
static int env_gc(lua_State *L) {
    env_data *env = (env_data *)luaL_checkudata(L, 1, LUASQL_ENVIRONMENT_DUCKDB);
    if (env != NULL && !(env->closed)) {
        env->closed = 1;
    }
    return 0;
}

/*
** Closes the environment on top of the stack.
** Returns true in case of success, or false in case the environment was
** already closed.
** Throws an error if the argument is not an environment.
*/
static int env_close(lua_State *L) {
    env_data *env = (env_data *)luaL_checkudata(L, 1, LUASQL_ENVIRONMENT_DUCKDB);
    luaL_argcheck(L, env != NULL, 1, LUASQL_PREFIX "environment expected");
    if (env->closed) {
        lua_pushboolean(L, 0);
        return 1;
    }
    env_gc(L);
    lua_pushboolean(L, 1);
    return 1;
}

/*
** Create metatables for each class of object.
*/
static void create_metatables(lua_State *L) {
    struct luaL_Reg environment_methods[] = {
        {"__gc", env_gc},
        {"__close", env_close},
        {"close", env_close},
        {"connect", env_connect},
        {NULL, NULL},
    };
    struct luaL_Reg connection_methods[] = {
        {"__gc", conn_gc},
        {"__close", conn_close},
        {"close", conn_close},
        {"execute", conn_execute},
        {"commit", conn_commit},
        {"rollback", conn_rollback},
        {"setautocommit", conn_setautocommit},
        {NULL, NULL},
    };
    struct luaL_Reg cursor_methods[] = {
        {"__gc", cur_gc},
        {"__close", cur_close},
        {"close", cur_close},
        {"getcolnames", cur_getcolnames},
        {"getcoltypes", cur_getcoltypes},
        {"fetch", cur_fetch},
        {"numrows", cur_numrows},
        {NULL, NULL},
    };
    luasql_createmeta(L, LUASQL_ENVIRONMENT_DUCKDB, environment_methods);
    luasql_createmeta(L, LUASQL_CONNECTION_DUCKDB, connection_methods);
    luasql_createmeta(L, LUASQL_CURSOR_DUCKDB, cursor_methods);
    lua_pop(L, 3);
}

/*
** Creates an Environment and returns it.
*/
static int create_environment(lua_State *L) {
    env_data *env = (env_data *)LUASQL_NEWUD(L, sizeof(env_data));
    luasql_setmeta(L, LUASQL_ENVIRONMENT_DUCKDB);

    /* fill in structure */
    env->closed = 0;
    return 1;
}

/*
** Creates the metatables for the objects and registers the
** driver open method.
*/
LUASQL_API int luaopen_luasql_duckdb(lua_State *L) {
    struct luaL_Reg driver[] = {
        {"duckdb", create_environment},
        {NULL, NULL},
    };
    create_metatables(L);
    lua_newtable(L);
    luaL_setfuncs(L, driver, 0);
    luasql_set_info(L);
    return 1;
}

