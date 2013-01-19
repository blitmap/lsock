local core = require('lsock.core')

for _, v in ipairs({ 'glue', 'friendly' }) do
	assert(require('lsock.' .. v))
end

return core
