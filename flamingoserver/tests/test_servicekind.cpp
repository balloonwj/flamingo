/**
 *  服务号类型取值的校验测试，test_servicekind.cpp
 *
 *  服务号原先只有 0 通知型 / 1 助手型，控制台的「集成型」保存后被映射成通知型。2026-10-04
 *  起服务端增加类型 2（集成型），service/save.do 只接受 0～2，其余拒绝，免得库里出现控制台
 *  与客户端都不认识的值。
 *    - SVK1：0、1、2 合法，-1、3 等不合法。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "doctest.h"
#include "../chatserversrc/ServiceAccountManager.h"

TEST_CASE("SVK1 服务号类型只接受 0 通知型、1 助手型、2 集成型")
{
    CHECK(isValidServiceKind(service_kind_notification));
    CHECK(isValidServiceKind(service_kind_assistant));
    CHECK(isValidServiceKind(service_kind_integration));
    CHECK(service_kind_integration == 2);

    CHECK_FALSE(isValidServiceKind(-1));
    CHECK_FALSE(isValidServiceKind(3));
    CHECK_FALSE(isValidServiceKind(100));
}
