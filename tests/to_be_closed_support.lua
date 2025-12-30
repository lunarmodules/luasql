---------------------------------------------------------------------
-- Lua 5.4 support to to-be-closed variables.
---------------------------------------------------------------------

assert(CONN, "Unable to access CONN variable with a connection object!")

local t = {}

do
	local cursor <close> = CUR_OK (CONN:execute("select count(*) from t"))
	t.cursor = cursor
	assert (tostring(t.cursor):match"cursor %(0x%w+%)", "cursor was closed")
end
assert (tostring(t.cursor):match"cursor %(closed%)", "cursor already open")

do
	local connection <close> = CONN_OK (ENV:connect (datasource, username, password))
	t.connection = connection
	assert (tostring(t.connection):match"connection %(0x%w+%)", "connection was closed")
end
assert (tostring(t.connection):match"connection %(closed%)", "connection already open")

do
	local environment <close> = ENV_OK (luasql[driver] ())
	t.environment = environment
	assert (tostring(t.environment):match"environment %(0x%w+%)", "environment was closed")
end
assert (tostring(t.environment):match"environment %(closed%)", "environment already open")
