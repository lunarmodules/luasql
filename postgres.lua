if not luasql and loadlib then
	local libname = "libluasqlpostgres.2.0.dylib"
	local libopen = "luasql_libopen_postgres"
	local init, err1, err2 = loadlib (libname, libopen)
	assert (init, (err1 or '')..(err2 or ''))
	init ()
end
