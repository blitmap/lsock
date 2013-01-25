local basename      = (...):match('^[^.]*')
local core          = require(basename .. '.core')
local bind          = core.bind
local pack_sockaddr = core.pack_sockaddr

core.bind =
	function (s, sockaddr)
		sockaddr = core[sockaddr] or sockaddr

		if type(sockaddr) == 'table' then
			sockaddr = core.pack_sockaddr(sockaddr)
		end

		return bind(s, sockaddr)
	end

return true
