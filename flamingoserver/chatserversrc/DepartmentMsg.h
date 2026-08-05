/**
 *  企业部门相关协议报文的结构体与 JSON 收发，DepartmentMsg.h
 *  balloonwj@qq.com   2026.05.21
 *
 *  对应 Msg.h 中 1200 系列协议（msg_type_getorgstructure 等）。
 *  把 1200-1203 报文的 JSON 解析/拼装从网络处理器中抽离为独立函数，
 *  使其可脱离 TcpConnection、数据库单独做单元测试。
 */
#ifndef __DEPARTMENT_MSG_H__
#define __DEPARTMENT_MSG_H__

#include <stdint.h>
#include <string>
#include <vector>

namespace dept {

//组织架构变更通知（cmd 1203 / msg_type_orgstructurenotify）的 changetype 字段取值。
enum org_change_type
{
    org_change_department = 1, //部门结构变更：新建/修改/删除/移动部门
    org_change_member     = 2  //成员归属变更：添加/修改/移除/移动部门成员
};

//部门成员（msg_type_getorgstructure 应答中 members 数组的元素）。
//前若干字段为成员展示信息，后三个字段为该成员在本部门的归属信息。
struct DepartmentMember
{
    DepartmentMember()
        : userid(0), facetype(0), status(0), clienttype(0)
        , isprimary(1), memberorder(0)
    {
    }

    int64_t     userid;       //用户ID
    std::string username;     //用户名（登录账号，登录凭据，全局唯一）
    std::string employeeno;   //工号（HR 工号，与登录账号 username 是不同字段）
    std::string nickname;     //用户昵称
    int32_t     facetype;     //头像类型
    std::string customface;   //自定义头像文件名
    int32_t     status;       //在线状态，取值见 Msg.h 的 enum online_type
    int32_t     clienttype;   //客户端类型：1=PC 2=android 3=ios
    std::string position;     //该成员在本部门的职位/职务
    int32_t     isprimary;    //是否主部门：1=主部门 0=兼属部门
    int32_t     memberorder;  //成员在本部门内的排序权重，值小者靠前
};

//部门（msg_type_getorgstructure 应答中 departments 数组的元素）。
struct DepartmentInfo
{
    DepartmentInfo()
        : deptid(0), enterpriseid(0), parentdeptid(0), deptlevel(1), deptorder(0), leaderuserid(0)
    {
    }

    int64_t      deptid;        //部门业务ID
    int64_t      enterpriseid;  //所属企业ID（多租户隔离用）
    std::string  deptname;      //部门名称
    int64_t      parentdeptid;  //父部门ID，0表示顶级部门
    int32_t      deptlevel;     //部门层级，顶级为1
    int32_t      deptorder;     //同级排序权重，值小者靠前
    int64_t      leaderuserid;  //部门负责人userid，0表示未指定
    std::string  deptdesc;      //部门描述
    std::vector<DepartmentMember> members; //本部门直属成员；只挂子部门不挂人的部门此处为空
};

//部门操作请求（msg_type_operatedepartment / cmd 1201 的请求体）。
//不同 operationtype 用到的字段不同，详见 Msg.h 中 1201 的 JSON 示例。
struct OperateDepartmentReq
{
    OperateDepartmentReq()
        : operationtype(0), deptid(0), parentdeptid(0)
        , newparentdeptid(0), leaderuserid(0), deptorder(0)
    {
    }

    int32_t     operationtype;    //操作类型，取值见 Msg.h 的 enum department_operation_type
    int64_t     deptid;           //目标部门ID（新建时不填，由服务器分配）
    int64_t     parentdeptid;     //父部门ID（新建时使用）
    int64_t     newparentdeptid;  //新父部门ID（移动时使用）
    std::string deptname;         //部门名称（新建、修改时使用）
    std::string deptdesc;         //部门描述（新建、修改时使用）
    int64_t     leaderuserid;     //部门负责人userid（新建、修改时使用）
    int32_t     deptorder;        //同级排序权重（新建、修改时使用）
};

//部门成员操作请求（msg_type_operatedeptmember / cmd 1202 的请求体）。
struct OperateDeptMemberReq
{
    OperateDeptMemberReq()
        : operationtype(0), deptid(0), newdeptid(0)
        , userid(0), isprimary(0), memberorder(0)
    {
    }

    int32_t     operationtype;  //操作类型，取值见 Msg.h 的 enum deptmember_operation_type
    int64_t     deptid;         //目标部门ID
    int64_t     newdeptid;      //新部门ID（移动成员时使用）
    int64_t     userid;         //被操作的用户ID
    std::string position;       //职位/职务（添加、修改时使用）
    int32_t     isprimary;      //是否主部门：1=主部门 0=兼属部门（添加、修改时使用）
    int32_t     memberorder;    //成员在部门内排序权重（添加、修改时使用）
};

/**
 *  解析 cmd 1201（部门操作）请求体 JSON。
 *  @param json 请求体 JSON 字符串。
 *  @param out  解析结果，仅在返回 true 时有效。
 *  @return true 表示 JSON 合法且含整型 operationtype 字段；false 表示解析失败。
 */
bool ParseOperateDepartmentReq(const std::string& json, OperateDepartmentReq& out);

/**
 *  解析 cmd 1202（部门成员操作）请求体 JSON。
 *  @param json 请求体 JSON 字符串。
 *  @param out  解析结果，仅在返回 true 时有效。
 *  @return true 表示 JSON 合法且含整型 operationtype 字段；false 表示解析失败。
 */
bool ParseOperateDeptMemberReq(const std::string& json, OperateDeptMemberReq& out);

/**
 *  解析 cmd 1200（拉取企业组织架构）应答体 JSON。
 *  主要供单元测试做 round-trip 校验，PC 客户端亦可复用此 schema。
 *  @param json     应答体 JSON 字符串。
 *  @param outDepts 解析出的部门数组，仅在返回 true 时有效。
 *  @return true 表示 JSON 合法且含 departments 数组；false 表示解析失败。
 */
bool ParseGetOrgStructureResp(const std::string& json, std::vector<DepartmentInfo>& outDepts);

/**
 *  拼装 cmd 1200（拉取企业组织架构）应答体 JSON。
 *  @param code  错误码，0 表示成功，取值见 Msg.h 的 enum error_code。
 *  @param msg   错误描述。
 *  @param depts 部门数组（扁平，含各部门内嵌成员）。
 *  @return 应答体 JSON 字符串（无缩进）。
 */
std::string BuildGetOrgStructureResp(int32_t code, const std::string& msg,
                                     const std::vector<DepartmentInfo>& depts);

/**
 *  拼装 cmd 1201（部门操作）应答体 JSON。
 *  @param code          错误码。
 *  @param msg           错误描述。
 *  @param operationtype 与请求一致的操作类型，便于客户端对应。
 *  @param deptid        被操作部门ID（新建时为服务器新分配的ID）。
 *  @return 应答体 JSON 字符串（无缩进）。
 */
std::string BuildOperateDepartmentResp(int32_t code, const std::string& msg,
                                       int32_t operationtype, int64_t deptid);

/**
 *  拼装 cmd 1202（部门成员操作）应答体 JSON。
 *  @param code          错误码。
 *  @param msg           错误描述。
 *  @param operationtype 与请求一致的操作类型。
 *  @param deptid        被操作部门ID。
 *  @param userid        被操作用户ID。
 *  @return 应答体 JSON 字符串（无缩进）。
 */
std::string BuildOperateDeptMemberResp(int32_t code, const std::string& msg,
                                       int32_t operationtype, int64_t deptid, int64_t userid);

/**
 *  拼装 cmd 1203（组织架构变更通知）报文体 JSON。
 *  @param changetype 变更类型：1=部门结构变更 2=成员归属变更。
 *  @param deptid     受影响部门ID，仅作提示。
 *  @return 通知体 JSON 字符串（无缩进）。
 */
std::string BuildOrgStructureNotify(int32_t changetype, int64_t deptid);

} // namespace dept

#endif // __DEPARTMENT_MSG_H__
