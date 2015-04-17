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
	orig_drop_table()
	CONN:commit()
end

