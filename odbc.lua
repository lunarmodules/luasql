if not sql and loadlib then
	local libname = "luasqlodbc.2.0.dll"
	local libopen = "luasql_libopen_odbc"
	local init, err1, err2 = loadlib (libname, libopen)
	assert (init, (err1 or '')..(err2 or ''))
	init ()
end
