local core    = require('lsock.core')
local timeval = core.timeval
local fields  = { 'tv_sec', 'tv_usec' }
local mt      = debug.getregistry()['lsock.timeval']

mt.__pairs =
	function (s)
		return
			cwrap
			(
				function ()
					for _, k in pairs(fields) do
						local v = s[k]

						cyield(k, v)
					end
				end
			)
	end

mt.__ipairs =
	function (s)
		return
			cwrap
			(
				function ()
					for i, k in ipairs(fields) do
						local v = s[k]

						cyield(i, v)
					end
				end
			)
	end

-- lsock.timeval({ tv_sec = 3, tv_usec = 283 }) -> timeval userdata
core.timeval =
	function (t)
		local l = timeval()

		for k, v in pairs(t or {}) do
			l[k] = v
		end

		return l
	end

return true
