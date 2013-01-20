local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')

-- this is more future-proof since we don't
-- rely on the key name in the registry
local fh_mt = debug.getmetatable(io.stdout)

fh_mt.__index =
	function (s, k)
		return fh_mt[k] or core[k]
	end

-- every time a socket is created, the lua file handle mt becomes the socket's mt */
debug.getregistry()[basename .. '.socket'] = fh_mt

return true
