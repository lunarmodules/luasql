/*
** LuaSQL, ODBC driver
** Authors: Pedro Rabinovitch, Roberto Ierusalimschy, Diego Nehab,
** Tomas Guisasola
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(WIN32)
#include <windows.h>
#include <sqlext.h>
#elif defined(INFORMIX)
#include <infxcli.h>
#endif

#include <lua.h>
#include <lauxlib.h>

#include "luasql.h"
#include "ls_odbc.h"

typedef struct {
    SQLHENV henv;             /* environment handle */
	unsigned conn_counter;    /* active connections counter */
} env_data;

/* connection userdatum */
typedef struct {
    env_data *env;            /* parent environment */
    SQLHDBC hdbc;             /* database connection handle */
    int cur_tag;              /* statement userdata tag */
    int invalid_tag;          /* tag used for invalid userdata */
} conn_data;

/* cursor userdatum */
typedef struct {
    conn_data *conn;          /* parent connection */
    SQLHSTMT hstmt;           /* statement handle */
    int numcols;              /* number of columns */
    int coltypes, colnames;   /* helper tables */
    int invalid_tag;          /* tag used for invalid userdata */
} cur_data;

/* we are lazy */
#define hENV SQL_HANDLE_ENV
#define hSTMT SQL_HANDLE_STMT
#define hDBC SQL_HANDLE_DBC
#define error(a) ((a) != SQL_SUCCESS && (a) != SQL_SUCCESS_WITH_INFO) 

/*------------------------------------------------------------------*\
* Pushes 1 and returns 1
\*------------------------------------------------------------------*/
static int pass(lua_State *L)
{
    lua_pushnumber(L, 1);
    return 1;
}

/*------------------------------------------------------------------*\
* Fails with error message from ODBC
* Inputs: 
*   type: type of handle used in operation
*   handle: handle used in operation
\*------------------------------------------------------------------*/
static int fail(lua_State *L,  const SQLSMALLINT type, const SQLHANDLE handle) 
{
    SQLCHAR State[6];
    SQLINTEGER NativeError;
    SQLSMALLINT MsgSize, i;
    SQLRETURN ret;
    char Msg[SQL_MAX_MESSAGE_LENGTH];
    luaL_Buffer b;
    lua_pushnil(L);
    luaL_buffinit(L, &b);
    i = 1;
    while (1) {
        ret = SQLGetDiagRec(type, handle, i, State, &NativeError, Msg, 
                sizeof(Msg), &MsgSize);
        if (ret == SQL_NO_DATA) break;
        luaL_addlstring(&b, Msg, MsgSize);
        luaL_putchar(&b, '\n');
        i++;
    } 
    luaL_pushresult(&b);
    return 2;
}

/*------------------------------------------------------------------*\
* Fails with explicit error message
* Inputs: 
*   type: type of handle used in operation
*   handle: handle used in operation
\*------------------------------------------------------------------*/
static int fail_direct(lua_State *L, const char *err) 
{
    lua_pushnil(L);
    lua_pushstring(L, err);
    return 2;
}

/*------------------------------------------------------------------*\
* Returns the name of an equivalent lua type for a SQL type.
\*------------------------------------------------------------------*/
static const char *sqltypetolua (const SQLSMALLINT type)
{
    switch (type) {
        case SQL_UNKNOWN_TYPE: case SQL_CHAR: case SQL_VARCHAR: 
        case SQL_TYPE_DATE: case SQL_TYPE_TIME: case SQL_TYPE_TIMESTAMP: 
        case SQL_DATE: case SQL_INTERVAL: case SQL_TIMESTAMP: 
        case SQL_LONGVARCHAR:
            return "string";
        case SQL_BIGINT: case SQL_TINYINT: case SQL_NUMERIC: 
        case SQL_DECIMAL: case SQL_INTEGER: case SQL_SMALLINT: 
        case SQL_FLOAT: case SQL_REAL: case SQL_DOUBLE:
            return "number";
        case SQL_BINARY: case SQL_VARBINARY: case SQL_LONGVARBINARY:
            return "binary";
        case SQL_BIT:
            return "bool";
        default:
            assert(0);
            return NULL;
    }
}

/*==================================================================*\
* Cursor functions
\*==================================================================*/
/*------------------------------------------------------------------*\
* Garbage collection handler for statement handles
\*------------------------------------------------------------------*/
static int sqlCurCollect(lua_State *L) 
{
    cur_data *curdata = (cur_data *) lua_touserdata(L, 1); 
    SQLCloseCursor(curdata->hstmt);
    SQLFreeHandle(hSTMT, curdata->hstmt);
    lua_unref(L, curdata->coltypes);
    lua_unref(L, curdata->colnames);
    return 0;
}

/*------------------------------------------------------------------*\
* Retrieves data from the i_th column in the current row
* Input
*   types: index in stack of column types table
*   hstmt: statement handle
*   i: column number
* Returns:
*   0 if successfull, non-zero otherwise;
\*------------------------------------------------------------------*/
static int push_column(lua_State *L, int types, const SQLHSTMT hstmt, 
        SQLUSMALLINT i)
{
    const char *tname;
    char type;
    /* get column type from types table */
    lua_pushnumber(L, i);
    lua_gettable(L, types);
    tname = lua_tostring(L, -1);
    if (!tname) return fail_direct(L, "SQL: Invalid type in table.");
    type = tname[1];
    lua_pop(L, 1);
    /* deal with data according to type */
    switch (type) {
        /* nUmber */
        case 'u': { 
              double num;
              SQLINTEGER got;
              SQLRETURN rc = SQLGetData(hstmt, i, SQL_C_DOUBLE, &num, 0, &got);
              if (error(rc)) return fail(L, hSTMT, hstmt);
              if (got == SQL_NULL_DATA) lua_pushnil(L);
              else lua_pushnumber(L, num);
              return 0;
          }
                  /* bOol */
        case 'o': { 
              char b;
              SQLINTEGER got;
              SQLRETURN rc = SQLGetData(hstmt, i, SQL_C_BIT, &b, 0, &got);
              if (error(rc)) return fail(L, hSTMT, hstmt);
              if (got == SQL_NULL_DATA) lua_pushnil(L);
              else lua_pushstring(L, b ? "true" : "false");
              return 0;
          }
        /* sTring */
        case 't': 
        /* bInary */
        case 'i': { 
              SQLSMALLINT stype = (type == 't') ? SQL_C_CHAR : SQL_C_BINARY;
              SQLINTEGER got;
              char *buffer;
              luaL_Buffer b;
              SQLRETURN rc;
              luaL_buffinit(L, &b);
              buffer = luaL_prepbuffer(&b);
              rc = SQLGetData(hstmt, i, stype, buffer, LUAL_BUFFERSIZE, &got);
              if (got == SQL_NULL_DATA) {
                  lua_pushnil(L);
                  return 0;
              }
              /* concat intermediary chunks */
              while (rc == SQL_SUCCESS_WITH_INFO) {
                  if (got >= LUAL_BUFFERSIZE || got == SQL_NO_TOTAL) {
                      got = LUAL_BUFFERSIZE;
                      /* get rid of null termination in string block */
                      if (stype == SQL_C_CHAR) got--;
                  }
                  luaL_addsize(&b, got);
                  buffer = luaL_prepbuffer(&b);
                  rc = SQLGetData(hstmt, i, stype, buffer, 
                          LUAL_BUFFERSIZE, &got);
              }
              /* concat last chunk */
              if (rc == SQL_SUCCESS) {
                  if (got >= LUAL_BUFFERSIZE || got == SQL_NO_TOTAL) {
                      got = LUAL_BUFFERSIZE;
                      /* get rid of null termination in string block */
                      if (stype == SQL_C_CHAR) got--;
                  }
                  luaL_addsize(&b, got);
              }
              if (rc == SQL_ERROR) return fail(L, hSTMT, hstmt);
              /* return everything we got */
              luaL_pushresult(&b);
              return 0;
          }
    }
    return 0;
}

/*------------------------------------------------------------------*\
* Fetches a single row of data from a given cursor, pushing the
* result as a Lua table.
\*------------------------------------------------------------------*/
static int sqlCurFetch(lua_State *L) 
{
    cur_data *curdata = (cur_data *) lua_touserdata(L, 1);
    SQLHSTMT hstmt = curdata->hstmt;
    int row, types, names;
    int i, ret; 
    SQLRETURN rc = SQLFetch(hstmt); 
    /* are there any more rows? */
    if (rc == SQL_NO_DATA) {
        lua_pushnil(L);
        return 1;
    } else if (error(rc)) fail(L, hSTMT, hstmt);
    /* push column type and name table */
    lua_getref(L, curdata->coltypes); types = lua_gettop(L);
    lua_getref(L, curdata->colnames); names = lua_gettop(L);
    /* create row of results */
    lua_newtable(L); row = lua_gettop(L);
    /* if we are in named mode, use column names as indexes */
    for (i = 1; i <= curdata->numcols; i++) {
        lua_pushnumber(L, i);
        lua_gettable(L, names);
        ret = push_column(L, types, hstmt, (SQLUSMALLINT) i);
        if (ret) return ret;
        lua_pushnumber(L, i);
        lua_pushvalue(L, -2);
        lua_settable(L, row);
        lua_settable(L, row);
    }
    /* return the row of values, already on top of stack */
    return 1;
}

/*------------------------------------------------------------------*\
* Closes a cursor.
\*------------------------------------------------------------------*/
static int sqlCurClose(lua_State *L) 
{
    cur_data *curdata = (cur_data *) lua_touserdata(L, 1);
    SQLHSTMT hstmt = curdata->hstmt;
    SQLRETURN ret = SQLCloseCursor(hstmt);
    if (error(ret)) return fail(L, hSTMT, hstmt);
    ret = SQLFreeHandle(hSTMT, hstmt);
    if (error(ret)) return fail(L, hSTMT, hstmt);
    lua_unref(L, curdata->coltypes);
    lua_unref(L, curdata->colnames);
    /* methods cannot be called on cursor anymore */
    lua_settop(L, 1); lua_settag(L, curdata->invalid_tag);
    return pass(L);
}

/*------------------------------------------------------------------*\
* Pushes environment object methods table
\*------------------------------------------------------------------*/
static void push_curmethods(lua_State *L)
{
    lua_newtable(L);
    lua_pushstring(L, "Fetch");
    lua_pushcfunction(L, sqlCurFetch);
    lua_settable(L, -3);
    lua_pushstring(L, "Close");
    lua_pushcfunction(L, sqlCurClose);
    lua_settable(L, -3);
}

/*==================================================================*\
* Connection functions
\*==================================================================*/
/*------------------------------------------------------------------*\
* Garbage collection handler for the conn handles
\*------------------------------------------------------------------*/
static int sqlConnCollect(lua_State *L) 
{
    conn_data *conndata = (conn_data *) lua_touserdata(L, 1);
    SQLDisconnect(conndata->hdbc);
    SQLFreeHandle(hDBC, conndata->hdbc);
    return 0;
}

/*------------------------------------------------------------------*\
* Creates a cursor table and leave it on the top of the stack.
\*------------------------------------------------------------------*/
static int push_curtable (lua_State *L, conn_data *conndata, 
        const SQLHSTMT hstmt, const SQLSMALLINT numcols) 
{
    SQLCHAR buffer[256];
    SQLSMALLINT namelen;
    SQLSMALLINT datatype;
    SQLRETURN ret;
    int names, types;
    env_data *envdata = conndata->env;
    short i;
    /* allocate cursor userdatum */
    cur_data *curdata = (cur_data *) lua_newuserdata(L, sizeof(cur_data));
    if (!curdata) return fail_direct(L, "SQL: memory allocation error");
    /* make it a cursor object */
    lua_settag(L, conndata->cur_tag);
    /* fill with inherited stuff */
    curdata->hstmt = hstmt;
    curdata->conn = conndata;
    curdata->numcols = numcols;
    curdata->invalid_tag = conndata->invalid_tag;
    /* compute column type and name tables */
    lua_newtable(L); names = lua_gettop(L);
    lua_newtable(L); types = lua_gettop(L);
    for (i = 1; i <= numcols; i++) {
        ret = SQLDescribeCol(hstmt, i, buffer, sizeof(buffer), 
                &namelen, &datatype, NULL, NULL, NULL);
        if (ret == SQL_ERROR) return fail(L, hSTMT, hstmt);
        lua_pushnumber(L, i);
        lua_pushstring(L, buffer);
        lua_settable(L, names);
        lua_pushnumber(L, i);
        lua_pushstring(L, sqltypetolua(datatype));
        lua_settable(L, types);
    }
    /* store references to type and name tables */
    curdata->coltypes = lua_ref(L, 1);
    curdata->colnames = lua_ref(L, 1);
    return 1;
}

/*------------------------------------------------------------------*\
* Executes a SQL statement.
* Returns
*   cursor object: if there are results or
*   row count: number of rows affected by statement if no results
\*------------------------------------------------------------------*/
static int sqlConnExecute(lua_State *L) 
{
    conn_data *conndata = (conn_data *) lua_touserdata(L, 1);
    const char *statement = luaL_check_string(L, 2);
    SQLHDBC hdbc = conndata->hdbc;
    SQLHSTMT hstmt;
    SQLSMALLINT numcols;
    SQLUINTEGER colsize;
    SQLINTEGER strlen;
    SQLUSMALLINT i;
    const char *param;
    size_t len;
    SQLRETURN ret;
    ret = SQLAllocHandle(hSTMT, hdbc, &hstmt);
    if (error(ret)) return fail(L, hDBC, hdbc);
    ret = SQLPrepare(hstmt, (char *) statement, SQL_NTS);
    if (error(ret)) return fail(L, hSTMT, hstmt);
    /* pass all extra arguments as parameters to statement */
    i = 3;
    while ((param = luaL_opt_lstr(L, i, NULL, &len))) {
        colsize = (SQLUINTEGER) len;
        strlen = (SQLINTEGER) len;
        ret = SQLBindParameter(
            hstmt, 
            (SQLUSMALLINT) (i-2), 
            SQL_PARAM_INPUT, 
            SQL_C_CHAR, 
            SQL_CHAR, 
            colsize, 
            (SQLSMALLINT) 0, 
            (char *) param, 
            (SQLINTEGER) sizeof(param), 
            &strlen
        );
        if (error(ret)) return fail(L, hSTMT, hstmt);
        i++;
    }
    /* execute the statement */
    ret = SQLExecute(hstmt);
    if (error(ret)) return fail(L, hSTMT, hstmt);
    /* determine the number of results */
    ret = SQLNumResultCols(hstmt, &numcols);
    if (error(ret)) {
        ret = fail(L, hSTMT, hstmt);
        SQLFreeHandle(hSTMT, hstmt);
        return ret;
    }
    /* if action has no results (e.g., UPDATE) */
    if (numcols <= 0) {
        SQLINTEGER numrows;
        ret = SQLRowCount(hstmt, &numrows);
        if (error(ret)) {
            ret = fail(L, hSTMT, hstmt);
            SQLFreeHandle(hSTMT, hstmt);
            return ret;
        }
        lua_pushnumber(L, numrows);
        return 1;
    /* if there is a results table (e.g., SELECT) */
    } else return push_curtable(L, conndata, hstmt, numcols);
}

/*------------------------------------------------------------------*\
* Closes a connection.
\*------------------------------------------------------------------*/
static int sqlConnClose(lua_State *L) 
{            
    conn_data *conndata = (conn_data *) lua_touserdata(L, 1);
    SQLRETURN ret = SQLDisconnect(conndata->hdbc);
    if (error(ret)) return fail(L, hDBC, conndata->hdbc);
    ret = SQLFreeHandle(hDBC, conndata->hdbc);
    if (error(ret)) return fail(L, hDBC, conndata->hdbc);
    /* methods cannot be called on cursor anymore */
    lua_settop(L, 1); lua_settag(L, conndata->invalid_tag);
    /* success */
    return pass(L);
}

/*------------------------------------------------------------------*\
* Returns a list with the names of the tables in the data source.
\*------------------------------------------------------------------*/
static int sqlConnTableList(lua_State *L) 
{
    conn_data *conndata = (conn_data *) lua_touserdata(L, 1);
    SQLHSTMT hstmt;
    int got, index, list;
    SQLUSMALLINT size;
    char *buffer;
    SQLRETURN ret = SQLAllocHandle(hSTMT, conndata->hdbc, &hstmt);
    if (error(ret)) return fail(L, hDBC, conndata->hdbc);
    ret = SQLTables(hstmt, NULL, 0, NULL, 0, NULL, 0, "TABLE", SQL_NTS );
    if (error(ret)) return fail(L, hSTMT, hstmt);
    ret = SQLGetInfo(conndata->hdbc, SQL_MAX_TABLE_NAME_LEN, 
            (SQLPOINTER) &size, sizeof(size), NULL);
    if (error(ret)) return fail(L, hSTMT, hstmt);
    size = size > 0 ? size : 256;
    buffer = (char *) malloc(size);
    if (!buffer) fail_direct(L, "SQL: allocation error.");
    /* create list */
    lua_newtable(L); list = lua_gettop(L);
    /* store fields */
    index = 1;
    while (1) {    
        /* ask for next table name */
        ret = SQLFetch(hstmt);
        if (ret == SQL_NO_DATA) break;
        if (error(ret)) {
            SQLFreeHandle(hSTMT, hstmt);
            return fail(L, hSTMT, hstmt);
        }
        lua_pushnumber(L, index);
        ret = SQLGetData(hstmt, 3, SQL_C_CHAR, buffer, size, &got);
        lua_pushlstring(L, buffer, got);
        /* save result on table name list */
        lua_settable(L, list);
        index++;
    }
    free(buffer);
    SQLFreeHandle(hSTMT, hstmt);
    /* return the table, it is already on top of stack */
    return 1;
}

/*------------------------------------------------------------------*\
* Rolls back a transaction. 
\*------------------------------------------------------------------*/
static int sqlConnCommit(lua_State *L) 
{
  conn_data *conndata = (conn_data *) lua_touserdata(L, 1);
  SQLRETURN ret = SQLEndTran(hDBC, conndata->hdbc, SQL_COMMIT);
  if (error(ret)) return fail(L, hSTMT, conndata->hdbc);
  else return pass(L);
}

/*------------------------------------------------------------------*\
* Rolls back a transaction. 
\*------------------------------------------------------------------*/
static int sqlConnRollback (lua_State *L) 
{
  conn_data *conndata = (conn_data *) lua_touserdata(L, 1);
  SQLRETURN ret = SQLEndTran(hDBC, conndata->hdbc, SQL_ROLLBACK);
  if (error(ret)) return fail(L, hSTMT, conndata->hdbc);
  else return pass(L);
}

/*------------------------------------------------------------------*\
* Sets the auto commit mode 
\*------------------------------------------------------------------*/
static int sqlConnSetAutoCommit(lua_State *L) 
{
  conn_data *conndata = (conn_data *) lua_touserdata(L, 1);
  const char *value = luaL_opt_string(L, 2, "true");
  SQLRETURN ret;
  if (*value == 't') {
      ret = SQLSetConnectAttr(conndata->hdbc, SQL_ATTR_AUTOCOMMIT, 
           (SQLPOINTER) SQL_AUTOCOMMIT_ON, 0);
  } else {
      ret = SQLSetConnectAttr(conndata->hdbc, SQL_ATTR_AUTOCOMMIT, 
           (SQLPOINTER) SQL_AUTOCOMMIT_OFF, 0);
  }
  if (error(ret)) return fail(L, hSTMT, conndata->hdbc);
  else return pass(L);
}

/*------------------------------------------------------------------*\
* Pushes connection object methods table
\*------------------------------------------------------------------*/
static void push_connmethods(lua_State *L)
{
    struct luaL_reg method[] = {
        {"TableList", sqlConnTableList},
        {"Close", sqlConnClose},
        {"Commit", sqlConnCommit},
        {"Rollback", sqlConnRollback},
        {"Execute", sqlConnExecute},
        {"SetAutoCommit", sqlConnSetAutoCommit},
    };
    unsigned short i;
    lua_newtable(L);
    for (i = 0; i < sizeof(method)/sizeof(method[0]); i++) {
        lua_pushstring(L, method[i].name);
        lua_pushcfunction(L, method[i].func);
        lua_settable(L, -3);
    }
}

/*==================================================================*\
* Environment functions
\*==================================================================*/
/*------------------------------------------------------------------*\
* Creates and pushes a connection object table
* Input: 
*   envdata: environment context
*   hdbc: newly created connection handle
* Returns:
*   number of items pushed on Lua stack
\*------------------------------------------------------------------*/
static int push_connectiontable(lua_State *L, env_data *envdata, SQLHDBC hdbc) 
{
    SQLRETURN ret;
    /* allocate userdatum */
    conn_data *conndata = (conn_data *) lua_newuserdata(L, sizeof(conn_data));
    if (!conndata) return fail_direct(L, "SQL: allocation error.");
    /* make it a connection object */
    lua_settag(L, envdata->conn_tag);
    /* fill structure */
    conndata->hdbc = hdbc;
    conndata->env = envdata;
    conndata->cur_tag = envdata->cur_tag;
    conndata->invalid_tag = envdata->invalid_tag;
    /* set auto commit mode */
    ret = SQLSetConnectAttr(conndata->hdbc, SQL_ATTR_AUTOCOMMIT, 
            (SQLPOINTER) SQL_AUTOCOMMIT_ON, 0);
    if (error(ret)) return fail(L, hDBC, conndata->hdbc);
    return 1;
}

/*------------------------------------------------------------------*\
* Creates and returns a connection object
* Lua Input: source [, user, pass]
*   source: data source
*   user, pass: data source authentication information
* Lua Returns:
*   conneciton object if successfull
*   nil and error message otherwise.
\*------------------------------------------------------------------*/
static int sqlEnvConnect(lua_State *L) 
{
    /* gets envdata from closure value */
    env_data *envdata = (env_data *) lua_touserdata(L, 1); 
    const char *source, *user, *pass;
    SQLRETURN ret;
    SQLHDBC hdbc;
    source = luaL_check_string(L, 2);
    user = luaL_opt_string(L, 3, NULL);
    pass = luaL_opt_string(L, 4, NULL);
    ret = SQLSetEnvAttr(envdata->henv, SQL_ATTR_ODBC_VERSION, 
        (void*)SQL_OV_ODBC3, 0);
    if (error(ret)) return fail_direct(L, "SQL: error setting SQL version.");
    /* tries to allocate connection handle */
    ret = SQLAllocHandle(hDBC, envdata->henv, &hdbc);
    if (error(ret)) return fail_direct(L, "SQL: connection allocation error.");
    /* tries to connect handle */
    ret = SQLConnect(hdbc, (char *) source, SQL_NTS, 
            (char *) user, SQL_NTS, (char *) pass, SQL_NTS);
    if (error(ret)) {
        ret = fail(L, hDBC, hdbc);
        SQLFreeHandle(hDBC, hdbc);
        return ret;
    }
    /* success, return connection object */
    return push_connectiontable(L, envdata, hdbc);
}

/*------------------------------------------------------------------*\
* Closes an environment object
* Lua returns:
*   SQL: SQL environment object
\*------------------------------------------------------------------*/
static int sqlEnvClose (lua_State *L) 
{
    env_data *envdata = (env_data *) lua_touserdata(L, 1); 
    SQLRETURN ret = SQLFreeHandle(hENV, envdata->henv);
    if (error(ret)) {
        int ret = fail(L, hENV, envdata->henv);
        envdata->henv = NULL;
        return ret;
    }
    return pass(L);
}

/*------------------------------------------------------------------*\
* Pushes environment object methods table
\*------------------------------------------------------------------*/
static void push_envmethods(lua_State *L)
{
    lua_newtable(L);
    lua_pushstring(L, "Connect");
    lua_pushcfunction(L, sqlEnvConnect);
    lua_settable(L, -3);
    lua_pushstring(L, "Close");
    lua_pushcfunction(L, sqlEnvClose);
    lua_settable(L, -3);
}

/*------------------------------------------------------------------*\
* Creates an environment object and and returns it to the calee.
* Lua returns:
*   SQL: SQL environment object
\*------------------------------------------------------------------*/
static int sqlEnvOpen (lua_State *L) 
{
    SQLRETURN ret;
    int env_tag;
    env_data *envdata = (env_data *) lua_newuserdata(L, sizeof(env_data));
    if (!envdata) return fail_direct(L, "SQL: allocation error.");
    ret = SQLAllocHandle(hENV, SQL_NULL_HANDLE, &envdata->henv);
    if (error(ret)) return fail_direct(L, "SQL: error creating environment.");
    push_envmethods(L);
    env_tag = lua_sqlnewtag(L);
    lua_settag(L, env_tag);
    push_connmethods(L);
    envdata->conn_tag = lua_sqlnewtag(L);
    lua_pushcfunction(L, sqlConnCollect);
    lua_settagmethod(L, envdata->conn_tag, "gc");
    push_curmethods(L);
    envdata->cur_tag = lua_sqlnewtag(L);
    lua_pushcfunction(L, sqlCurCollect);
    lua_settagmethod(L, envdata->cur_tag, "gc");
    envdata->invalid_tag = lua_newtag(L);
    return 1;
}

/*==================================================================*\
* Exported functions
\*==================================================================*/
/*------------------------------------------------------------------*\
* Registers environment creation function in SQL table
\*------------------------------------------------------------------*/
LUASQL_API void lua_sqllibopen_odbc(lua_State *L) 
{
    lua_getglobal(L, "SQL"); 
    lua_pushstring(L, "odbc"); 
    lua_pushcfunction(L, sqlEnvOpen); 
    lua_settable(L, -3); 
} 
