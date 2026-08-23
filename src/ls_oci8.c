/*
** LuaSQL, Oracle driver
** Authors: Tomas Guisasola, Leonardo Godinho
** See Copyright Notice in license.html
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "oci.h"
#include "oratypes.h"
#include "ociapr.h"
#include "ocidem.h"

#include "lua.h"
#include "lauxlib.h"

#include "luasql.h"

#define LUASQL_ENVIRONMENT_OCI8 "Oracle environment"
#define LUASQL_CONNECTION_OCI8 "Oracle connection"
#define LUASQL_CURSOR_OCI8 "Oracle cursor"
#define LUASQL_STATEMENT_OCI8 "Oracle statement"

typedef struct {
	short         closed;
	int           conn_counter;
	OCIEnv       *envhp;
	OCIError     *errhp; /* !!! */
} env_data;


typedef struct {
	short         closed;
	short         loggedon;
	short         auto_commit;        /* 0 for manual commit */
	int           cur_counter;        /* number of open cursors */
	int           stmt_counter;       /* number of open statements */
	int           env;                /* reference to environment */
	OCISvcCtx    *svchp;              /* service handle */
	OCIError     *errhp;              /* !!! */
} conn_data;

typedef struct {
	short       closed;
    OCIStmt    *stmthp;      /* statement handle, OCIStmtPrepare2 */
    OCIError   *errhp;      
    int         conn;        /* reference to connection in registry */
    ub2         type;        /* OCI_STMT_SELECT or DML */
    int         cursor_open; /* bool: 1 = cursor active, 0 = none */
} stmt_data;

typedef union {
	lua_Integer     i;
	char   *s;
	double  d;
#ifdef SQLT_DAT
	OCIDate date;
#endif
#ifdef SQLT_TIMESTAMP
	OCIDateTime *datetime;
#endif
} column_value;


typedef struct {
	ub2           type;    /* database type */
	text         *name;    /* column name */
	ub4           namelen; /* column name length */
	ub2           max;     /* maximum size */
	sb2           null;    /* is null? */
	OCIDefine    *define;  /* define handle */
	column_value  val;
} column_data;


typedef struct {
	short         closed;
	int           conn;               /* reference to connection */
	int           numcols;            /* number of columns */
	int           colnames, coltypes; /* reference to column info tables */
	int           curr_tuple;         /* next tuple to be read */
	char         *text;               /* text of SQL statement */
	OCIStmt      *stmthp;             /* statement handle */
	OCIError     *errhp;              /* !!! */
	column_data  *cols;               /* array of columns */
	int           stmt_ref;           /* registry ref to parent stmt_data when stmthp
	                                     is borrowed (stmt:execute path), else LUA_NOREF */
} cur_data;


int checkerr (lua_State *L, sword status, OCIError *errhp);
#define ASSERT(L,exp,err) {sword s = exp; if (s) return checkerr (L, s, err);}


/*
** Check for valid environment.
*/
static env_data *getenvironment (lua_State *L) {
	env_data *env = (env_data *)luaL_checkudata (L, 1, LUASQL_ENVIRONMENT_OCI8);
	luaL_argcheck (L, env != NULL, 1, LUASQL_PREFIX"environment expected");
	luaL_argcheck (L, !env->closed, 1, LUASQL_PREFIX"environment is closed");
	return env;
}


/*
** Check for valid connection.
*/
static conn_data *getconnection (lua_State *L) {
	conn_data *conn = (conn_data *)luaL_checkudata (L, 1, LUASQL_CONNECTION_OCI8);
	luaL_argcheck (L, conn != NULL, 1, LUASQL_PREFIX"connection expected");
	luaL_argcheck (L, !conn->closed, 1, LUASQL_PREFIX"connection is closed");
	return conn;
}


/*
** Check for valid cursor.
*/
static cur_data *getcursor (lua_State *L) {
	cur_data *cur = (cur_data *)luaL_checkudata (L, 1, LUASQL_CURSOR_OCI8);
	luaL_argcheck (L, cur != NULL, 1, LUASQL_PREFIX"cursor expected");
	luaL_argcheck (L, !cur->closed, 1, LUASQL_PREFIX"cursor is closed");
	return cur;
}

/*
** Check for valid statement.
*/
static stmt_data *getstatement (lua_State *L) {
	stmt_data *stmt = (stmt_data *)luaL_checkudata (L, 1, LUASQL_STATEMENT_OCI8);
	luaL_argcheck (L, stmt != NULL, 1, LUASQL_PREFIX"statement expected");
	luaL_argcheck (L, !stmt->closed, 1, LUASQL_PREFIX"statement is closed");
	return stmt;
}


/*
** Retrieve the environment from a connection's registry reference.
** The connection stores a Lua registry ref to its parent environment;
** this helper dereferences it and returns the env_data pointer.
*/
static env_data *getenvfromconn (lua_State *L, conn_data *conn) {
	env_data *env;
	lua_rawgeti (L, LUA_REGISTRYINDEX, conn->env);
	if (!lua_isuserdata (L, -1))
		luaL_error (L, LUASQL_PREFIX"invalid environment in connection!");
	env = (env_data *)lua_touserdata (L, -1);
	lua_pop (L, 1);
	return env;
}


/*
** Push nil plus an error message.
*/
int checkerr (lua_State *L, sword status, OCIError *errhp) {
	lua_pushnil (L);
	switch (status) {
		case OCI_SUCCESS:
			lua_pushnil (L); /* !!!!!!!!!!!!! */
			break;
		case OCI_SUCCESS_WITH_INFO:
			lua_pushstring (L, LUASQL_PREFIX"Success with info!");
			break;
		case OCI_NEED_DATA:
			lua_pushstring (L, LUASQL_PREFIX"OCI_NEED_DATA");
			break;
		case OCI_NO_DATA:
			lua_pushstring (L, LUASQL_PREFIX"OCI_NODATA");
			break;
		case OCI_ERROR: {
			text errbuf[512];
			sb4 errcode = 0;
			OCIErrorGet (errhp, (ub4) 1, (text *) NULL, &errcode,
				errbuf, (ub4) sizeof (errbuf), OCI_HTYPE_ERROR);
			lua_pushstring (L, LUASQL_PREFIX);
			lua_pushstring (L, errbuf);
			lua_concat (L, 2);
			break;
		}
		case OCI_INVALID_HANDLE:
			lua_pushstring (L, LUASQL_PREFIX"OCI_INVALID_HANDLE");
			break;
		case OCI_STILL_EXECUTING:
			lua_pushstring (L, LUASQL_PREFIX"OCI_STILL_EXECUTE");
			break;
		case OCI_CONTINUE:
			lua_pushstring (L, LUASQL_PREFIX"OCI_CONTINUE");
			break;
	}
	return 2;
}


/*
** Copy the column name to the column structure and convert it to lower case.
*/
static void copy_column_name (column_data *col, text *name) {
	unsigned int i;
	col->name = (text *)malloc (col->namelen);
	memcpy (col->name, name, col->namelen);
	for (i = 0; i < col->namelen; i++)
		col->name[i] = tolower (col->name[i]);
}


/*
** Alloc buffers for column values.
*/
static int alloc_column_buffer (lua_State *L, cur_data *cur, int i) {
	/* column index ranges from 1 to numcols */
	/* C array index ranges from 0 to numcols-1 */
	column_data *col = &(cur->cols[i-1]);
	OCIParam *param;
	text *name;

	ASSERT (L, OCIParamGet (cur->stmthp, OCI_HTYPE_STMT, cur->errhp,
		(dvoid **)&param, i), cur->errhp);
	ASSERT (L, OCIAttrGet (param, OCI_DTYPE_PARAM,
		(dvoid *)&(name), (ub4 *)&(col->namelen),
		OCI_ATTR_NAME, cur->errhp), cur->errhp);
	copy_column_name (col, name);
	ASSERT (L, OCIAttrGet (param, OCI_DTYPE_PARAM,
		(dvoid *)&(col->type), (ub4 *)0, OCI_ATTR_DATA_TYPE,
		cur->errhp), cur->errhp);

	switch (col->type) {
		case SQLT_VCS:
		case SQLT_AFC:
		case SQLT_AVC:
			ASSERT (L, OCIAttrGet (param, OCI_DTYPE_PARAM,
				(dvoid *)&(col->max), 0, OCI_ATTR_DATA_SIZE,
				cur->errhp), cur->errhp);
			col->val.s = calloc (col->max + 1, sizeof(col->val.s));
			ASSERT (L, OCIDefineByPos (cur->stmthp, &(col->define),
				cur->errhp, (ub4)i, col->val.s, col->max+1,
				SQLT_STR /*col->type*/, (dvoid *)&(col->null), (ub2 *)0,
				(ub2 *)0, (ub4) OCI_DEFAULT), cur->errhp);
			break;
		case SQLT_CHR:
		case SQLT_STR:
			ASSERT (L, OCIAttrGet (param, OCI_DTYPE_PARAM,
				(dvoid *)&(col->max), 0, OCI_ATTR_DATA_SIZE,
				cur->errhp), cur->errhp);
				col->val.s = calloc (col->max * 2 + 1, sizeof(col->val.s));
			ASSERT (L, OCIDefineByPos (cur->stmthp, &(col->define),
				cur->errhp, (ub4)i, col->val.s, col->max * 2 + 1,
				SQLT_STR /*col->type*/, (dvoid *)&(col->null), (ub2 *)0,
				(ub2 *)0, (ub4) OCI_DEFAULT), cur->errhp);
			break;
#ifdef SQLT_DAT
		case SQLT_DAT:
			ASSERT (L, OCIDefineByPos (cur->stmthp, &(col->define),
				cur->errhp, (ub4)i, &(col->val.date), sizeof(col->val.date),
				SQLT_ODT /*col->type*/, (dvoid *)&(col->null), (ub2 *)0,
				(ub2 *)0, (ub4) OCI_DEFAULT), cur->errhp);
			break;
#endif
#ifdef SQLT_TIMESTAMP
		case SQLT_TIMESTAMP: {
			env_data *env;
			conn_data *conn;
			lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
			conn = (conn_data *)lua_touserdata (L, -1);
			lua_pop (L, 1);
			env = getenvfromconn (L, conn);
			ASSERT (L, OCIDescriptorAlloc (env->envhp,(dvoid*)&(col->val.datetime),
				OCI_DTYPE_TIMESTAMP, 0, (void **)0), cur->errhp);
			ASSERT (L, OCIDefineByPos (cur->stmthp, &(col->define), cur->errhp,
				(ub4)i, &(col->val.datetime), sizeof(col->val.datetime),
				SQLT_TIMESTAMP, (dvoid *)&(col->null), (ub2 *)0, (ub2 *)0, (ub4)
				OCI_DEFAULT), cur->errhp);
			break;
		}
#endif
#ifdef SQLT_TIMESTAMP_TZ
		case SQLT_TIMESTAMP_TZ: {
			env_data *env;
			conn_data *conn;
			lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
			conn = (conn_data *)lua_touserdata (L, -1);
			lua_pop (L, 1);
			env = getenvfromconn (L, conn);
			ASSERT (L, OCIDescriptorAlloc (env->envhp,(dvoid*)&(col->val.datetime),
				OCI_DTYPE_TIMESTAMP_TZ, 0, (void **)0), cur->errhp);
			ASSERT (L, OCIDefineByPos (cur->stmthp, &(col->define), cur->errhp,
				(ub4)i, &(col->val.datetime), sizeof(col->val.datetime),
				SQLT_TIMESTAMP_TZ, (dvoid *)&(col->null), (ub2 *)0, (ub2 *)0, (ub4)
				OCI_DEFAULT), cur->errhp);
			break;
		}
#endif
#ifdef SQLT_TIMESTAMP_LTZ
		case SQLT_TIMESTAMP_LTZ: {
			env_data *env;
			conn_data *conn;
			lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
			conn = (conn_data *)lua_touserdata (L, -1);
			lua_pop (L, 1);
			env = getenvfromconn (L, conn);
			ASSERT (L, OCIDescriptorAlloc (env->envhp,(dvoid*)&(col->val.datetime),
				OCI_DTYPE_TIMESTAMP_LTZ, 0, (void **)0), cur->errhp);
			ASSERT (L, OCIDefineByPos (cur->stmthp, &(col->define), cur->errhp,
				(ub4)i, &(col->val.datetime), sizeof(col->val.datetime),
				SQLT_TIMESTAMP_LTZ, (dvoid *)&(col->null), (ub2 *)0, (ub2 *)0,
				(ub4) OCI_DEFAULT), cur->errhp);
			break;
		}
#endif
		case SQLT_NUM:
		case SQLT_FLT:
		case SQLT_INT:
		/* case SQLT_UIN: */
			ASSERT (L, OCIDefineByPos (cur->stmthp, &(col->define),
				cur->errhp, (ub4)i, &(col->val.d), sizeof(col->val.d),
				SQLT_FLT, (dvoid *)&(col->null), (ub2 *)0,
				(ub2 *)0, (ub4) OCI_DEFAULT), cur->errhp);
			break;
		case SQLT_CLOB: {
			env_data *env;
			conn_data *conn;
			lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
			conn = (conn_data *)lua_touserdata (L, -1);
			lua_pop (L, 1);
			env = getenvfromconn (L, conn);
			ASSERT (L, OCIDescriptorAlloc (env->envhp, (dvoid *)&(col->val.s),
				OCI_DTYPE_LOB, (size_t)0, (dvoid **)0), cur->errhp);
			ASSERT (L, OCIDefineByPos (cur->stmthp, &(col->define),
				cur->errhp, (ub4)i, &(col->val.s), (sb4)sizeof(dvoid *),
				SQLT_CLOB, (dvoid *)&(col->null), (ub2 *)0, (ub2 *)0,
				OCI_DEFAULT), cur->errhp);
			break;
		}
		default:
			luaL_error (L, LUASQL_PREFIX"invalid type %d #%d", col->type, i);
			break;
	}
	return 0;
}


/*
** Deallocate column buffers.
*/
static void free_column_buffers (lua_State *L, cur_data *cur, int i) {
	/* column index ranges from 1 to numcols */
	/* C array index ranges from 0 to numcols-1 */
	column_data *col = &(cur->cols[i-1]);
	free (col->name);
	switch (col->type) {
		case SQLT_INT:
		case SQLT_FLT:
		case SQLT_NUM:
			break;
		case SQLT_CHR:
		case SQLT_STR:
		case SQLT_VCS:
		case SQLT_AFC:
		case SQLT_AVC:
			free(col->val.s);
			break;
#ifdef SQLT_DAT
		case SQLT_DAT:
			break;
#endif
#ifdef SQLT_TIMESTAMP
		case SQLT_TIMESTAMP:
			OCIDescriptorFree (col->val.datetime, OCI_DTYPE_TIMESTAMP);
			break;
#endif
#ifdef SQLT_TIMESTAMP_TZ
		case SQLT_TIMESTAMP_TZ:
			OCIDescriptorFree (col->val.datetime, OCI_DTYPE_TIMESTAMP_TZ);
			break;
#endif
#ifdef SQLT_TIMESTAMP_LTZ
		case SQLT_TIMESTAMP_LTZ:
			OCIDescriptorFree (col->val.datetime, OCI_DTYPE_TIMESTAMP_LTZ);
			break;
#endif
		case SQLT_CLOB:
			OCIDescriptorFree (col->val.s, OCI_DTYPE_LOB);
			break;
		default:
			/* Ignore unknown types during cleanup */
			break;
	}
}


/*
** Push a value on top of the stack.
*/
static int pushvalue (lua_State *L, cur_data *cur, int i) {
	/* column index ranges from 1 to numcols */
	/* C array index ranges from 0 to numcols-1 */
	column_data *col = &(cur->cols[i-1]);
	if (col->null) {
		/* Oracle NULL => Lua nil */
		lua_pushnil (L);
		return 1;
	}
	switch (col->type) {
		case SQLT_NUM:
		case SQLT_INT:
		case SQLT_FLT:
			lua_pushnumber (L, col->val.d);
			break;
		case SQLT_CHR:
		case SQLT_STR:
		case SQLT_VCS:
		case SQLT_AFC:
		case SQLT_AVC:
			lua_pushstring (L, (char *)(col->val.s));
			break;
#ifdef SQLT_DAT
		case SQLT_DAT: {
			text buf[65];
			ub4 buflen = sizeof(buf);
			ASSERT (L, OCIDateToText (cur->errhp, &(col->val.date), NULL, 0, NULL, 0,
				&buflen, buf), cur->errhp);
			lua_pushstring (L, (char *)(buf));
			break;
		}
#endif
#ifdef SQLT_TIMESTAMP_LTZ
		case SQLT_TIMESTAMP_LTZ:
#endif
#ifdef SQLT_TIMESTAMP_TZ
		case SQLT_TIMESTAMP_TZ:
#endif
#ifdef SQLT_TIMESTAMP
		case SQLT_TIMESTAMP: {
			conn_data *conn;
			env_data *env;
			lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
			conn = lua_touserdata (L, -1);
			lua_pop (L, 1);
			env = getenvfromconn (L, conn);
			text buf[65];
			ub4 buflen = sizeof(buf);
			ASSERT (L, OCIDateTimeToText (env->envhp, cur->errhp, col->val.datetime,
				NULL, 0, 6, NULL, 0, &buflen, buf), cur->errhp);
			lua_pushstring (L, (char *)(buf));
			break;
		}
#endif
		case SQLT_CLOB: {
			ub4 lob_len;
			conn_data *conn;
			env_data *env;
			lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
			conn = lua_touserdata (L, -1);
			lua_pop (L, 1);
			env = getenvfromconn (L, conn);
			ASSERT (L, OCILobGetLength (conn->svchp, cur->errhp,
				(OCILobLocator *)col->val.s, &lob_len), cur->errhp);
			if (lob_len > 0) {
				char *lob_buffer=malloc(lob_len);
				ub4 amount = lob_len;

				ASSERT(L, OCILobRead(conn->svchp, cur->errhp,
					(OCILobLocator *) col->val.s, &amount, (ub4) 1,
					(dvoid *) lob_buffer, (ub4) lob_len, (dvoid *)0,
					(sb4 (*) (dvoid *, CONST dvoid *, ub4, ub1)) 0,
					(ub2) 0, (ub1) SQLCS_IMPLICIT), cur->errhp);
				lua_pushlstring (L, lob_buffer, amount);
				free(lob_buffer);
			} else {
				lua_pushstring (L, "");
			}
			break;
		}
		default:
			luaL_error (L, LUASQL_PREFIX"unexpected error");
	}
	return 1;
}


/*
** Closes the cursor and nullify all structure fields.
*/
static void cur_nullify (lua_State *L, cur_data *cur) {
	int i;
	conn_data *conn;

	/* Deallocate buffers. */
	for (i = 1; i <= cur->numcols; i++) {
		free_column_buffers (L, cur, i);
	}
	free (cur->cols);
	cur->cols = NULL;
	free (cur->text);
	cur->text = NULL;

	/* Nullify structure fields. */
	cur->closed = 1;
	if (cur->stmthp) {
		if (cur->stmt_ref != LUA_NOREF) {
			/*
			** This cursor was created by stmt:execute(): the stmthp is owned by the
			** parent stmt_data and must be released via OCIStmtRelease (New API)
			** during stmt:close(). Here we will clear the cursor_open flag on
			** the parent so it can be re-executed, then remove our reference.
			*/
			stmt_data *parent_stmt;
			lua_rawgeti(L, LUA_REGISTRYINDEX, cur->stmt_ref);
			parent_stmt = (stmt_data *)lua_touserdata(L, -1);
			lua_pop(L, 1);
			if (parent_stmt) parent_stmt->cursor_open = 0;
			luaL_unref(L, LUA_REGISTRYINDEX, cur->stmt_ref);
			cur->stmt_ref = LUA_NOREF;
		} else {
			/* conn:execute() path: cursor owns the handle, free it. */
			OCIHandleFree((dvoid *)cur->stmthp, OCI_HTYPE_STMT);
		}
		cur->stmthp = NULL;
	}
	if (cur->errhp) {
		OCIHandleFree ((dvoid *)cur->errhp, OCI_HTYPE_ERROR);
		cur->errhp = NULL;
	}

	/* Decrement cursor counter on connection object */
	if (cur->conn != LUA_NOREF) {
		lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
		conn = lua_touserdata (L, -1);
		lua_pop (L, 1); /* Pop the connection object from the stack */
		if (conn) conn->cur_counter--;
		luaL_unref (L, LUA_REGISTRYINDEX, cur->conn);
		cur->conn = LUA_NOREF;
	}

	luaL_unref (L, LUA_REGISTRYINDEX, cur->colnames);
	cur->colnames = LUA_NOREF;
	luaL_unref (L, LUA_REGISTRYINDEX, cur->coltypes);
	cur->coltypes = LUA_NOREF;
}


/*
** Get another row of the given cursor.
*/
static int cur_fetch (lua_State *L) {
	cur_data *cur = getcursor (L);
	sword status = OCIStmtFetch (cur->stmthp, cur->errhp, 1,
		OCI_FETCH_NEXT, OCI_DEFAULT);

	if (status == OCI_NO_DATA) {
		/* No more rows: auto-close the cursor */
		cur_nullify (L, cur);
		lua_pushnil (L);
		return 1;
	} else if (status != OCI_SUCCESS) {
		/* Error */
		return checkerr (L, status, cur->errhp);
	}

	if (lua_istable (L, 2)) {
		int i;
		const char *opts = luaL_optstring (L, 3, "n");
		if (strchr (opts, 'n') != NULL)
			/* Copy values to numerical indices */
			for (i = 1; i <= cur->numcols; i++) {
				int ret = pushvalue (L, cur, i);
				if (ret != 1)
					return ret;
				lua_rawseti (L, 2, i);
			}
		if (strchr (opts, 'a') != NULL)
			/* Copy values to alphanumerical indices */
			for (i = 1; i <= cur->numcols; i++) {
				column_data *col = &(cur->cols[i-1]);
				int ret;
				lua_pushlstring (L, col->name, col->namelen);
				if ((ret = pushvalue (L, cur, i)) != 1)
					return ret;
				lua_rawset (L, 2);
			}
		lua_pushvalue(L, 2);
		return 1; /* return table */
	}
	else {
		int i;
		luaL_checkstack (L, cur->numcols, LUASQL_PREFIX"too many columns");
		for (i = 1; i <= cur->numcols; i++) {
			int ret = pushvalue (L, cur, i);
			if (ret != 1)
				return ret;
		}
		return cur->numcols; /* return #numcols values */
	}
}


/*
** Close the cursor on top of the stack.
** Return 1
*/
static int cur_close (lua_State *L) {
	cur_data *cur = (cur_data *)luaL_checkudata (L, 1, LUASQL_CURSOR_OCI8);
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
** Cursor object collector function
*/
static int cur_gc (lua_State *L) {
	cur_data *cur = (cur_data *)luaL_checkudata (L, 1, LUASQL_CURSOR_OCI8);
	if (cur != NULL && !(cur->closed))
		cur_nullify (L, cur);
	return 0;
}


/*
** Return the list of field names as a table on top of the stack.
*/
static int cur_getcolnames (lua_State *L) {
	cur_data *cur = getcursor (L);
	if (cur->colnames != LUA_NOREF)
		lua_rawgeti (L, LUA_REGISTRYINDEX, cur->colnames);
	else {
		int i;
		lua_newtable (L);
		for (i = 1; i <= cur->numcols; i++) {
			column_data *col = &(cur->cols[i-1]);
			lua_pushlstring (L, col->name, col->namelen);
			lua_rawseti (L, -2, i);
		}
		lua_pushvalue (L, -1);
		cur->colnames = luaL_ref (L, LUA_REGISTRYINDEX);
	}
	return 1;
}


/*
**
*/
static char *getcolumntype (column_data *col) {
	switch (col->type) {
		case SQLT_CHR:
		case SQLT_STR:
		case SQLT_VCS:
		case SQLT_AFC:
		case SQLT_AVC:
			return "string";
#ifdef SQLT_DAT
		case SQLT_DAT:
			return "date";
#endif
#ifdef SQLT_TIMESTAMP_LTZ
		case SQLT_TIMESTAMP_LTZ:
#endif
#ifdef SQLT_TIMESTAMP_TZ
		case SQLT_TIMESTAMP_TZ:
#endif
#ifdef SQLT_TIMESTAMP
		case SQLT_TIMESTAMP:
			return "timestamp";
#endif
		case SQLT_NUM:
		case SQLT_FLT:
		case SQLT_INT:
		/* case SQLT_UIN: */
			return "number";
		case SQLT_CLOB:
			return "string";
		default:
			return "";
	}
}


/*
** Return the list of field types as a table on top of the stack.
*/
static int cur_getcoltypes (lua_State *L) {
	cur_data *cur = getcursor (L);
	if (cur->coltypes != LUA_NOREF)
		lua_rawgeti (L, LUA_REGISTRYINDEX, cur->coltypes);
	else {
		int i;
		lua_newtable (L);
		for (i = 1; i <= cur->numcols; i++) {
			column_data *col = &(cur->cols[i-1]);
			lua_pushnumber (L, i);
			lua_pushstring (L, getcolumntype (col));
			lua_rawset (L, -3);
		}
		lua_pushvalue (L, -1);
		cur->coltypes = luaL_ref (L, LUA_REGISTRYINDEX);
	}
	return 1;
}


/*
** Close a Connection object.
*/
static int conn_close (lua_State *L) {
	env_data *env;
	conn_data *conn = (conn_data *)luaL_checkudata (L, 1, LUASQL_CONNECTION_OCI8);
	luaL_argcheck (L, conn != NULL, 1, LUASQL_PREFIX"connection expected");
	if (conn->closed) {
		lua_pushboolean (L, 0);
		lua_pushstring (L, "Connection is already closed");
		return 2;
	}
	if (conn->cur_counter > 0){
		lua_pushboolean (L, 0);
		lua_pushstring (L, "There are open cursors");
		return 2;
	}
	if (conn->stmt_counter > 0){
		lua_pushboolean (L, 0);
		lua_pushstring (L, "There are open statements");
		return 2;
	}

	/* Nullify structure fields. */
	conn->closed = 1;
	if (conn->svchp) {
		if (conn->loggedon)
			OCILogoff (conn->svchp, conn->errhp);
		else
			OCIHandleFree ((dvoid *)conn->svchp, OCI_HTYPE_SVCCTX);
	}
	if (conn->errhp)
		OCIHandleFree ((dvoid *)conn->errhp, OCI_HTYPE_ERROR);
	/* Decrement connection counter on environment object */
	env = getenvfromconn (L, conn);
	env->conn_counter--;
	luaL_unref (L, LUA_REGISTRYINDEX, conn->env);

	lua_pushboolean (L, 1);
	return 1;
}


/*
** Create a new Cursor object and push it on top of the stack.
**
** stmt_ref: This argument manages the ownership and lifecycle of the underlying 
**           OCIStmt handle (stmthp). It expects one of two values:
**           1) LUA_NOREF: Used when the cursor is created via `conn:execute()`. 
**              In this case, the cursor exclusively owns the statement handle 
**              and is responsible for freeing it (via OCIHandleFree) when closed.
**           2) A valid Lua registry reference (integer): Used when the cursor is 
**              created via `stmt:execute()`. Here, the statement handle is "borrowed" 
**              from a parent `stmt_data` object. The cursor does not free the handle; 
**              instead, when the cursor closes, it uses this reference to find the 
**              parent statement and resets its `cursor_open` flag to 0, allowing the 
**              parent statement to be executed again.
*/
static int create_cursor (lua_State *L, int o, conn_data *conn, OCIStmt *stmt,
                          const char *text, int stmt_ref) {
	int i;
	env_data *env;
	cur_data *cur = (cur_data *)LUASQL_NEWUD(L, sizeof(cur_data));
	luasql_setmeta (L, LUASQL_CURSOR_OCI8);

	conn->cur_counter++;
	/* fill in structure */
	cur->closed = 0;
	cur->numcols = 0;
	cur->colnames = LUA_NOREF;
	cur->coltypes = LUA_NOREF;
	cur->curr_tuple = 0;
	cur->stmthp = stmt;
	cur->errhp = NULL;
	cur->cols = NULL;
	cur->stmt_ref = stmt_ref;  /* LUA_NOREF or ref to parent stmt_data */
	cur->text = strdup (text); /* Stores the statement. For prepared statement, 
	                              let's just pass empty string. */
	lua_pushvalue (L, o);
	cur->conn = luaL_ref (L, LUA_REGISTRYINDEX);

	/* error handler */
	env = getenvfromconn (L, conn);
	ASSERT (L, OCIHandleAlloc((dvoid *) env->envhp,
		(dvoid **) &(cur->errhp), (ub4) OCI_HTYPE_ERROR, (size_t) 0,
		(dvoid **) 0), conn->errhp);
	/* get number of columns */
	ASSERT (L, OCIAttrGet ((dvoid *)stmt, (ub4)OCI_HTYPE_STMT,
		(dvoid *)&cur->numcols, (ub4 *)0, (ub4)OCI_ATTR_PARAM_COUNT,
		cur->errhp), cur->errhp);
	cur->cols = (column_data *)malloc (sizeof(column_data) * cur->numcols);
	/* define output variables */
	/* Oracle and Lua column indices ranges from 1 to numcols */
	/* C array indices ranges from 0 to numcols-1 */
	for (i = 1; i <= cur->numcols; i++) {
		int ret = alloc_column_buffer (L, cur, i);
		if (ret)
			return ret;
	}

	return 1;
}


/*
** Execute an SQL statement.
** Return a Cursor object if the statement is a query, otherwise
** return the number of tuples affected by the statement.
*/
static int conn_execute (lua_State *L) {
	env_data *env;
	conn_data *conn = getconnection (L);
	const char *statement = luaL_checkstring (L, 2);
	sword status;
	ub4 prefetch = 0;
	ub4 iters;
	ub4 mode;
	ub2 type;
	OCIStmt *stmthp;

	/* get environment */
	env = getenvfromconn (L, conn);
	/* statement handle */
	ASSERT (L, OCIHandleAlloc ((dvoid *)env->envhp, (dvoid **)&stmthp,
		OCI_HTYPE_STMT, (size_t)0, (dvoid **)0), conn->errhp);
	ASSERT (L, OCIAttrSet ((dvoid *)stmthp, (ub4)OCI_HTYPE_STMT,
		(dvoid *)&prefetch, (ub4)0, (ub4)OCI_ATTR_PREFETCH_ROWS,
		conn->errhp), conn->errhp);
	ASSERT (L, OCIStmtPrepare (stmthp, conn->errhp, (text *)statement,
		(ub4) strlen(statement), (ub4) OCI_NTV_SYNTAX, (ub4) OCI_DEFAULT),
		conn->errhp);
	/* statement type */
	ASSERT (L, OCIAttrGet ((dvoid *)stmthp, (ub4) OCI_HTYPE_STMT,
		(dvoid *)&type, (ub4 *)0, (ub4)OCI_ATTR_STMT_TYPE, conn->errhp),
		conn->errhp);
	if (type == OCI_STMT_SELECT)
		iters = 0;
	else
		iters = 1;
	if (conn->auto_commit)
		mode = OCI_COMMIT_ON_SUCCESS;
	else
		mode = OCI_DEFAULT;
	/* execute statement */
	status = OCIStmtExecute (conn->svchp, stmthp, conn->errhp, iters,
		(ub4)0, (CONST OCISnapshot *)NULL, (OCISnapshot *)NULL, mode);
	if (status && (status != OCI_NO_DATA)) {
		OCIHandleFree ((dvoid *)stmthp, OCI_HTYPE_STMT);
		return checkerr (L, status, conn->errhp);
	}
	if (type == OCI_STMT_SELECT) {
		/* create cursor — cursor owns the handle (conn:execute path) */
		return create_cursor (L, 1, conn, stmthp, statement, LUA_NOREF);
	} else {
		/* return number of rows */
		int rows_affected;
		ASSERT (L, OCIAttrGet ((dvoid *)stmthp, (ub4)OCI_HTYPE_STMT,
			(dvoid *)&rows_affected, (ub4 *)0,
			(ub4)OCI_ATTR_ROW_COUNT, conn->errhp), conn->errhp);
		OCIHandleFree ((dvoid *)stmthp, OCI_HTYPE_STMT);
		lua_pushnumber (L, rows_affected);
		return 1;
	}
}

/*
** Close a prepared statement.
*/
static int stmt_close (lua_State *L) {
	conn_data *conn;
	stmt_data *stmt = (stmt_data *)luaL_checkudata (L, 1, LUASQL_STATEMENT_OCI8);
	luaL_argcheck (L, stmt != NULL, 1, LUASQL_PREFIX"statement expected");
	if (stmt->closed) {
		lua_pushnil (L);
		lua_pushstring (L, "Statement is already closed");
		return 2;
	}
	if (stmt->cursor_open) {
		lua_pushnil (L);
		lua_pushstring (L, LUASQL_PREFIX"cannot close statement with open cursor");
		return 2;
	}

	if (stmt->stmthp) {
		sword status = OCIStmtRelease (stmt->stmthp, stmt->errhp, NULL, 0, OCI_DEFAULT);
		if (status) {
			return checkerr (L, status, stmt->errhp);
		}
		stmt->stmthp = NULL;
	}
	stmt->closed = 1;

	if (stmt->errhp) {
		OCIHandleFree ((dvoid *)stmt->errhp, OCI_HTYPE_ERROR);
		stmt->errhp = NULL;
	}

	/* Decrement statement counter on connection object */
	lua_rawgeti (L, LUA_REGISTRYINDEX, stmt->conn);
	conn = lua_touserdata (L, -1);
	conn->stmt_counter--;
	luaL_unref (L, LUA_REGISTRYINDEX, stmt->conn);

	lua_pushboolean (L, 1);
	return 1;
}


/*
** Prepares an SQL statement.
** Returns the statement object.
*/
static int conn_prepare (lua_State *L) {
	env_data *env;
	conn_data *conn = getconnection (L);
	const char *statement = luaL_checkstring (L, 2);
	ub2 type;
	OCIStmt *stmthp;
	OCIError *errhp = NULL;

	/* get environment */
	env = getenvfromconn (L, conn);

	/* Allocate userdata before OCI allocations so it doesn't leak on out of memory.
	   Do NOT set the metatable yet to prevent __gc on uninitialized data. */
	stmt_data *stmt = (stmt_data *)LUASQL_NEWUD(L, sizeof(stmt_data));
	stmt->closed = 1;
	stmt->stmthp = NULL;
	stmt->errhp = NULL;
	stmt->type = 0;
	stmt->cursor_open = 0;
	stmt->conn = LUA_NOREF;

	/* prepare statement via OCIStmtPrepare2 (allocates handle internally) */
	ASSERT (L, OCIStmtPrepare2 (conn->svchp, &stmthp, conn->errhp,
		(text *)statement, (ub4) strlen(statement),
		(text *)NULL, (ub4) 0,
		(ub4) OCI_NTV_SYNTAX, (ub4) OCI_DEFAULT),
		conn->errhp);

	/* get statement type. If it fails, release stmthp */
	{
		sword s = OCIAttrGet ((dvoid *)stmthp, (ub4) OCI_HTYPE_STMT,
			(dvoid *)&type, (ub4 *)0, (ub4)OCI_ATTR_STMT_TYPE, conn->errhp);
		if (s) {
			OCIStmtRelease (stmthp, conn->errhp, NULL, 0, OCI_DEFAULT);
			return checkerr (L, s, conn->errhp);
		}
	}

	/* allocate error handle. If it fails, release stmthp */
	{
		sword s = OCIHandleAlloc ((dvoid *)env->envhp,
			(dvoid **)&errhp, (ub4) OCI_HTYPE_ERROR, (size_t) 0,
			(dvoid **) 0);
		if (s) {
			OCIStmtRelease (stmthp, conn->errhp, NULL, 0, OCI_DEFAULT);
			return checkerr (L, s, conn->errhp);
		}
	}

	/* All OCI resources ready. Attach metatable and finalize fields. */
	luasql_setmeta (L, LUASQL_STATEMENT_OCI8);

	stmt->closed = 0;
	stmt->stmthp = stmthp;
	stmt->errhp = errhp;
	stmt->type = type;
	lua_pushvalue (L, 1);
	stmt->conn = luaL_ref (L, LUA_REGISTRYINDEX);

	conn->stmt_counter++;

	return 1;
}


/*
** Binds parameters to a prepared statement.
** Returns 0 on success, or a Lua return value on error.
*/
static int stmt_bind (lua_State *L, stmt_data *stmt, int num_params, int is_named,
                      column_value **p_bind_values, OCIBind ***p_bind_handles, sb2 **p_inds, ub2 **p_bind_types) {
    column_value *bind_values = (column_value *)calloc(num_params, sizeof(column_value));
    OCIBind **bind_handles = (OCIBind **)calloc(num_params, sizeof(OCIBind *));
    sb2 *inds = (sb2 *)calloc(num_params, sizeof(sb2));
    ub2 *bind_types = (ub2 *)calloc(num_params, sizeof(ub2));

    *p_bind_values = bind_values;
    *p_bind_handles = bind_handles;
    *p_inds = inds;
    *p_bind_types = bind_types;

    if (num_params > 0 && (!bind_values || !bind_handles || !inds || !bind_types)) {
        lua_pushnil(L);
        lua_pushstring(L, LUASQL_PREFIX"out of memory allocating bind buffers");
        return 2;
    }

    env_data *env = NULL;
    lua_rawgeti(L, LUA_REGISTRYINDEX, stmt->conn);
    conn_data *conn = (conn_data *)lua_touserdata(L, -1);
    if (conn) env = getenvfromconn(L, conn);
    lua_pop(L, 1);

    int param_idx = 0;
    lua_pushnil(L);
    while (lua_next(L, 2) != 0) {
        /* fetching value and type: key at -2, value at -1 */
        int luasql_type;
        int is_null = 0;

        if (lua_istable(L, -1)) {
            /* {value, luasql.type} — get type from index 2 */
            lua_rawgeti(L, -1, 2);
            luasql_type = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
            if (luasql_type == LUASQL_TYPE_NULL)
                is_null = 1;
        } else {
            /* luasql.type.null */
            luasql_type = LUASQL_TYPE_NULL;
            is_null = 1;
        }

        dvoid *bind_ptr = NULL;
        sb4 bind_size = 0;
        ub2 bind_type = 0;

        if (!is_null) {
            inds[param_idx] = 0; /* not null */
            lua_rawgeti(L, -1, 1);
            switch (luasql_type) {
                case LUASQL_TYPE_INT:
                    bind_values[param_idx].i = (lua_Integer)lua_tointeger(L, -1);
                    bind_ptr = &bind_values[param_idx].i;
                    bind_size = sizeof(lua_Integer);
                    bind_type = SQLT_INT;
                    break;
                case LUASQL_TYPE_NUMBER:
                    bind_values[param_idx].d = (double)lua_tonumber(L, -1);
                    bind_ptr = &bind_values[param_idx].d;
                    bind_size = sizeof(double);
                    bind_type = SQLT_FLT;
                    break;
                case LUASQL_TYPE_BOOLEAN:
                    bind_values[param_idx].s = lua_toboolean(L, -1) ? "1" : "0";
                    bind_ptr = bind_values[param_idx].s;
                    bind_size = 1;
                    bind_type = SQLT_CHR;
                    break;
                case LUASQL_TYPE_STRING:
                case LUASQL_TYPE_TIME:
                    bind_values[param_idx].s = (char *)lua_tostring(L, -1);
                    bind_ptr = bind_values[param_idx].s;
                    bind_size = strlen(bind_values[param_idx].s) + 1;
                    bind_type = SQLT_STR;
                    break;
                case LUASQL_TYPE_TIMESTAMP: {
                    const char *date_str = lua_tostring(L, -1);
#ifdef SQLT_TIMESTAMP
                    bind_type = SQLT_TIMESTAMP;
                    bind_types[param_idx] = bind_type;
                    if (env) {
                        sword rc = OCIDescriptorAlloc(env->envhp, (dvoid **)&bind_values[param_idx].datetime, OCI_DTYPE_TIMESTAMP, 0, (void **)0);
                        if (rc == OCI_SUCCESS) {
                            rc = OCIDateTimeFromText(
                                env->envhp,
                                stmt->errhp,
                                (const oratext *)date_str, (size_t)strlen(date_str),
                                (const oratext *)"YYYY-MM-DD HH24:MI:SS", 21,
                                NULL, 0,
                                bind_values[param_idx].datetime
                            );
                            if (rc != OCI_SUCCESS) {
                                lua_pop(L, 1); /* pop value from lua_rawgeti(L, -1, 1) */
                                return checkerr(L, rc, stmt->errhp);
                            }
                        } else {
                            lua_pop(L, 1);
                            lua_pushnil(L);
                            lua_pushstring(L, LUASQL_PREFIX"failed to allocate timestamp descriptor");
                            return 2;
                        }
                    } else {
                        lua_pop(L, 1);
                        lua_pushnil(L);
                        lua_pushstring(L, LUASQL_PREFIX"missing environment handle for timestamp");
                        return 2;
                    }
                    bind_ptr  = &bind_values[param_idx].datetime;
                    bind_size = sizeof(OCIDateTime *);
#else
                    bind_values[param_idx].s = (char *)date_str;
                    bind_ptr  = bind_values[param_idx].s;
                    bind_size = strlen(bind_values[param_idx].s) + 1;
                    bind_type = SQLT_STR;
#endif
                    break;
                }
                case LUASQL_TYPE_DATE: {
                    const char *date_str = lua_tostring(L, -1);
#ifdef SQLT_ODT
                    {
                        sword rc = OCIDateFromText(
                            stmt->errhp,
                            (const oratext *)date_str, (ub4)strlen(date_str),
                            (const oratext *)"YYYY-MM-DD", 10,
                            NULL, 0,
                            &bind_values[param_idx].date
                        );
                        if (rc != OCI_SUCCESS) {
                            lua_pop(L, 1); /* pop value from lua_rawgeti(L, -1, 1) */
                            lua_pushnil(L);
                            lua_pushstring(L, LUASQL_PREFIX"invalid date format, expected YYYY-MM-DD");
                            return 2;
                        }
                    }
                    bind_ptr  = &bind_values[param_idx].date;
                    bind_size = sizeof(OCIDate);
                    bind_type = SQLT_ODT;
#else
                    bind_values[param_idx].s = (char *)date_str;
                    bind_ptr  = bind_values[param_idx].s;
                    bind_size = strlen(bind_values[param_idx].s) + 1;
                    bind_type = SQLT_STR;
#endif
                    break;
                }
            }
            lua_pop(L, 1);
        } else {
            inds[param_idx] = -1; /* NULL */
            bind_type = SQLT_STR; /* default safe type for null */
            bind_size = 0;
            bind_ptr = NULL;
        }

        /* bind data */
        sword status;
        if (is_named) {
            const char *name = lua_tostring(L, -2);
            status = OCIBindByName(stmt->stmthp, &bind_handles[param_idx], stmt->errhp,
                                   (text *)name, strlen(name),
                                   bind_ptr, bind_size, bind_type,
                                   (dvoid *)&inds[param_idx], (ub2 *)0, (ub2 *)0,
                                   (ub4)0, (ub4 *)0, OCI_DEFAULT);
        } else {
            ub4 pos = (ub4)lua_tointeger(L, -2); /*OCI also follows 1-based indexing*/
            status = OCIBindByPos(stmt->stmthp, &bind_handles[param_idx], stmt->errhp,
                                  pos, bind_ptr, bind_size, bind_type,
                                  (dvoid *)&inds[param_idx], (ub2 *)0, (ub2 *)0,
                                  (ub4)0, (ub4 *)0, OCI_DEFAULT);
        }

        if (status) {
            lua_pop(L, 2); /* pop lua_next key and value before returning */
            return checkerr(L, status, stmt->errhp);
        }

        bind_types[param_idx] = bind_type;
        param_idx++;
        lua_pop(L, 1); 
    }
    return 0;
}

/*
** Free bind resources allocated during stmt_bind.
*/
static void free_bind_buffers (int num_params, column_value *bind_values, OCIBind **bind_handles, sb2 *inds, ub2 *bind_types) {
    if (bind_values && bind_types) {
        for (int i = 0; i < num_params; i++) {
#ifdef SQLT_TIMESTAMP
            if (bind_types[i] == SQLT_TIMESTAMP && bind_values[i].datetime) {
                OCIDescriptorFree(bind_values[i].datetime, OCI_DTYPE_TIMESTAMP);
            }
#endif
        }
    }
    if (bind_values) free(bind_values);
    if (bind_handles) free(bind_handles);
    if (inds) free(inds);
    if (bind_types) free(bind_types);
}

/*
** Validates the Params table (bind_data)  
** bind the data with the Prepared Statement [ Upto this part is done! ]
** execute the Prepared Statement
** return a Cursor object if the statement is a query, otherwise
** return the number of tuples affected by the statement.
*/
static int stmt_execute (lua_State *L) {
    stmt_data *stmt = getstatement (L);

    if (stmt->cursor_open) {
        lua_pushnil(L);
        lua_pushstring(L, LUASQL_PREFIX"cannot execute: cursor still open");
        return 2;
    }

    luaL_checktype(L, 2, LUA_TTABLE);

    /* Validates the Params table (bind_data) */
    int is_named;
    int num_params = 0;
    if (!luasql_validate_params(L, 2, &is_named, &num_params))
        return 2;  /* nil+errmsg already on stack */

    column_value *bind_values = NULL;
    OCIBind **bind_handles = NULL;
    sb2 *inds = NULL;
    ub2 *bind_types = NULL;

    if (num_params > 0) {
        int ret = stmt_bind(L, stmt, num_params, is_named, &bind_values, &bind_handles, &inds, &bind_types);
        if (ret) {
            free_bind_buffers(num_params, bind_values, bind_handles, inds, bind_types);
            return ret;
        }
    }

    /*
    ** Execute the prepared statement.
    ** Retrieve the connection from the Lua registry so we can access
    ** svchp and the auto_commit flag.
    */
    lua_rawgeti(L, LUA_REGISTRYINDEX, stmt->conn);
    conn_data *conn = (conn_data *)lua_touserdata(L, -1);
    int conn_idx = lua_gettop(L);   /* stack: [stmt, params, conn] */

    ub4 iters = (stmt->type == OCI_STMT_SELECT) ? 0 : 1;
    ub4 mode  = conn->auto_commit ? OCI_COMMIT_ON_SUCCESS : OCI_DEFAULT;

    sword status = OCIStmtExecute(conn->svchp, stmt->stmthp, stmt->errhp,
                                  iters, (ub4)0,
                                  (CONST OCISnapshot *)NULL,
                                  (OCISnapshot *)NULL, mode);

    /* Free Bind resources */
    free_bind_buffers(num_params, bind_values, bind_handles, inds, bind_types);

    if (status && status != OCI_NO_DATA) {
        lua_pop(L, 1); /* pop conn */
        return checkerr(L, status, stmt->errhp);
    }

    if (stmt->type == OCI_STMT_SELECT) {
        /*
        ** For a SELECT, create a Cursor object. We pass a fresh Lua registry ref 
        ** to the stmt itself.
        */
        lua_pushvalue(L, 1); /* push stmt userdata */
        int stmt_ref_for_cur = luaL_ref(L, LUA_REGISTRYINDEX);
        stmt->cursor_open = 1;
        int ret = create_cursor(L, conn_idx, conn, stmt->stmthp, "", stmt_ref_for_cur);
        if (ret != 1) {
            cur_data *cur = (cur_data *)luaL_checkudata(L, conn_idx + 1, LUASQL_CURSOR_OCI8);
            if (cur) {
                cur->stmthp = NULL;
                cur->stmt_ref = LUA_NOREF;
            }
            luaL_unref(L, LUA_REGISTRYINDEX, stmt_ref_for_cur);
            stmt->cursor_open = 0;
        }
        lua_remove(L, conn_idx); /* pop conn */
        return ret;
    } else {
        /* DML: return the number of rows affected. */
        lua_pop(L, 1); /* pop conn */
        ub4 rows_affected = 0;
        ASSERT(L, OCIAttrGet((dvoid *)stmt->stmthp, (ub4)OCI_HTYPE_STMT,
                             (dvoid *)&rows_affected, (ub4 *)0,
                             (ub4)OCI_ATTR_ROW_COUNT, stmt->errhp),
               stmt->errhp);
        lua_pushnumber(L, (lua_Number)rows_affected);
        return 1;
    }
}


/*
** Commit the current transaction.
*/
static int conn_commit (lua_State *L) {
	conn_data *conn = getconnection (L);
	ASSERT (L, OCITransCommit (conn->svchp, conn->errhp, OCI_DEFAULT),
		conn->errhp);
/*
	if (conn->auto_commit == 0)
		ASSERT (L, OCITransStart (conn->svchp, conn->errhp...
*/
	lua_pushboolean (L, 1);
	return 1;
}


/*
** Rollback the current transaction.
*/
static int conn_rollback (lua_State *L) {
	conn_data *conn = getconnection (L);
	ASSERT (L, OCITransRollback (conn->svchp, conn->errhp, OCI_DEFAULT),
		conn->errhp);
/*
	if (conn->auto_commit == 0)
		sql_begin(conn);
*/
	lua_pushboolean (L, 1);
	return 1;
}


/*
** Set "auto commit" property of the connection.
** If 'true', then rollback current transaction.
** If 'false', then start a new transaction.
*/
static int conn_setautocommit (lua_State *L) {
	conn_data *conn = getconnection (L);
	if (lua_toboolean (L, 2)) {
		conn->auto_commit = 1;
		/* Undo active transaction. */
		ASSERT (L, OCITransRollback (conn->svchp, conn->errhp,
			OCI_DEFAULT), conn->errhp);
	}
	else {
		conn->auto_commit = 0;
		/* sql_begin(conn);*/
	}
	lua_pushboolean(L, 1);
	return 1;
}


/*
** Connects to a data source.
*/
static int env_connect (lua_State *L) {
	env_data *env = getenvironment (L);
	const char *sourcename = luaL_checkstring(L, 2);
	const char *username = luaL_optstring(L, 3, NULL);
	const char *password = luaL_optstring(L, 4, NULL);
	/* Sizes of strings */
	size_t snlen = strlen(sourcename);
	size_t userlen = (username) ? strlen(username) : 0;
	size_t passlen = (password) ? strlen(password) : 0;
	/* Alloc connection object */
	conn_data *conn = (conn_data *)LUASQL_NEWUD(L, sizeof(conn_data));

	/* fill in structure */
	luasql_setmeta (L, LUASQL_CONNECTION_OCI8);
	conn->env = LUA_NOREF;
	conn->closed = 1;
	conn->auto_commit = 1;
	conn->cur_counter = 0;
	conn->stmt_counter = 0;
	conn->loggedon = 0;
	conn->svchp = NULL;
	conn->errhp = NULL;
	lua_pushvalue (L, 1);
	conn->env = luaL_ref (L, LUA_REGISTRYINDEX);

	/* error handler */
	ASSERT (L, OCIHandleAlloc((dvoid *) env->envhp,
		(dvoid **) &(conn->errhp), /* !!! */
		(ub4) OCI_HTYPE_ERROR, (size_t) 0, (dvoid **) 0), env->errhp);
	/* service handler */
	/*ASSERT (L, OCIHandleAlloc((dvoid *) env->envhp,
		(dvoid **) &(conn->svchp),
		(ub4) OCI_HTYPE_SVCCTX, (size_t) 0, (dvoid **) 0), conn->errhp);
*/
	/* login */
	ASSERT (L, OCILogon(env->envhp, conn->errhp, &(conn->svchp),
		(CONST text*)username, userlen,
		(CONST text*)password, passlen,
		(CONST text*)sourcename, snlen), conn->errhp);
	conn->closed = 0;
	env->conn_counter++;
	conn->loggedon = 1;

	return 1;
}


/*
** Close environment object.
*/
static int env_close (lua_State *L) {
	env_data *env = (env_data *)luaL_checkudata (L, 1, LUASQL_ENVIRONMENT_OCI8);
	luaL_argcheck (L, env != NULL, 1, LUASQL_PREFIX"environment expected");
	if (env->closed) {
		lua_pushboolean (L, 0);
		lua_pushstring (L, "Environment is already closed");
		return 2;
	}
	if (env->conn_counter > 0){
		lua_pushboolean (L, 0);
		lua_pushstring (L, "There are open connections");
		return 2;
	}

	env->closed = 1;
	/* release resources */
	if (env->envhp)
		OCIHandleFree ((dvoid *)env->envhp, OCI_HTYPE_ENV);
	if (env->errhp)
		OCIHandleFree ((dvoid *)env->errhp, OCI_HTYPE_ERROR);
	lua_pushboolean (L, 1);
	return 1;
}


/*
** Creates an Environment and returns it.
*/
static int create_environment (lua_State *L) {
	env_data *env = (env_data *)LUASQL_NEWUD(L, sizeof(env_data));
	luasql_setmeta (L, LUASQL_ENVIRONMENT_OCI8);
	/* fill in structure */
	env->closed = 0;
	env->conn_counter = 0;
	env->envhp = NULL;
	env->errhp = NULL;
/* maybe OCI_SHARED and OCI_THREADED ??? */
	if (OCIEnvCreate ( &(env->envhp), (ub4)OCI_DEFAULT, (dvoid *)0,
			(dvoid * (*)(dvoid *, size_t)) 0,
			(dvoid * (*)(dvoid *, dvoid *, size_t)) 0,
			(void (*)(dvoid *, dvoid *)) 0,
			(size_t) 0,
			(dvoid **) 0))
		luasql_faildirect (L, "couldn't create environment");
	/* error handler */
	ASSERT (L, OCIHandleAlloc((dvoid *) env->envhp,
		(dvoid **) &(env->errhp), /* !!! */
		(ub4) OCI_HTYPE_ERROR, (size_t) 0, (dvoid **) 0), env->errhp);
	return 1;
}


/*
** Create metatables for each class of object.
*/
static void create_metatables (lua_State *L) {
	struct luaL_Reg environment_methods[] = {
		{"__gc", env_close}, /* Should this method be changed? */
		{"__close", env_close},
		{"close", env_close},
		{"connect", env_connect},
		{NULL, NULL},
	};
	struct luaL_Reg connection_methods[] = {
		{"__gc", conn_close}, /* Should this method be changed? */
		{"__close", conn_close},
		{"close", conn_close},
		{"execute", conn_execute},
		{"prepare", conn_prepare},
		{"commit", conn_commit},
		{"rollback", conn_rollback},
		{"setautocommit", conn_setautocommit},
		{NULL, NULL},
	};
	struct luaL_Reg cursor_methods[] = {
		{"__gc", cur_gc},
		{"__close", cur_gc},
		{"close", cur_close},
		{"getcolnames", cur_getcolnames},
		{"getcoltypes", cur_getcoltypes},
		{"fetch", cur_fetch},
		{NULL, NULL},
	};
	struct luaL_Reg statement_methods[] = {
		{"__gc", stmt_close},
		{"__close", stmt_close},
		{"execute", stmt_execute},
		{"close", stmt_close},
		{NULL, NULL},
	};
	luasql_createmeta (L, LUASQL_ENVIRONMENT_OCI8, environment_methods);
	luasql_createmeta (L, LUASQL_CONNECTION_OCI8, connection_methods);
	luasql_createmeta (L, LUASQL_CURSOR_OCI8, cursor_methods);
	luasql_createmeta (L, LUASQL_STATEMENT_OCI8, statement_methods);
	lua_pop (L, 4);
}


/*
** Creates the metatables for the objects and registers the
** driver open method.
*/
LUASQL_API int luaopen_luasql_oci8 (lua_State *L) {
	struct luaL_Reg driver[] = {
		{"oci8", create_environment},
		{NULL, NULL},
	};
	create_metatables (L);
	lua_newtable (L);
	luaL_setfuncs (L, driver, 0);
	luasql_set_info (L);
	luasql_set_types (L);
	return 1;
}
