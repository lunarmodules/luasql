/*
** LuaSQL, Firebird driver
** Authors: Scott Morgan
** ls_firebird.c
*/

#include <ibase.h>	/* The Firebird API*/
#include <time.h>	/* For managing time values */
#include <malloc.h>
#include <string.h>
#include <stdio.h>

/* Lua API */
#include <lua.h>
#include <lauxlib.h>

#include "luasql.h"

#define LUASQL_ENVIRONMENT_FIREBIRD "Firebird environment"
#define LUASQL_CONNECTION_FIREBIRD "Firebird connection"
#define LUASQL_STATEMENT_FIREBIRD "Firebird statement"
#define LUASQL_CURSOR_FIREBIRD "Firebird cursor"

typedef struct {
	short closed;
	ISC_STATUS status_vector[20];     /* for error results */
	int lock;                         /* lock count for open connections */
} env_data;

typedef struct {
	/* general */
	env_data*		env;              /* the DB enviroment this is in */
	short			closed;
	int				lock;             /* lock count for open cursors */
	int				autocommit;       /* should each statement be commited */
	/* implimentation */
	isc_db_handle	db;               /* the database handle */
	char			dpb_buffer[1024]; /* holds the database paramet buffer */
	short			dpb_length;       /* the used amount of the dpb */
	isc_tr_handle	transaction;      /* the transaction handle */
	/* config */
	unsigned short  dialect;          /* dialect of SQL used */
} conn_data;

typedef struct {
	short			closed;
	env_data*		env;              /* the DB enviroment this is in */
	conn_data*		conn;             /* the DB connection this cursor is from */
	XSQLDA			*in_sqlda;        /* the parameter data array */
	isc_stmt_handle handle;           /* the statement handle */
	int             type;             /* the statment's type (SELECT, UPDATE, etc...) */
	int				lock;             /* lock count for open statements */
} stmt_data;

typedef struct {
	short			closed;
	env_data*		env;              /* the DB enviroment this is in */
	stmt_data*		stmt;             /* the DB statment this cursor is from */
	XSQLDA			*out_sqlda;       /* the cursor data array */
} cur_data;

/* How many fields to pre-alloc to the cursor */
#define CURSOR_PREALLOC 10

/* Macro to ease code reading */
#define CHECK_DB_ERROR( X ) ( (X)[0] == 1 && (X)[1] )

/* Use the new interpret API if available */
#undef FB_INTERPRET
#if FB_API_VER >= 20
  #define FB_INTERPRET(BUF, LEN, VECTOR) fb_interpret(BUF, LEN, VECTOR)
#else
  #define FB_INTERPRET(BUF, LEN, VECTOR) isc_interpret(BUF, VECTOR)
#endif

#if LUA_VERSION_NUM>=503
#define luasql_pushinteger lua_pushinteger
#define luasql_isinteger lua_isinteger
#else
#define luasql_pushinteger lua_pushnumber
#define luasql_isinteger lua_isnumber
#endif

/* MSVC still doesn't support C99 properly until 2015 */ 
#if defined(_MSC_VER) && _MSC_VER<1900
#pragma warning(disable:4996)	/* and complains if you try to work around it */
#define snprintf _snprintf
#endif

/*
** Returns a standard database error message
*/
static int return_db_error(lua_State *L, const ISC_STATUS *pvector)
{
	char errmsg[512];

	lua_pushnil(L);
	FB_INTERPRET(errmsg, 512, &pvector);
	lua_pushstring(L, errmsg);
	while(FB_INTERPRET(errmsg, 512, &pvector)) {
		lua_pushstring(L, "\n * ");
		lua_pushstring(L, errmsg);
		lua_concat(L, 3);
	}

	return 2;
}

/*
** Allocate memory for XSQLDA data
*/
static void malloc_sqlda_vars(XSQLDA *sqlda)
{
	int i;
	XSQLVAR *var;

	/* prep the result set ready to handle the data */
	for (i=0, var = sqlda->sqlvar; i < sqlda->sqld; i++, var++) {
		switch(var->sqltype & ~1) {
		case SQL_VARYING:
			var->sqldata = (char *)malloc(sizeof(char)*var->sqllen + 2);
			break;
		case SQL_TEXT:
			var->sqldata = (char *)malloc(sizeof(char)*var->sqllen);
			break;
		case SQL_SHORT:
			var->sqldata = (char *)malloc(sizeof(short));
			break;			
		case SQL_LONG:
			var->sqldata = (char *)malloc(sizeof(long));
			break;
		case SQL_INT64:
			var->sqldata = (char *)malloc(sizeof(ISC_INT64));
			break;
		case SQL_FLOAT:
			var->sqldata = (char *)malloc(sizeof(float));
			break;
		case SQL_DOUBLE:
			var->sqldata = (char *)malloc(sizeof(double));
			break;
		case SQL_TYPE_TIME:
			var->sqldata = (char *)malloc(sizeof(ISC_TIME));
			break;
		case SQL_TYPE_DATE:
			var->sqldata = (char *)malloc(sizeof(ISC_DATE));
			break;
		case SQL_TIMESTAMP:
			var->sqldata = (char *)malloc(sizeof(ISC_TIMESTAMP));
			break;
		case SQL_BLOB:
			var->sqldata = (char *)malloc(sizeof(ISC_QUAD));
			break;
		/* TODO : add extra data type handles here */
		}

		if (var->sqltype & 1) {
			/* allocate variable to hold NULL status */
			var->sqlind = (short *)malloc(sizeof(short));
		} else {
			var->sqlind = NULL;
		}
	}
}

/*
** Frees memory allocated to XSQLDA data
*/
static void free_sqlda_vars(XSQLDA *sqlda) {
	int i;
	XSQLVAR *var;

	if(sqlda != NULL) {
		for (i=0, var = sqlda->sqlvar; i < sqlda->sqld; i++, var++) {
			free(var->sqldata);
			free(var->sqlind);
		}
	}
}

/*
** Registers a given C object in the registry to avoid GC
*/
static void luasql_registerobj(lua_State *L, int index, void *obj)
{
	lua_pushvalue(L, index);
	lua_pushlightuserdata(L, obj);
	lua_pushvalue(L, -2);
	lua_settable(L, LUA_REGISTRYINDEX);
	lua_pop(L, 1);
}

/*
** Unregisters a given C object from the registry
*/
static void luasql_unregisterobj(lua_State *L, void *obj)
{
	lua_pushlightuserdata(L, obj);
	lua_pushnil(L);
	lua_settable(L, LUA_REGISTRYINDEX);
}

/*
** Free's up the memory alloc'd to the statement data
*/
static void free_stmt(stmt_data* stmt)
{
	/* free the field memory blocks */
	free_sqlda_vars(stmt->in_sqlda);

	/* free the data array */
	free(stmt->in_sqlda);
}

static int stmt_shut(lua_State *L, stmt_data* stmt)
{
	isc_dsql_free_statement(stmt->env->status_vector, &stmt->handle, DSQL_drop);
	if ( CHECK_DB_ERROR(stmt->env->status_vector) ) {
		return return_db_error(L, stmt->env->status_vector);
	}

	free_stmt(stmt);

	/* remove statement from lock count and check if connection can be unregistered */
	stmt->closed = 1;
	if(--stmt->conn->lock == 0)
		luasql_unregisterobj(L, stmt->conn);

	return 0;
}

/*
** Free's up the memory alloc'd to the cursor data
*/
static void free_cur(cur_data* cur)
{
	/* free the field memory blocks */
	free_sqlda_vars(cur->out_sqlda);

	/* free the data array */
	free(cur->out_sqlda);
}

/*
** Shuts down a cursor
*/
static int cur_shut(lua_State *L, cur_data *cur)
{
	isc_dsql_free_statement(cur->env->status_vector, &cur->stmt->handle, DSQL_close);
	if ( CHECK_DB_ERROR(cur->env->status_vector) ) {
		return return_db_error(L, cur->env->status_vector);
	}

	/* free the cursor data */
	free_cur(cur);

	/* remove cursor from lock count and check if statment can be unregistered */
	cur->closed = 1;
	if(--cur->stmt->lock == 0)
		luasql_unregisterobj(L, cur->stmt);

	return 0;
}

/*
** Check for valid environment.
*/
static env_data *getenvironment (lua_State *L, int i) {
	env_data *env = (env_data *)luaL_checkudata (L, i, LUASQL_ENVIRONMENT_FIREBIRD);
	luaL_argcheck (L, env != NULL, i, "environment expected");
	luaL_argcheck (L, !env->closed, i, "environment is closed");
	return env;
}

/*
** Check for valid connection.
*/
static conn_data *getconnection (lua_State *L, int i) {
	conn_data *conn = (conn_data *)luaL_checkudata (L, i, LUASQL_CONNECTION_FIREBIRD);
	luaL_argcheck (L, conn != NULL, i, "connection expected");
	luaL_argcheck (L, !conn->closed, i, "connection is closed");
	return conn;
}

/*
** Check for valid statement.
*/
static stmt_data *getstatement (lua_State *L, int i) {
	stmt_data *stmt = (stmt_data *)luaL_checkudata (L, i, LUASQL_STATEMENT_FIREBIRD);
	luaL_argcheck (L, stmt != NULL, i, "statement expected");
	luaL_argcheck (L, !stmt->closed, i, "statement is closed");
	return stmt;
}

/*
** Check for valid cursor.
*/
static cur_data *getcursor (lua_State *L, int i) {
	cur_data *cur = (cur_data *)luaL_checkudata (L, i, LUASQL_CURSOR_FIREBIRD);
	luaL_argcheck (L, cur != NULL, i, "cursor expected");
	luaL_argcheck (L, !cur->closed, i, "cursor is closed");
	return cur;
}

/*
** Dumps the list of item's types into a new table
*/
static int dump_xsqlda_types(lua_State *L, XSQLDA* sqlda) {
	int i;
	XSQLVAR *var;

	lua_newtable(L);

	for (i=1, var = sqlda->sqlvar; i <= sqlda->sqld; i++, var++) {
		lua_pushnumber(L, i);
		switch(var->sqltype & ~1) {
		case SQL_VARYING:
		case SQL_TEXT:
		case SQL_TYPE_TIME:
		case SQL_TYPE_DATE:
		case SQL_TIMESTAMP:
		case SQL_BLOB:
            lua_pushstring(L, "string");
			break;
		case SQL_SHORT:
		case SQL_LONG:
		case SQL_INT64:
#if LUA_VERSION_NUM>=503
            lua_pushstring(L, "integer");
			break;
#endif
		case SQL_FLOAT:
		case SQL_DOUBLE:
            lua_pushstring(L, "number");
			break;
		default:
            lua_pushstring(L, "unknown");
			break;
		}
		lua_settable(L, -3);
	}

	return 1;
}

/*
** Returns the statement type
*/
static int get_statement_type(stmt_data *stmt)
{
	int length, type;
	char type_item[] = { isc_info_sql_stmt_type };
	char res_buffer[88], *pres;

	pres = res_buffer;

	isc_dsql_sql_info(  stmt->env->status_vector, &stmt->handle,
						sizeof(type_item), type_item,
						sizeof(res_buffer), res_buffer );
	if (stmt->env->status_vector[0] == 1 && stmt->env->status_vector[1] > 0)
		return -1;

	/* check the type of the statement */
	if (*pres == isc_info_sql_stmt_type)
	{
		pres++;
		length = isc_vax_integer(pres, 2);
		pres += 2;
		type = isc_vax_integer(pres, length);
		pres += length;
	} else
		return -2;	/* should have had the isc_info_sql_stmt_type info */

	return type;
}

/*
** Return the number of rows affected by last operation
*/
static int count_rows_affected(env_data *env, cur_data *cur)
{
	int length, type, res=0;
	int del_count = 0, ins_count = 0, upd_count = 0, sel_count = 0;
	char type_item[] = { isc_info_sql_stmt_type, isc_info_sql_records };
	char res_buffer[88], *pres;

	pres = res_buffer;

	isc_dsql_sql_info( env->status_vector, &cur->stmt->handle,
						sizeof(type_item), type_item,
						sizeof(res_buffer), res_buffer );
	if (cur->env->status_vector[0] == 1 && cur->env->status_vector[1] > 0)
		return -1;

	/* check the type of the statement */
	if (*pres == isc_info_sql_stmt_type)
	{
		pres++;
		length = isc_vax_integer(pres, 2);
		pres += 2;
		type = isc_vax_integer(pres, length);
		pres += length;
	} else
		return -2;	/* should have had the isc_info_sql_stmt_type info */

	if(type > 4)
		return 0;	/* not a SELECT, INSERT, UPDATE or DELETE SQL statement */

	if (*pres == isc_info_sql_records)
	{
		pres++;
		length = isc_vax_integer(pres, 2); /* normally 29 bytes */
        pres += 2;

		while(*pres != 1) {
			switch(*pres) {
			case isc_info_req_select_count:
				pres++;
				length = isc_vax_integer(pres, 2);
				pres += 2;
				sel_count = isc_vax_integer(pres, length);
				pres += length;
				break;
			case isc_info_req_insert_count:
				pres++;
				length = isc_vax_integer(pres, 2);
				pres += 2;
				ins_count = isc_vax_integer(pres, length);
				pres += length;
				break;
			case isc_info_req_update_count:
				pres++;
				length = isc_vax_integer(pres, 2);
				pres += 2;
				upd_count = isc_vax_integer(pres, length);
				pres += length;
				break;
			case isc_info_req_delete_count:
				pres++;
				length = isc_vax_integer(pres, 2);
				pres += 2;
				del_count = isc_vax_integer(pres, length);
				pres += length;
				break;
			default:
				pres++;
				break;
			}
		}
	} else
		return -3;

	switch(type) {
	case isc_info_sql_stmt_select:
		res = sel_count;
		break;
	case isc_info_sql_stmt_delete:
		res = del_count;
		break;
	case isc_info_sql_stmt_update:
		res = upd_count;
		break;
	case isc_info_sql_stmt_insert:
		res = ins_count;
		break;
	}
	return res;
}

static void fill_param(XSQLVAR *var, ISC_SHORT type, ISC_SCHAR *data, ISC_SHORT len)
{
	var->sqltype = type;
	*var->sqlind = 0;
	var->sqllen = len;

	if((type & ~1) == SQL_TEXT) {
		--var->sqllen;
	}

	if(var->sqldata != NULL) {
		free(var->sqldata);
	}
	var->sqldata = (ISC_SCHAR *)malloc(len);
	memcpy(var->sqldata, data, len);
}

static void parse_params(lua_State *L, stmt_data* stmt, int params)
{
	int i;
	for(i=0; i<stmt->in_sqlda->sqln; i++) {
		XSQLVAR *var;
		const char* str;
		ISC_INT64 inum;
		double fnum;

		lua_pushnumber(L, i+1);
		lua_gettable(L, params);

		var = &stmt->in_sqlda->sqlvar[i];
		if(var->sqlind == NULL) {
			var->sqlind = (ISC_SHORT *)malloc(sizeof(ISC_SHORT));
		}

		if(lua_isnil(L, -1)) {
			// nil -> NULL
			*var->sqlind = -1;
		} else {
			switch(var->sqltype & ~1) {
			case SQL_VARYING:
			case SQL_BLOB:
			case SQL_TEXT:
				str = lua_tostring(L, -1);
				fill_param(var, SQL_TEXT+1, (ISC_SCHAR *)str, strlen(str)+1);
				break;

			case SQL_INT64:
			case SQL_LONG:
			case SQL_SHORT:
				inum = (ISC_INT64)lua_tonumber(L, -1);
				fill_param(var, SQL_INT64+1, (ISC_SCHAR *)&inum, sizeof(ISC_INT64));
				break;

			case SQL_DOUBLE:
			case SQL_D_FLOAT:
			case SQL_FLOAT:
				fnum = (double)lua_tonumber(L, -1);
				fill_param(var, SQL_DOUBLE+1, (ISC_SCHAR *)&fnum, sizeof(double));
				break;

			case SQL_TIMESTAMP:
			case SQL_TYPE_TIME:
			case SQL_TYPE_DATE:
				switch(lua_type(L, -1)) {
				case LUA_TNUMBER: {
					/* os.time type value passed */
					time_t t_time = (time_t)lua_tointeger(L,-1);
					struct tm *tm_time = localtime(&t_time);
					ISC_TIMESTAMP isc_ts;
					isc_encode_timestamp(tm_time, &isc_ts);

					fill_param(var, SQL_TIMESTAMP+1, (ISC_SCHAR *)&isc_ts, sizeof(ISC_TIMESTAMP));
				}	break;

				case LUA_TSTRING: {
					/* date/time string passed */
					str = lua_tostring(L, -1);
					fill_param(var, SQL_TEXT+1, (ISC_SCHAR *)str, strlen(str)+1);
				}	break;

				default: {
					/* unknown pass empty string, which should error out */
					str = lua_tostring(L, -1);
					fill_param(var, SQL_TEXT+1, (ISC_SCHAR *)"", 1);
				}	break;
				}
				break;
			}
		}

		lua_pop(L,1);  /* param value */
	}
}

/*
** Prepares a SQL statement.
** Lua input:
**   SQL statement
**  [parmeter table]
** Returns
**   statement object ready for setting parameters
**   nil and error message otherwise.
*/
static int conn_prepare (lua_State *L) {
	conn_data *conn = getconnection(L,1);
	const char *statement = luaL_checkstring(L, 2);

	stmt_data* user_stmt;

	stmt_data stmt;

	memset(&stmt, 0, sizeof(stmt_data));

	stmt.closed = 0;
	stmt.env = conn->env;
	stmt.conn = conn;

	stmt.handle = NULL;

	/* create a statement to handle the query */
	isc_dsql_allocate_statement(conn->env->status_vector, &conn->db, &stmt.handle);
	if ( CHECK_DB_ERROR(conn->env->status_vector) ) {
		return return_db_error(L, conn->env->status_vector);
	}

	/* process the SQL ready to run the query */
	isc_dsql_prepare(conn->env->status_vector, &conn->transaction, &stmt.handle, 0, (char*)statement, conn->dialect, NULL);
	if ( CHECK_DB_ERROR(conn->env->status_vector) ) {
		free_stmt(&stmt);
		return return_db_error(L, conn->env->status_vector);
	}

	/* what type of SQL statement is it? */
	stmt.type = get_statement_type(&stmt);
	if(stmt.type < 0) {
		free_stmt(&stmt);
		return return_db_error(L, stmt.env->status_vector);
	}

	/* an unsupported SQL statement (something like COMMIT) */
	if(stmt.type > 5) {
		free_stmt(&stmt);
		return luasql_faildirect(L, "unsupported SQL statement");
	}

	/* bind the input parameters */
	stmt.in_sqlda = (XSQLDA*)malloc(XSQLDA_LENGTH(1));
	stmt.in_sqlda->version = SQLDA_VERSION1;
	stmt.in_sqlda->sqln = 1;
	isc_dsql_describe_bind(conn->env->status_vector, &stmt.handle, 1, stmt.in_sqlda);
	if ( CHECK_DB_ERROR(conn->env->status_vector) ) {
		free_stmt(&stmt);
		return return_db_error(L, conn->env->status_vector);
	}
	/* resize the parameter set if needed */
	if (stmt.in_sqlda->sqld > stmt.in_sqlda->sqln)
	{
		short n = stmt.in_sqlda->sqld;
		free(stmt.in_sqlda);
		stmt.in_sqlda = (XSQLDA *)malloc(XSQLDA_LENGTH(n));
		memset(stmt.in_sqlda, 0, XSQLDA_LENGTH(n));
		stmt.in_sqlda->sqln = n;
		stmt.in_sqlda->version = SQLDA_VERSION1;
		isc_dsql_describe_bind(conn->env->status_vector, &stmt.handle, 1, stmt.in_sqlda);
		if ( CHECK_DB_ERROR(conn->env->status_vector) ) {
			free_stmt(&stmt);
			return return_db_error(L, conn->env->status_vector);
		}
	}
	malloc_sqlda_vars(stmt.in_sqlda);

	/* is there a parameter table to use */
	if(lua_istable(L, 3)) {
		parse_params(L, &stmt, 3);
	}

	/* copy the statement into a new lua userdata object */
	user_stmt = (stmt_data*)lua_newuserdata(L, sizeof(stmt_data));
	luasql_setmeta (L, LUASQL_STATEMENT_FIREBIRD);
	memcpy((void*)user_stmt, (void*)&stmt, sizeof(stmt_data));

	/* add statement to the lock count */
	luasql_registerobj(L, 1, conn);
	++conn->lock;

	return 1;
}

static int raw_execute (lua_State *L, int stmt_indx)
{
	int count;
	cur_data cur;
	stmt_data *stmt;

	if(stmt_indx < 0) {
		stmt_indx = lua_gettop(L) + stmt_indx + 1;
	}

	stmt = getstatement(L,stmt_indx);

	/* is there already a cursor open */
	if(stmt->lock > 0) {
		return luasql_faildirect(L, "statement already has an open cursor");
	}

	memset(&cur, 0, sizeof(cur_data));
	cur.closed = 0;
	cur.stmt = stmt;
	cur.env = stmt->env;

	/* if it's a SELECT statment, allocate a cursor */
	if(stmt->type == isc_info_sql_stmt_select) {
		char cur_name[64];
		snprintf(cur_name, sizeof(cur_name), "dyn_cursor_%p", (void *)stmt);

		/* open the cursor ready for fetch cycles */
		isc_dsql_set_cursor_name(cur.env->status_vector, &cur.stmt->handle, cur_name, 0);
		if ( CHECK_DB_ERROR(cur.env->status_vector) ) {
			lua_pop(L, 1);	/* the userdata */
			free_cur(&cur);
			return return_db_error(L, cur.env->status_vector);
		}
	}

	/* run the query */
	isc_dsql_execute(stmt->env->status_vector, &stmt->conn->transaction, &stmt->handle, 1, stmt->in_sqlda);
	if ( CHECK_DB_ERROR(stmt->env->status_vector) ) {
		free_cur(&cur);
		return return_db_error(L, cur.env->status_vector);
	}

	/* size the result, set if needed */
	cur.out_sqlda = (XSQLDA*)malloc(XSQLDA_LENGTH(1));
	cur.out_sqlda->version = SQLDA_VERSION1;
	cur.out_sqlda->sqln = 1;
	isc_dsql_describe(cur.env->status_vector, &cur.stmt->handle, 1, cur.out_sqlda);
	if (cur.out_sqlda->sqld > cur.out_sqlda->sqln) {
		short n = cur.out_sqlda->sqld;
		free(cur.out_sqlda);
		cur.out_sqlda = (XSQLDA *)malloc(XSQLDA_LENGTH(n));
		cur.out_sqlda->sqln = n;
		cur.out_sqlda->version = SQLDA_VERSION1;
		isc_dsql_describe(cur.env->status_vector, &cur.stmt->handle, 1, cur.out_sqlda);
		if ( CHECK_DB_ERROR(cur.env->status_vector) ) {
			free_cur(&cur);
			return return_db_error(L, cur.env->status_vector);
		}
	}
	malloc_sqlda_vars(cur.out_sqlda);

	/* what do we return? a cursor or a count */
	if(cur.out_sqlda->sqld > 0) { /* a cursor */
		cur_data* user_cur = (cur_data*)lua_newuserdata(L, sizeof(cur_data));
		luasql_setmeta (L, LUASQL_CURSOR_FIREBIRD);

		/* copy the cursor into a new lua userdata object */
		memcpy((void*)user_cur, (void*)&cur, sizeof(cur_data));

		/* add cursor to the lock count */
		luasql_registerobj(L, stmt_indx, user_cur->stmt);
		++user_cur->stmt->lock;
	} else { /* a count */
		/* if autocommit is set, commit change */
		if(cur.stmt->conn->autocommit) {
			isc_commit_retaining(cur.env->status_vector, &cur.stmt->conn->transaction);
			if ( CHECK_DB_ERROR(cur.env->status_vector) ) {
				free_cur(&cur);
				return return_db_error(L, cur.env->status_vector);
			}
		}

		if( (count = count_rows_affected(cur.env, &cur)) < 0 ) {
			free_cur(&cur);
			return return_db_error(L, cur.env->status_vector);
		}

		luasql_pushinteger(L, count);

		/* totaly finished with the cursor */
		isc_dsql_free_statement(cur.env->status_vector, &cur.stmt->handle, DSQL_close);
		free_cur(&cur);
	}

	return 1;
}

/*
** Executes a SQL statement.
** Lua input:
**   SQL statement
**  [parameter table]
** Returns
**   cursor object: if there are results or
**   row count: number of rows affected by statement if no results
**   nil and error message otherwise.
*/

static int conn_execute (lua_State *L) {
	int ret;
	stmt_data *stmt;

	/* prepare the statement */
	if( (ret = conn_prepare(L)) != 1) {
		return ret;
	}

	/* execute and check result */
	if((ret = raw_execute(L, -1)) != 1) {
		return ret;
	}

	/* for neatness, remove stmt from stack */
	stmt = getstatement(L, -(ret+1));
	lua_remove(L, -(ret+1));

	/* if statement doesn't return a cursor, close it */
	if(stmt->type != isc_info_sql_stmt_select) {
		if((ret = stmt_shut(L, stmt)) != 0) {
			return ret;
		}
	}

	return 1;
}

/*
** Commits the current transaction
*/
static int conn_commit(lua_State *L) {
	conn_data *conn = getconnection(L,1);

	isc_commit_retaining(conn->env->status_vector, &conn->transaction);
	if ( CHECK_DB_ERROR(conn->env->status_vector) )
		return return_db_error(L, conn->env->status_vector);

	lua_pushboolean(L, 1);
	return 1;
}

/*
** Rolls back the current transaction
** Lua Returns:
**   1 if rollback is sucsessful
**   nil and error message otherwise.
*/
static int conn_rollback(lua_State *L) {
	conn_data *conn = getconnection(L,1);

	isc_rollback_retaining(conn->env->status_vector, &conn->transaction);
	if ( CHECK_DB_ERROR(conn->env->status_vector) )
		return return_db_error(L, conn->env->status_vector);

	lua_pushboolean(L, 1);
	return 1;
}

/*
** Sets the autocommit state of the connection
** Lua Returns:
**   autocommit state (0:off, 1:on)
**   nil and error message on error.
*/
static int conn_setautocommit(lua_State *L) {
	conn_data *conn = getconnection(L,1);

	if(lua_toboolean(L, 2))
		conn->autocommit = 1;
	else
		conn->autocommit = 0;

	lua_pushboolean(L, 1);
	return 1;
}

/*
** Closes a connection.
** Lua Returns:
**   1 if close was sucsessful, 0 if already closed
**   nil and error message otherwise.
*/
static int conn_close (lua_State *L) {
	conn_data *conn = (conn_data *)luaL_checkudata(L,1,LUASQL_CONNECTION_FIREBIRD);
	luaL_argcheck (L, conn != NULL, 1, "connection expected");

	/* already closed */
	if(conn->closed != 0) {
		lua_pushboolean(L, 0);
		return 1;
	}

	/* are all related statements closed? */
	if(conn->lock > 0)
		return luasql_faildirect(L, "there are still open statements/cursors");

	if(conn->autocommit != 0)
		isc_commit_transaction(conn->env->status_vector, &conn->transaction);
	else
		isc_rollback_transaction(conn->env->status_vector, &conn->transaction);
	if ( CHECK_DB_ERROR(conn->env->status_vector) )
		return return_db_error(L, conn->env->status_vector);

	isc_detach_database(conn->env->status_vector, &conn->db);
	if ( CHECK_DB_ERROR(conn->env->status_vector) )
		return return_db_error(L, conn->env->status_vector);

	conn->closed = 1;
	--conn->env->lock;

	/* check environment can be GC'd */
	if(conn->env->lock == 0)
		luasql_unregisterobj(L, conn->env);

	lua_pushboolean(L, 1);
	return 1;
}

/*
** GCs an connection object
*/
static int conn_gc (lua_State *L) {
	conn_data *conn = (conn_data *)luaL_checkudata(L,1,LUASQL_CONNECTION_FIREBIRD);

	if(conn->closed == 0) {
		if(conn->autocommit != 0)
			isc_commit_transaction(conn->env->status_vector, &conn->transaction);
		else
			isc_rollback_transaction(conn->env->status_vector, &conn->transaction);

		isc_detach_database(conn->env->status_vector, &conn->db);

		conn->closed = 1;
		--conn->env->lock;

		/* check environment can be GC'd */
		if(conn->env->lock == 0)
			luasql_unregisterobj(L, conn->env);
	}

	return 0;
}

/*
** Escapes a given string so that it can't break out of it's delimiting quotes
*/
static int conn_escape(lua_State *L) {
	size_t len;
	const char *from = luaL_checklstring (L, 2, &len);
	char *res = malloc(len*sizeof(char)*2+1);
	char *to = res;

	if(res) {
		while(*from != '\0') {
			*(to++) = *from;
			if(*from == '\'')
				*(to++) = *from;

			from++;
		}
		*to = '\0';

		lua_pushstring(L, res);
		free(res);
		return 1;
	}

	luaL_error(L, "could not allocate escaped string");
	return 0;
}

/*
** Pushes the indexed value onto the lua stack
*/
static void push_column(lua_State *L, int i, cur_data *cur) {
	int varcharlen;
	struct tm timevar;
	char timestr[256];
	ISC_STATUS blob_stat;
	isc_blob_handle blob_handle = 0;
	ISC_QUAD blob_id;
	luaL_Buffer b;
	char *buffer;
	unsigned short actual_seg_len;

	if( (cur->out_sqlda->sqlvar[i].sqlind != NULL) &&
		(*(cur->out_sqlda->sqlvar[i].sqlind) != 0) ) {
		/* a null field? */
		lua_pushnil(L);
	} else {
		switch(cur->out_sqlda->sqlvar[i].sqltype & ~1) {
		case SQL_VARYING:
			varcharlen = (int)isc_vax_integer(cur->out_sqlda->sqlvar[i].sqldata, 2);
			lua_pushlstring(L, cur->out_sqlda->sqlvar[i].sqldata+2, varcharlen);
			break;
		case SQL_TEXT:
			lua_pushlstring(L, cur->out_sqlda->sqlvar[i].sqldata, cur->out_sqlda->sqlvar[i].sqllen);
			break;
		case SQL_SHORT:
			luasql_pushinteger(L, *(short*)(cur->out_sqlda->sqlvar[i].sqldata));
			break;
		case SQL_LONG:
			luasql_pushinteger(L, *(long*)(cur->out_sqlda->sqlvar[i].sqldata));
			break;
		case SQL_INT64:
			luasql_pushinteger(L, *(ISC_INT64*)(cur->out_sqlda->sqlvar[i].sqldata));
			break;
		case SQL_FLOAT:
			lua_pushnumber(L, *(float*)(cur->out_sqlda->sqlvar[i].sqldata));
			break;
		case SQL_DOUBLE:
			lua_pushnumber(L, *(double*)(cur->out_sqlda->sqlvar[i].sqldata));
			break;
		case SQL_TYPE_TIME:
			isc_decode_sql_time((ISC_TIME*)(cur->out_sqlda->sqlvar[i].sqldata), &timevar);
			strftime(timestr, 255, "%X", &timevar);
			lua_pushstring(L, timestr);
			break;
		case SQL_TYPE_DATE:
			isc_decode_sql_date((ISC_DATE*)(cur->out_sqlda->sqlvar[i].sqldata), &timevar);
			strftime(timestr, 255, "%Y-%m-%d", &timevar);
			lua_pushstring(L, timestr);
			break;
		case SQL_TIMESTAMP:
			isc_decode_timestamp((ISC_TIMESTAMP*)(cur->out_sqlda->sqlvar[i].sqldata), &timevar);
			strftime(timestr, 255, "%Y-%m-%d %X", &timevar);
			lua_pushstring(L, timestr);
			break;
		case SQL_BLOB:
			/* get the BLOB ID and open it */
			memcpy(&blob_id, cur->out_sqlda->sqlvar[i].sqldata, sizeof(ISC_QUAD));
			isc_open_blob2(	cur->env->status_vector,
							&cur->stmt->conn->db, &cur->stmt->conn->transaction,
							&blob_handle, &blob_id, 0, NULL );
			/* fetch the blob data */
			luaL_buffinit(L, &b);
			buffer = luaL_prepbuffer(&b);

			blob_stat = isc_get_segment(	cur->env->status_vector,
											&blob_handle, &actual_seg_len,
											LUAL_BUFFERSIZE, buffer );
			while(blob_stat == 0 || cur->env->status_vector[1] == isc_segment) {
				luaL_addsize(&b, actual_seg_len);
				buffer = luaL_prepbuffer(&b);
				blob_stat = isc_get_segment(	cur->env->status_vector,
												&blob_handle, &actual_seg_len,
												LUAL_BUFFERSIZE, buffer );
			}

			/* finnished, close the BLOB */
			isc_close_blob(cur->env->status_vector, &blob_handle);
			blob_handle = 0;

			luaL_pushresult(&b);
			break;
		default:
			lua_pushstring(L, "<unsupported data type>");
			break;
		}
	}
}

/*
** Returns a map of parameter IDs to their types
*/
static int stmt_get_params (lua_State *L) {
	stmt_data *stmt = getstatement(L,1);

	return dump_xsqlda_types(L, stmt->in_sqlda);
}

/*
** Executes the statement
** Lua input:
**   [table of values]
** Returns
**   cursor object: if there are results or
**   row count: number of rows affected by statement if no results
**   nil and error message otherwise.
*/
static int stmt_execute (lua_State *L) {
	stmt_data *stmt = getstatement(L,1);

	/* is there a parameter table to use */
	if(lua_istable(L, 2)) {
		parse_params(L, stmt, 2);
	}

	return raw_execute(L, 1);
}

/*
** Closes a statement object
** Lua Returns:
**   true if close was sucsessful, false if already closed
**   nil and error message otherwise.
*/
static int stmt_close (lua_State *L) {
	stmt_data *stmt = (stmt_data *)luaL_checkudata(L,1,LUASQL_STATEMENT_FIREBIRD);
	luaL_argcheck (L, stmt != NULL, 1, "statement expected");

	if(stmt->lock > 0) {
		return luasql_faildirect(L, "there are still open cursors");
	}

	if(stmt->closed == 0) {
		int res = stmt_shut(L, stmt);
		if(res != 0) {
			return res;
		}

		/* return sucsess */
		lua_pushboolean(L, 1);
		return 1;
	}

	lua_pushboolean(L, 0);
	return 1;
}

/*
** Frees up memory alloc'd to a statement
*/
static int stmt_gc (lua_State *L) {
	stmt_data *stmt = (stmt_data *)luaL_checkudata(L,1,LUASQL_STATEMENT_FIREBIRD);
	luaL_argcheck (L, stmt != NULL, 1, "statement expected");

	if(stmt->closed == 0) {
		if(stmt_shut(L, stmt) != 0) {
			return 1;
		}
	}

	return 0;
}

/*
** Returns a row of data from the query
** Lua Returns:
**   list of results or table of results depending on call
**   nil and error message otherwise.
*/
static int cur_fetch (lua_State *L) {
	ISC_STATUS fetch_stat;
	int i;
	cur_data *cur = getcursor(L,1);
	const char *opts = luaL_optstring (L, 3, "n");
	int num = strchr(opts, 'n') != NULL;
	int alpha = strchr(opts, 'a') != NULL;

	if ((fetch_stat = isc_dsql_fetch(cur->env->status_vector, &cur->stmt->handle, 1, cur->out_sqlda)) == 0) {
		if (lua_istable (L, 2)) {
			/* remove the option string */
			lua_settop(L, 2);

			/* loop through the columns */
			for (i = 0; i < cur->out_sqlda->sqld; i++) {
				push_column(L, i, cur);

				if( num ) {
					lua_pushnumber(L, i+1);
					lua_pushvalue(L, -2);
					lua_settable(L, 2);
				}

				if( alpha ) {
					lua_pushlstring(L, cur->out_sqlda->sqlvar[i].aliasname, cur->out_sqlda->sqlvar[i].aliasname_length);
					lua_pushvalue(L, -2);
					lua_settable(L, 2);
				}

				lua_pop(L, 1);
			}

			/* returning given table */
			return 1;
		} else {
			for (i = 0; i < cur->out_sqlda->sqld; i++)
				push_column(L, i, cur);

			/* returning a list of values */
			return cur->out_sqlda->sqld;
		}
	}

	/* isc_dsql_fetch returns 100 if no more rows remain to be retrieved
	   so this can be ignored */
	if (fetch_stat != 100L)
		return return_db_error(L, cur->env->status_vector);

	/* shut cursor */
	return cur_shut(L, cur);
}

/*
** Returns a table of column names from the query
** Lua Returns:
**   a table of column names
**   nil and error message otherwise.
*/
static int cur_colnames (lua_State *L) {
	int i;
	XSQLVAR *var;
	cur_data *cur = getcursor(L,1);

	lua_newtable(L);

	for (i=1, var = cur->out_sqlda->sqlvar; i <= cur->out_sqlda->sqld; i++, var++) {
		lua_pushnumber(L, i);
		lua_pushlstring(L, var->aliasname, var->aliasname_length);
		lua_settable(L, -3);
	}

	return 1;
}

/*
** Returns a table of column types from the query
** Lua Returns:
**   a table of column types
**   nil and error message otherwise.
*/
static int cur_coltypes (lua_State *L) {
	cur_data *cur = getcursor(L,1);

	return dump_xsqlda_types(L, cur->out_sqlda);
}

/*
** Closes a cursor object
** Lua Returns:
**   true if close was sucsessful, false if already closed
**   nil and error message otherwise.
*/
static int cur_close (lua_State *L) {
	cur_data *cur = (cur_data *)luaL_checkudata(L,1,LUASQL_CURSOR_FIREBIRD);
	luaL_argcheck (L, cur != NULL, 1, "cursor expected");

	if(cur->closed == 0) {
		int res = cur_shut(L, cur);
		if(res != 0) {
			return res;
		}

		/* return sucsess */
		lua_pushboolean(L, 1);
		return 1;
	}

	lua_pushboolean(L, 0);
	return 1;
}

/*
** GCs a cursor object
*/
static int cur_gc (lua_State *L) {
	cur_data *cur = (cur_data *)luaL_checkudata(L,1,LUASQL_CURSOR_FIREBIRD);
	luaL_argcheck (L, cur != NULL, 1, "cursor expected");

	if(cur->closed == 0) {
		if(cur_shut(L, cur) != 0) {
			return 1;
		}
	}

	return 0;
}

/*
** Creates an Environment and returns it.
*/
static int create_environment (lua_State *L) {
	int i;
	env_data *env;

	env = (env_data *)lua_newuserdata (L, sizeof (env_data));
	luasql_setmeta (L, LUASQL_ENVIRONMENT_FIREBIRD);
	/* fill in structure */
	for(i=0; i<20; i++)
		env->status_vector[i] = 0;
	env->closed = 0;
	env->lock = 0;

	return 1;
}

/*
** Reforms old connection style to new one
** Lua Input: source, [user, [pass]]
**   source: data source
**   user, pass: data source authentication information
** Lua Returns:
**   new connection details table
*/
static void env_connect_fix_old (lua_State *L) {
	if(lua_isstring(L, 2)) {
		/* convert to new table format */
		int n = lua_gettop(L);

		const char *sourcename = luaL_checkstring (L, 2);
		const char *username = luaL_optstring (L, 3, "");
		const char *password = luaL_optstring (L, 4, "");

		lua_newtable(L);
		lua_pushstring(L, "source");
		lua_pushstring(L, sourcename);
		lua_settable(L, -3);
		lua_pushstring(L, "user");
		lua_pushstring(L, username);
		lua_settable(L, -3);
		lua_pushstring(L, "password");
		lua_pushstring(L, password);
		lua_settable(L, -3);

		while(n > 1) {
			lua_remove(L, n--);
		}
	}
}

static char* add_dpb_string(char* dpb, char* dpb_end, char item, const char* str)
{
	size_t len = strlen(str);
	size_t i;

	if(dpb+2 < dpb_end) {
		*dpb++ = item;
		*dpb++ = (char)len;
		for(i=0; dpb<dpb_end && i<len; i++) {
			*dpb++ = str[i];
		}
	}

	return dpb;
}

/*
** Creates and returns a connection object
** Lua Input: {
**    source = <database source/path>,
**    user = <user name>,
**    password = <user password>,
**   [charset = <connection charset (UTF8)>,]
**   [dialect = <SQL dialect to use (3)>,]
** }
** Lua Returns:
**   connection object if successfull
**   nil and error message otherwise.
*/
static int env_connect (lua_State *L) {
	char *dpb, *dpb_end;
	static char isc_tpb[] = {
		isc_tpb_version3,
		isc_tpb_write
	};

	conn_data conn;
	conn_data* res_conn;
	const char *sourcename;

	env_data *env = (env_data *) getenvironment (L, 1);

	if(lua_gettop(L) < 2) {
		return luasql_faildirect(L, "No connection details provided");
	}

	if(!lua_istable(L, 2)) {
		env_connect_fix_old(L);
	}

	sourcename = luasql_table_optstring(L, 2, "source", NULL);

	if(sourcename == NULL) {
		return luasql_faildirect(L, "connection details table missing 'source'");
	}

	conn.env = env;
	conn.db = 0L;
	conn.transaction = 0L;
	conn.lock = 0;
	conn.autocommit = 0;

	/* construct a database parameter buffer. */
	dpb = conn.dpb_buffer;
	dpb_end = conn.dpb_buffer + sizeof(conn.dpb_buffer);
	*dpb++ = isc_dpb_version1;
	*dpb++ = isc_dpb_num_buffers;
	*dpb++ = 1;
	*dpb++ = 90;

	/* add the user name and password */
	dpb = add_dpb_string(dpb, dpb_end, isc_dpb_user_name, luasql_table_optstring(L, 2, "user", ""));
	dpb = add_dpb_string(dpb, dpb_end, isc_dpb_password, luasql_table_optstring(L, 2, "password", ""));

	/* other database parameters */
	dpb = add_dpb_string(dpb, dpb_end, isc_dpb_lc_ctype, luasql_table_optstring(L, 2, "charset", "UTF8"));
	conn.dialect = (unsigned short)luasql_table_optnumber(L, 2, "dialect", 3);

	/* the length of the dpb */
	conn.dpb_length = (short)(dpb - conn.dpb_buffer);

	/* do the job */
	isc_attach_database(env->status_vector, (short)strlen(sourcename), (char*)sourcename, &conn.db,
						conn.dpb_length,	conn.dpb_buffer);

	/* an error? */
	if ( CHECK_DB_ERROR(conn.env->status_vector) )
		return return_db_error(L, conn.env->status_vector);

	/* open up the transaction handle */
	isc_start_transaction(	env->status_vector, &conn.transaction, 1,
							&conn.db, (unsigned short)sizeof(isc_tpb),
							isc_tpb );

	/* an error? */
	if ( CHECK_DB_ERROR(conn.env->status_vector) )
		return return_db_error(L, conn.env->status_vector);

	/* create the lua object and add the connection to the lock */
	res_conn = (conn_data*)lua_newuserdata(L, sizeof(conn_data));
	luasql_setmeta (L, LUASQL_CONNECTION_FIREBIRD);
	memcpy(res_conn, &conn, sizeof(conn_data));
	res_conn->closed = 0;   /* connect now officially open */

	/* register the connection */
	luasql_registerobj(L, 1, env);
	++env->lock;

	return 1;
}

/*
** Closes an environment object
** Lua Returns:
**   1 if close was sucsessful, 0 if already closed
**   nil and error message otherwise.
*/
static int env_close (lua_State *L) {
	env_data *env = (env_data *)luaL_checkudata (L, 1, LUASQL_ENVIRONMENT_FIREBIRD);
	luaL_argcheck (L, env != NULL, 1, "environment expected");
	
	/* already closed? */
	if(env->closed == 1) {
		lua_pushboolean(L, 0);
		return 1;
	}

	/* check the lock */
	if(env->lock > 0)
		return luasql_faildirect(L, "there are still open connections");

	/* unregister */
	luasql_unregisterobj(L, env);

	/* mark as closed */
	env->closed = 1;

	lua_pushboolean(L, 1);
	return 1;
}

/*
** GCs an environment object
*/
static int env_gc (lua_State *L) {
	/* nothing to be done for the FB envronment */
	return 0;
}

/*
** Create metatables for each class of object.
*/
static void create_metatables (lua_State *L) {
	struct luaL_Reg environment_methods[] = {
		{"__gc", env_gc},
		{"close", env_close},
		{"connect", env_connect},
		{NULL, NULL},
	};
	struct luaL_Reg connection_methods[] = {
		{"__gc", conn_gc},
		{"close", conn_close},
		{"prepare", conn_prepare},
		{"execute", conn_execute},
		{"commit", conn_commit},
		{"rollback", conn_rollback},
		{"setautocommit", conn_setautocommit},
		{"escape", conn_escape},
		{NULL, NULL},
	};
	struct luaL_Reg statement_methods[] = {
		{"__gc", stmt_gc},
		{"close", stmt_close},
		{"getparamtypes", stmt_get_params},
		{"execute", stmt_execute},
		{NULL, NULL},
	};
	struct luaL_Reg cursor_methods[] = {
		{"__gc", cur_gc},
		{"close", cur_close},
		{"fetch", cur_fetch},
		{"getcoltypes", cur_coltypes},
		{"getcolnames", cur_colnames},
		{NULL, NULL},
	};
	luasql_createmeta (L, LUASQL_ENVIRONMENT_FIREBIRD, environment_methods);
	luasql_createmeta (L, LUASQL_CONNECTION_FIREBIRD, connection_methods);
	luasql_createmeta (L, LUASQL_STATEMENT_FIREBIRD, statement_methods);
	luasql_createmeta (L, LUASQL_CURSOR_FIREBIRD, cursor_methods);
	lua_pop (L, 4);
}

/*
** Creates the metatables for the objects and registers the
** driver open method.
*/
LUASQL_API int luaopen_luasql_firebird (lua_State *L) {
	struct luaL_Reg driver[] = {
		{"firebird", create_environment},
		{NULL, NULL},
	};
	create_metatables (L);
	luasql_find_driver_table (L);
	luaL_setfuncs (L, driver, 0);
	luasql_set_info (L);
	return 1;
} 
