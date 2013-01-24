local basename   = (...):match('^[^.]*')
local core       = require(basename .. '.core')
local socketpair = core.socketpair

-- socketpair('AF_UNIX', 'SOCK_STREAM') -> file handle userdata, file handle userdata
core.socketpair =
	function (domain, socktype, protocol)

		domain   = core[domain  ] or domain
		socktype = core[socktype] or socktype
		protocol = core[protocol] or protocol or 0

		return socketpair(domain, socktype, protocol)
	end

return true
