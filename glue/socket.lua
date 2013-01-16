local core = require('lsock.core')

local socket_mt = {}

local methods =
	{
		accept   = core.accept,
		bind     = core.bind,
		connect  = core.connect,
		listen   = core.listen,
		recv     = core.recv,
		sendij   = core.sendij,
		shutdown = core.shutdown
	}

-- we're hackishly exploiting the lua filehandle metatable
local fh_mt = debug.getmetatable(io.stdout)

fh_mt.__index =
	function (s, k)
		return fh_mt[k] or methods[k]
	end

-- every time a socket is created, the lua file handle mt becomes the socket's mt */
debug.getregistry()['lsock socket'] = fh_mt

return true
