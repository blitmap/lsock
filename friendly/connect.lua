local basename      = (...):match('^[^.]*')
local core          = require(basename .. '.core')
local connect       = core.connect
local pack_sockaddr = core.pack_sockaddr

core.connect =
	function (s, sockaddr)
		sockaddr = core[sockaddr] or sockaddr

		if type(sockaddr) == 'table' then
			sockaddr = core.pack_sockaddr(sockaddr)
		end

		return connect(s, sockaddr)
	end

return true
