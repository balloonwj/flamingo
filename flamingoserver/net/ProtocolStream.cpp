#ifndef _WIN32
#include <arpa/inet.h>
#else
#include <Winsock2.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

#include "ProtocolStream.h"
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <cassert>
#include <algorithm>
#include <stdio.h>

using namespace std;

namespace net
{
    //计算校验和
    unsigned short checksum(const unsigned short *buffer, int size)
    {
        unsigned int cksum = 0;
        while (size > 1)
        {
            cksum += *buffer++;
            size -= sizeof(unsigned short);
        }
        if (size)
        {
            cksum += *(unsigned char*)buffer;
        }
        //将32位数转换成16
        while (cksum >> 16)
            cksum = (cksum >> 16) + (cksum & 0xffff);

        return (unsigned short)(~cksum);
    }

    //将一个四字节的整形数值压缩成1~5个字节
    bool compress_(unsigned int i, char *buf, size_t &len)
    {
        len = 0;
        for (int a = 4; a >= 0; a--)
        {
            char c;
            c = i >> (a * 7) & 0x7f;
            if (c == 0x00 && len == 0)
                continue;

            if (a == 0)
                c &= 0x7f;
            else
                c |= 0x80;
            buf[len] = c;
            len++;
        }
        if (len == 0)
        {
            len++;
            buf[0] = 0;
        }

        //cout << "compress:" << i << endl;
        //cout << "compress len:" << len << endl;
        return true;
    }

    //将一个1~5个字节的值还原成四字节的整形值
    bool uncompress_(char *buf, size_t len, unsigned int &i)
    {
        i = 0;
        for (int index = 0; index < (int)len; index++)
        {
            char c = *(buf + index);
            i = i << 7;

            c &= 0x7f;
            i |= c;
        }
        //cout << "uncompress:" << i << endl;
        return true;
    }

    BinaryReadStream::BinaryReadStream(const char* ptr_, size_t len_)
        : ptr(ptr_), len(len_), cur(ptr_)
    {
        cur += BINARY_PACKLEN_LEN_2 + CHECKSUM_LEN;
    }
    bool BinaryReadStream::IsEmpty() const
    {
        return len <= BINARY_PACKLEN_LEN_2;
    }
    size_t BinaryReadStream::GetSize() const
    {
        return len;
    }
    bool BinaryReadStream::ReadCString(char* str, size_t strlen, /* out */ size_t& outlen)
    {
        size_t fieldlen;
        size_t headlen;
        if (!ReadLengthWithoutOffset(headlen, fieldlen)) {
            return false;
        }

        // user buffer is not enough
        if (fieldlen > strlen) {
            return false;
        }

        // 偏移到数据的位置
        //cur += BINARY_PACKLEN_LEN_2;	
        cur += headlen;
        if (cur + fieldlen > ptr + len)
        {
            outlen = 0;
            return false;
        }
        memcpy(str, cur, fieldlen);
        outlen = fieldlen;
        cur += outlen;
        return true;
    }
    bool BinaryReadStream::ReadString(string* str, size_t maxlen, size_t& outlen)
    {
        size_t headlen;
        size_t fieldlen;
        if (!ReadLengthWithoutOffset(headlen, fieldlen)) {
            return false;
        }

        // user buffer is not enough
        if (maxlen != 0 && fieldlen > maxlen) {
            return false;
        }

        // 偏移到数据的位置
        //cur += BINARY_PACKLEN_LEN_2;	
        cur += headlen;
        if (cur + fieldlen > ptr + len)
        {
            outlen = 0;
            return false;
        }
        str->assign(cur, fieldlen);
        outlen = fieldlen;
        cur += outlen;
        return true;
    }
    bool BinaryReadStream::ReadCCString(const char** str, size_t maxlen, size_t& outlen)
    {
        size_t headlen;
        size_t fieldlen;
        if (!ReadLengthWithoutOffset(headlen, fieldlen)) {
            return false;
        }
        // user buffer is not enough
        if (maxlen != 0 && fieldlen > maxlen) {
            return false;
        }

        // 偏移到数据的位置
        //cur += BINARY_PACKLEN_LEN_2;	
        cur += headlen;

        //memcpy(str, cur, fieldlen);
        if (cur + fieldlen > ptr + len)
        {
            outlen = 0;
            return false;
        }
        *str = cur;
        outlen = fieldlen;
        cur += outlen;
        return true;
    }
    bool BinaryReadStream::ReadInt32(int32_t& i)
    {
        const int VALUE_SIZE = sizeof(int32_t);

        if (cur + VALUE_SIZE > ptr + len)
            return false;

        memcpy(&i, cur, VALUE_SIZE);
        i = ntohl(i);

        cur += VALUE_SIZE;

        return true;
    }
    bool BinaryReadStream::ReadInt64(int64_t& i)
    {
        char int64str[128];
        size_t length;
        if (!ReadCString(int64str, 128, length))
            return false;

        i = atoll(int64str);

        return true;
    }
    bool BinaryReadStream::ReadShort(short& i)
    {
        const int VALUE_SIZE = sizeof(short);

        if (cur + VALUE_SIZE > ptr + len) {
            return false;
        }

        memcpy(&i, cur, VALUE_SIZE);
        i = ntohs(i);

        cur += VALUE_SIZE;

        return true;
    }
    bool BinaryReadStream::ReadChar(char& c)
    {
        const int VALUE_SIZE = sizeof(char);

        if (cur + VALUE_SIZE > ptr + len) {
            return false;
        }

        memcpy(&c, cur, VALUE_SIZE);
        cur += VALUE_SIZE;

        return true;
    }
    bool BinaryReadStream::ReadLength(size_t & outlen)
    {
        size_t headlen;
        if (!ReadLengthWithoutOffset(headlen, outlen)) {
            return false;
        }

        //cur += BINARY_PACKLEN_LEN_2;
        cur += headlen;
        return true;
    }
    bool BinaryReadStream::ReadLengthWithoutOffset(size_t& headlen, size_t & outlen)
    {
        headlen = 0;
        const char *temp = cur;
        char buf[5];
        for (size_t i = 0; i<sizeof(buf); i++)
        {
            memcpy(buf + i, temp, sizeof(char));
            temp++;
            headlen++;

            //if ((buf[i] >> 7 | 0x0) == 0x0)
            if ((buf[i] & 0x80) == 0x00)
                break;
        }
        if (cur + headlen > ptr + len)
            return false;

        unsigned int value;
        uncompress_(buf, headlen, value);
        outlen = value;

        /*if ( cur + BINARY_PACKLEN_LEN_2 > ptr + len ) {
        return false;
        }

        unsigned int tmp;
        memcpy(&tmp, cur, sizeof(tmp));
        outlen = ntohl(tmp);*/
        return true;
    }
    bool BinaryReadStream::IsEnd() const
    {
        assert(cur <= ptr + len);
        return cur == ptr + len;
    }
    const char* BinaryReadStream::GetData() const
    {
        return ptr;
    }
    size_t BinaryReadStream::ReadAll(char * szBuffer, size_t iLen) const
    {
        size_t iRealLen = min(iLen, len);
        memcpy(szBuffer, ptr, iRealLen);
        return iRealLen;
    }

    //=================class BinaryWriteStream implementation============//
    BinaryWriteStream::BinaryWriteStream(string *data) :
        m_data(data)
    {
        m_data->clear();
        char str[BINARY_PACKLEN_LEN_2 + CHECKSUM_LEN];
        m_data->append(str, sizeof(str));
    }
    bool BinaryWriteStream::WriteCString(const char* str, size_t len)
    {
        char buf[5];
        size_t buflen;
        compress_(len, buf, buflen);
        m_data->append(buf, sizeof(char)*buflen);

        m_data->append(str, len);

        //unsigned int ulen = htonl(len);
        //m_data->append((char*)&ulen,sizeof(ulen));
        //m_data->append(str,len);
        return true;
    }
    bool BinaryWriteStream::WriteString(const string& str)
    {
        return WriteCString(str.c_str(), str.length());
    }
    const char* BinaryWriteStream::GetData() const
    {
        return m_data->data();
    }
    size_t BinaryWriteStream::GetSize() const
    {
        return m_data->length();
    }
    bool BinaryWriteStream::WriteInt32(int32_t i, bool isNULL)
    {
        int32_t i2 = 999999999;
        if (isNULL == false)
            i2 = htonl(i);
        m_data->append((char*)&i2, sizeof(i2));
        return true;
    }
    bool BinaryWriteStream::WriteInt64(int64_t value, bool isNULL)
    {
        char int64str[128];
        if (isNULL == false)
        {
#ifndef _WIN32
            sprintf(int64str, "%ld", value);
#else
            sprintf(int64str, "%lld", value);
#endif
            WriteCString(int64str, strlen(int64str));
        }
        else
            WriteCString(int64str, 0);
        return true;
    }
    bool BinaryWriteStream::WriteShort(short i, bool isNULL)
    {
        short i2 = 0;
        if (isNULL == false)
            i2 = htons(i);
        m_data->append((char*)&i2, sizeof(i2));
        return true;
    }
    bool BinaryWriteStream::WriteChar(char c, bool isNULL)
    {
        char c2 = 0;
        if (isNULL == false)
            c2 = c;
        (*m_data) += c2;
        return true;
    }
    bool BinaryWriteStream::WriteDouble(double value, bool isNULL)
    {
        char   doublestr[128];
        if (isNULL == false)
        {
            sprintf(doublestr, "%f", value);
            WriteCString(doublestr, strlen(doublestr));
        }
        else
            WriteCString(doublestr, 0);
        return true;
    }
    void BinaryWriteStream::Flush()
    {
        char *ptr = &(*m_data)[0];
        unsigned int ulen = htonl(m_data->length());
        memcpy(ptr, &ulen, sizeof(ulen));
    }
    void BinaryWriteStream::Clear()
    {
        m_data->clear();
        char str[BINARY_PACKLEN_LEN_2 + CHECKSUM_LEN];
        m_data->append(str, sizeof(str));
    }
}// end namespace

