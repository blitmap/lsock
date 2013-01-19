local core     = require('lsock.core')
local shutdown = core.shutdown

core.shutdown =
	function (s, m)
		m = core[m] ~= nil and core[m] or m or core.SHUT_RDWR

		return shutdown(s, m)
	end
