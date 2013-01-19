local core   = require('lsock.core')
local listen = core.listen

core.listen =
	function (s, backlog)
		return listen(s, backlog ~= nil and backlog or 0)
	end

return true
