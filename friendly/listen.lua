local basename = (...):match('^[^.]*')
local core   = require(basename .. '.core')
local listen = core.listen

core.listen =
	function (s, backlog)
		return listen(s, backlog ~= nil and backlog or 0)
	end

return true
