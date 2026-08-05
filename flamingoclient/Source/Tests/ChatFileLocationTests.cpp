/**
 *  文件气泡本地落点决策单元测试（Suite CFL），ChatFileLocationTests.cpp
 *
 *  覆盖 balloonchatwnd::ResolveLocalFilePath 的优先级与回退：
 *    CFL1 登记路径存在时优先用它（哪怕缓存副本也在）；
 *    CFL2 登记路径失效（文件被移走 / 删除）时回退到缓存副本；
 *    CFL3 没有登记（旧数据 / 库被删）时直接用缓存副本；
 *    CFL4 两处都不可用时返回空串——调用方据此渲染成待下载态；
 *    CFL5 路径为空串时不因"存在标志为真"而被误用（空路径不是有效落点）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-03
 */
#include "catch_amalgamated.hpp"
#include "../BalloonChatWnd/ChatFileLocation.h"

using namespace balloonchatwnd;

TEST_CASE("CFL1 登记路径存在时优先于缓存副本")
{
    //发送方的源文件就在其工作目录，比缓存副本更贴近用户预期，
    //「打开所在文件夹」也才指向有意义的位置
    const std::wstring reg   = L"D:\\work\\report.zip";
    const std::wstring cache = L"C:\\Users\\me\\ChatFile\\abc\\report.zip";

    CHECK(ResolveLocalFilePath(reg, true, cache, true) == reg);
    //缓存不在也不影响，登记那份可用即可
    CHECK(ResolveLocalFilePath(reg, true, cache, false) == reg);
}

TEST_CASE("CFL2 登记失效时回退到缓存副本")
{
    const std::wstring reg   = L"D:\\work\\report.zip";
    const std::wstring cache = L"C:\\Users\\me\\ChatFile\\abc\\report.zip";

    //源文件被移走 / 删掉，但之前下载过一份在缓存里 —— 仍应直接可用，不必重下
    CHECK(ResolveLocalFilePath(reg, false, cache, true) == cache);
}

TEST_CASE("CFL3 没有登记时直接用缓存副本")
{
    const std::wstring cache = L"C:\\Users\\me\\ChatFile\\abc\\report.zip";

    //旧版本留下的数据、或登记库被删：靠 MD5 推导出的缓存路径仍能找回文件
    CHECK(ResolveLocalFilePath(std::wstring(), false, cache, true) == cache);
    //登记为空时"存在标志"无意义，不该影响判断
    CHECK(ResolveLocalFilePath(std::wstring(), true, cache, true) == cache);
}

TEST_CASE("CFL4 两处都不可用时返回空串")
{
    const std::wstring reg   = L"D:\\work\\report.zip";
    const std::wstring cache = L"C:\\Users\\me\\ChatFile\\abc\\report.zip";

    //本地确实没这个文件了 —— 调用方据此渲染成待下载态
    CHECK(ResolveLocalFilePath(reg, false, cache, false).empty());
    //连路径都推导不出来（缺 md5 / 未登录）
    CHECK(ResolveLocalFilePath(std::wstring(), false, std::wstring(), false).empty());
}

TEST_CASE("CFL5 空路径不因存在标志为真而被误用")
{
    //空串不是有效落点，即便调用方误传了 true 也不能被返回出去，
    //否则上层会拿着空路径去"打开文件"
    CHECK(ResolveLocalFilePath(std::wstring(), true, std::wstring(), true).empty());

    //只有缓存为空、登记可用时，仍应正常返回登记
    const std::wstring reg = L"D:\\work\\report.zip";
    CHECK(ResolveLocalFilePath(reg, true, std::wstring(), true) == reg);
}
