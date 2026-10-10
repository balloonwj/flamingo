/*
 *  speexdsp 的 Win32/MSVC 源码内嵌静态库构建配置（Flamingo 自备，HAVE_CONFIG_H 包含）。
 *  balloonwj@qq.com   2026-06-12
 *
 *  选「纯 C 浮点 + alloca」最稳路径，避开官方 win32/config.h 的 USE_SSE 分支（SSE 开时
 *  不定义 USE_ALLOCA、改用非线程安全全局伪栈）：
 *    - FLOATING_POINT + USE_SMALLFT：浮点构建、FFT 用 smallft（不用 kiss）；
 *    - USE_ALLOCA：用 MSVC _alloca 做栈临时分配（线程安全，多引擎并发安全）；
 *    - 不定义 USE_SSE/USE_SSE2：走纯 C 路径，语音 DSP 性能足够、免 SIMD 编译分歧；
 *    - inline=__inline / EXPORT 空：MSVC 关键字适配、静态库不导出符号。
 */
#define inline __inline
#define USE_ALLOCA
#define FLOATING_POINT
#define USE_SMALLFT
#define EXPORT
