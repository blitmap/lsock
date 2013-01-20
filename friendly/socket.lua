local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')
local socket   = core.socket

-- socket('AF_UNIX', 'SOCK_STREAM') -> file handle userdata
core.socket =
	function (domain, socktype, protocol)
		return socket(core[domain] or domain, core[socktype] or socktype, protocol or 0)
	end

return true
