#pragma once

#include <string>
#include "../base/Platform.h"

namespace net
{
    class InetAddress
    {
    public:
        explicit InetAddress(uint16_t port = 0, bool loopbackOnly = false);

        /// @param ip should be "1.2.3.4"
        InetAddress(const std::string& ip, uint16_t port);

        InetAddress(const struct sockaddr_in& addr)
            : m_addr(addr)
        { }

        std::string toIp() const;
        std::string toIpPort() const;
        uint16_t toPort() const;

        const struct sockaddr_in& getSockAddrInet() const { return m_addr; }
        void setSockAddrInet(const struct sockaddr_in& addr) { m_addr = addr; }

        uint32_t ipNetEndian() const { return m_addr.sin_addr.s_addr; }
        uint16_t portNetEndian() const { return m_addr.sin_port; }

        static bool resolve(const std::string& hostname, InetAddress* result);

    private:
        struct sockaddr_in      m_addr;

    };

}
