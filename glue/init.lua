for _, v in ipairs({ 'socket', 'sockaddr', 'linger', 'timeval' }) do
	require('lsock.glue.' .. v)
end

return true
