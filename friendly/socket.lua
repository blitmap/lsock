local core = require('lsock.core')

local core_socket = core.socket

-- this allows for: s = lsock.socket('AF_UNIX', 'SOCK_STREAM')
core.socket =
	function (domain, socktype, protocol)
		return core_socket(core[domain] or domain, core[socktype] or socktype, protocol or 0)
	end

return true
