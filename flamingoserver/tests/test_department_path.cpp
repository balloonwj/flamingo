/**
 *  DepartmentPath 物化路径算法单元测试（Suite A），test_department_path.cpp
 *  balloonwj@qq.com   2026.05.21
 */
#include "doctest.h"
#include "../chatserversrc/DepartmentPath.h"

TEST_CASE("A1-A3 MakeChildPath")
{
    CHECK(dept::MakeChildPath("", 1001) == "/1001/");                      //A1 根部门
    CHECK(dept::MakeChildPath("/1001/", 1005) == "/1001/1005/");           //A2 二级部门
    CHECK(dept::MakeChildPath("/1001/1005/", 2001) == "/1001/1005/2001/"); //A3 三级部门
}

TEST_CASE("A4-A5 MakeChildLevel")
{
    CHECK(dept::MakeChildLevel(0) == 1);  //A4 根部门层级
    CHECK(dept::MakeChildLevel(2) == 3);  //A5 逐级 +1
}

TEST_CASE("A6-A9 IsAncestorOrSelf")
{
    CHECK(dept::IsAncestorOrSelf("/1001/", "/1001/"));            //A6 自身
    CHECK(dept::IsAncestorOrSelf("/1001/", "/1001/1005/"));       //A7 真子孙
    CHECK_FALSE(dept::IsAncestorOrSelf("/1001/1005/", "/1001/")); //A8 父不是子的后代
    CHECK_FALSE(dept::IsAncestorOrSelf("/1001/", "/10011/"));     //A9 前缀粘连边界
}

TEST_CASE("A10-A12 WouldFormCycle")
{
    CHECK(dept::WouldFormCycle("/1001/1005/", "/1001/1005/"));       //A10 移到自身
    CHECK(dept::WouldFormCycle("/1001/1005/", "/1001/1005/2001/"));  //A11 移到自己的子孙
    CHECK_FALSE(dept::WouldFormCycle("/1001/1005/", "/1001/1008/")); //A12 移到无关部门
}

TEST_CASE("A13-A14 RebasePath")
{
    //A13 被移动子树根节点自身
    CHECK(dept::RebasePath("/1001/1005/", "/1001/1005/", "/1008/1005/") == "/1008/1005/");
    //A14 子树内的后代节点
    CHECK(dept::RebasePath("/1001/1005/2001/", "/1001/1005/", "/1008/1005/") == "/1008/1005/2001/");
}

//A15-A18 覆盖"在物化路径最前面插入公司根部门"这一用法。
//企业引入公司根部门后，该企业原有部门要整体下沉一级，路径需在最前面插入
//新根的ID。做法是把 oldPrefix 取为单个分隔符 "/"，newPrefix 取为新根自己的
//路径 "/<新根ID>/"。存量数据迁移脚本改写 f_dept_path 时用的就是这条规则，
//此处以本函数作为该规则的参照实现，锁定其预期结果。
TEST_CASE("A15-A18 RebasePath 用于插入公司根部门")
{
    //公司根部门自身的路径，新根ID取 9001。
    const std::string newRootPath = "/9001/";

    //A15 原顶级部门下沉为公司根的子部门
    CHECK(dept::RebasePath("/1005/", "/", newRootPath) == "/9001/1005/");
    //A16 原二级部门整条链一并下沉，链内相对关系不变
    CHECK(dept::RebasePath("/1005/2001/", "/", newRootPath) == "/9001/1005/2001/");
    //A17 新根ID与原部门ID位数不同也不会粘连，分隔符始终成对出现
    CHECK(dept::RebasePath("/10011/", "/", newRootPath) == "/9001/10011/");
    //A18 本函数不幂等：对已下沉过的路径再插一次会得到两层根。迁移脚本必须
    //    靠企业的 f_root_dept_id 是否已写入来防止重复执行，不能依赖本函数自身。
    CHECK(dept::RebasePath("/9001/1005/", "/", newRootPath) == "/9001/9001/1005/");
}
