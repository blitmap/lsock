local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')
local linger   = core.linger
local cwrap    = coroutine.wrap
local cyield   = coroutine.yield
local fields   = { 'l_onoff', 'l_linger' }
local mt       = debug.getregistry()[basename .. '.linger']

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

-- linger({ l_onoff = 1, l_linger = 20 }) -> linger userdata
core.linger =
	function (t)
		local l = linger()

		for k, v in pairs(t or {}) do
			l[k] = v
		end

		return l
	end

return true
