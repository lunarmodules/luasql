/*
** LuaSQL, Oracle driver
** Authors: Tomas Guisasola, Leonardo Godinho
** $Id: ls_oci8.c,v 1.3 2003/05/30 09:51:54 tomas Exp $
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <oci.h>
#include <oratypes.h>
#include <ociapr.h>
#include <ocidem.h>

#include <lua.h>
#include <lauxlib.h>

#include "luasql.h"

#define LUASQL_ENVIRONMENT_OCI8 "Oracle environment"
#define LUASQL_CONNECTION_OCI8 "Oracle connection"
#define LUASQL_CURSOR_OCI8 "Oracle cursor"


typedef struct {
	short      closed;
	int        conn_counter;
	OCIEnv    *envhp;
	OCIError  *errhp; /* !!! */
} env_data;


typedef struct {
	short      closed;
	short      loggedon;
	short      auto_commit;        /* 0 for manual commit */
	int        cur_counter;
	int        env;                /* reference to environment */
	int        numcols;
	OCISvcCtx *svchp;
	OCIError  *errhp; /* !!! */
} conn_data;


typedef struct {
	ub2        type;
	ub4        max;
	sb2        null;
	OCIDefine *define;
	void      *buffer;
} col_info;


typedef struct {
	short      closed;
	int        conn;               /* reference to connection */
	int        numcols;            /* number of columns */
	int        colnames, coltypes; /* reference to column information tables */
	int        curr_tuple;         /* next tuple to be read */
	OCIStmt   *stmthp;
	OCIError  *errhp; /* !!! */
	col_info  *cols;
} cur_data;


int checkerr (lua_State *L, sword status, OCIError *errhp);
#define ASSERT(L,exp,err) {sword s = exp; if (s) return checkerr (L, s, err);}


LUASQL_API int luasql_libopen_oracle (lua_State *L);


/*
** Check for valid environment.
*/
static env_data *getenvironment (lua_State *L) {
	env_data *env = (env_data *)luaL_checkudata (L, 1, LUASQL_ENVIRONMENT_OCI8);
	luaL_argcheck (L, env != NULL, 1, "environment expected");
	luaL_argcheck (L, !env->closed, 1, "environment is closed");
	return env;
}


/*
** Check for valid connection.
*/
static conn_data *getconnection (lua_State *L) {
	conn_data *conn = (conn_data *)luaL_checkudata (L, 1, LUASQL_CONNECTION_OCI8);
	luaL_argcheck (L, conn != NULL, 1, "connection expected");
	luaL_argcheck (L, !conn->closed, 1, "connection is closed");
	return conn;
}


/*
** Check for valid cursor.
*/
static cur_data *getcursor (lua_State *L) {
	cur_data *cur = (cur_data *)luaL_checkudata (L, 1, LUASQL_CURSOR_OCI8);
	luaL_argcheck (L, cur != NULL, 1, "cursor expected");
	luaL_argcheck (L, !cur->closed, 1, "cursor is closed");
	return cur;
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
			lua_pushstring (L, LUASQL_PREFIX"Sucesso com info!");
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
** Get another row of the given cursor.
*/
/*
static int cur_fetch (lua_State *L) {
	cur_data *cur = getcursor (L);
	int tuple = cur->curr_tuple;

	if (tuple >= PQntuples(cur->pg_res)) {
		lua_pushnil(L);  *//* no more results *//*
		return 1;
	}

	cur->curr_tuple++;
	if (lua_istable (L, 2)) {
		int i;
		const char *opts = luaL_optstring (L, 3, "n");
		if (strchr (opts, 'n') != NULL)
			*//* Copy values to numerical indices *//*
			for (i = 1; i <= cur->numcols; i++) {
				pushvalue (L, res, tuple, i);
				lua_rawseti (L, 2, i);
			}
		if (strchr (opts, 'a') != NULL)
			*//* Copy values to alphanumerical indices *//*
			for (i = 1; i <= cur->numcols; i++) {
				lua_pushstring (L, PQfname (res, i-1));
				pushvalue (L, res, tuple, i);
				lua_rawset (L, 2);
			}
		lua_pushvalue(L, 2);
		return 1; *//* return table *//*
	}
	else {
		int i;
		for (i = 1; i <= cur->numcols; i++)
			pushvalue (L, res, tuple, i);
		return cur->numcols; *//* return #numcols values *//*
	}
}
*/


/*
** Close the cursor on top of the stack.
** Return 1
*/
static int cur_close (lua_State *L) {
	conn_data *conn;
	cur_data *cur = (cur_data *)luaL_checkudata (L, 1, LUASQL_CURSOR_OCI8);
	if (cur->closed)
		return 0;

	/* Nullify structure fields. */
	cur->closed = 1;
	if (cur->stmthp)
		OCIHandleFree ((dvoid *)cur->stmthp, OCI_HTYPE_STMT);
	if (cur->errhp)
		OCIHandleFree ((dvoid *)cur->errhp, OCI_HTYPE_ERROR);
	free (cur->cols);
	/* Decrement cursor counter on connection object */
	lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
	conn = lua_touserdata (L, -1);
	conn->cur_counter--;
	luaL_unref (L, LUA_REGISTRYINDEX, cur->conn);

	lua_pushnumber(L, 1);
	return 1;
}


/*
** Create a table with the names and the types of the fields.
** The names are stored at the position they appear in the result;
** the types are stored in entries named by the corresponding field.
*/
static int cur_colinfo (lua_State *L) {
	cur_data *cur = getcursor (L);
	/*PGresult *result = cur->pg_res;*/
	conn_data *conn;
	/*char typename[100];*/
	int i;

	lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
	if (!lua_isuserdata (L, -1))
		luaL_error (L, LUASQL_PREFIX"unexpected error (ColInfo)");
	conn = (conn_data *)lua_touserdata (L, -1);
	lua_newtable (L);
	for (i = 1; i <= cur->numcols; i++) {
		/*lua_pushstring(L, PQfname(result, i-1));*/
		lua_pushvalue(L, -1);
		lua_rawseti(L, -3, i);
		/*lua_pushstring(L, getcolumntype (conn->pg_conn, result, i-1, typename));*/
		lua_rawset(L, -3);
	}
	return 1;
}

static int cur_getcolnames (lua_State *L) {
	cur_data *cur = getcursor (L);
	if (cur->colnames != LUA_NOREF)
		lua_rawgeti (L, LUA_REGISTRYINDEX, cur->colnames);
	else {
		/*PGresult *result = cur->pg_res;*/
		int i;
		lua_newtable (L);
		for (i = 1; i <= cur->numcols; i++) {
			/*lua_pushstring (L, PQfname (result, i-1));*/
			lua_rawseti (L, -2, i);
		}
	}
	return 1;
}

static int cur_getcoltypes (lua_State *L) {
	cur_data *cur = getcursor (L);
	if (cur->coltypes != LUA_NOREF)
		lua_rawgeti (L, LUA_REGISTRYINDEX, cur->coltypes);
	else {
		/*PGresult *result = cur->pg_res;*/
		conn_data *conn;
		/*char typename[100];*/
		int i;
		lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
		if (!lua_isuserdata (L, -1))
			luaL_error (L, LUASQL_PREFIX"unexpected error (ColInfo)");
		conn = (conn_data *)lua_touserdata (L, -1);
		lua_newtable (L);
		for (i = 1; i <= cur->numcols; i++) {
			/*lua_pushstring(L, getcolumntype (conn->pg_conn, result, i-1, typename));*/
			lua_rawseti (L, -2, i);
		}
	}
	return 1;
}


/*
** Push the number of rows.
*/
static int cur_numrows (lua_State *L) {
	int n;
	cur_data *cur = getcursor (L);
	ASSERT (L, OCIAttrGet ((dvoid *) cur->stmthp, OCI_HTYPE_STMT, (dvoid *)&n, (ub4)0,
		OCI_ATTR_NUM_ROWS, cur->errhp), cur->errhp);
	lua_pushnumber (L, n);
	return 1;
}


/*
** Close a Connection object.
*/
static int conn_close (lua_State *L) {
	env_data *env;
	conn_data *conn = (conn_data *)luaL_checkudata (L, 1, LUASQL_CONNECTION_OCI8);
	if (conn->closed)
		return 0;
	if (conn->cur_counter > 0)
		return luaL_error (L, LUASQL_PREFIX"there are open cursors");

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
	lua_rawgeti (L, LUA_REGISTRYINDEX, conn->env);
	env = lua_touserdata (L, -1);
	env->conn_counter--;
	luaL_unref (L, LUA_REGISTRYINDEX, conn->env);

	lua_pushnumber(L, 1);
	return 1;
}


/* colinfo ???
	for (i = 1; i <= conn->numcols; i++) {
		dvoid **parmdpp;
		text *name;
		ub4 name_len;
		ub2 max_size;
		ASSERT (L, OCIParamGet (conn->stmthp, OCI_HTYPE_STMT, cur->errhp,
			&parmdpp, (ub4)i), cur->errhp);
		ASSERT (L, OCIAttrGet (parmdpp, (ub4)OCI_DTYPE_PARAM,
			(dvoid **)&name, name_len, OCI_ATTR_NAME, cur->errhp), cur->errhp);
		ASSERT (L, OCIAttrGet (kk
	}
*/


/*
static char *oracle2luatypa (ub2 data_type) {
	switch (data_type) {
		case SQLT_CHR:
			return "string";
		case SQLT_INT:
			return "number";
	}
}
*/


/*
** Obtain column information.
*/
static int prepare_buffers (lua_State *L, cur_data *cur, int i) {
	OCIParam *param;

	ASSERT (L, OCIParamGet (cur->stmthp, OCI_HTYPE_STMT, cur->errhp,
		(dvoid **)&param, i), cur->errhp);
	ASSERT (L, OCIAttrGet (param, OCI_DTYPE_PARAM,
		(dvoid *)&(cur->cols[i].type), (ub4 *)0, OCI_ATTR_DATA_TYPE,
		cur->errhp), cur->errhp);
	switch (cur->cols[i].type) {
		case SQLT_CHR:
			ASSERT (L, OCIAttrGet (param, OCI_DTYPE_PARAM,
				(dvoid *)&(cur->cols[i].max), (ub4 *)0, OCI_ATTR_DATA_SIZE,
				cur->errhp), cur->errhp);
			cur->cols[i].buffer = calloc (cur->cols[i].max + 1, sizeof(char));
			ASSERT (L, OCIDefineByPos (cur->stmthp, &(cur->cols[i].define),
				cur->errhp, (ub4)i, cur->cols[i].buffer, cur->cols[i].max,
				SQLT_STR, (dvoid *)&(cur->cols[i].null), (ub2 *)0,
				(ub2 *)0, (ub4) OCI_DEFAULT), cur->errhp);
			break;
		case SQLT_NUM:
			cur->cols[i].buffer = malloc (sizeof(int));
			ASSERT (L, OCIDefineByPos (cur->stmthp, &(cur->cols[i].define),
				cur->errhp, (ub4)i, cur->cols[i].buffer, (sb4)sizeof(int),
				SQLT_INT, (dvoid *)&(cur->cols[i].null), (ub2 *)0,
				(ub2 *)0, (ub4) OCI_DEFAULT), cur->errhp);
			break;
		case SQLT_CLOB: {
			env_data *env;
			conn_data *conn;
			lua_rawgeti (L, LUA_REGISTRYINDEX, cur->conn);
			conn = (conn_data *)lua_touserdata (L, -1);
			lua_rawgeti (L, LUA_REGISTRYINDEX, conn->env);
			env = (env_data *)lua_touserdata (L, -1);
			lua_pop (L, 2);
			ASSERT (L, OCIDescriptorAlloc (env->envhp,
				&(cur->cols[i].buffer), OCI_DTYPE_LOB, (size_t)0,
				(dvoid **)0), cur->errhp);
			ASSERT (L, OCIDefineByPos (cur->stmthp,
				&(cur->cols[i].define), cur->errhp, (ub4)i,
				&(cur->cols[i].buffer), (sb4)sizeof(dvoid *),
				SQLT_CLOB, (dvoid *)&(cur->cols[i].null),
				(ub2 *)0, (ub2 *)0, OCI_DEFAULT),
				cur->errhp);
			break;
		}
		default:
			break;
	}
	return 0;
}


/*
** Create a new Cursor object and push it on top of the stack.
*/
static int create_cursor (lua_State *L, int o, conn_data *conn, OCIStmt *stmt) {
	int i;
	env_data *env;
	cur_data *cur = (cur_data *)lua_newuserdata(L, sizeof(cur_data));
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
	lua_pushvalue (L, o);
	cur->conn = luaL_ref (L, LUA_REGISTRYINDEX);

	/* error handler */
	lua_rawgeti (L, LUA_REGISTRYINDEX, conn->env);
	env = lua_touserdata (L, -1);
	lua_pop (L, 1);
	ASSERT (L, OCIHandleAlloc((dvoid *) env->envhp,
		(dvoid **) &(cur->errhp), (ub4) OCI_HTYPE_ERROR, (size_t) 0,
		(dvoid **) 0), conn->errhp);
	/* get number of columns */
	ASSERT (L, OCIAttrGet ((dvoid *)stmt, (ub4)OCI_HTYPE_STMT,
		(dvoid *)&cur->numcols, (ub4 *)0, (ub4)OCI_ATTR_PARAM_COUNT,
		cur->errhp), cur->errhp);
	cur->cols = (col_info *)malloc (sizeof(col_info) * cur->numcols);
	/* define output variables */
	for (i = 1; i <= cur->numcols; i++) {
		int ret = prepare_buffers (L, cur, i);
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
	lua_rawgeti (L, LUA_REGISTRYINDEX, conn->env);
	if (!lua_isuserdata (L, -1))
		luaL_error(L,LUASQL_PREFIX"invalid environment in connection!");
	env = (env_data *)lua_touserdata (L, -1);
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
	if (status && (status != OCI_NO_DATA))
		return checkerr (L, status, conn->errhp);
	if (type == OCI_STMT_SELECT) {
		/* create cursor */
		return create_cursor (L, 1, conn, stmthp);
	} else {
		/* return number of rows */
		int rows_affected;
		ASSERT (L, OCIAttrGet ((dvoid *)stmthp, (ub4)OCI_HTYPE_STMT,
			(dvoid *)&rows_affected, (ub4 *)0,
			(ub4)OCI_ATTR_ROW_COUNT, conn->errhp), conn->errhp);
		lua_pushnumber (L, rows_affected);
		return 1;
	}
}


/*
** Commit the current transaction.
*/
static int conn_commit (lua_State *L) {
	conn_data *conn = getconnection (L);
	return 0;
}


/*
** Rollback the current transaction.
*/
static int conn_rollback (lua_State *L) {
	conn_data *conn = getconnection (L);
	return 0;
}


/*
** Set "auto commit" property of the connection.
** If 'true', then rollback current transaction.
** If 'false', then start a new transaction.
*/
static int conn_setautocommit (lua_State *L) {
	conn_data *conn = getconnection (L);
	return 0;
}


/*
** Connects to a data source.
*/
static int env_connect (lua_State *L) {
	env_data *env = getenvironment (L);
	const char *sourcename = luaL_checkstring(L, 2);
	const char *username = luaL_optstring(L, 3, NULL);
	const char *password = luaL_optstring(L, 4, NULL);
	conn_data *conn = (conn_data *)lua_newuserdata(L, sizeof(conn_data));

	/* fill in structure */
	luasql_setmeta (L, LUASQL_CONNECTION_OCI8);
	conn->env = LUA_NOREF;
	conn->closed = 0;
	conn->auto_commit = 1;
	conn->cur_counter = 0;
	conn->loggedon = 0;
	conn->svchp = NULL;
	conn->errhp = NULL;
	env->conn_counter++;
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
		(CONST text*)username, strlen(username),
		(CONST text*)password, strlen(password),
		(CONST text*)sourcename, strlen(sourcename)), conn->errhp);
	conn->loggedon = 1;

	return 1;
}


/*
** Close environment object.
*/
static int env_close (lua_State *L) {
	env_data *env = (env_data *)luaL_checkudata (L, 1, LUASQL_ENVIRONMENT_OCI8);
	if (env->closed)
		return 0;
	if (env->conn_counter > 0)
		return luaL_error (L, LUASQL_PREFIX"there are open connections");

	env->closed = 1;
	/* desalocar: env->errhp e env->envhp */
	if (env->envhp)
		OCIHandleFree ((dvoid *)env->envhp, OCI_HTYPE_ENV);
	if (env->errhp)
		OCIHandleFree ((dvoid *)env->errhp, OCI_HTYPE_ERROR);
	lua_pushnumber (L, 1);
	return 1;
}


/*
** Creates an Environment and returns it.
*/
static int create_environment (lua_State *L) {
	env_data *env = (env_data *)lua_newuserdata(L, sizeof(env_data));
	luasql_setmeta (L, LUASQL_ENVIRONMENT_OCI8);
	/* fill in structure */
	env->closed = 0;
	env->conn_counter = 0;
	env->envhp = NULL;
	env->errhp = NULL;
/* talvez OCI_SHARED e OCI_THREADED ??? */
	if (OCIEnvCreate ( &(env->envhp), (ub4)OCI_DEFAULT, (dvoid *)0,
			(dvoid * (*)(dvoid *, size_t)) 0,
			(dvoid * (*)(dvoid *, dvoid *, size_t)) 0,
			(void (*)(dvoid *, dvoid *)) 0,
			(size_t) 0,
			(dvoid **) 0))
		luasql_faildirect (L, LUASQL_PREFIX"couldn't create environment");
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
		/*{"fetch", cur_fetch},*/
		{"numrows", cur_numrows},
		{NULL, NULL},
	};
	luasql_createmeta (L, LUASQL_ENVIRONMENT_OCI8, environment_methods);
	luasql_createmeta (L, LUASQL_CONNECTION_OCI8, connection_methods);
	luasql_createmeta (L, LUASQL_CURSOR_OCI8, cursor_methods);
}


/*
** Creates the metatables for the objects and registers the
** driver open method.
*/
LUASQL_API int luasql_libopen_oracle (lua_State *L) { 
	luasql_getlibtable (L);
	lua_pushstring(L, "oracle");
	lua_pushcfunction(L, create_environment);
	lua_settable(L, -3);

	create_metatables (L);

	return 0;
}