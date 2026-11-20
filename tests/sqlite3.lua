---------------------------------------------------------------------
-- SQLite specific tests and configurations.
---------------------------------------------------------------------

DROP_TABLE_RETURN_VALUE = 0

---------------------------------------------------------------------
-- Produces a SQL statement which completely erases a table.
-- @param table_name String with the name of the table.
-- @return String with SQL statement.
---------------------------------------------------------------------
function sql_erase_table (table_name)
	return string.format ("delete from %s where 1", table_name)
end

function checkUnknownDatabase(ENV)
	-- skip this test
end

function finalization ()
	os.execute ("rm -rf "..datasource)
end

table.insert (CONN_METHODS, "escape")
table.insert (EXTENSIONS, escape)

local function assert_end_of_results (cur, sql)
	local row, err = cur:fetch ({})
	if row ~= nil then
		cur:close ()
	end
	assert2 (nil, row, sql)
	assert2 (nil, err, sql)
end

table.insert (EXTENSIONS, function ()
	for _, sql in ipairs ({ "select * from t where 1 = 0", "select null where 0" }) do
		local cur = CUR_OK (CONN:execute (sql))
		assert_end_of_results (cur, sql)
	end

	-- Fetch into a table to distinguish an all-NULL row from no row.
	for _, sql in ipairs ({ "select null", "select null, null" }) do
		local cur = CUR_OK (CONN:execute (sql))
		local row = assert (cur:fetch ({}))
		assert2 (nil, next (row), sql)
		assert_end_of_results (cur, sql)
	end

	io.write (" empty and NULL results")
end)

table.insert (EXTENSIONS, function ()
	local cur = CUR_OK (CONN:execute "select sqlite_version()")
	local version = assert (cur:fetch ())
	cur:close ()
	local major, minor = version:match ("^(%d+)%.(%d+)")
	major, minor = tonumber (major), tonumber (minor)
	if major < 3 or (major == 3 and minor < 35) then
		io.write (" returning (skipped: SQLite < 3.35)")
		return
	end

	-- Keep sqlite3_changes() on the suite's shared connection unchanged.
	local conn = CONN_OK (ENV:connect (":memory:"))
	assert (conn:execute "create table returning_test (value text)")
	local sql = "insert into returning_test values ('first'), ('second') returning *"
	cur = CUR_OK (conn:execute (sql))
	local first = assert (cur:fetch ())
	local second = assert (cur:fetch ())
	assert_end_of_results (cur, sql)
	assert (first == "first" and second == "second" or first == "second" and second == "first",
		"INSERT RETURNING did not return both inserted values")

	-- The returned rows can look correct even if the statement ran twice.
	cur = CUR_OK (conn:execute "select count(*) from returning_test")
	local count = cur:fetch ()
	cur:close ()
	assert2 (2, tonumber (count), "INSERT RETURNING stored an unexpected number of rows")
	assert (conn:close ())

	io.write (" returning")
end)
