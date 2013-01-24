local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')
local socket   = core.socket

-- socket('AF_UNIX', 'SOCK_STREAM') -> file handle userdata
core.socket =
	function (domain, socktype, protocol)

		domain   = core[domain  ] or domain
		socktype = core[socktype] or socktype
		protocol = core[protocol] or 0

		return socket(domain, socktype, protocol)
	end

return true
