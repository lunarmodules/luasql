/*
** LuaSQL, Firebird driver
** Authors: Scott Morgan
** ls_firebird.c
*/

#include <ibase.h>	/* The Firebird API*/
#include <time.h>	/* For managing time values */
#include <malloc.h>
#include <string.h>

/* Lua API */
#include <lua.h>
#include <lauxlib.h>

#include "luasql.h"
#include "ls_firebird.h"

#define LUASQL_ENVIRONMENT_FIREBIRD "Firebird environment"
#define LUASQL_CONNECTION_FIREBIRD "Firebird connection"
#define LUASQL_CURSOR_FIREBIRD "Firebird cursor"

typedef struct {
	short closed;
	ISC_STATUS status_vector[20];	/* for error results */
	int lock;						/* lock count for open connections */
} env_data;

typedef struct {
	short			closed;
	env_data*		env;			/* the DB enviroment this is in */
	isc_db_handle	db;				/* the database handle */
	char*			dpb_buffer;		/* holds the database paramet buffer */
	short			dpb_length;		/* the used amount of the dpb */
	isc_tr_handle	transaction;	/* the transaction handle */
	int				lock;			/* lock count for open cursors */
	int				autocommit;		/* should each statment be commited */
} conn_data;

typedef struct {
	short			closed;
	env_data*		env;			/* the DB enviroment this is in */
	conn_data*		conn;			/* the DB connection this cursor is from */
	isc_stmt_handle stmt;			/* the statment handle */
	XSQLDA			*out_sqlda;
	XSQLVAR			*var;
} cur_data;

/*
** Check for valid environment.
*/
static env_data *getenvironment (lua_State *L) {
	env_data *env = (env_data *)luaL_checkudata (L, 1, LUASQL_ENVIRONMENT_FIREBIRD);
	luaL_argcheck (L, env != NULL, 1, "environment expected");
	luaL_argcheck (L, !env->closed, 1, "environment is closed");
	return env;
}

/*
** Returns the statment type
*/
int get_statment_type(cur_data* cur)
{
	int length, type;
	char type_item[] = { isc_info_sql_stmt_type };
	char res_buffer[88], *pres;

	pres = res_buffer;

	isc_dsql_sql_info( cur->env->status_vector, &cur->stmt,
						sizeof(type_item), type_item,
						sizeof(res_buffer), res_buffer );
	if (cur->env->status_vector[0] == 1 && cur->env->status_vector[1] > 0)
		return -1;

	/* check the type of the statment */
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
int count_rows_affected(cur_data* cur)
{
	int length, type, res;
	int del_count = 0, ins_count = 0, upd_count = 0, sel_count = 0;
	char type_item[] = { isc_info_sql_stmt_type, isc_info_sql_records };
	char res_buffer[88], *pres;

	pres = res_buffer;

	isc_dsql_sql_info( cur->env->status_vector, &cur->stmt,
						sizeof(type_item), type_item,
						sizeof(res_buffer), res_buffer );
	if (cur->env->status_vector[0] == 1 && cur->env->status_vector[1] > 0)
		return -1;

	/* check the type of the statment */
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
		return 0;	/* not a SELECT, INSERT, UPDATE or DELETE SQL statment */

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

/*
** Executes a SQL statement.
** Returns
**   cursor object: if there are results or
**   row count: number of rows affected by statement if no results
*/
static int conn_execute (lua_State *L) {
	char errmsg[512];
	long *pvector;
	conn_data *conn = (conn_data *)luaL_checkudata(L,1,LUASQL_CONNECTION_FIREBIRD);
	const char *statement = luaL_checkstring(L, 2);
	int dialect = (int)luaL_optnumber(L, 3, 3);

	XSQLVAR *var;
	long dtype;
	int i, n, count, stmt_type;

	cur_data* cur;

	/* closed? */
	if(conn->closed != 0) {
		lua_pushnil(L);
		lua_pushstring(L, "connection is closed");
		return 2;
	}

	cur = (cur_data*)malloc(sizeof(cur_data));
	cur->closed = 0;
	cur->env = conn->env;
	cur->conn = conn;
	cur->stmt = NULL;

	cur->out_sqlda = (XSQLDA *)malloc(XSQLDA_LENGTH(0));
	cur->out_sqlda->version = SQLDA_VERSION1;
	cur->out_sqlda->sqln = 0;

	/* create a statment to handle the query */
	isc_dsql_allocate_statement(conn->env->status_vector, &conn->db, &cur->stmt);
	if (conn->env->status_vector[0] == 1 && conn->env->status_vector[1])
	{
		lua_pushnil(L);
		pvector = conn->env->status_vector;
		isc_interprete(errmsg, &pvector);
		lua_pushstring(L, errmsg);

		return 2;
	}

	/* process the SQL ready to run the query */
	isc_dsql_prepare(conn->env->status_vector, &conn->transaction, &cur->stmt, 0, (char*)statement, dialect, cur->out_sqlda);
	if (conn->env->status_vector[0] == 1 && conn->env->status_vector[1])
	{
		lua_pushnil(L);
		pvector = conn->env->status_vector;
		isc_interprete(errmsg, &pvector);
		lua_pushstring(L, errmsg);

		return 2;
	}

	/* what type of SQL statment is it? */
	stmt_type = get_statment_type(cur);
	if(stmt_type < 0) {
		lua_pushnil(L);
		pvector = conn->env->status_vector;
		isc_interprete(errmsg, &pvector);
		lua_pushstring(L, errmsg);

		return 2;
	}

	/* an unsupported SQL statment (something like COMMIT) */
	if(stmt_type > 5) {
		lua_pushnil(L);
		lua_pushstring(L, "Unsupported SQL statment");

		return 2;
	}

	/* resize the result set if needed */
	if (cur->out_sqlda->sqld > cur->out_sqlda->sqln)
	{
		n = cur->out_sqlda->sqld;
		free(cur->out_sqlda);
		cur->out_sqlda = (XSQLDA *)malloc(XSQLDA_LENGTH(n));
		cur->out_sqlda->sqln = n;
		cur->out_sqlda->version = SQLDA_VERSION1;
		isc_dsql_describe(conn->env->status_vector, &cur->stmt, 1, cur->out_sqlda);
		if (conn->env->status_vector[0] == 1 && conn->env->status_vector[1])
		{
			lua_pushnil(L);
			pvector = conn->env->status_vector;
			isc_interprete(errmsg, &pvector);
			lua_pushstring(L, errmsg);

			return 2;
		}
	}

	/* prep the result set ready to handle the data */
	for (i=0, var = cur->out_sqlda->sqlvar; i < cur->out_sqlda->sqld; i++, var++) {
		dtype = (var->sqltype & ~1); /* drop flag bit for now */
		switch(dtype) {
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

	/* run the query */
	isc_dsql_execute(conn->env->status_vector, &conn->transaction, &cur->stmt, 1, NULL);
	if (conn->env->status_vector[0] == 1 && conn->env->status_vector[1])
	{
		lua_pushnil(L);
		pvector = conn->env->status_vector;
		isc_interprete(errmsg, &pvector);
		lua_pushstring(L, errmsg);

		return 2;
	}

	/* if autocommit is set and it's a non SELECT query, commit change */
	if(conn->autocommit != 0 && stmt_type > 1) {
		isc_commit_retaining(conn->env->status_vector, &conn->transaction);
		if (conn->env->status_vector[0] == 1 && conn->env->status_vector[1])
		{
			pvector = conn->env->status_vector;
			isc_interprete(errmsg, &pvector);
			lua_pushnil(L);
			lua_pushstring(L, errmsg);

			return 2;
		}
	}

	/* what do we return? a cursor or a count */
	if(cur->out_sqlda->sqld > 0) { /* a cursor */
		cur_data* user_cur;
		/* open the cursor ready for fetch cycles */
		isc_dsql_set_cursor_name(cur->env->status_vector, &cur->stmt, "dyn_cursor", (unsigned short)NULL);
		if (cur->env->status_vector[0] == 1 && cur->env->status_vector[1] > 0)
		{
			lua_pushnil(L);
			pvector = cur->env->status_vector;
			isc_interprete(errmsg, &pvector);
			lua_pushstring(L, errmsg);

			return 2;
		}

		/* copy the cursor into a new lua userdata object */
		user_cur = (cur_data*)lua_newuserdata(L, sizeof(cur_data));
		luasql_setmeta (L, LUASQL_CURSOR_FIREBIRD);

		memcpy((void*)user_cur, (void*)cur, sizeof(cur_data));
		free(cur);	/* finnished with this copy */

		/* add cursor to the lock count */
		++conn->lock;
	} else { /* a count */
		if( (count = count_rows_affected(cur)) < 0 ) {
			lua_pushnil(L);
			pvector = cur->env->status_vector;
			isc_interprete(errmsg, &pvector);
			lua_pushstring(L, errmsg);

			return 2;
		}
		lua_pushnumber(L, count);

		/* totaly finnished with the cursor */
		free(cur->out_sqlda);
		free(cur);
	}

	return 1;
}

/*
** Commits the current transaction
*/
static int conn_commit(lua_State *L) {
	char errmsg[512];
	long *pvector;
	conn_data *conn = (conn_data *)luaL_checkudata(L,1,LUASQL_CONNECTION_FIREBIRD);

	/* closed? */
	if(conn->closed != 0) {
		lua_pushnil(L);
		lua_pushstring(L, "connection is closed");
		return 2;
	}

	isc_commit_retaining(conn->env->status_vector, &conn->transaction);
	if (conn->env->status_vector[0] == 1 && conn->env->status_vector[1])
	{
		pvector = conn->env->status_vector;
		isc_interprete(errmsg, &pvector);
		lua_pushnil(L);
		lua_pushstring(L, errmsg);

		return 2;
	}

	lua_pushnumber(L, 1);
	return 1;
}

/*
** Rolls back the current transaction
** Lua Returns:
**   1 if rollback is sucsessful
**   nil and error message otherwise.
*/
static int conn_rollback(lua_State *L) {
	char errmsg[512];
	long *pvector;
	conn_data *conn = (conn_data *)luaL_checkudata(L,1,LUASQL_CONNECTION_FIREBIRD);

	/* closed? */
	if(conn->closed != 0) {
		lua_pushnil(L);
		lua_pushstring(L, "connection is closed");
		return 2;
	}

	isc_rollback_retaining(conn->env->status_vector, &conn->transaction);
	if (conn->env->status_vector[0] == 1 && conn->env->status_vector[1])
	{
		pvector = conn->env->status_vector;
		isc_interprete(errmsg, &pvector);
		lua_pushnil(L);
		lua_pushstring(L, errmsg);

		return 2;
	}

	lua_pushnumber(L, 1);
	return 1;
}

/*
** Sets the autocommit state of the connection
** Lua Returns:
**   autocommit state (0:off, 1:on)
**   nil and error message on error.
*/
static int conn_setautocommit(lua_State *L) {
	conn_data *conn = (conn_data *)luaL_checkudata(L,1,LUASQL_CONNECTION_FIREBIRD);

	/* closed? */
	if(conn->closed != 0) {
		lua_pushnil(L);
		lua_pushstring(L, "connection is closed");
		return 2;
	}

	if(lua_toboolean(L, 2))
		conn->autocommit = 1;
	else
		conn->autocommit = 0;

	lua_pushboolean(L, conn->autocommit);
	return 1;
}

/*
** Closes a connection.
** Lua Returns:
**   1 if close was sucsessful, 0 if already closed
**   nil and error message otherwise.
*/
static int conn_close (lua_State *L) {
	char errmsg[512];
	long *pvector;
	conn_data *conn = (conn_data *)luaL_checkudata(L,1,LUASQL_CONNECTION_FIREBIRD);

	/* already closed */
	if(conn->closed != 0) {
		lua_pushnumber(L, 0);
		return 1;
	}

	/* are all related cursors closed? */
	if(conn->lock > 0) {
		lua_pushnil(L);
		lua_pushstring(L, "There are still open cursors");
		return 2;
	}

	if(conn->autocommit != 0)
		isc_commit_transaction(conn->env->status_vector, &conn->transaction);
	else
		isc_rollback_transaction(conn->env->status_vector, &conn->transaction);
	if (conn->env->status_vector[0] == 1 && conn->env->status_vector[1])
	{
		pvector = conn->env->status_vector;
		isc_interprete(errmsg, &pvector);
		lua_pushnil(L);
		lua_pushstring(L, errmsg);

		return 2;
	}

	isc_detach_database(conn->env->status_vector, &conn->db);
	if (conn->env->status_vector[0] == 1 && conn->env->status_vector[1])
	{
		pvector = conn->env->status_vector;
		isc_interprete(errmsg, &pvector);
		lua_pushnil(L);
		lua_pushstring(L, errmsg);

		return 2;
	}

	free((void*)conn->dpb_buffer);

	conn->closed = 1;
	--conn->env->lock;

	lua_pushnumber(L, 1);
	return 1;
}

/*
** Returns a row of data from the query
** Lua Returns:
**   list of results or table of results depending on call
**   nil and error message otherwise.
*/
static int cur_fetch (lua_State *L) {
	char errmsg[512];
	long *pvector;
	ISC_STATUS fetch_stat;
	struct tm timevar;
	char timestr[256];
	int i, varcharlen;
	int fetch_mode = 0;
	isc_blob_handle blob_handle = NULL;
	ISC_QUAD blob_id;
	unsigned short actual_seg_len;
	ISC_STATUS blob_stat;
	char *buffer;
	luaL_Buffer b;
	cur_data *cur = (cur_data *)luaL_checkudata(L,1,LUASQL_CURSOR_FIREBIRD);

	/* closed? */
	if(cur->closed != 0) {
		lua_pushnil(L);
		lua_pushstring(L, "cursor is closed");
		return 2;
	}

	/* is a table provided? */
	if( lua_gettop(L) > 1 ) {
		fetch_mode = 1;

		if(!lua_istable (L, 2)) {
			lua_pushnil(L);
			lua_pushstring(L, "Need table in paramter 1");
			return 2;
		}
	}

	/* do we have a fetch mode? */
	if( (lua_gettop(L) > 2) ) {
		if((strcmp(lua_tostring(L, 3), "a") == 0) ) 
			fetch_mode = 2;
		lua_settop(L, 2);
	}

	if ((fetch_stat = isc_dsql_fetch(cur->env->status_vector, &cur->stmt, 1, cur->out_sqlda)) == 0) {
		/* loop through the columns */
		for (i = 0; i < cur->out_sqlda->sqld; i++) {
			/* push the field index (1-base) onto the stack*/
			if( fetch_mode == 1 ) {
				lua_pushnumber(L, i+1);
			}

			if( fetch_mode == 2) {
				lua_pushlstring(L, cur->out_sqlda->sqlvar[i].aliasname, cur->out_sqlda->sqlvar[i].aliasname_length);
			}

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
					lua_pushnumber(L, *(short*)(cur->out_sqlda->sqlvar[i].sqldata));
					break;
				case SQL_LONG:
					lua_pushnumber(L, *(long*)(cur->out_sqlda->sqlvar[i].sqldata));
					break;
				case SQL_INT64:
					lua_pushnumber(L, (lua_Number)*(ISC_INT64*)(cur->out_sqlda->sqlvar[i].sqldata));
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
					strftime(timestr, 255, "%x", &timevar);
					lua_pushstring(L, timestr);
					break;
				case SQL_TIMESTAMP:
					isc_decode_timestamp((ISC_TIMESTAMP*)(cur->out_sqlda->sqlvar[i].sqldata), &timevar);
					strftime(timestr, 255, "%x %X", &timevar);
					lua_pushstring(L, timestr);
					break;
				case SQL_BLOB:
					/* get the BLOB ID and open it */
					memcpy(&blob_id, cur->out_sqlda->sqlvar[i].sqldata, sizeof(ISC_QUAD));
					isc_open_blob2(	cur->env->status_vector,
									&cur->conn->db, &cur->conn->transaction,
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
					blob_handle = NULL;

					luaL_pushresult(&b);
					break;
				default:
					lua_pushstring(L, "<unsupported data type>");
					break;
				}
			}

			/* fill the table */
			if( fetch_mode > 0 ) {
				lua_settable(L, 2);
			}
		}

		/* just returning a table */
		if( fetch_mode > 0 )
			return 1;

		/* returning a list of values */
		return cur->out_sqlda->sqld;
	}

	/* isc_dsql_fetch returns 100 if no more rows remain to be retrieved
	   so this can be ignored */
	if (fetch_stat != 100L) {
		lua_pushnil(L);
		pvector = cur->env->status_vector;
		isc_interprete(errmsg, &pvector);
		lua_pushstring(L, errmsg);

		return 2;
	}

	return 0;
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
	cur_data *cur = (cur_data *)luaL_checkudata(L,1,LUASQL_CURSOR_FIREBIRD);

	/* closed? */
	if(cur->closed != 0) {
		lua_pushnil(L);
		lua_pushstring(L, "cursor is closed");
		return 2;
	}

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
	int i;
	XSQLVAR *var;
	cur_data *cur = (cur_data *)luaL_checkudata(L,1,LUASQL_CURSOR_FIREBIRD);

	/* closed? */
	if(cur->closed != 0) {
		lua_pushnil(L);
		lua_pushstring(L, "cursor is closed");
		return 2;
	}

	lua_newtable(L);

	for (i=1, var = cur->out_sqlda->sqlvar; i <= cur->out_sqlda->sqld; i++, var++) {
		lua_pushnumber(L, i);
		switch(var->sqltype) {
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
** Closes a cursor object
** Lua Returns:
**   1 if close was sucsessful, 0 if already closed
**   nil and error message otherwise.
*/
static int cur_close (lua_State *L) {
	char errmsg[512];
	int i;
	XSQLVAR *var;
	cur_data *cur = (cur_data *)luaL_checkudata(L,1,LUASQL_CURSOR_FIREBIRD);

	if(cur->closed == 0 ) {
		isc_dsql_free_statement(cur->env->status_vector, &cur->stmt, DSQL_drop);
		if (cur->env->status_vector[0] == 1 && cur->env->status_vector[1] > 0)
		{
			isc_interprete(errmsg, (ISC_STATUS**)&cur->env->status_vector);
			lua_pushnil(L);
			lua_pushstring(L, errmsg);

			return 2;
		}

		/* free the field memory blocks */
		for (i=0, var = cur->out_sqlda->sqlvar; i < cur->out_sqlda->sqld; i++, var++) {
			free(var->sqldata);
		}

		/* free the statment handler */
		free(cur->out_sqlda);

		cur->closed = 1;

		/* remove cursor from lock count */
		--cur->conn->lock;

		/* return sucsess */
		lua_pushnumber(L, 1);
		return 1;
	}

	lua_pushnumber(L, 0);
	return 1;
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
** Creates and returns a connection object
** Lua Input: source, user, pass
**   source: data source
**   user, pass: data source authentication information
** Lua Returns:
**   connection object if successfull
**   nil and error message otherwise.
*/
static int env_connect (lua_State *L) {
	char *dpb;
	char errmsg[512];
	long *pvector;
	int i;
	static char isc_tpb[] = {	isc_tpb_version3,
								isc_tpb_write		};
	conn_data* conn;

	env_data *env = (env_data *) getenvironment (L);
	const char *sourcename = luaL_checkstring (L, 2);
	const char *username = luaL_checkstring (L, 3);
	const char *password = luaL_checkstring (L, 4);

	/* check for an open enviroment */
	if(env->closed != 0) {
		lua_pushnil(L);
		lua_pushstring(L, "Enviroment is closed");
	}

	conn = (conn_data*)lua_newuserdata(L, sizeof(conn_data));
	luasql_setmeta (L, LUASQL_CONNECTION_FIREBIRD);

	conn->closed = 0;
	conn->env = env;
	conn->db = 0L;
    conn->dpb_buffer = (char*)malloc(sizeof(char) * 256);
	conn->transaction = 0L;
	conn->lock = 0;
	conn->autocommit = 0;

	/* Construct a database parameter buffer. */
	dpb = conn->dpb_buffer;
	*dpb++ = isc_dpb_version1;
	*dpb++ = isc_dpb_num_buffers;
	*dpb++ = 1;
	*dpb++ = 90;

	/* add the user name and password */
	*dpb++ = isc_dpb_user_name;
    *dpb++ = (char)strlen(username);
	for(i=0; i<(int)strlen(username); i++)
		*dpb++ = username[i];
	*dpb++ = isc_dpb_password;
    *dpb++ = (char)strlen(password);
	for(i=0; i<(int)strlen(password); i++)
		*dpb++ = password[i];

	/* the length of the dpb */
	conn->dpb_length = (short)(dpb - conn->dpb_buffer);

	/* do the job */
	isc_attach_database(env->status_vector, (short)strlen(sourcename), (char*)sourcename, &conn->db,
						conn->dpb_length,	conn->dpb_buffer);

	/* an error? */
	if (env->status_vector[0] == 1 && env->status_vector[1]) {
		lua_pushnil(L);
		pvector = env->status_vector;
		isc_interprete(errmsg, &pvector);
		lua_pushstring(L, errmsg);

		return 2;
	}

	/* open up the transaction handle */
	isc_start_transaction(	env->status_vector, &conn->transaction, 1,
							&conn->db, (unsigned short)sizeof(isc_tpb),
							isc_tpb );

	/* return NULL on error */
	if (env->status_vector[0] == 1 && env->status_vector[1]) {
		lua_pushnil(L);
		pvector = env->status_vector;
		isc_interprete(errmsg, &pvector);
		lua_pushstring(L, errmsg);

		return 2;
	}

	/* add the connection to the lock */
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
	env_data *env = (env_data *)luaL_checkudata(L, 1, LUASQL_ENVIRONMENT_FIREBIRD);

	/* already closed? */
	if(env->closed == 1) {
		lua_pushnumber(L, 0);
		return 1;
	}

	/* check the lock */
	if(env->lock > 0) {
		lua_pushnil(L);
		lua_pushstring(L, "There are still open connections");
		return 2;
	}

	/* mark as closed */
	env->closed = 1;

	lua_pushnumber(L, 1);
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
		{"fetch", cur_fetch},
		{"getcoltypes", cur_coltypes},
		{"getcolnames", cur_colnames},
		{NULL, NULL},
	};
	luasql_createmeta (L, LUASQL_ENVIRONMENT_FIREBIRD, environment_methods);
	luasql_createmeta (L, LUASQL_CONNECTION_FIREBIRD, connection_methods);
	luasql_createmeta (L, LUASQL_CURSOR_FIREBIRD, cursor_methods);
}

/*
** Creates the metatables for the objects and registers the
** driver open method.
*/
LUASQL_API int luaopen_luasql_firebird (lua_State *L) {
	struct luaL_reg driver[] = {
		{"firebird", create_environment},
		{NULL, NULL},
	};
	create_metatables (L);
	luaL_openlib (L, LUASQL_TABLENAME, driver, 0);
	luasql_set_info (L);
	return 1;
} 
