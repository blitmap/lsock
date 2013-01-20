local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')
local shutdown = core.shutdown

core.shutdown =
	function (s, m)
		return shutdown(s, core[m] or m or core.SHUT_RDWR)
	end
