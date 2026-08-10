/**
 *  flamingoAdmin —— 列表分页纯计算函数实现。详见 Util/ListPaging.h。
 *
 *  balloonwj@qq.com   2026-06-01
 */
#include "stdafx.h"
#include "Util/ListPaging.h"

namespace flamingoadmin {

int ListPageCount(int total, int pageSize)
{
    if (pageSize <= 0)
    {
        pageSize = 1;
    }
    if (total <= 0)
    {
        return 1;
    }
    return (total + pageSize - 1) / pageSize;   // 向上取整
}

int ListClampPage(int page, int total, int pageSize)
{
    const int count = ListPageCount(total, pageSize);
    if (page < 0)
    {
        return 0;
    }
    if (page > count - 1)
    {
        return count - 1;
    }
    return page;
}

void ListPageRange(int page, int pageSize, int total, int& outBegin, int& outCount)
{
    outBegin = 0;
    outCount = 0;
    if (pageSize <= 0)
    {
        pageSize = 1;
    }
    if (total <= 0)
    {
        return;   // 空表:区间为空
    }
    page = ListClampPage(page, total, pageSize);
    const int begin = page * pageSize;
    if (begin >= total)
    {
        outBegin = total;   // 理论上夹取后不会发生,稳妥兜底
        outCount = 0;
        return;
    }
    int count = total - begin;
    if (count > pageSize)
    {
        count = pageSize;
    }
    outBegin = begin;
    outCount = count;
}

} // namespace flamingoadmin
