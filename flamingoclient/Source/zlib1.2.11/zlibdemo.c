//zlib压缩与解压使用示例
//#include "zlib.h"

//int CompressBuf()
//{
//    char text[] = "zlib compress and uncompress test\nturingo@163.com\n2012-11-05\n";
//    uLong nSrcLen = strlen(text) + 1;  /* 需要把字符串的结束符'\0'也一并处理 */
//    char* buf = NULL;
//    uLong nDestLen;
//
//    /* 计算缓冲区大小，并为其分配内存 */
//    nDestLen = compressBound(nSrcLen); /* 压缩后的长度是不会超过nDestLen的 */
//    if ((buf = (char*)malloc(sizeof(char) * nDestLen)) == NULL)
//    {
//        printf("no enough memory!\n");
//        return -1;
//    }
//
//    /* 压缩 */
//    if (compress((Bytef*)buf, &nDestLen, (const Bytef*)text, nSrcLen) != Z_OK)
//    {
//        printf("compress failed!\n");
//        return -1;
//    }
//
//    /* 解压缩 */
//    if (uncompress((Bytef*)text, &nSrcLen, (const Bytef*)buf, nDestLen) != Z_OK)
//    {
//        printf("uncompress failed!\n");
//        return -1;
//    }
//
//    /* 打印结果，并释放内存 */
//    printf("%s", text);
//    if (buf != NULL)
//    {
//        free(buf);
//        buf = NULL;
//    }
//
//    return 0;
//}