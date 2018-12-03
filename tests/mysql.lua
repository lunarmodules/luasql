---------------------------------------------------------------------
-- MySQL specific tests and configurations.
---------------------------------------------------------------------

QUERYING_STRING_TYPE_NAME = "binary(65535)"

---------------------------------------------------------------------
-- Seeks to an arbitrary row in a query result set.
---------------------------------------------------------------------
function seek ()
    -- Inserts three rows.
    assert2 (3, CONN:execute"insert into t (f1) values ('a'), ('b'), ('c')", "could not insert a new record")
    cur = CUR_OK(CONN:execute"select * from t")
    assert2('a', cur:fetch())
    assert2('b', cur:fetch())
	cur:seek(1)
    assert2('b', cur:fetch())
    assert2('c', cur:fetch())
	cur:seek(1)
    assert2('b', cur:fetch())
    assert2('c', cur:fetch())
    assert2(nil, cur:fetch())
    cur:close()

	io.write (" seek")
end

table.insert (CUR_METHODS, "numrows")
table.insert (EXTENSIONS, numrows)
table.insert (CUR_METHODS, "seek")
table.insert (EXTENSIONS, seek)
table.insert (CONN_METHODS, "escape")
table.insert (EXTENSIONS, escape)

---------------------------------------------------------------------
-- Build SQL command to create the test table.
---------------------------------------------------------------------
local _define_table = define_table
function define_table (n)
	return _define_table(n) .. " ENGINE = InnoDB;"
end

---------------------------------------------------------------------
-- MySQL versions 4.0.x do not implement rollback.
---------------------------------------------------------------------
local _rollback = rollback
function rollback ()
	if luasql._MYSQLVERSION and string.sub(luasql._MYSQLVERSION, 1, 3) == "4.0" then
		io.write("skipping rollback test (mysql version 4.0.x)")
		return
	else
		_rollback ()
	end
end
