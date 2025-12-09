---------------------------------------------------------------------
-- Lua 5.4 support to to-be-closed variables.
---------------------------------------------------------------------

assert(CONN, "Unable to access CONN variable with a connection object!")

local cursor <close> = CONN:execute("select * from t")
CUR_OK (cursor)

local connection <close> = ENV:connect (datasource, username, password)
CONN_OK (connection)

local environment <close> = luasql[driver] ()
ENV_OK (environment)