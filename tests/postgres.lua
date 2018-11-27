---------------------------------------------------------------------
-- PostgreSQL specific tests and configurations.
-- $Id: postgres.lua,v 1.2 2006/01/25 19:15:21 tomas Exp $
---------------------------------------------------------------------

DEFAULT_USERNAME = "postgres"
PREPARED_STATEMENT_ARGUMENT = function(n) return "$"..n end

table.insert (CUR_METHODS, "numrows")
table.insert (EXTENSIONS, numrows)
table.insert (CONN_METHODS, "escape")
table.insert (EXTENSIONS, escape)
table.insert (EXTENSIONS, execparams)
