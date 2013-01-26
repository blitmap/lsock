local basename   = (...):match('^[^.]*')
local core       = require(basename .. '.core')
local setsockopt = core.setsockopt

-- setsockopt(s, 'SOL_SOCKET', 'SO_SNDBUF', 2048)
core.setsockopt =
	function (s, l, o, v)

		l = core[l] or l
		o = core[o] or o

		return setsockopt(s, l, o, v)
	end

return true
