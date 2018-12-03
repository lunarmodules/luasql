---------------------------------------------------------------------
-- Firebird specific tests and configurations.
---------------------------------------------------------------------

DEFINITION_STRING_TYPE_NAME = "VARCHAR(80)"
QUERYING_STRING_TYPE_NAME = "string"

CHECK_GETCOL_INFO_TABLES = false

local orig_create_table = create_table
local orig_drop_table = drop_table

---------------------------------------------------------------------
-- New metadata needs to be commited before it is used
---------------------------------------------------------------------
function create_table ()
	orig_create_table()
	CONN:commit()
end

function drop_table ()
	-- Firebird prefers to keep DDL stuff (CREATE TABLE, etc.) 
	-- seperate. So we need a new transaction i.e. connection
	-- to work in
	assert(CONN:close ())
	CONN = assert(ENV:connect (datasource, username, password))
	orig_drop_table()
	CONN:commit()
end

table.insert (CONN_METHODS, "escape")
table.insert (EXTENSIONS, escape)

-- Check RETURNING support
table.insert (EXTENSIONS, function()
	local cur = assert (CONN:execute[[
EXECUTE BLOCK
RETURNS (A INTEGER, B INTEGER)
AS
BEGIN
  A = 123;
  B = 321;
  SUSPEND;
END
]])

	local f1, f2 = cur:fetch ()
	assert2 (123, f1)
	assert2 (321, f2)
	cur:close ()
	
	io.write (" returning")
end)

