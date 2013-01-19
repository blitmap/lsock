for _, v in ipairs({ 'socket', 'sockaddr', 'linger', 'timeval' }) do
	assert(require('lsock.glue.' .. v))
end

return true
