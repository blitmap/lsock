local basename   = (...):match('^[^.]*')
local core       = require(basename .. '.core')
local socketpair = core.socketpair

-- socketpair('AF_UNIX', 'SOCK_STREAM') -> file handle userdata, file handle userdata
core.socketpair =
	function (domain, socktype, protocol)
		return socketpair(core[domain] or domain, core[socktype] or socktype, protocol or 0)
	end

return true
