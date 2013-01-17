/* compile: gcc -o core{.so,.c} -shared -fPIC -W -Wall -O2 -g -llua -lm -ldl -pedantic -ansi -std=c89 -flto -fstack-protector-all */

#ifndef _POSIX_SOURCE
#	define _POSIX_SOURCE
#endif

#ifndef _GNU_SOURCE
#	define _GNU_SOURCE
#endif

#ifdef _WIN32
#	pragma comment(lib, "Ws2_32.lib")
#	define WIN32_LEAN_AND_MEAN /* avoid MVC stuffs */
#	include <windows.h>
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <io.h>             /* _open_osfhandle() / _get_osfhandle() */
#else
#	include <arpa/inet.h>
#	include <sys/socket.h>
#	include <netdb.h>
#	include <netinet/in.h>
#	include <sys/types.h>
#	include <sys/un.h>
#	include <string.h>
#	include <unistd.h>
#	include <errno.h>
#endif

#include <lauxlib.h>
#include <lualib.h>

#define NUL '\0'
#define BZERO(buf, sz) memset(buf, NUL, sz)

#define CLEARSTACK(L) lua_settop(L, 0)

#define LSOCK_SOCKADDR "lsock sockaddr"
#define LSOCK_LINGER   "lsock linger_t"
#define LSOCK_TIMEVAL  "lsock_timeval_t"

#define LSOCK_FDOPEN_MODE "r+b"

#define NEWUDATA(L, sz) BZERO(lua_newuserdata(L, sz), sz)

#ifdef _WIN32
typedef SOCKET lsocket;
#else
typedef int    lsocket;
#endif

/* in liolib.c */
typedef luaL_Stream LStream;

typedef struct
{
	LStream lstream;
	lsocket s;
}
LSockStream; /* this should match LStream in Lua's source with the addition of the member 'sd' */

#define CHECKLSOCKSTREAM(L, index) ((LSockStream *) luaL_checkudata(L, index, LUA_FILEHANDLE))
#define     CHECKLSTREAM(L, index) ((LStream     *) luaL_checkudata(L, index, LUA_FILEHANDLE))
#define     CHECKLSOCKET(L, index) ((CHECKLSOCKSTREAM(L, 1))->s)

#define LSOCK_STRERROR(L, fname) lsock_error(L, errno,     &strerror, fname)
#define LSOCK_GAIERROR(L)        lsock_error(L, errno, &gai_strerror, NULL )

#ifdef _WIN32
#	define INVALID_LSOCKET(s) (INVALID_SOCKET == s)
#else
#	define INVALID_LSOCKET(s) (s < 0)
#endif

/* including sockaddr_un in this only increases the byte count by 18 */
union LSockAddr
{
	struct sockaddr_storage storage;
	struct sockaddr         addr;
	struct sockaddr_in      in;
	struct sockaddr_in6     in6;
	struct sockaddr_un      un; 
};

/* TODO:
**
** Bindings:
**
** IO:
**	- lsock_select()
**
**	Only available on Linux:
**		- lsock_socketpair()
**
** Handshaking:
**	- lsock_connect()
**	- lsock_bind()
**
** Option Controls:
**	- lsock_ioctl(userdata file_handle, number request, userdata...)
**	- lsock_getsockopt(userdata sock, number level, number option, userdata option_value)
**	- lsock_setsockopt(userdata sock, number level, number option, userdata option_value)
**
** Host to Network/Network to Host:
**	- lsock_inet_ntop(number af, userdata) -> string   (InetNtop() on Windows)
**	- lsock_inet_pton() -> userdata (InetPton() on Windows)
**	- lsock_htonl(number)   -> userdata
**	- lsock_htons(number)   -> userdata
**	- lsock_ntohl(userdata) -> number
**	- lsock_ntohs(userdata) -> number
**
**	Only available on Windows (WAT?!):
**		- htond(number)    -> userdata
**		- htonf(number)    -> userdata
**		- htonll(number)   -> userdata
**		- ntohd(userdata)  -> number
**		- ntohf(userdata)  -> number
**		- ntohll(userdata) -> number
**
** Socket Info:
**	- lsock_getsockname()
**	- lsock_getpeername()
**	- lsock_gethostname()
**
**	- lsock_getaddrinfo()
**	- lsock_getnameinfo()
**
**	Maybe...
**		- lsock_getprotobyname()
**		- lsock_getprotobynumber()
**
** Data structure creation:
**	- lsock_struct_addrinfo_un()
**	- lsock_struct_addrinfo_in()
**	- lsock_struct_linger()
**	- lsock_struct_timeval()
*/

/* going to and from SOCKET and fd in Windows: _open_osfhandle()/_get_osfhandle() */

/* {{{ lsock_error() */

static int
lsock_error(lua_State * const L, const int err, char * (*errfunc)(int), const char * fname)
{
	char * msg = errfunc(err);

	lua_pushnil(L);

	if (NULL == fname)
		lua_pushstring(L, msg); /* even if err is not a valid errno, strerror() will return a pointer to "" */
	else
		lua_pushfstring(L, "%s: %s", fname, msg);

	lua_pushinteger(L, err);

	return 3;
}

/* }}} */

/* {{{ newsockstream(): based directly on: newprefile() & newfile() from Lua 5.2 sources */

static int
lsock_io_fclose(lua_State * const L)
{
	LSockStream * const p = CHECKLSOCKSTREAM(L, 1);

	const int res = fclose(p->lstream.f);

	return luaL_fileresult(L, (res == 0), NULL);
}

static LSockStream *
newsockstream(lua_State * const L)
{
	LSockStream * const p = lua_newuserdata(L, sizeof(LSockStream));

	p->lstream.closef = NULL; /* mark file handle as 'closed' */

	luaL_setmetatable(L, LUA_FILEHANDLE);

	p->lstream.f      = NULL;
	p->lstream.closef = &lsock_io_fclose;

#ifdef _WIN32
	p->s             = INVALID_SOCKET;
#else
	p->s             = -1; /* invalid fd on Linux */
#endif
	
	return p;
}

/* }}} */

/* {{{ */

static union LSockAddr *
newsockaddr(lua_State * const L)
{
	union LSockAddr * addr = lua_newuserdata(L, sizeof(union LSockAddr));

	luaL_setmetatable(L, LSOCK_SOCKADDR);

	return addr;
}

/* }}} */

/* {{{ lsocket_to_fd() */

#ifdef _WIN32

static int
lsocket_to_fd(const lsocket handle)
{
	const int fd = _open_osfhandle(handle, 0); /* no _O_APPEND, _O_RDONLY, _O_TEXT */

	if (-1 == fd)
		luaL_error(L, "_open_osfhandle(): %s", strerror(errno));

	return fd;
}

#endif

/* }}} */

/* {{{ develop_lsocket() */

/* meant to take the raw lsocket type and an LSockStream and fill out the fields as it converts it into an fd (for Windows mostly) */
static int
develop_fh(lua_State * const L, const lsocket sock, LSockStream * stream)
{
	FILE * sockfile = NULL;

	if (INVALID_LSOCKET(sock))
		return LSOCK_STRERROR(L, NULL);

	stream->s = sock;

	/* socket() gives us a SOCKET, not an fd, on Windows */
#ifdef _WIN32
	sockfile = fdopen(lsocket_to_fd(sock), LSOCK_FDOPEN_MODE);
#else
	sockfile = fdopen(sock, LSOCK_FDOPEN_MODE);
#endif

	if (NULL == sockfile)
#ifdef _WIN32 /* maybe this is overdoing it. */
		return LSOCK_STRERROR(L, "_fdopen()"); /* Windows has _fdopen() to be ISO conformant, apparently */
#else   
		return LSOCK_STRERROR(L, "fdopen()");
#endif

	stream->lstream.f = sockfile;

	return 0;
}

/* }}} */

/* {{{ lsock__number_to_rawstring */

static int
lsock__netnumber_tostring(lua_State * const L)
{
	const char * s = lua_tostring(L, 1);

	/* requirement that lua_Number be at least long int */
	lua_Number * n = luaL_checkudata(L, 1, s);

	lua_pushnumber(L, *n);
	lua_tostring(L, -1); /* changes the number we pushed to a string in-place */

	return 1;
}

/* }}} */


/* {{{ lsock_sockaddr() -> lsock_sockaddr userdata */

static int
lsock_sockaddr(lua_State * const L)
{
	/* I could be smart about this and allocate only as per address-family usage,
	** but if all of these sockaddr structures are the same size I can freely
	** convert between them depending on member assignment from Lua */
	(void) newsockaddr(L);

	return 1;
}

/* }}} */

/* {{{ lsock__sockaddr_getset() */

static const char * const sockaddr_fields[] =
{
	  "ss_family",                                                             /* sockaddr_storage structure */
	  "sa_family",   "sa_data",                                                /* sockaddr         structure */
	 "sin_family",  "sin_port", "sin_addr",                                    /* sockaddr_in      structure */
	"sin6_family", "sin6_port", "sin6_flowinfo", "sin6_addr", "sin6_scope_id", /* sockaddr_in6     structure */
	 "sun_family",  "sun_path",                                                /* sockaddr_un      structure */
	NULL
};

enum SOCKADDR_OPTS
{
	  SS_FAMILY,
	  SA_FAMILY,   SA_DATA,
	 SIN_FAMILY,  SIN_PORT, SIN_ADDR,
	SIN6_FAMILY, SIN6_PORT, SIN6_FLOWINFO, SIN6_ADDR, SIN6_SCOPE_ID,
	 SUN_FAMILY,  SUN_PATH
};

/* this function handles both __index and __newindex,
** we take advantage of the requirement of Lua's syntax
** that object.member = is invalid, by checking for
** "nothing" as the third arg */
static int
lsock__sockaddr_getset(lua_State * const L)
{
	union
	{
		struct sockaddr_storage storage;
		struct sockaddr         addr;
		struct sockaddr_in      in;
		struct sockaddr_in6     in6;
		struct sockaddr_un      un;
	} * p = luaL_checkudata(L, 1, LSOCK_SOCKADDR); /* the mt is set in the registry by lsock/init.lua, not lsock/core.so (this file) */

	int o = luaL_checkoption(L, 2, NULL, sockaddr_fields);

	/* used as boolean */
	int newindex = !lua_isnone(L, 3);

	int af = p->storage.ss_family; /* we should be working from this!, not `o' */

	switch (o)
	{
		/* same place and size in memory */
		case   SS_FAMILY:
		case   SA_FAMILY:
		case  SUN_FAMILY:
		case  SIN_FAMILY:
		case SIN6_FAMILY:

			if (newindex)
				p->storage.ss_family = luaL_checkinteger(L, 3);
			else
				lua_pushinteger(L, p->storage.ss_family);

			break;

		case  SIN_PORT:
		case SIN6_PORT:

			if ((o == SIN_PORT && af != AF_INET) || (o == SIN6_PORT && af != AF_INET6))
				luaL_error(L, "invalid index \"%s\"; incompatible address family", lua_tostring(L, 2));
			
			if (newindex)
				if (SIN_PORT == o)
					p->in.sin_port = htons(luaL_checkinteger(L, 3));
				else
					p->in6.sin6_port = htons(luaL_checkinteger(L, 3));
			else
				lua_pushinteger(L, ntohs(o == SIN_PORT ? p->in.sin_port : p->in6.sin6_port));

			break;

		case  SIN_ADDR:
		case SIN6_ADDR:
			if ((o == SIN_ADDR && af != AF_INET) || (o == SIN6_ADDR && af != AF_INET6))
				luaL_error(L, "invalid index \"%s\"; incompatible address family", lua_tostring(L, 2));

			if (newindex)
			{
				size_t l = 0;
				const char * src = luaL_checklstring(L, 3, &l);
				size_t sz = 0;
				void * dst = NULL;
				int stat;

				if (af == AF_INET)
				{
					dst = &p->in.sin_addr;
					sz  = sizeof(p->in.sin_addr);
				}
				else
				{
					dst = &p->in6.sin6_addr;
					sz  = sizeof(p->in6.sin6_addr);
				}

				/* NUL this just to be sure */
				BZERO(dst, sz);

				stat = inet_pton(af, src, dst);

				if (1 != stat) /* success is 1, funnily enough */
				{
					if (0 == stat)
						luaL_error(L, "invalid network address for address family (AF_INET%s): %s", o == SIN_ADDR ? "" : "6", src);
					else
						luaL_error(L, strerror(errno));
				}
			}
			else
			{
				/* +1 for good measure? */
				char dst[INET6_ADDRSTRLEN];

				void * src = NULL;

				if (AF_INET == af)
					src = &p->in.sin_addr;
				else
					src = &p->in6.sin6_addr;

				BZERO(dst, sizeof(dst));

				if (NULL == inet_ntop(af, src, dst, sizeof(dst)))
					luaL_error(L, strerror(errno));

				lua_pushstring(L, dst);
			}

			break;
			
		case  SA_DATA:
		case SUN_PATH:

			{
				size_t sz = SA_DATA == o ? 14 : 108;

				if (newindex)
				{

					size_t l = 0;
					const char * s = luaL_checklstring(L, 3, &l);
					void * dst = &p->addr.sa_data;

					/* .sun_path gets an address family check, .sa_data does not */
					if (o == SUN_PATH)
					{
						if (af != AF_UNIX)
							luaL_error(L, "invalid index \"sun_path\"; address family is not AF_UNIX");

						if (l > 108)
							luaL_error(L, "%s (greater than 108 bytes)", strerror(ENAMETOOLONG));
/*							luaL_error(L, "string assigned to \"%s\" must be <= %d bytes (was %d)", tostring(L, 2), sz, l); */

						if (s[l] != NUL)
							luaL_error(L, "field \"sun_path\" must be NUL-terminated");

						dst = &p->un.sun_path;
					}

					BZERO(dst, sz); /* clear this first */

					memcpy(dst, s, l);
				}
				else
				{
					void * src = NULL;

					if (o == SUN_PATH)
						src = &p->addr.sa_data;
					else
						src = &p->un.sun_path;

					lua_pushlstring(L, (const char *) src, sz);
				}
			}

			break;

		case SIN6_FLOWINFO:
		case SIN6_SCOPE_ID:

			if (af != AF_INET6)
				luaL_error(L, "invalid index \"%s\"; address family is not AF_INET6", lua_tostring(L, 2));

			if (newindex)
				/* we're hoping that the lua_Number returned from luaL_checknumber() is of long-size :p */
				if (SIN6_FLOWINFO == o)
					p->in6.sin6_flowinfo = htonl(luaL_checknumber(L, 3));
				else
					p->in6.sin6_scope_id = htonl(luaL_checknumber(L, 3));
			else
				lua_pushnumber(L, ntohl(o == SIN6_FLOWINFO ? p->in6.sin6_flowinfo : p->in6.sin6_scope_id));

			break;
			
	}

	return newindex ? 0 : 1;
}

/* }}} */

/* {{{ lsock_accept() */

static int
lsock_accept(lua_State * const L)
{
	lsocket                serv = CHECKLSOCKET(L, 1);

	union LSockAddr      * info     = newsockaddr(L);
	socklen_t              sz       = sizeof(union LSockAddr);

	lsocket                sock     = accept(serv, (struct sockaddr *) info, &sz);
	LSockStream          * sockfile = newsockstream(L);

	/* the way this signals an error is unfortunate */
	if (develop_fh(L, sock, sockfile))
		return 3;

	return 2;
}

/* }}} */

/* {{{ lsock_listen(sock, backlog_number) */

static int
lsock_listen(lua_State * const L)
{
	lsocket      serv = CHECKLSOCKET(L, 1);
	const int backlog = luaL_checkint(L, 2);

	if (listen(serv, backlog))
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);

	return 1;
}

/* }}} */

/* {{{ lsock_bind() */

/* bind(sock, sockaddr_userdata) -> true -or- nil, 'error message', error_constant */

static int
lsock_bind(lua_State * const L)
{
	lsocket     serv = CHECKLSOCKET(L, 1);
	int * const addr = luaL_checkudata(L, 2, LSOCK_SOCKADDR);

	if (bind(serv, (const struct sockaddr *) addr, lua_rawlen(L, 2))) /* note use of rawlen() instead of sizeof(union LSockAddr) -- just to be safe */
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);
	
	return 1;
}

/* }}} */

/* {{{ lsock_connect(sock, sockaddr_userdata) -> true -or- nil, 'error message', error_constant */

/* identical to lsock_bind(), pretty much */

static int
lsock_connect(lua_State * const L)
{
	lsocket     client = CHECKLSOCKET(L, 1);
	int * const addr   = luaL_checkudata(L, 2, LSOCK_SOCKADDR);

	if (connect(client, (const struct sockaddr *) addr, lua_rawlen(L, 2))) /* rawlen() for safety */
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);

	return 1;
}

/* }}} */

/* {{{ lsock_recv() */

/* recv(sock, length, flags) -> 3, 'cat' */

static int
lsock_recv(lua_State * const L)
{
	lsocket sock  =  CHECKLSOCKET(L, 1);
	size_t length = luaL_checkint(L, 2);
	int    flags  = luaL_checkint(L, 3);

	char * buf    = NULL;
	int    gotten = 0;

	luaL_Buffer B;

	buf = luaL_buffinitsize(L, &B, length);

	BZERO(buf, length); /* just for the hell of it */

	gotten = recv(sock, buf, length, flags);
	
	if (-1 == gotten)
		return LSOCK_STRERROR(L, NULL);

	luaL_pushresultsize(&B, gotten);

	return 1; /* success! */
}

/* }}} */

/* {{{ lsock_sendij() */

/* sendij(sock, 'cat', 1, 3, flags) -> 3 */

static int
lsock_sendijto(lua_State * const L)
{
	size_t s_len = 0;

	lsocket      sock         = CHECKLSOCKET(L, 1);
	const char * s            = luaL_checklstring(L, 2, &s_len);
	int          i            = luaL_checkint    (L, 3        );
	size_t       j            = luaL_checkint    (L, 4        );
	int          flags        = luaL_checkint    (L, 5        );

	union LSockAddr * to = NULL;
	size_t dest_len           = 0;

	ssize_t sent              = 0;

	if (!lua_isnone(L, 6))
	{
		to       = luaL_checkudata(L, 6, LSOCK_SOCKADDR);
		dest_len = lua_rawlen(L, 6); /* important */
	}

	sent = 0;

	if (i < 1 || j > s_len)
		luaL_error(L, "out of bounds [%d, %d]; send data is %d bytes", i, j, s_len);

	sent = sendto(sock, s + (i - 1), j - (i + 1), flags, (const struct sockaddr *) to, dest_len);

	if (-1 == sent)
		return LSOCK_STRERROR(L, "sendto()");

	lua_pushinteger(L, sent);

	return 1; /* success! */
}

/* }}} */

/* {{{ lsock_shutdown() */

/* shutdown(sock, how) -> true  -or-  nil, errno */
/* lsock/init.lua wraps this and sets it as the __gc for sockets */

static int
lsock_shutdown(lua_State * const L)
{
	lsocket sock =  CHECKLSOCKET(L, 1);
	int     how  = luaL_checkint(L, 2);

	if (shutdown(sock, how))
		return LSOCK_STRERROR(L, NULL);
	
	lua_pushboolean(L, 1);

	return 1;
}

/* }}} */

/* {{{ lsock_socket() */

/* socket('AF_INET', 'SOCK_STREAM', 0) -> 29 */

static int
lsock_socket(lua_State * const L)
{
	int domain   = luaL_checkint(L, 1),
	    type     = luaL_checkint(L, 2),
	    protocol = luaL_checkint(L, 3);

	lsocket       sock     = socket(domain, type, protocol);
	LSockStream * sockfile = newsockstream(L);

	/* bad semantics is bad */
	if (develop_fh(L, sock, sockfile))
		return 3;

	return 1;
}

/* }}} */

/* {{{ lsock_strerror() */

static int
lsock_strerror(lua_State * const L)
{
	lua_pushstring(L, strerror(luaL_checkint(L, 1)));

	return 1;
}

/* }}} */

/* {{{ lsock_gai_strerror() */

static int
lsock_gai_strerror(lua_State * const L)
{
	lua_pushstring(L, strerror(luaL_checkint(L, 1)));

	return 1;
}

/* }}} */

/* {{{ lsock_shutdown() */

#ifdef _WIN32

static void
lsock_cleanup(lua_State * const L)
{
	((void) L);

	WSACleanup();
}

#endif

/* }}} */

/* {{{ luaopen_lsock_core() */

#define LUA_REG(x) { #x, lsock_##x }

static const luaL_Reg lsocklib[] =
{
	/* alphabetical */
	LUA_REG(accept),
	LUA_REG(bind),
	LUA_REG(connect),
	LUA_REG(gai_strerror),
	LUA_REG(listen),
	LUA_REG(recv),
	LUA_REG(sendijto),
	LUA_REG(shutdown),
	LUA_REG(sockaddr),
	LUA_REG(socket),
	LUA_REG(strerror),
	LUA_REG(_netnumber_tostring),
	LUA_REG(_sockaddr_getset),
	{ NULL, NULL }
};

#undef LUA_REG


LUALIB_API int
luaopen_lsock_core(lua_State * const L)
{

#ifdef _WIN32

	WSADATA wsaData;

	int stat = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (stat != 0)
	{
		WSACleanup();
		luaL_error(L, "WSAStartup() failed [%d]: %s\n", stat, strerror(stat));
	}

	if
	(
		2 != LOBYTE(wsaData.wVersion) ||
		2 != HIBYTE(wsaData.wVersion)
	)
	{
		WSACleanup();
		luaL_error(L, "Could not find a usable version of Winsock.dll");
	}

#endif

	luaL_newlib(L, lsocklib);

#define LSOCK_CONST(C) \
    lua_pushinteger(L, C);  \
    lua_setfield(L, -2, #C)

	/* protocol family constants */
	LSOCK_CONST(PF_APPLETALK);
	LSOCK_CONST(PF_INET     );
	LSOCK_CONST(PF_INET6    );
	LSOCK_CONST(PF_IPX      );
	LSOCK_CONST(PF_IRDA     );
	LSOCK_CONST(PF_UNIX     );
	LSOCK_CONST(PF_UNSPEC   );

	/* address family constants */
	LSOCK_CONST(AF_APPLETALK);
	LSOCK_CONST(AF_INET     );
	LSOCK_CONST(AF_INET6    );
	LSOCK_CONST(AF_IPX      );
	LSOCK_CONST(AF_IRDA     );
	LSOCK_CONST(AF_UNIX     );
	LSOCK_CONST(AF_UNSPEC   );

	/* socket type constants */
	LSOCK_CONST(SOCK_DGRAM    );
	LSOCK_CONST(SOCK_RAW      );
	LSOCK_CONST(SOCK_RDM      );
	LSOCK_CONST(SOCK_SEQPACKET);
	LSOCK_CONST(SOCK_STREAM   );

	/* protocol constants */
	LSOCK_CONST(IPPROTO_ICMP  );
	LSOCK_CONST(IPPROTO_ICMPV6);
	LSOCK_CONST(IPPROTO_IGMP  );
	LSOCK_CONST(IPPROTO_TCP   );
	LSOCK_CONST(IPPROTO_UDP   );

	/* errno's, alphabetical */
	LSOCK_CONST(EACCES         );
	LSOCK_CONST(EADDRINUSE     );
	LSOCK_CONST(EADDRNOTAVAIL  );
	LSOCK_CONST(EAFNOSUPPORT   );
	LSOCK_CONST(EAGAIN         );
	LSOCK_CONST(EBADF          );
	LSOCK_CONST(ECONNABORTED   );
	LSOCK_CONST(EDESTADDRREQ   );
	LSOCK_CONST(EINVAL         );
	LSOCK_CONST(EINTR          );
	LSOCK_CONST(EIO            );
	LSOCK_CONST(EISDIR         );
	LSOCK_CONST(ELOOP          );
	LSOCK_CONST(EMFILE         );
	LSOCK_CONST(ENAMETOOLONG   );
	LSOCK_CONST(ENFILE         );
	LSOCK_CONST(ENOBUFS        );
	LSOCK_CONST(ENOENT         );
	LSOCK_CONST(ENOTDIR        );
	LSOCK_CONST(ENOTSOCK       );
	LSOCK_CONST(ENOBUFS        );
	LSOCK_CONST(ENOMEM         );
	LSOCK_CONST(ENOTCONN       );
	LSOCK_CONST(ENOTSOCK       );
	LSOCK_CONST(EOPNOTSUPP     );
	LSOCK_CONST(EPROTO         );
	LSOCK_CONST(EPROTONOSUPPORT);
	LSOCK_CONST(EPROTOTYPE     );
	LSOCK_CONST(EROFS          );
	LSOCK_CONST(EWOULDBLOCK    );

	/* getaddrinfo() constants */
	LSOCK_CONST(AI_ADDRCONFIG              );
	LSOCK_CONST(AI_ALL                     );
	LSOCK_CONST(AI_CANONIDN                );
	LSOCK_CONST(AI_CANONNAME               );
	LSOCK_CONST(AI_IDN                     );
	LSOCK_CONST(AI_IDN_ALLOW_UNASSIGNED    );
	LSOCK_CONST(AI_IDN_USE_STD3_ASCII_RULES);
	LSOCK_CONST(AI_NUMERICHOST             );
	LSOCK_CONST(AI_NUMERICSERV             );
	LSOCK_CONST(AI_PASSIVE                 );
	LSOCK_CONST(AI_V4MAPPED                );

	/* getaddrinfo() errors */
	LSOCK_CONST(EAI_ADDRFAMILY);
	LSOCK_CONST(EAI_AGAIN     );
	LSOCK_CONST(EAI_BADFLAGS  );
	LSOCK_CONST(EAI_FAIL      );
	LSOCK_CONST(EAI_FAMILY    );
	LSOCK_CONST(EAI_MEMORY    );
	LSOCK_CONST(EAI_NODATA    );
	LSOCK_CONST(EAI_NONAME    );
	LSOCK_CONST(EAI_OVERFLOW  );
	LSOCK_CONST(EAI_SERVICE   );
	LSOCK_CONST(EAI_SOCKTYPE  );
	LSOCK_CONST(EAI_SYSTEM    );

	/* send() & recv() flag constants */
	LSOCK_CONST(MSG_EOR    );
	LSOCK_CONST(MSG_OOB    );
	LSOCK_CONST(MSG_PEEK   );
	LSOCK_CONST(MSG_WAITALL);

	LSOCK_CONST(SHUT_RD  );
	LSOCK_CONST(SHUT_RDWR);
	LSOCK_CONST(SHUT_WR  );

#undef LSOCK_CONST

#ifdef _WIN32

	lua_puchcfunction(L, &lsock_cleanup);
	lua_setfield(L, -1, "__gc");
	lua_pushvalue(L, -1)
	lua_setmetatable(L, -1) /* it is its own metatable */

#endif

	return 1;
}

/* }}} */
