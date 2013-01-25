local basename   = (...):match('^[^.]*')
local core       = require(basename .. '.core')
local getsockopt = core.getsockopt

-- getsockopt(s, 'SOL_SOCKET', 'SO_SNDBUF') -> 18485..
core.getsockopt =
	function (s, l, o)

		l = core[l] or l
		o = core[o] or o

		return getsockopt(s, l, o)
	end

return true
