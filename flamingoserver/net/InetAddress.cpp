#include "InetAddress.h"
#include <string.h>
#include "../base/AsyncLog.h"
#include "Endian.h"
#include "Sockets.h"

// INADDR_ANY use (type)value casting.
//static const in_addr_t kInaddrAny = INADDR_ANY;
//static const in_addr_t kInaddrLoopback = INADDR_LOOPBACK;

static const in_addr_t kInaddrAny = INADDR_ANY;
static const in_addr_t kInaddrLoopback = INADDR_LOOPBACK;

//     /* Structure describing an Internet socket address.  */
//     struct sockaddr_in {
//         sa_family_t    sin_family; /* address family: AF_INET */
//         uint16_t       sin_port;   /* port in network byte order */
//         struct in_addr sin_addr;   /* internet address */
//     };

//     /* Internet address. */
//     typedef uint32_t in_addr_t;
//     struct in_addr {
//         in_addr_t       s_addr;     /* address in network byte order */
//     };

using namespace net;

//static_assert(sizeof(InetAddress) == sizeof(struct sockaddr_in), "sizeof(InetAddress) == sizeof(struct sockaddr_in)");

InetAddress::InetAddress(uint16_t port, bool loopbackOnly/* = false*/)
{
	memset(&addr_, 0, sizeof addr_);
	addr_.sin_family = AF_INET;
	in_addr_t ip = loopbackOnly ? kInaddrLoopback : kInaddrAny;
	addr_.sin_addr.s_addr = sockets::hostToNetwork32(ip);
	addr_.sin_port = sockets::hostToNetwork16(port);
}

InetAddress::InetAddress(const std::string& ip, uint16_t port)
{
    memset(&addr_, 0, sizeof addr_);
	sockets::fromIpPort(ip.c_str(), port, &addr_);
}

std::string InetAddress::toIpPort() const
{
	char buf[32];
	sockets::toIpPort(buf, sizeof buf, addr_);
	return buf;
}

std::string InetAddress::toIp() const
{
	char buf[32];
	sockets::toIp(buf, sizeof buf, addr_);
	return buf;
}

uint16_t InetAddress::toPort() const
{
	return sockets::networkToHost16(addr_.sin_port);
}

static thread_local char t_resolveBuffer[64 * 1024];

bool InetAddress::resolve(const std::string& hostname, InetAddress* out)
{
	//assert(out != NULL);
	struct hostent hent;
	struct hostent* he = NULL;
	int herrno = 0;
	memset(&hent, 0, sizeof(hent));

#ifndef WIN32
	int ret = gethostbyname_r(hostname.c_str(), &hent, t_resolveBuffer, sizeof t_resolveBuffer, &he, &herrno);
	if (ret == 0 && he != NULL)
	{
		//assert(he->h_addrtype == AF_INET && he->h_length == sizeof(uint32_t));
		out->addr_.sin_addr = *reinterpret_cast<struct in_addr*>(he->h_addr);
		return true;
	}

	if (ret)
	{
		LOGSYSE("InetAddress::resolve");
	}

#endif
    //TODO: Windows上重新实现一下
	return false;
}
