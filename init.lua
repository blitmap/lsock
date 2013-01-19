local core = require('lsock.core')

for _, v in ipairs({ 'glue', 'friendly' }) do
	require('lsock.' .. v)
end

return core
