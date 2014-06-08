/* compile: gcc -o lsock.{so,c} -shared -fPIC -pedantic -std=c89 -W -Wall -Wextra -Werror -llua -fstack-protector-all -fvisibility=hidden -Os -s */

/* cross-platform includes */
#include <sys/types.h>
#include <errno.h>
#include <lauxlib.h>
#include <lualib.h>

/* platform-specific includes */

#if _WIN32
#	pragma comment(lib, "Ws2_32.lib")
#	define _CRT_SECURE_NO_WARNINGS /* blah!! */
#	define WIN32_LEAN_AND_MEAN /* avoid MVC stuffs */
#	include <windows.h>
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <io.h>             /* _open_osfhandle() / _get_osfhandle() */
#endif

#ifdef __APPLE__
#	define __APPLE_USE_RFC_2292
#	include <sys/uio.h>
#endif

#ifdef __linux
#	define _POSIX_SOURCE
#	define _GNU_SOURCE
#	include <stropts.h>
#	include <sys/sendfile.h>
#endif

/* Mac OS X + Linux */
#ifndef _WIN32
#	include <arpa/inet.h>
#	include <sys/socket.h>
#	include <netdb.h>
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <netinet/udp.h>
#	include <net/if.h>
#	include <sys/types.h>
#	include <sys/un.h>
#	include <string.h>
#	include <unistd.h>
#	include <sys/time.h>
#	include <fcntl.h>
#	include <sys/ioctl.h>
#	include <sys/select.h>
#endif

/* platform-specific defines */
#ifdef _WIN32
#	define EXPOSE_SYMBOL __declspec(dllexport)
#	define NET_ERRNO WSAGetLastError()
#else
#	define EXPOSE_SYMBOL __attribute__((visibility("default")))
#	define INVALID_SOCKET -1
#	define NET_ERRNO (errno)
#	ifndef UNIX_PATH_MAX
#		define UNIX_PATH_MAX MIN(108, MEMBER_SIZE(struct sockaddr_un, sun_path))
#	endif
#endif

/* mapping native types to portable names */
#ifdef _WIN32
typedef SOCKET lsocket;
typedef SSIZE_T ssize_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int16 uint16_t;
#else
typedef int lsocket;
#endif

#define ZERO_OUT(buf, sz) memset(buf, 0, sz)
#define MEMBER_SIZE(type, member) sizeof(((type *) NULL)->member)
#define LENGTH(arr) (sizeof(arr) / sizeof(arr[0]))

#define LSOCK_NEWUDATA(L, sz) ZERO_OUT(lua_newuserdata(L, sz), sz)

#define   LSOCK_CHECKFH(L, index) ((luaL_Stream *) luaL_checkudata(L, index, LUA_FILEHANDLE))
#define LSOCK_CHECKSOCK(L, index) file_to_sock(L, LSOCK_CHECKFH(L, index)->f)
#define   LSOCK_CHECKFD(L, index) file_to_fd(L, LSOCK_CHECKFH(L, index)->f)

#define LSOCK_STRERROR(L, fname) lsock_error(L, NET_ERRNO, (char * (*)(int)) &strerror,     fname)
#define LSOCK_GAIERROR(L, err  ) lsock_error(L, err,             (char * (*)(int)) &gai_strerror, NULL )
#define LSOCK_STRFATAL(L, fname) lsock_fatal(L, NET_ERRNO, (char * (*)(int)) &strerror,     fname)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define PUSHFIELD(state, tidx, type, field, v) \
	do { lua_push ## type(L, v); lua_setfield(L, -1 + (tidx), field); } while (0)

/* including sockaddr_un in this only increases the byte count by 18 */
typedef union
{
	struct sockaddr_storage ss;
	struct sockaddr         sa;
	struct sockaddr_in      in;
	struct sockaddr_in6     in6;
#ifndef _WIN32
	struct sockaddr_un      un;
#endif
} lsockaddr;

static int lsock_error(lua_State * L, int err, char * (*errfunc)(int), char * fname)
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

static int lsock_fatal(lua_State * L, int err, char * (*errfunc)(int), char * fname)
{
	char * msg = errfunc(err);

	if (NULL == fname)
		return luaL_error(L, msg);
	else
		return luaL_error(L, "%s: %s", fname, msg);
}

static int api_strerror(lua_State * L)
{
	lua_pushstring(L, strerror(luaL_checkint(L, 1)));

	return 1;
}

static int api_gai_strerror(lua_State * L)
{
	lua_pushstring(L, (char *) gai_strerror(luaL_checkint(L, 1)));

	return 1;
}

/* this is evil, unreadable, and clever; ripped from 5.2 */
static size_t posrelat(ptrdiff_t pos, size_t len)
{
	if (pos >= 0)
		return (size_t) pos;
	else if (0u - (size_t) pos > len)
		return 0;
	else
		return len - ((size_t) -pos) + 1;
}

static void strij(lua_State * L, int idx, const char ** s, size_t * count)
{
	int t;

	size_t l = 0;
	size_t i;
	size_t j;
	const char * str;

	idx = lua_absindex(L, idx);
	t   = lua_type(L, idx);

	luaL_argcheck(L, LUA_TSTRING == t || LUA_TTABLE == t, idx, "string or table expected");

	if (LUA_TSTRING == t)
	{
		*s = luaL_optlstring(L, idx, "", count);
		return;
	}

	/* for safey? */
	*s     = "";
	*count = 0;

	/* from here we assume table -- see above argcheck() */

	/* string must be t[1] */

	lua_pushnumber(L, 1);
	lua_gettable(L, idx);

	str = luaL_optlstring(L, -1, "", &l);
	lua_pop(L, 1);

	/* starting_index = t[2] or t.i or 1 */

	lua_pushnumber(L, 2);
	lua_gettable(L, idx);

	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_getfield(L, idx, "i");
	}

	i = posrelat(luaL_optint(L, -1, 1), 1);
	lua_pop(L, 1);

	/* ending_index = t[3] or t.j or #('the string') */

	lua_pushnumber(L, 3);
	lua_gettable(L, idx);

	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_getfield(L, idx, "j");
	}

	j = posrelat(luaL_optint(L, -1, l), l);
	lua_pop(L, 1);

	/* ---- */

	/* pretty identical to string.sub() here */
	i = MAX(i, 1);
	j = MIN(j, l);

	/* string.sub('abcdefghij', -4, -6) -> string.sub('abcdefghij', 7, 5) -> ''
	** we are asserting that i comes before j */
	if (i > j)
		return;

	*s     = (str - 1) + i;
	*count = (j   - i) + 1;
}

static int file_to_fd(lua_State * L, FILE * stream)
{
	int fd = -1;

#ifdef _WIN32
	fd = _fileno(stream);
#else
	fd = fileno(stream);
#endif

	if (-1 == fd)
#ifdef _WIN32
		LSOCK_STRFATAL(L, "_fileno()");
#else
		LSOCK_STRFATAL(L, "fileno()");
#endif

	return fd;
}

static FILE * fd_to_file(lua_State * L, int fd, char * mode)
{
	FILE * stream = NULL;

	if (NULL == mode)
		mode = "r+b";

#ifdef _WIN32
	stream = _fdopen(fd, mode);
#else
	stream = fdopen(fd, mode);
#endif

	if (NULL == stream)
#ifdef _WIN32
		LSOCK_STRFATAL(L, "_fdopen()");
#else
		LSOCK_STRFATAL(L, "fdopen()");
#endif

	return stream;
}

static int sock_to_fd(lua_State * L, lsocket sock)
{

#ifdef _WIN32
	int fd = -1;

	fd = _open_osfhandle(sock, 0); /* no _O_APPEND, _O_RDONLY, _O_TEXT */

	if (-1 == fd)
		LSOCK_STRFATAL(L, "_open_osfhandle()");

	return fd;
#else
	(void) L;

	return sock;
#endif

}

static lsocket fd_to_sock(lua_State * L, int fd)
{

#ifdef _WIN32
	/* for the record: I feel bad about this */
	uintptr_t l = _get_osfhandle(fd);

	if (INVALID_HANDLE_VALUE == (HANDLE) l)
		return LSOCK_STRFATAL(L, "_get_osfhandle()");

	return (lsocket) l;
#else
	(void) L;

	return fd; /* hah */
#endif

}

static lsocket file_to_sock(lua_State * L, FILE * stream)
{
	/* wheee shortness. */
	return fd_to_sock(L, file_to_fd(L, stream));
}


static FILE * sock_to_file(lua_State * L, lsocket sock, char * mode)
{
	return fd_to_file(L, sock_to_fd(L, sock), mode);
}

#if 0
static void
timeval_to_table(lua_State * L, struct timeval * t)
{
	lua_createtable(L, 0, 2);

	PUSHFIELD(L, -1, integer, "tv_sec",  t->tv_sec);
	PUSHFIELD(L, -1, integer, "tv_usec", t->tv_usec);
}
#endif

static struct timeval * table_to_timeval(lua_State * L, int idx)
{
	struct timeval * t;

	idx = lua_absindex(L, idx);

	t = (struct timeval *) LSOCK_NEWUDATA(L, sizeof(struct timeval));

	lua_getfield(L, idx, "tv_sec");

	if (!lua_isnil(L, -1))
		t->tv_sec = (long) lua_tonumber(L, -1);

	lua_pop(L, 1);

	lua_getfield(L, idx, "tv_usec");

	if (!lua_isnil(L, -1))
		t->tv_usec = (long) lua_tonumber(L, -1);

	lua_pop(L, 1);

	return t;
}

static void linger_to_table(lua_State * L, struct linger * l)
{
	lua_createtable(L, 0, 2);

	PUSHFIELD(L, -1, unsigned, "l_onoff",  l->l_onoff);
	PUSHFIELD(L, -1, unsigned, "l_linger", l->l_linger);
}

static struct linger * table_to_linger(lua_State * L, int idx)
{
	struct linger * l;

	idx = lua_absindex(L, idx);

	l = (struct linger *) LSOCK_NEWUDATA(L, sizeof(struct linger));

	lua_getfield(L, idx, "l_onoff");

	if (!lua_isnil(L, -1))
		l->l_onoff = (u_short) lua_tonumber(L, -1);

	lua_pop(L, 1);

	lua_getfield(L, idx, "l_linger");

	if (!lua_isnil(L, -1))
		l->l_linger = (u_short) lua_tonumber(L, -1);

	lua_pop(L, 1);

	return l;
}

static int sockaddr_to_table(lua_State * L, const char * sa, size_t lsa_sz)
{
	lsockaddr * lsa = (lsockaddr *) sa;

	if (lsa_sz < MEMBER_SIZE(struct sockaddr, sa_family))
		return 0;

	switch (lsa->sa.sa_family)
	{
#ifndef _WIN32
		case AF_UNIX:  if (lsa_sz < sizeof(struct sockaddr_un))  goto invalid_sockaddr; break;
#endif
		case AF_INET:  if (lsa_sz < sizeof(struct sockaddr_in))  goto invalid_sockaddr; break;
		case AF_INET6: if (lsa_sz < sizeof(struct sockaddr_in6)) goto invalid_sockaddr; break;
		default:       if (lsa_sz < sizeof(lsockaddr))           goto invalid_sockaddr; break;
invalid_sockaddr:
		return 0;
	}

	lua_createtable(L, 0, 7); /* 5 fields in sockaddr_in6 + 2 in sockaddr_storage */

	PUSHFIELD(L, -1, number, "ss_family", lsa->ss.ss_family);
	PUSHFIELD(L, -1, number, "sa_family", lsa->sa.sa_family);

	lua_pushlstring(L, lsa->sa.sa_data, MEMBER_SIZE(lsockaddr, sa.sa_data));
	lua_setfield(L, -2, "sa_data");

	switch (lsa->ss.ss_family)
	{
		case AF_INET:
			{
				char dst[INET_ADDRSTRLEN];

				ZERO_OUT(dst, sizeof(dst));

				PUSHFIELD(L, -1, number, "sin_family", lsa->in.sin_family);
				PUSHFIELD(L, -1, number, "sin_port",   lsa->in.sin_port);

#ifdef _WIN32
				if (NULL == InetNtop(AF_INET, &lsa->in.sin_addr, (PWSTR) dst, sizeof(dst)))
					LSOCK_STRFATAL(L, "InetNtop()");
#else
				if (NULL == inet_ntop(AF_INET, &lsa->in.sin_addr, dst, sizeof(dst)))
					LSOCK_STRFATAL(L, "inet_ntop()");
#endif

				PUSHFIELD(L, -1, string, "sin_addr", dst);
			}

			break;

		case AF_INET6:
			{
				char dst[INET6_ADDRSTRLEN];

				ZERO_OUT(dst, sizeof(dst));

				PUSHFIELD(L, -1, number, "sin6_family",   lsa->in6.sin6_family         );
				PUSHFIELD(L, -1, number, "sin6_port",     ntohs(lsa->in6.sin6_port)    );
				PUSHFIELD(L, -1, number, "sin6_flowinfo", ntohl(lsa->in6.sin6_flowinfo));
				PUSHFIELD(L, -1, number, "sin6_scope_id", ntohl(lsa->in6.sin6_scope_id));

#ifdef _WIN32
				if (NULL == InetNtop(AF_INET6, (char *) &lsa->in6.sin6_addr, (PWSTR) dst, sizeof(dst)))
					LSOCK_STRFATAL(L, "InetNtop()");
#else
				if (NULL == inet_ntop(AF_INET6, (char *) &lsa->in6.sin6_addr, dst, sizeof(dst)))
					LSOCK_STRFATAL(L, "inet_ntop()");
#endif

				PUSHFIELD(L, -1, string, "sin6_addr", dst);
			}

			break;

#ifndef _WIN32
		case AF_UNIX:
			PUSHFIELD(L, -1, number,  "sun_family", lsa->un.sun_family);

			lua_pushlstring(L, lsa->un.sun_path, UNIX_PATH_MAX);
			lua_setfield(L, -2, "sun_path");

			break;
#endif
	}

	return 1;
}


static char * sockaddr_members[] =
{
	"ss_family",
	"sa_family",   "sa_data",
	"sin_family",  "sin_port",  "sin_addr",
	"sin6_family", "sin6_port", "sin6_flowinfo", "sin6_addr", "sin6_scope_id",
#ifndef _WIN32
	"sun_family",  "sun_path"
#endif
};

enum
{
	SS_FAMILY,
	SA_FAMILY,   SA_DATA,
	SIN_FAMILY,  SIN_PORT,  SIN_ADDR,
	SIN6_FAMILY, SIN6_PORT, SIN6_ADDR, SIN6_FLOWINFO, SIN6_SCOPE_ID,
	SUN_FAMILY,  SUN_PATH
};

static const char * table_to_sockaddr(lua_State * L, int idx)
{
	unsigned int i;

	size_t out_sz = 0;
	lsockaddr lsa;

	ZERO_OUT(&lsa, sizeof(lsa));

	idx = lua_absindex(L, idx);

	for (i = 0; i < LENGTH(sockaddr_members); i++)
	{
		lua_getfield(L, idx, sockaddr_members[i]);

		if (lua_isnil(L, -1))
			goto next_member;

		switch (i)
		{
			case   SS_FAMILY:
			case   SA_FAMILY:
			case  SIN_FAMILY:
			case SIN6_FAMILY:
#ifndef _WIN32
			case  SUN_FAMILY:
#endif
				lsa.ss.ss_family = (u_short) luaL_checkint(L, -1);
				break;

			case  SIN_PORT:   lsa.in.sin_port = (u_short) htons(luaL_checkint(L, -1)); break;
			case SIN6_PORT: lsa.in6.sin6_port = (u_short) htons(luaL_checkint(L, -1)); break;

			case SIN6_FLOWINFO: lsa.in6.sin6_flowinfo = htonl(luaL_checklong(L, -1)); break;
			case SIN6_SCOPE_ID: lsa.in6.sin6_scope_id = htonl(luaL_checklong(L, -1)); break;

			case  SA_DATA:
#ifndef _WIN32
			case SUN_PATH:
#endif
				{
					size_t sz = 0;
					void       * dst = NULL;
					const char * src = NULL;

					src = luaL_checklstring(L, -1, &sz);

					if (i == SA_DATA)
					{
						dst = &lsa.sa.sa_data;
						sz  = MAX(MEMBER_SIZE(struct sockaddr, sa_data), sz); /* should be MAX(14, l) */
					}
#ifndef _WIN32
					else
					{
						dst = &lsa.un.sun_path;
						sz  = MAX(UNIX_PATH_MAX, sz); /* should be MAX(108, l) */
					}
#endif

					memcpy(dst, src, sz);
				}

				break;

			case  SIN_ADDR:
			case SIN6_ADDR:
				{
					int stat, af;
					void       * dst = NULL;
					const char * src = NULL;

					src = luaL_checkstring(L, -1);

					if (i == SIN_ADDR)
					{
						af  = AF_INET;
						dst = &lsa.in.sin_addr;
					}
					else
					{
						af  = AF_INET6;
						dst = &lsa.in6.sin6_addr;
					}

#ifdef _WIN32
					stat = InetPton(af, (PCWSTR) src, dst);
#else
					stat = inet_pton(af, src, dst);
#endif

					if (1 != stat) /* success is 1, funnily enough */
					{
						if (0 == stat)
							luaL_error(L, "invalid address for family (AF_INET%s)", i == SIN_ADDR ? "" : "6");
						else
#ifdef _WIN32
							LSOCK_STRFATAL(L, "InetPton()");
#else
							LSOCK_STRFATAL(L, "inet_pton()");
#endif
					}
				}

				break;
		}

next_member:
		lua_pop(L, 1);
	}

	switch (lsa.ss.ss_family)
	{
#ifndef _WIN32
		case AF_UNIX:  out_sz = sizeof(struct sockaddr_un);  break;
#endif
		case AF_INET:  out_sz = sizeof(struct sockaddr_in);  break;
		case AF_INET6: out_sz = sizeof(struct sockaddr_in6); break;
		default:       out_sz = sizeof(lsockaddr);
	}

	return lua_pushlstring(L, (char *) &lsa, out_sz);
}


static int api_pack_sockaddr(lua_State * L)
{
	luaL_checktype(L, 1, LUA_TTABLE);

	(void) table_to_sockaddr(L, 1);

	return 1;
}

static int api_unpack_sockaddr(lua_State * L)
{
	size_t       l = 0;
	const char * s = luaL_checklstring(L, 1, &l);

	return sockaddr_to_table(L, s, l);
}

static int close_stream(lua_State * L)
{
	luaL_Stream * p = LSOCK_CHECKFH(L, 1);

#ifdef _WIN32
	SOCKET s = file_to_sock(p->f);

	return luaL_fileresult(L, (closesocket(s) && 0 == fclose(p->f)), NULL);
#else
	return luaL_fileresult(L, (0 == fclose(p->f)), NULL);
#endif
}

/* based directly on: newprefile() & newfile() from Lua 5.2 sources */
static luaL_Stream * newfile(lua_State * L)
{
	luaL_Stream * p = (luaL_Stream *) LSOCK_NEWUDATA(L, sizeof(luaL_Stream));

	luaL_setmetatable(L, LUA_FILEHANDLE);

	p->f      = NULL;
	p->closef = &close_stream;

	return p;
}


static int api_htons(lua_State * L)
{
	lua_Number n = luaL_checknumber(L, 1);
	uint16_t   s = (uint16_t) n;

	if (s != n) /* type promotion back to lua_Number */
	{
		lua_pushnil(L);
		lua_pushfstring(L, "number cannot be represented as [network] short (%s)", s > n ? "underflow" : "overflow");
		return 2;
	}

	s = htons(s);

	lua_pushlstring(L, (char *) &s, sizeof(uint16_t));

	return 1;
}

static int api_ntohs(lua_State * L)
{
	uint16_t     h = 0;
	size_t       l = 0;
	const char * s = luaL_checklstring(L, 1, &l);

	if (sizeof(uint16_t) != l) /* obviously 2 bytes... */
	{
		lua_pushnil(L);
		lua_pushliteral(L, "string length must be sizeof(uint16_t) (2 bytes)");
		return 2;
	}

	h = ntohs(*((uint16_t *) s));

	lua_pushnumber(L, h);

	return 1;
}

static int api_htonl(lua_State * L)
{
	lua_Number n = luaL_checknumber(L, 1);
	uint32_t   l = (uint32_t) n;

	if (l != n) /* type promotion back to lua_Number */
	{
		lua_pushnil(L);
		lua_pushfstring(L, "number cannot be represented as [network] long (%s)", l > n ? "underflow" : "overflow");
		return 2;
	}

	l = htonl(l);

	lua_pushlstring(L, (char *) &l, sizeof(uint32_t));

	return 1;
}

static int api_ntohl(lua_State * L)
{
	uint32_t     h = 0;
	size_t       l = 0;
	const char * s = luaL_checklstring(L, 1, &l);

	if (sizeof(uint32_t) != l) /* 4 bytes */
	{
		lua_pushnil(L);
		lua_pushliteral(L, "string length must be sizeof(uint32_t) (4 bytes)");
		return 2;
	}

	h = ntohl(*((uint32_t *) s));

	lua_pushnumber(L, h);

	return 1;
}

static int api_accept(lua_State * L)
{
	luaL_Stream * fh;
	lsocket       new_sock;
	lsockaddr     info;

	lsocket       serv = LSOCK_CHECKSOCK(L, 1);
	socklen_t     sz   = sizeof(lsockaddr);

	ZERO_OUT(&info, sizeof(info));

	new_sock = accept(serv, (struct sockaddr *) &info, &sz);

	if (INVALID_SOCKET == new_sock)
		return LSOCK_STRERROR(L, NULL);

	fh = newfile(L);
	fh->f = sock_to_file(L, new_sock, NULL);

	lua_pushlstring(L, (char *) &info, sz);

	return 2;
}

static int api_listen(lua_State * L)
{
	lsocket serv = LSOCK_CHECKSOCK(L, 1);
	int  backlog = luaL_optinteger(L, 2, 0);

	/* a backlog of zero hints the implementation
	** to set the ideal connect queue size */
	if (listen(serv, backlog))
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);

	return 1;
}

static int api_bind(lua_State * L)
{
	size_t       sz   = 0;
	lsocket      serv = LSOCK_CHECKSOCK(L, 1);
	const char * addr = luaL_checklstring(L, 2, &sz);

	if (bind(serv, (struct sockaddr *) addr, sz))
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);

	return 1;
}

static int api_connect(lua_State * L)
{
	size_t       sz     = 0;
	lsocket      client = LSOCK_CHECKSOCK(L, 1);
	const char * addr   = luaL_checklstring(L, 2, &sz);

	if (connect(client, (struct sockaddr *) addr, sz))
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);

	return 1;
}

static int api_getsockname(lua_State * L)
{
	lsockaddr addr;

	lsocket    s = LSOCK_CHECKSOCK(L, 1);
	socklen_t sz = sizeof(addr);

	ZERO_OUT(&addr, sizeof(addr));

	if (getsockname(s, (struct sockaddr *) &addr, &sz))
		return LSOCK_STRERROR(L, NULL);

	lua_pushlstring(L, (char *) &addr, sz);

	return 1;
}

static int api_getpeername(lua_State * L)
{
	lsockaddr addr;

	lsocket    s = LSOCK_CHECKSOCK(L, 1);
	socklen_t sz = sizeof(addr);

	ZERO_OUT(&addr, sizeof(addr));

	if (getpeername(s, (struct sockaddr *) &addr, &sz))
		return LSOCK_STRERROR(L, NULL);

	lua_pushlstring(L, (char *) &addr, sz);

	return 1;
}

static int api_sendto(lua_State * L)
{
	ssize_t sent;

	int flags;

	size_t data_len = 0;
	const char * data = NULL;

	size_t sa_len;
	const char * sa;

	lsocket s = LSOCK_CHECKSOCK(L, 1);
	strij(L, 2, &data, &data_len);
	flags = luaL_checkint(L, 3);

	sa = luaL_optlstring(L, 4, "", &sa_len);

	sent = sendto(s, data, data_len, flags, (struct sockaddr *) &sa, sa_len);

	if (sent < 0)
		return LSOCK_STRERROR(L, NULL);

	lua_pushnumber(L, sent);

	return 1;
}

static int api_send(lua_State * L)
{
	int n = lua_gettop(L);

	/* the sock is already associated with a sockaddr (connect())
	** arg-shave-off anything after arg 3 to avoid confusing sendto() */
	if (n > 3)
		lua_pop(L, n - 3);

	return api_sendto(L);
}

static int api_recvfrom(lua_State * L)
{
	ssize_t gotten;

	const char * from     = NULL;
	socklen_t    from_len = 0;

	char * buf;
	luaL_Buffer B;

	lsocket s      = LSOCK_CHECKSOCK(L, 1);
	size_t  buflen = luaL_checkint  (L, 2);
	int     flags  = luaL_checkint  (L, 3);

	from = luaL_optlstring(L, 4, "", (size_t *) &from_len);

	buf = luaL_buffinitsize(L, &B, buflen);

	ZERO_OUT(buf, buflen); /* a must! */

	gotten = recvfrom(s, buf, buflen, flags, (struct sockaddr *) from, &from_len);

	if (gotten < 0)
		return LSOCK_STRERROR(L, NULL);

	luaL_pushresultsize(&B, gotten);

	return 1; /* success! */
}

static int api_recv(lua_State * L)
{
	int n = lua_gettop(L);

	/* the sock is already associated with a sockaddr (connect())
	** arg-shave-off anything after arg 3 to avoid confusing recvfrom() */
	if (n > 3)
		lua_pop(L, n - 3);

	return api_recvfrom(L);
}

static int api_shutdown(lua_State * L)
{
	lsocket sock = LSOCK_CHECKSOCK(L, 1);
	int     how  = luaL_checkint  (L, 2);

	if (shutdown(sock, how))
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);

	return 1;
}

static int api_socket(lua_State * L)
{
	luaL_Stream * stream;

	int domain   = luaL_checkint(L, 1),
		type     = luaL_checkint(L, 2),
		protocol = luaL_optint  (L, 3, 0);

	lsocket new_sock = socket(domain, type, protocol);

	if (INVALID_SOCKET == new_sock)
		return LSOCK_STRERROR(L, NULL);

	stream    = newfile(L);
	stream->f = sock_to_file(L, new_sock, NULL);

	return 1;
}

static int api_should_block(lua_State * L)
{
	lsocket s = LSOCK_CHECKSOCK(L, 1);
	int     b = lua_isnone(L, 2) ? 1 : lua_toboolean(L, 2);

#ifdef _WIN32
	if (SOCKET_ERROR == ioctlsocket(s, FIONBIO, (u_long *) &b))
		return LSOCK_STRERROR(L, "ioctlsocket()");
#else
	int flags = fcntl(s, F_GETFL);

	if (-1 == flags)
		return LSOCK_STRERROR(L, "fcntl(F_GETFL)");

	if (b)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;

	/* could be redundant (O_NONBLOCK might already be set) */
	flags = fcntl(s, F_SETFL, flags);

	if (-1 == flags)
		return LSOCK_STRERROR(L, "fcntl(F_SETFL)");

	flags = b ? 1 : 0;

	flags = ioctl(s, FIONBIO, &flags);

	if (-1 == flags)
		return LSOCK_STRERROR(L, "ioctl()");
#endif

	lua_pushboolean(L, 1); /* success, not the passed/read blocking state */

	return 1;
}

/* you can also use io.close()... */
static int api_close(lua_State * L)
{
	return close_stream(L);
}

static int sockopt_boolean(lua_State * L)
{
	lsocket s  = LSOCK_CHECKSOCK(L, 1);
	int level  =   luaL_checkint(L, 2);
	int option =   luaL_checkint(L, 3);
	int get    =      lua_isnone(L, 4);

	int value = 0;
	socklen_t sz = sizeof(value);

	if (get)
	{
		if (getsockopt(s, level, option, (char *) &value, &sz))
			return LSOCK_STRERROR(L, NULL);

		lua_pushboolean(L, value);
	}
	else
	{
		value = lua_isnumber(L, 4) ? lua_tointeger(L, 4) : lua_toboolean(L, 4);

		if (setsockopt(s, level, option, (char *) &value, sz))
			return LSOCK_STRERROR(L, NULL);
	}

	return get;
}

static int sockopt_integer(lua_State * L)
{
	lsocket s  = LSOCK_CHECKSOCK(L, 1);
	int level  =   luaL_checkint(L, 2);
	int option =   luaL_checkint(L, 3);
	int get    =      lua_isnone(L, 4);

	int value = 0;
	socklen_t sz = sizeof(value);

	if (get)
	{
		if (getsockopt(s, level, option, (char *) &value, &sz))
			return LSOCK_STRERROR(L, NULL);

		lua_pushnumber(L, value);
	}
	else
	{
		value = luaL_checkint(L, 4);

		if (setsockopt(s, level, option, (char *) &value, sz))
			return LSOCK_STRERROR(L, NULL);
	}

	return get;
}

static int sockopt_linger(lua_State * L)
{
	lsocket s  = LSOCK_CHECKSOCK(L, 1);
	int level  =   luaL_checkint(L, 2);
	int option =   luaL_checkint(L, 3);
	int get    =      lua_isnone(L, 4);

	if (get)
	{
		struct linger l;
		socklen_t sz = sizeof(l);

		ZERO_OUT(&l, sz);

		if (getsockopt(s, level, option, (char *) &l, &sz))
			return LSOCK_STRERROR(L, NULL);

		linger_to_table(L, &l);
	}
	else
	{
		struct linger * l = NULL;
		socklen_t sz = 0;

		luaL_checktype(L, 4, LUA_TTABLE);

		l  = table_to_linger(L, 4);
		sz = lua_rawlen(L, -1);

		if (setsockopt(s, level, option, (char *) l, sz))
			return LSOCK_STRERROR(L, NULL);
	}

	return get;
}

#ifndef _WIN32

static int sockopt_ifnam(lua_State * L)
{
	lsocket s  = LSOCK_CHECKSOCK(L, 1);
	int level  =   luaL_checkint(L, 2);
	int option =   luaL_checkint(L, 3);
	int get    =      lua_isnone(L, 4);

	if (get)
	{
		char buf[IFNAMSIZ + 1]; /* +1 for safety (this is like ~17 bytes anyway) */
		socklen_t sz = sizeof(buf);

		ZERO_OUT(buf, sizeof(buf));

		if (getsockopt(s, level, option, buf, &sz))
			return LSOCK_STRERROR(L, NULL);

		lua_pushlstring(L, buf, sz);
	}
	else
	{
		socklen_t sz = 0;

		const char * value = luaL_checklstring(L, 4, (size_t *) &sz);

		if (setsockopt(s, level, option, value, sz))
			return LSOCK_STRERROR(L, NULL);
	}

	return get;
}

#endif

enum { SOCKOPT_BOOLEAN, SOCKOPT_INTEGER, SOCKOPT_LINGER, SOCKOPT_INADDR, SOCKOPT_IN6ADDR, SOCKOPT_IFNAM };

static int option_to_handler(const int level, const int option)
{
	if (SOL_SOCKET == level)
	{
		switch (option)
		{
			case SO_ACCEPTCONN:
			case SO_BROADCAST:
			case SO_REUSEADDR:
			case SO_KEEPALIVE:
			case SO_OOBINLINE:
			case SO_DONTROUTE:
			case SO_DEBUG:
#ifndef _WIN32
			case SO_TIMESTAMP:
#endif
#ifdef __linux
			case SO_BSDCOMPAT:
			case SO_NO_CHECK:
			case SO_MARK:
#endif
				return SOCKOPT_BOOLEAN;

			case SO_RCVLOWAT:
			case SO_RCVTIMEO:
			case SO_SNDLOWAT:
			case SO_SNDTIMEO:
			case SO_SNDBUF:
			case SO_RCVBUF:
			case SO_ERROR:
			case SO_TYPE:
#ifdef __linux
			case SO_PRIORITY:
			case SO_PROTOCOL:
			case SO_RCVBUFFORCE:
			case SO_SNDBUFFORCE:
			case SO_PEEK_OFF:
			case SO_DOMAIN:
#endif
				return SOCKOPT_INTEGER;

			case SO_LINGER:
				return SOCKOPT_LINGER;

#ifdef __linux
			case SO_BINDTODEVICE:
				return SOCKOPT_IFNAM;
#endif
		}
	}
	else if (IPPROTO_TCP == level) /* SOL_TCP */
	{
		switch (option)
		{
			case TCP_MAXSEG:
#ifndef _WIN32
			case TCP_KEEPINTVL:
			case TCP_KEEPCNT:
#endif
#ifdef __linux
			case TCP_WINDOW_CLAMP:
			case TCP_DEFER_ACCEPT: /* NOT A BOOLEAN, # of seconds to wait for data before drop */
			case TCP_LINGER2:
			case TCP_KEEPIDLE:
			case TCP_SYNCNT:
#endif
				return SOCKOPT_INTEGER;

			case TCP_NODELAY:
#ifdef __linux
			case TCP_CORK:
#endif
				return SOCKOPT_BOOLEAN;

			/* maybe handle TCP_INFO at some point */
		}
	}
	else if (IPPROTO_UDP == level) /* SOL_UDP */
	{
		switch (option)
		{
#ifdef _WIN32
			case UDP_CHECKSUM_COVERAGE:
			case UDP_NOCHECKSUM:
#endif
#ifdef __linux
			case UDP_CORK:
#endif
				return SOCKOPT_BOOLEAN;
		}
	}
	else if (IPPROTO_IP == level) /* SOL_IP */
	{
		switch (option)
		{
			case IP_TTL:
			case IP_TOS:
			case IP_MULTICAST_TTL:
#ifndef _WIN32
			case IP_RECVTTL:
#endif
#ifdef __linux
			case IP_MTU_DISCOVER:
			case IP_RECVTOS:
#endif
				return SOCKOPT_INTEGER;

			case IP_HDRINCL:
			case IP_MULTICAST_LOOP:
#ifndef _WIN32
			case IP_RECVOPTS:
			case IP_RETOPTS:
#endif
#ifdef __linux
			case IP_PKTINFO:
			case IP_RECVERR:
			case IP_ROUTER_ALERT:
#endif
				return SOCKOPT_BOOLEAN;

			case IP_MULTICAST_IF:
				return SOCKOPT_INADDR;
		}
	}
	else if (IPPROTO_IPV6 == level) /* SOL_IPV6 */
	{
		switch (option)
		{

			case IPV6_MULTICAST_LOOP:
#ifndef _WIN32
#endif
#ifdef __linux
			case IPV6_ROUTER_ALERT:
			case IPV6_NEXTHOP:
			case IPV6_HOPLIMIT:
			case IPV6_DSTOPTS:
			case IPV6_HOPOPTS:
#endif
				return SOCKOPT_BOOLEAN;

			case IPV6_CHECKSUM:
			case IPV6_MULTICAST_HOPS:
			case IPV6_UNICAST_HOPS:
#ifdef __linux
			case IPV6_AUTHHDR:
			case IPV6_ADDRFORM:
			case IPV6_PKTINFO:
#endif
				return SOCKOPT_INTEGER;
		}
	}

	/* unknown level+option combo */
	return -1;
}

static int sockopt(lua_State * L)
{
	int level, option, get;

	LSOCK_CHECKSOCK(L, 1);

	level  = luaL_checkint(L, 2);
	option = luaL_checkint(L, 3);
	get    =    lua_isnone(L, 4);

	switch (option_to_handler(level, option))
	{
		case SOCKOPT_BOOLEAN: return sockopt_boolean(L);
		case SOCKOPT_INTEGER: return sockopt_integer(L);
		case SOCKOPT_LINGER:  return sockopt_linger(L);
#ifndef _WIN32
		case SOCKOPT_IFNAM:   return sockopt_ifnam(L);
#endif
	}

	/* unkown level+option */
	lua_pushnil(L);
	lua_pushfstring
	(
		L,
		"unknown level or option passed to %ssockopt(); you can force it with %ssockopt_boolean/integer()..",
		get ? "get" : "set",
		get ? "get" : "set"
	);

	return 2;
}

static int api_getsockopt(lua_State * L)
{
	int n = lua_gettop(L);

	if (n > 3)
		lua_pop(L, n - 3);

	return sockopt(L);
}

static int api_setsockopt(lua_State * L)
{
	luaL_checkany(L, 4);

	return sockopt(L);
}

static int api_getaddrinfo(lua_State * L)
{
	int ret;
	int i = 1;

	struct addrinfo hints, * info, * p;

	/* node and service both cannot be NULL, getaddrinfo() will spout EAI_NONAME */
	const char * nname = lua_isnil(L, 1) ? NULL : luaL_checkstring(L, 1);
	const char * sname = lua_isnil(L, 2) ? NULL : luaL_checkstring(L, 2);

	ZERO_OUT(&hints, sizeof(struct addrinfo));

	if (!lua_isnone(L, 3))
	{
		luaL_checktype(L, 3, LUA_TTABLE); /* getaddrinfo('www.google.com', 'http', { options }) */

		lua_getfield(L, 3, "ai_flags");

		if (!lua_isnil(L, -1))
			hints.ai_flags = lua_tointeger(L, -1);

		lua_pop(L, 1);

		lua_getfield(L, 3, "ai_family");

		if (!lua_isnil(L, -1))
			hints.ai_family = lua_tointeger(L, -1);

		lua_pop(L, 1);

		lua_getfield(L, 3, "ai_socktype");

		if (!lua_isnil(L, -1))
			hints.ai_socktype = lua_tointeger(L, -1);

		lua_pop(L, 1);

		lua_getfield(L, 3, "ai_protocol");

		if (!lua_isnil(L, -1))
			hints.ai_protocol = lua_tointeger(L, -1);

		lua_pop(L, 1);
	}

	info = NULL;

	ret = getaddrinfo(nname, sname, &hints, &info);

	if (0 != ret)
		return LSOCK_GAIERROR(L, ret);

	ret = 1; /* to reflect how many returns */

	lua_newtable(L);

	for (p = info; p != NULL; p = (i++, p->ai_next))
	{
		/* the sequence index in the outer table */
		lua_pushnumber(L, i);

		/* allocate for 7-at-most members */
		lua_createtable(L, 0, 7);

		PUSHFIELD(L, -1, integer, "ai_flags",    p->ai_flags   );
		PUSHFIELD(L, -1, integer, "ai_family",   p->ai_family  );
		PUSHFIELD(L, -1, integer, "ai_socktype", p->ai_socktype);
		PUSHFIELD(L, -1, integer, "ai_protocol", p->ai_protocol);
		PUSHFIELD(L, -1, integer, "ai_addrlen",  p->ai_addrlen );

		lua_pushlstring(L, (char *) p->ai_addr, p->ai_addrlen);
		lua_setfield(L, -2, "ai_addr");

		if (NULL != p->ai_canonname)
			PUSHFIELD(L, -1, string, "ai_canonname", p->ai_canonname);

		lua_settable(L, -3);
	}

	if (NULL != info->ai_canonname)
	{
		lua_pushstring(L, info->ai_canonname);
		ret++;
	}

	freeaddrinfo(info);

	return ret;
}

static int api_getnameinfo(lua_State * L)
{
	int stat;

	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

	size_t       sz    = 0;
	const char * sa    = luaL_checklstring(L, 1, &sz);
	int          flags = luaL_checkint(L, 2);

	ZERO_OUT(hbuf, NI_MAXHOST);
	ZERO_OUT(sbuf, NI_MAXSERV);

	stat = getnameinfo((struct sockaddr *) sa, sz, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), flags);

	if (0 != stat)
		return LSOCK_GAIERROR(L, stat);

	lua_pushstring(L, hbuf);
	lua_pushstring(L, sbuf);

	return 2;
}

enum { R, W, E };

static int api_select(lua_State * L)
{
	int x, y, stat;

	fd_set set[3];
	int highsock = 0;
	struct timeval * t = NULL;

	luaL_checktype(L, 1, LUA_TTABLE); /*  readfds */
	luaL_checktype(L, 2, LUA_TTABLE); /* writefds */
	luaL_checktype(L, 3, LUA_TTABLE); /* errorfds */

	if (!lua_isnoneornil(L, 4))
	{
		luaL_checktype(L, 4, LUA_TTABLE);
		t = table_to_timeval(L, 4);
	}

	ZERO_OUT(&set, sizeof(set));

	/* parse tables into fd_sets */
	for (x = 1; x <= 3; x++)
	{
		int how_many = luaL_len(L, x);

		for (y = 1; y <= how_many; y++)
		{
			int fd = -1;

			lua_pushnumber(L, y);
			lua_gettable(L, x);

			fd = LSOCK_CHECKFD(L, -1);

			highsock = MAX(highsock, fd);

			FD_SET(fd, &set[x - 1]);
		}
	}

	stat = select(highsock + 1, &set[R], &set[W], &set[E], t);

	if (-1 == stat)
		LSOCK_STRERROR(L, NULL);

	/* parse fd_sets back into tables */
	for (x = 1; x <= 3; x++)
	{
		int how_many = luaL_len(L, x);

		/* reuse stat for how many array elems we might have */
		lua_createtable(L, stat, 0);

		for (y = 1; y <= how_many; y++)
		{
			int fd;

			/* push the file handle (socket) userdata */
			lua_pushnumber(L, y);
			lua_gettable(L, x);

			fd = LSOCK_CHECKFD(L, -1);

			if (FD_ISSET(fd, &set[y - 1]))
			{
				lua_pushnumber(L, y); /* the numeric index */
				lua_pushvalue(L, -2); /* the file handle */
				lua_settable(L, -4);  /* the new outgoing table */
			}

			/* remove the file handle userdata */
			lua_pop(L, 1);
		}
	}

	/* returns the read, write, and exception tables */
	return 3;
}

static int api_unread_bytes(lua_State * L)
{
	lsocket s = LSOCK_CHECKSOCK(L, 1);

	size_t available = 0;

#ifdef _WIN32
	if (ioctlsocket(s, FIONREAD, (u_long *) &available))
		return LSOCK_STRERROR(L, "ioctlsocket()");
#else
	if (ioctl(s, FIONREAD, &available))
		return LSOCK_STRERROR(L, "ioctl()");
#endif

	lua_pushnumber(L, available);
	return 1;
}

static int api_pipe(lua_State * L)
{
	lsocket pair[2] = { -1, -1 };

	luaL_Stream * r = NULL;
	luaL_Stream * w = NULL;

#ifdef _WIN32
	if (0 == CreatePipe((PHANDLE) &pair[0], (PHANDLE) &pair[1], NULL, (DWORD) luaL_optnumber(L, 1, 0)))
		return lsock_error(L, GetLastError(), (char * (*)(int)) &strerror, "CreatePipe()");
#else
	if (pipe(pair))
		return LSOCK_STRERROR(L, NULL);
#endif

	r = newfile(L);
	r->f = fd_to_file(L, pair[0], "rb");

	w = newfile(L);
	w->f = fd_to_file(L, pair[1], "wb");

	return 2;
}


#ifndef _WIN32

/* FIXME: Windows -> connect 2 TCP sockets on random (local) port */

static int api_socketpair(lua_State * L)
{
	int pair[2] = { -1, -1 };

	luaL_Stream * one = NULL;
	luaL_Stream * two = NULL;

	int domain   = luaL_checknumber(L, 1),
		type     = luaL_checknumber(L, 2),
		protocol = luaL_checknumber(L, 3);

	if (socketpair(domain, type, protocol, pair))
		return LSOCK_STRERROR(L, NULL);

	one = newfile(L);
	one->f = fd_to_file(L, pair[0], NULL);

	two = newfile(L);
	two->f = fd_to_file(L, pair[1], NULL);

	return 2;
}

/* FIXME: Windows -> TransmitFile() */

static int api_sendfile(lua_State * L)
{
	ssize_t sent;

	int    out    =    LSOCK_CHECKFD(L, 1);
	int    in     =    LSOCK_CHECKFD(L, 2);
	off_t  offset =   luaL_optnumber(L, 3, 0);
#ifdef __linux
	size_t count  = luaL_checknumber(L, 4);
#endif
#ifdef __APPLE__
	off_t count = luaL_checknumber(L, 4);
#endif

#ifdef __linux
	sent = sendfile(out, in, lua_isnil(L, 3) ? NULL : &offset, count);
#endif
#ifdef __APPLE__
	/* Differences:
	** 1) out<->in reversed from Linux sendfile()
	** 2) NULL for the header/trailer structure (Mac OS X sendfile rocks!)
	** 3) 0 for flags (future expansion); otherwise EINVAL will be returned */
	sent = sendfile(in, out, offset, &count, NULL, 0);
#endif

	if (-1 == sent)
		return LSOCK_STRERROR(L, NULL);

#ifdef __APPLE__
	sent = count;
#endif

	lua_pushnumber(L, sent);

	return 1;
}

#endif

static int api_getfd(lua_State * L)
{
	lua_pushnumber(L, LSOCK_CHECKFD(L, 1));

	return 1;
}

#ifdef _WIN32

static int lsock_cleanup(lua_State * L)
{
	((void) L);

	WSACleanup();

	return 0;
}

static void lsock_startup(lua_State * L)
{
	WSADATA wsaData;

	int stat = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (stat != 0)
	{
		lsock_cleanup(L);
		luaL_error(L, "WSAStartup() failed [%d]: %s\n", stat, strerror(stat));
	}

	if (2 != LOBYTE(wsaData.wVersion) || 2 != HIBYTE(wsaData.wVersion))
	{
		lsock_cleanup(L);
		luaL_error(L, "Could not find a usable version of Winsock.dll");
	}
}

#endif

static luaL_Reg lsocklib[] =
{
#define REGISTER(x) { #x, api_##x }

	/* the portable API */
	REGISTER(accept),
	REGISTER(bind),
	REGISTER(should_block),
	REGISTER(close),
	REGISTER(connect),
	REGISTER(gai_strerror),
	REGISTER(getaddrinfo),
	REGISTER(getfd),
	REGISTER(getpeername),
	REGISTER(getnameinfo),
	REGISTER(getsockname),
	REGISTER(getsockopt),
	REGISTER(htons),
	REGISTER(ntohs),
	REGISTER(htonl),
	REGISTER(ntohl),
	REGISTER(listen),
	REGISTER(pack_sockaddr),
	REGISTER(pipe),
	REGISTER(recv),
	REGISTER(recvfrom),
	REGISTER(select),
	REGISTER(send),
	REGISTER(sendto),
	REGISTER(setsockopt),
	REGISTER(shutdown),
	REGISTER(socket),
	REGISTER(strerror),
	REGISTER(unpack_sockaddr),
	REGISTER(unread_bytes),

	/* Linux + Mac-specific API */
#ifndef _WIN32
	REGISTER(sendfile),
	REGISTER(socketpair),
#endif
	{ NULL, NULL }

#undef REGISTER
};

EXPOSE_SYMBOL int luaopen_lsock(lua_State * L)
{
#ifdef _WIN32
	lsock_startup(L);
#endif

	luaL_newlib(L, lsocklib);

	lua_newtable(L);

#define CONSTANT(C) PUSHFIELD(L, -1, integer, #C, C)

	/* portable constants (alphabetical) */
	CONSTANT(AF_APPLETALK);
	CONSTANT(AF_INET);
	CONSTANT(AF_INET6);
	CONSTANT(AF_IPX);
	CONSTANT(AF_UNSPEC);
	CONSTANT(AI_ADDRCONFIG);
	CONSTANT(AI_ALL);
	CONSTANT(AI_CANONNAME);
	CONSTANT(AI_NUMERICHOST);
	CONSTANT(AI_NUMERICSERV);
	CONSTANT(AI_PASSIVE);
	CONSTANT(AI_V4MAPPED);
	CONSTANT(EACCES);
	CONSTANT(EADDRINUSE);
	CONSTANT(EADDRNOTAVAIL);
	CONSTANT(EAFNOSUPPORT);
	CONSTANT(EAGAIN);
	CONSTANT(EAI_AGAIN);
	CONSTANT(EAI_BADFLAGS);
	CONSTANT(EAI_FAIL);
	CONSTANT(EAI_FAMILY);
	CONSTANT(EAI_MEMORY);
	CONSTANT(EAI_NODATA);
	CONSTANT(EAI_NONAME);
	CONSTANT(EAI_SERVICE);
	CONSTANT(EAI_SOCKTYPE);
	CONSTANT(EBADF);
	CONSTANT(ECONNABORTED);
	CONSTANT(EDESTADDRREQ);
	CONSTANT(EINTR);
	CONSTANT(EINVAL);
	CONSTANT(EIO);
	CONSTANT(EISDIR);
	CONSTANT(ELOOP);
	CONSTANT(EMFILE);
	CONSTANT(ENAMETOOLONG);
	CONSTANT(ENFILE);
	CONSTANT(ENOBUFS);
	CONSTANT(ENOENT);
	CONSTANT(ENOMEM);
	CONSTANT(ENOTCONN);
	CONSTANT(ENOTDIR);
	CONSTANT(ENOTSOCK);
	CONSTANT(EOPNOTSUPP);
	CONSTANT(EPROTO);
	CONSTANT(EPROTONOSUPPORT);
	CONSTANT(EPROTOTYPE);
	CONSTANT(EROFS);
	CONSTANT(EWOULDBLOCK);
	CONSTANT(IPPROTO_AH);
	CONSTANT(IPPROTO_EGP);
	CONSTANT(IPPROTO_ESP);
	CONSTANT(IPPROTO_FRAGMENT);
	CONSTANT(IPPROTO_ICMP);
	CONSTANT(IPPROTO_ICMPV6);
	CONSTANT(IPPROTO_IDP);
	CONSTANT(IPPROTO_IGMP);
	CONSTANT(IPPROTO_IP);
	CONSTANT(IPPROTO_IPV6);
	CONSTANT(IPPROTO_MAX);
	CONSTANT(IPPROTO_NONE);
	CONSTANT(IPPROTO_PIM);
	CONSTANT(IPPROTO_PUP);
	CONSTANT(IPPROTO_RAW);
	CONSTANT(IPPROTO_ROUTING);
	CONSTANT(IPPROTO_SCTP);
	CONSTANT(IPPROTO_TCP);
	CONSTANT(IPPROTO_UDP);
	CONSTANT(IPV6_CHECKSUM);
	CONSTANT(IPV6_HOPLIMIT);
	CONSTANT(IPV6_JOIN_GROUP);
	CONSTANT(IPV6_LEAVE_GROUP);
	CONSTANT(IPV6_MULTICAST_HOPS);
	CONSTANT(IPV6_MULTICAST_IF);
	CONSTANT(IPV6_MULTICAST_LOOP);
	CONSTANT(IPV6_PKTINFO);
	CONSTANT(IPV6_UNICAST_HOPS);
	CONSTANT(IPV6_V6ONLY);
	CONSTANT(IP_ADD_MEMBERSHIP);
	CONSTANT(IP_ADD_SOURCE_MEMBERSHIP);
	CONSTANT(IP_BLOCK_SOURCE);
	CONSTANT(IP_DROP_MEMBERSHIP);
	CONSTANT(IP_DROP_SOURCE_MEMBERSHIP);
	CONSTANT(IP_HDRINCL);
	CONSTANT(IP_MULTICAST_IF);
	CONSTANT(IP_MULTICAST_LOOP);
	CONSTANT(IP_MULTICAST_TTL);
	CONSTANT(IP_OPTIONS);
	CONSTANT(IP_PKTINFO);
	CONSTANT(IP_TOS);
	CONSTANT(IP_TTL);
	CONSTANT(IP_UNBLOCK_SOURCE);
	CONSTANT(MSG_CTRUNC);
	CONSTANT(MSG_DONTROUTE);
	CONSTANT(MSG_OOB);
	CONSTANT(MSG_PEEK);
	CONSTANT(MSG_TRUNC);
	CONSTANT(MSG_WAITALL);
	CONSTANT(NI_DGRAM);
	CONSTANT(NI_NAMEREQD);
	CONSTANT(NI_NOFQDN);
	CONSTANT(NI_NUMERICHOST);
	CONSTANT(NI_NUMERICSERV);
	CONSTANT(PF_APPLETALK);
	CONSTANT(PF_INET);
	CONSTANT(PF_INET6);
	CONSTANT(PF_IPX);
	CONSTANT(PF_UNSPEC);
	CONSTANT(SOCK_DGRAM);
	CONSTANT(SOCK_RAW);
	CONSTANT(SOCK_RDM);
	CONSTANT(SOCK_SEQPACKET);
	CONSTANT(SOCK_STREAM);
	CONSTANT(SOL_SOCKET);
	CONSTANT(SOMAXCONN);
	CONSTANT(SO_ACCEPTCONN);
	CONSTANT(SO_BROADCAST);
	CONSTANT(SO_DEBUG);
	CONSTANT(SO_DONTROUTE);
	CONSTANT(SO_ERROR);
	CONSTANT(SO_KEEPALIVE);
	CONSTANT(SO_LINGER);
	CONSTANT(SO_OOBINLINE);
	CONSTANT(SO_RCVBUF);
	CONSTANT(SO_RCVLOWAT);
	CONSTANT(SO_RCVTIMEO);
	CONSTANT(SO_REUSEADDR);
	CONSTANT(SO_SNDBUF);
	CONSTANT(SO_SNDLOWAT);
	CONSTANT(SO_SNDTIMEO);
	CONSTANT(SO_TYPE);
	CONSTANT(TCP_MAXSEG);
	CONSTANT(TCP_NODELAY);

/* Windows-only */
#ifdef _WIN32
	CONSTANT(SD_BOTH);
	CONSTANT(SD_RECEIVE);
	CONSTANT(SD_SEND);
	CONSTANT(UDP_CHECKSUM_COVERAGE);
	CONSTANT(UDP_NOCHECKSUM);
#endif

/* Linux-only */
#ifdef __linux
	CONSTANT(AF_IRDA);
	CONSTANT(AI_CANONIDN);
	CONSTANT(AI_IDN);
	CONSTANT(AI_IDN_ALLOW_UNASSIGNED);
	CONSTANT(AI_IDN_USE_STD3_ASCII_RULES);
	CONSTANT(IPPROTO_COMP);
	CONSTANT(IPPROTO_DCCP);
	CONSTANT(IPPROTO_DSTOPTS);
	CONSTANT(IPPROTO_HOPOPTS);
	CONSTANT(IPPROTO_UDPLITE);
	CONSTANT(IPV6_ADDRFORM);
	CONSTANT(IPV6_AUTHHDR);
	CONSTANT(IPV6_DSTOPTS);
	CONSTANT(IPV6_HOPOPTS);
	CONSTANT(IPV6_NEXTHOP);
	CONSTANT(IPV6_ROUTER_ALERT);
	CONSTANT(IP_MTU_DISCOVER);
	CONSTANT(IP_RECVERR);
	CONSTANT(IP_RECVTOS);
	CONSTANT(IP_ROUTER_ALERT);
	CONSTANT(MSG_CONFIRM);
	CONSTANT(MSG_ERRQUEUE);
	CONSTANT(MSG_FASTOPEN);
	CONSTANT(MSG_FIN);
	CONSTANT(MSG_MORE);
	CONSTANT(MSG_NOSIGNAL);
	CONSTANT(MSG_PROXY);
	CONSTANT(MSG_RST);
	CONSTANT(MSG_SYN);
	CONSTANT(MSG_TRYHARD);
	CONSTANT(MSG_WAITFORONE);
	CONSTANT(NI_IDN);
	CONSTANT(NI_IDN_ALLOW_UNASSIGNED);
	CONSTANT(NI_IDN_USE_STD3_ASCII_RULES);
	CONSTANT(PF_IRDA);
	CONSTANT(SOL_IP);
	CONSTANT(SOL_IPV6);
	CONSTANT(SOL_TCP);
	CONSTANT(SOL_UDP);
	CONSTANT(SO_BINDTODEVICE);
	CONSTANT(SO_BSDCOMPAT);
	CONSTANT(SO_DOMAIN);
	CONSTANT(SO_MARK);
	CONSTANT(SO_NO_CHECK);
	CONSTANT(SO_PEEK_OFF);
	CONSTANT(SO_PRIORITY);
	CONSTANT(SO_PROTOCOL);
	CONSTANT(SO_RCVBUFFORCE);
	CONSTANT(SO_SNDBUFFORCE);
	CONSTANT(TCP_CORK);
	CONSTANT(TCP_DEFER_ACCEPT);
	CONSTANT(TCP_KEEPIDLE);
	CONSTANT(TCP_LINGER2);
	CONSTANT(TCP_SYNCNT);
	CONSTANT(TCP_WINDOW_CLAMP);
	CONSTANT(UDP_CORK);
#endif

/* Linux + Mac shared */
#ifndef _WIN32
	CONSTANT(EAI_ADDRFAMILY);
	CONSTANT(EAI_OVERFLOW);
	CONSTANT(EAI_SYSTEM);
	CONSTANT(IPPROTO_ENCAP);
	CONSTANT(IPPROTO_GRE);
	CONSTANT(IPPROTO_IPIP);
	CONSTANT(IPPROTO_MTP);
	CONSTANT(IPPROTO_RSVP);
	CONSTANT(IPPROTO_TP);
	CONSTANT(IP_RECVOPTS);
	CONSTANT(IP_RETOPTS);
	CONSTANT(MSG_DONTWAIT);
	CONSTANT(MSG_EOR);
	CONSTANT(PF_LOCAL);
	CONSTANT(SHUT_RD);
	CONSTANT(SHUT_RDWR);
	CONSTANT(SHUT_WR);
	CONSTANT(SO_TIMESTAMP);
	CONSTANT(TCP_KEEPCNT);
	CONSTANT(TCP_KEEPINTVL);
#endif

#undef CONSTANT

	PUSHFIELD(L, -1, literal, "INADDR_ANY",             "0.0.0.0"        );
	PUSHFIELD(L, -1, literal, "INADDR_BROADCAST",       "255.255.255.255");
	PUSHFIELD(L, -1, literal, "INADDR_NONE",            "255.255.255.255");
	PUSHFIELD(L, -1, literal, "INADDR_LOOPBACK",        "127.0.0.1"      );
	PUSHFIELD(L, -1, literal, "INADDR_UNSPEC_GROUP",    "224.0.0.0"      );
	PUSHFIELD(L, -1, literal, "INADDR_ALLHOSTS_GROUP",  "224.0.0.1"      );
	PUSHFIELD(L, -1, literal, "INADDR_ALLRTRS_GROUP",   "224.0.0.2"      );
	PUSHFIELD(L, -1, literal, "INADDR_MAX_LOCAL_GROUP", "224.0.0.255"    );

	PUSHFIELD(L, -1, literal, "in6addr_any",      "::" );
	PUSHFIELD(L, -1, literal, "in6addr_loopback", "::1");

	/* the _INIT's are just tables to pass to pack_sockaddr */
	lua_createtable(L, 0, 2);
	PUSHFIELD(L, -1, integer, "sin6_family", AF_INET6);
	PUSHFIELD(L, -1, literal, "sin6_addr", "::");
	lua_setfield(L, -2, "IN6ADDR_ANY_INIT");

	lua_createtable(L, 0, 2);
	PUSHFIELD(L, -1, integer, "sin6_family", AF_INET6);
	PUSHFIELD(L, -1, literal, "sin6_addr", "::1");
	lua_setfield(L, -2, "IN6ADDR_LOOPBACK_INIT");

	PUSHFIELD(L, -1, integer, "INET_ADDRSTRLEN",  16);
	PUSHFIELD(L, -1, integer, "INET6_ADDRSTRLEN", 46);

	/* table of constants is reachable under 2 names */
	lua_pushvalue(L, -1);
	lua_setfield(L, -3, "C");
	lua_setfield(L, -2, "constants");

#ifdef _WIN32
	lua_pushcfunction(L, &lsock_cleanup);
	lua_setfield(L, -2, "__gc");
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2); /* it is its own metatable */
#endif

	return 1;
}

