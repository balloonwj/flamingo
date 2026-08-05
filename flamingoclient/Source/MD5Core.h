/**
 *  MD5 摘要算法的纯 C 接口（RSA Data Security 参考实现的声明部分）。
 *
 *  从 MD5Sum.h 中拆出：那里的 MD5Sum 类依赖 MFC 的 CString，凡是不链接 MFC 的
 *  编译单元（如 net/FileStreamTransfer.cpp 及其单元测试）都无法包含它。本头文件
 *  只含 MD5_CTX 与三个 C 函数，无任何 MFC / Windows 依赖，可被任意模块安全包含。
 *  实现仍在 MD5Sum.cpp 里，两边共用同一份。
 *
 *  典型用法（流式增量计算）：
 *      MD5_CTX ctx;
 *      MD5Init(&ctx);
 *      MD5Update(&ctx, buf, len);      //可多次调用，逐块喂入
 *      unsigned char digest[16];
 *      MD5Final(digest, &ctx);
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

/* Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
rights reserved.

License to copy and use this software is granted provided that it
is identified as the "RSA Data Security, Inc. MD5 Message-Digest
Algorithm" in all material mentioning or referencing this software
or this function.

RSA Data Security, Inc. makes no representations concerning either
the merchantability of this software or the suitability of this
software for any particular purpose. It is provided "as is"
without express or implied warranty of any kind.

These notices must be retained in any copies of any part of this
documentation and/or software.
*/

typedef unsigned char*POINTER;
typedef unsigned short int UINT2;
typedef unsigned long int UINT4;

//MD5 增量计算的上下文：调用方在栈上分配，经 MD5Init 初始化后逐块 MD5Update，
//最后 MD5Final 取出 16 字节摘要。
typedef struct {
  UINT4 state[4];
  UINT4 count[2];
  unsigned char buffer[64];
} MD5_CTX;

//初始化上下文，开始一次新的摘要计算。
void MD5Init (MD5_CTX*);

//喂入一块数据（可反复调用，内容按调用顺序拼接参与计算）。第三个参数为本块字节数。
void MD5Update (MD5_CTX*, const unsigned char*, unsigned int);

//取出最终的 16 字节摘要并结束本次计算；上下文用毕即废，如需再算须重新 MD5Init。
void MD5Final (unsigned char [16], MD5_CTX*);
