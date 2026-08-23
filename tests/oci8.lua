---------------------------------------------------------------------
-- Oracle specific tests and configurations.
---------------------------------------------------------------------

-- NOTE: For Unicode tests to work correctly, set NLS_LANG=.AL32UTF8
-- in your environment before running the test suite.

DEFINITION_STRING_TYPE_NAME = "varchar(60)"
QUERYING_STRING_TYPE_NAME = "string"

-- Oracle Autonomous Database enables parallel DML by default, which
-- causes ORA-12838 when mixing DML and SELECT in the same transaction.
-- Wrap the rollback test to disable parallel DML for this session.
local _orig_rollback = rollback
function rollback ()
	-- Disable parallel DML to avoid ORA-12838 on Autonomous DB
	assert(CONN:execute("ALTER SESSION DISABLE PARALLEL DML"),
		"couldn't disable parallel DML")
	return _orig_rollback()
end
