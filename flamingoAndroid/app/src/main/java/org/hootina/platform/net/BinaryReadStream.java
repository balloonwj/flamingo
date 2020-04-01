package org.hootina.platform.net;

import java.io.UnsupportedEncodingException;

/**
 *@desc    二进制协议解码类，解析C++结构体
 *@author  zhangyl
 *@date    2017.08.18
 *@version 1.0
 */

public class BinaryReadStream {
    private byte[] _data;
    //当前数据指针
    private int	   _cur;

    public static int intToBigEndian(int n)
    {
        byte[] b = new byte[4];
        b[0] = (byte) (n & 0xff);
        b[1] = (byte) (n >> 8 & 0xff);
        b[2] = (byte) (n >> 16 & 0xff);
        b[3] = (byte) (n >> 24 & 0xff);

        return   b[3] & 0xFF |
                (b[2] & 0xFF) << 8 |
                (b[1] & 0xFF) << 16 |
                (b[0] & 0xFF) << 24;
        //int k = (int)(b[0] << 24) + (int)(b[1] << 16) + (int)(b[2] << 16) + (int)b[3];
        //return k;
    }

    public static long longToBigEndian(long n)
    {
        byte[] b = new byte[8];
        b[0] = (byte) (n & 0xff);
        b[1] = (byte) (n >> 8 & 0xff);
        b[2] = (byte) (n >> 16 & 0xff);
        b[3] = (byte) (n >> 24 & 0xff);
        b[4] = (byte) (n >> 32 & 0xff);
        b[5] = (byte) (n >> 40 & 0xff);
        b[6] = (byte) (n >> 48 & 0xff);
        b[7] = (byte) (n >> 56 & 0xff);

        return   b[7] & 0xFF |
                (b[6] & 0xFF) << 8 |
                (b[5] & 0xFF) << 16 |
                (b[4] & 0xFF) << 24 |
                (b[3] & 0xFF) << 32 |
                (b[2] & 0xFF) << 40 |
                (b[1] & 0xFF) << 48 |
                (b[0] & 0xFF) << 56;
        //int k = (int)(b[0] << 24) + (int)(b[1] << 16) + (int)(b[2] << 16) + (int)b[3];
        //return k;
    }

    BinaryReadStream(byte[] data) {
        //cur += BINARY_PACKLEN_LEN_2 + CHECKSUM_LEN;
        //前四个字节是流的长度，第五个和第六个字节是流的校验和
        _data = data;
        _cur = 6;
    }


    int uncompressInteger(byte[] buf, int nValidLength) {
        int i = 0;
        for (int index = 0; index < nValidLength; index++)
        {
            byte c = buf[index];
            i = i << 7;

            c &= 0x7f;
            i |= c;
        }

        return i;
    }

    int read7BitEncoded(byte[] buf, int nValidLength)
    {
        byte c;
        int value = 0;
        int bitCount = 0;
        int index = 0;
        do
        {
            c = buf[index];
            int x = (c & 0x7F);
            x <<= bitCount;
            value += x;
            bitCount += 7;
            ++index;
        } while ((c & 0x80) != 0);

        return value;
    }

    Boolean isEmpty() {
        return _data.length < 6 ? true : false;
    }

    int getSize() {
        if (_data.length < 6)
            return 0;
        else
            return _data.length - 6;
    }

//    bool BinaryReadStream::readString(char* str, size_t strlen, /* out */ size_t& outlen)
//    {
//        size_t fieldlen;
//        size_t headlen;
//        if (!ReadLengthWithoutOffset(headlen, fieldlen)) {
//            return false;
//        }
//
//        // user buffer is not enough
//        if (fieldlen > strlen) {
//            return false;
//        }
//
//        // 偏移到数据的位置
//        //cur += BINARY_PACKLEN_LEN_2;
//        cur += headlen;
//        if (cur + fieldlen > ptr + len)
//        {
//            outlen = 0;
//            return false;
//        }
//        memcpy(str, cur, fieldlen);
//        outlen = fieldlen;
//        cur += outlen;
//        return true;
//    }

    String readString() {
        int headlen = readLengthWithoutOffset();
        if (headlen == -1)
            return "";

        // user buffer is not enough
        if (_cur + headlen > _data.length)
            return "";

        byte[] bytes = new byte[headlen];
        for (int i = 0; i < headlen; ++i)
        {
            bytes[i] = _data[_cur + i];
        }

        _cur += headlen;

        String res = "";
        try {
            res = new String(bytes, "UTF-8");
        } catch (UnsupportedEncodingException e) {

        }

        return res;
    }

//    bool BinaryReadStream::ReadCCString(const char** str, size_t maxlen, size_t& outlen)
//    {
//        size_t headlen;
//        size_t fieldlen;
//        if (!ReadLengthWithoutOffset(headlen, fieldlen)) {
//            return false;
//        }
//        // user buffer is not enough
//        if (maxlen != 0 && fieldlen > maxlen) {
//            return false;
//        }
//
//        // 偏移到数据的位置
//        //cur += BINARY_PACKLEN_LEN_2;
//        cur += headlen;
//
//        //memcpy(str, cur, fieldlen);
//        if (cur + fieldlen > ptr + len)
//        {
//            outlen = 0;
//            return false;
//        }
//        *str = cur;
//        outlen = fieldlen;
//        cur += outlen;
//        return true;
//    }

    int readInt32() {
        //返回最大值表示出错
        if (_cur + 4 > _data.length)
            return Integer.MAX_VALUE;

        byte[] b = new byte[4];
        b[3] = _data[_cur + 3];
        b[2] = _data[_cur + 2];
        b[1] = _data[_cur + 1];
        b[0] = _data[_cur];

        //little endian最低位(b[0])权重最高
        int i =  b[3] & 0xFF |
                (b[2] & 0xFF) << 8 |
                (b[1] & 0xFF) << 16 |
                (b[0] & 0xFF) << 24;

        _cur += 4;

        //i = (int)(_data[_cur + 3] << 24)  + (int)(_data[_cur+2] << 16) + (int)(_data[_cur+1] << 8) + (int)(_data[_cur]);
        return i;
    }

    long readInt64() {
        //int64就是以String格式存储的，直接读取出来转成String就可以了。
        String int64String = readString();
        //返回最大值表示失败
        if (int64String == null || int64String.isEmpty())
            return Long.MAX_VALUE;

        long l;

        try {
            l = Long.parseLong(int64String);
        } catch (NumberFormatException e) {
            l = Long.MAX_VALUE;
        }

        return l;
    }


//    bool BinaryReadStream::ReadInt64(int64_t& i)
//    {
//        char int64str[128];
//        size_t length;
//        if (!ReadCString(int64str, 128, length))
//            return false;
//
//        i = atoll(int64str);
//
//        return true;
//    }
//    bool BinaryReadStream::ReadShort(short& i)
//    {
//        const int VALUE_SIZE = sizeof(short);
//
//        if (cur + VALUE_SIZE > ptr + len) {
//            return false;
//        }
//
//        memcpy(&i, cur, VALUE_SIZE);
//        i = ntohs(i);
//
//        cur += VALUE_SIZE;
//
//        return true;
//    }
//    bool BinaryReadStream::ReadChar(char& c)
//    {
//        const int VALUE_SIZE = sizeof(char);
//
//        if (cur + VALUE_SIZE > ptr + len) {
//            return false;
//        }
//
//        memcpy(&c, cur, VALUE_SIZE);
//        cur += VALUE_SIZE;
//
//        return true;
//    }
//    bool BinaryReadStream::ReadLength(size_t & outlen)
//    {
//        size_t headlen;
//        if (!ReadLengthWithoutOffset(headlen, outlen)) {
//            return false;
//        }
//
//        //cur += BINARY_PACKLEN_LEN_2;
//        cur += headlen;
//        return true;
//    }

    int readLengthWithoutOffset() {
        //buf数组中有效的位数
        int nValidHeadLength = 0;
        byte[] buf = new byte[5];

        for (int i = 0; i< 5 ; i++)
        {
            if (_cur + i >= _data.length)
                return -1;

            buf[i] = _data[_cur+i];
            nValidHeadLength ++;

            //if ((buf[i] >> 7 | 0x0) == 0x0)
            if ((buf[i] & 0x80) == 0x00)
                break;
        }

        _cur += nValidHeadLength;

        //return uncompressInteger(buf, nValidHeadLength);
        return read7BitEncoded(buf, nValidHeadLength);
    }

//    bool BinaryReadStream::IsEnd() const
//    {
//        assert(cur <= ptr + len);
//        return cur == ptr + len;
//    }
//    const char* BinaryReadStream::GetData() const
//    {
//        return ptr;
//    }
//    size_t BinaryReadStream::ReadAll(char * szBuffer, size_t iLen) const
//    {
//        size_t iRealLen = min(iLen, len);
//        memcpy(szBuffer, ptr, iRealLen);
//        return iRealLen;
//    }
}
