/**
 *  一个强大的协议类, protocolstream.h
 *  zhangyl 2017.05.27
 */

#ifndef __PROTOCOL_STREAM_H__
#define __PROTOCOL_STREAM_H__

#include <stdlib.h>
#include <sys/types.h>
#include <string>
#include <sstream>
#include <stdint.h>

 //二进制协议的打包解包类，内部的服务器之间通讯，统一采用这些类
namespace net
{
    enum
    {
        TEXT_PACKLEN_LEN = 4,
        TEXT_PACKAGE_MAXLEN = 0xffff,
        BINARY_PACKLEN_LEN = 2,
        BINARY_PACKAGE_MAXLEN = 0xffff,

        TEXT_PACKLEN_LEN_2 = 6,
        TEXT_PACKAGE_MAXLEN_2 = 0xffffff,

        BINARY_PACKLEN_LEN_2 = 4,               //4字节头长度
        BINARY_PACKAGE_MAXLEN_2 = 0x10000000,   //包最大长度是256M,足够了

        CHECKSUM_LEN = 2,
    };

    //计算校验和
    unsigned short checksum(const unsigned short* buffer, int size);
    //将一个4字节的整型数值压缩成1~5个字节
    void write7BitEncoded(uint32_t value, std::string& buf);
    //将一个8字节的整型值编码成1~10个字节
    void write7BitEncoded(uint64_t value, std::string& buf);

    //将一个1~5个字节的字符数组值还原成4字节的整型值
    void read7BitEncoded(const char* buf, uint32_t len, uint32_t& value);
    //将一个1~10个字节的值还原成4字节的整型值
    void read7BitEncoded(const char* buf, uint32_t len, uint64_t& value);

    class BinaryStreamReader final
    {
    public:
        BinaryStreamReader(const char* ptr, size_t len);
        ~BinaryStreamReader() = default;

        virtual const char* GetData() const;
        virtual size_t GetSize() const;
        bool IsEmpty() const;
        bool ReadString(std::string* str, size_t maxlen, size_t& outlen);
        bool ReadCString(char* str, size_t strlen, size_t& len);
        bool ReadCCString(const char** str, size_t maxlen, size_t& outlen);
        bool ReadInt32(int32_t& i);
        bool ReadInt64(int64_t& i);
        bool ReadShort(short& i);
        bool ReadChar(char& c);
        size_t ReadAll(char* szBuffer, size_t iLen) const;
        bool IsEnd() const;
        const char* GetCurrent() const { return cur; }

    public:
        bool ReadLength(size_t& len);
        bool ReadLengthWithoutOffset(size_t& headlen, size_t& outlen);

    private:
        BinaryStreamReader(const BinaryStreamReader&) = delete;
        BinaryStreamReader& operator=(const BinaryStreamReader&) = delete;

    private:
        const char* const ptr;
        const size_t      len;
        const char* cur;
    };

    class BinaryStreamWriter final
    {
    public:
        BinaryStreamWriter(std::string* data);
        ~BinaryStreamWriter() = default;

        virtual const char* GetData() const;
        virtual size_t GetSize() const;
        bool WriteCString(const char* str, size_t len);
        bool WriteString(const std::string& str);
        bool WriteDouble(double value, bool isNULL = false);
        bool WriteInt64(int64_t value, bool isNULL = false);
        bool WriteInt32(int32_t i, bool isNULL = false);
        bool WriteShort(short i, bool isNULL = false);
        bool WriteChar(char c, bool isNULL = false);
        size_t GetCurrentPos() const { return m_data->length(); }
        void Flush();
        void Clear();

    private:
        BinaryStreamWriter(const BinaryStreamWriter&) = delete;
        BinaryStreamWriter& operator=(const BinaryStreamWriter&) = delete;

    private:
        std::string* m_data;
    };

}// end namespace

#endif //!__PROTOCOL_STREAM_H__