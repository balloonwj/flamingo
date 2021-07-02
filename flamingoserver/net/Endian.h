#pragma once

namespace net
{
	namespace sockets
	{
		inline uint64_t hostToNetwork64(uint64_t host64)
		{
#ifdef WIN32
            return htonll(host64);
#else
			return htobe64(host64);
#endif
		}

		inline uint32_t hostToNetwork32(uint32_t host32)
		{
#ifdef WIN32
            return htonl(host32);
#else
            return htobe32(host32);
#endif
		}

		inline uint16_t hostToNetwork16(uint16_t host16)
		{
#ifdef WIN32		
			return htons(host16);
#else
            return htobe16(host16);
#endif
		}

		inline uint64_t networkToHost64(uint64_t net64)
		{
#ifdef WIN32
            return ntohll(net64);
#else
			return be64toh(net64);
#endif
		}

		inline uint32_t networkToHost32(uint32_t net32)
		{
#ifdef WIN32
            return ntohl(net32);
#else
			return be32toh(net32);
#endif
		}

		inline uint16_t networkToHost16(uint16_t net16)
		{
#ifdef WIN32
			return ntohs(net16);
#else
            return be16toh(net16);
#endif
		}
	}// end namespace sockets
}// end namespace net
