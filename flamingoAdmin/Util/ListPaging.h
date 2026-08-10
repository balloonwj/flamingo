/**
 *  flamingoAdmin —— 列表分页纯计算函数。
 *
 *  把"总条数 / 每页条数 / 当前页"到"页数、夹取、本页项区间"的算术抽成无副作用
 *  的纯函数,供公告列表等分页 UI 复用,并可独立单测(同 Util/ScrollbarMetrics)。
 *  页号一律 0-based。
 *
 *  balloonwj@qq.com   2026-06-01
 */
#pragma once

namespace flamingoadmin {

// 计算总页数。
//   total：总条数;<=0 时返回 1(空列表也占 1 页,便于界面显示"第 1 / 1 页")。
//   pageSize：每页条数;<=0 时按 1 处理。
//   返回:>=1 的页数。
int ListPageCount(int total, int pageSize);

// 把页号夹取到合法区间 [0, ListPageCount-1]。
//   page：待夹取的页号(0-based,可越界 / 为负)。
//   total / pageSize：同 ListPageCount。
//   返回:夹取后的合法页号。
int ListClampPage(int page, int total, int pageSize);

// 求第 page 页(0-based)对应的项区间。
//   page：页号(内部会先夹取到合法区间)。
//   pageSize：每页条数(<=0 按 1)。
//   total：总条数。
//   outBegin：出参,本页首项在整表中的下标(空表 / 越界时为 total 或 0)。
//   outCount：出参,本页实际条数(末页可能不足 pageSize;空表为 0)。
void ListPageRange(int page, int pageSize, int total, int& outBegin, int& outCount);

} // namespace flamingoadmin
