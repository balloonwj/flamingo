/**
 *  发起共享、取消共享、调整成员的业务流程与对话框单元测试（Suite SSM / SCD，标签 [sharemanage]），ShareManageTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 15 步的 15b、15c）：
 *    - SSM1 发起成功：先以 clientshareid 写本机记录再发 2201；应答到达后补写 shareid、立即发 2210 报告可提供，观察者收到 0；
 *    - SSM2 发起被拒（821）：删除本机记录、不发 2210，观察者收到错误码与不满足条件的成员；
 *    - SSM3 超时重发：用同一个 clientshareid 重发（包体逐字相同），重发前那一次的迟到应答按 clientshareid 配上；
 *           发满 kShareCreateMaxAttempts 次仍无应答时以「没有应答」结束并删除本机记录；
 *    - SSM4 取消与调整成员：2202 成功后删除本机记录，失败时保留；2204 被拒时带回不满足条件的成员；
 *           观察者解除登记后不再回调；三组都为空的调整不发送；
 *    - SSM5 退出登录时尚未有结果的请求以「没有应答」结束；登录时清理上次进程遗留的、没有 shareid 的本机记录；
 *    - SCD1 群聊发起：打开时向宿主要群成员，候选不含本人；没选对象、没选成员时提示原因；发出的 2201 带勾选的成员与权限；
 *           被拒时标出不满足条件的成员并显示原因，再次发起成功后对话框关闭；「共享名称」框的提示文字是默认名称，
 *           不填时发出默认名称「<本人>的文件共享」、填了时发出去掉首尾空白的名字，都不是文件夹名（2026-10-04 起）；
 *    - SCD2 单聊发起单个文件：对方固定勾选、取消不掉，单文件共享没有权限、不带「包含隐藏文件」；共享名称不是文件名；
 *    - SCD3 调整成员：按已有成员预先勾选，已不在群里的成员用宿主给的名字追加在末尾；没有变化时提示；
 *           发出的 2204 与成员表比较的结果一致，成功后关闭；「共享名称」框显示当前名称、不可编辑；
 *    - SCD4 截图（标签 [.snapshot]，默认不运行）：群聊共享文件夹与单聊共享文件两种对话框，存到仓库根 snapshots 目录供人工核对；
 *    - SCD5 权限列放得下表头文字（2026-10-03 实机核对发现 120 DPI 下「新建文件夹」被截断）：五个表头勾选框都放得下
 *           「勾选框 + 文字」，成员行的权限勾选框与表头同列对齐；窗口 DPI 变化、字体变大后依然如此；
 *    - SCS1 卡片状态（第 16 步）：2203 与推给本人的 2206 更新状态并经宿主通知，没有变化时不重复通知；2205 应答只补缺、不通知；
 *    - SCS2 已取消是终态（第 2 期第 6 步验收发现）：取消之后迟到的「可用」「暂不可用」与 2205 里的旧状态都不改回；
 *           本人被移出成员的「已失效」不是终态，重新被加入后恢复；
 *    - SCP1 本会话共享浮层（第 16 步）：弹出时按会话发两份 2205 并为取群角色向宿主要成员；两份都到后合并显示；
 *           群管理员对别人的共享多出「取消共享」；取消发 2202，成功后重新拉取；
 *    - SCP2 浮层里「打开」「调整成员」「发起共享」经模块接口打开对应窗口并关闭浮层；没有共享时提示本会话还没有共享；
 *    - SCP3 截图（标签 [.snapshot]）：浮层存到 snapshots/fileshare_conversation_popup.png；
 *    - SCW1 「文件共享」窗口（第 17 步）：打开时拉两份列表（不按会话筛选）；「我的共享」选中一行后详情列出类型、路径、所在会话、
 *           状态、成员与权限、正在访问的人、最近操作，操作按钮按本机记录与状态给出；已失效的只能定位；
 *    - SCW2 操作：调整成员与打开经模块接口，定位到会话经宿主，取消共享发 2202、成功后重新拉取；「共享给我的」页签列出分享者与我的权限；
 *    - SCW3 截图（标签 [.snapshot]）：窗口存到 snapshots/fileshare_center.png，弹出浮层提示时存到 fileshare_center_toast.png，
 *           「共享给我的」页签存到 fileshare_center_shared.png；
 *    - SCW4 顶栏与卡片（2026-10-03 美化后改写，原先测的是 DuiTab 页签条）：两个胶囊页签选中的品牌蓝实心、另一个白底灰边，
 *           文字带共享个数，宽度正好放下文字；窗口浅灰底、列表在白色卡片里且没有灰边；窗口 DPI 变化、字体变大后页签随之加宽，
 *           仍然正好放下；
 *    - SCC1 按会话统计可用共享的个数（聊天窗头部文件共享按钮的角标）：登记会话时按会话发两份 2205，两份都到了才数、才通知，
 *           本人发起的与共享给本人的都算，暂不可用与已失效的不算；
 *    - SCC2 状态变化：2203 让个数增减时通知，个数不变时不通知，不属于登记会话的共享不影响；
 *    - SCC3 再次登记即重新拉取（新发起的共享由此计入）；注销后迟到的结果丢弃、不再通知；
 *    - SCC4 列表失败时保留上一次的列表；重连后 RefreshAll 重新拉取全部登记的会话；退出登录 Reset 后不再通知；
 *    - STD1 按选好的对象发起（「文件共享」窗口的「发起共享」，2026-10-03 起）：只选 1 人时在与他的单聊里发起，对方固定勾选，
 *           成功后请宿主打开这个单聊并关闭对话框；
 *    - STD2 选了多人：先校验共享对象，再请宿主建群（成员是勾选的人），建群结果到达后在新群里发起，成功后打开群聊；
 *    - STD3 建群失败：显示原因、可以重试，不发 2201；宿主没发出建群请求时同样提示；
 *    - STD4 群已建好、共享被拒：提示群已建好，重试时不再建群，仍在这个群里发起；
 *    - STD5 选了多人但只勾选一个：按单聊发起，不建群；
 *    - STD6 等待建群时不能重复提交；不在等待时收到建群结果不做任何事；
 *    - SCW6 「文件共享」窗口的「发起共享」：经模块请宿主弹出选人窗口，弹不出时用浮层提示说明原因；
 *    - SAV1 成员头像（2026-10-03 起，标签 [shareavatar]）：向宿主取头像，宿主不给时为问号与中性灰；
 *    - SAV2 头像位图：字形用宿主给的底色、同一个人只生成一次；照片在本机时画照片，照片文件不存在时改画字形；
 *    - SAV3 头像控件：圆形 DuiAvatar 挂着位图，字形与底色也已设好；
 *    - SCD6 发起共享对话框的成员行：头像（字形或照片）在勾选框与名字之间；用鼠标消息点名字等于点勾选框；宿主通知头像变了后
 *           成员行重建、换上新头像，勾选与权限保留；
 *    - SCW7 「文件共享」窗口详情里的成员与「共享给我的」分享者每人一行带头像（字形或照片），详情的整行文字不变；
 *           宿主通知头像变了后详情重建、换上新头像；
 *    - SCP4 浮层的「管理全部共享」请模块打开「文件共享」窗口并记住本会话；浮层自己的「发起共享」成功后不另开聊天窗
 *           （2026-10-03 起，标签 [sharelaunch]）；
 *    - SCW8 从聊天窗打开的「文件共享」窗口，「发起共享」直接在那个会话里发起、不弹选人窗口，清除记住的会话后回到选人流程；
 *    - SCD7 发起共享对话框只在要求时（从「文件共享」窗口发起）于成功后打开所在会话的聊天窗；
 *    - SCD8 共享名称命中敏感词（822）：显示原因，对话框不关、不再忙碌，改名后重新发起成功（2026-10-04 起）；
 *    - SBN1 2220 应答里的共享名称交给访问者（浏览窗口的标题与路径栏第一节用它，2026-10-04 起）；
 *    - SCW9 「文件共享」窗口的列表（2026-10-03 美化起）：搜索按名称过滤、保持选择、切换页签后仍生效；行右端的状态胶囊
 *           （可用绿、已失效灰），选中行浅蓝圆角底色与左侧品牌蓝竖条；
 *    - SCW10 「文件共享」窗口的详情（2026-10-03 美化起，标签 [sharecenterdetail]）：没选中时只有提示文字；选中后头部有类型图标、
 *           名称、路径与状态胶囊；操作按钮按字体定宽、「取消共享」红色靠右，最多的组合在 1 倍与 2 倍 DPI 下都放得下；
 *           「定位到会话」是信息表里的链接，点了打开所在会话；小节标题带个数，成员名字下面是权限胶囊；定时刷新保持滚动位置，
 *           换选别的共享回到顶部；内容不满一屏时下面是白底，滚动条自动隐藏、它那一列也是白底；信息表的标签列与最近操作的
 *           时间列按字体定宽（2 倍 DPI 下也与右列留有间距）；
 *    - SCW11 浮层提示取代底部提示行（2026-10-03 起）：打开时列表在加载但不弹提示；列表失败弹红底提示、多显示一秒；
 *    - SCW12 取消共享时弹「正在取消…」、成功后弹「已取消共享」（深灰底），提示在顶栏下方水平居中、不压住顶栏；
 *    - SCW13 窗口拉到最小尺寸时排得下：顶栏各控件依次排开不重叠、搜索框不被压窄，左侧列表留有足够宽度，详情的操作按钮在卡片之内；
 *    - SCW14 窗口拉宽时详情卡片分到多出宽度的一半、缩回后复原；最近操作的文字挂着显示全文的悬停提示；定时刷新时正在访问的人与
 *           最近操作都没有变化就不重建详情，新增一条操作记录后才重建并列出它。
 *    - SWP6~SWP10 「文件共享」窗口的位置记忆（2026-10-07，标签 [shareplacement]；读写配置与计算矩形的纯函数见
 *           ShareWindowPlacementTests.cpp 的 SWP1~SWP5）：首次打开在主显示器工作区里居中；挪动、改大小后关窗，配置文件记下
 *           还原态矩形，重开时原样恢复；最大化后关窗记下最大化，重开时以最大化显示、还原后回到原矩形；先最大化再最小化后关窗
 *           仍记为最大化；保存的位置已经完全在屏幕外时，重开改为在主显示器工作区里居中。
 *    - SWP14、SWP15 共享浏览窗口的位置记忆与错开摆放（2026-10-07；纯函数见 ShareWindowPlacementTests.cpp 的 SWP11~SWP13）：
 *           没有保存过时在主显示器工作区里居中，不再以参照窗口为中心；挪动、改大小后关窗写进 [ShareBrowser]（[ShareCenter]
 *           不受影响），之后打开任何一个共享的浏览窗口都按它恢复；第二个浏览窗口相对第一个右下错开、大小相同。
 *  测试进程里直接使用 DLL 的 CShareService 与 CShareCreateDialog，宿主是本文件里的桩。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <stdio.h>    //_snprintf_s
#include <cstdlib>    //std::abs
#include <string.h>   //strcpy_s
#include <algorithm>   //std::max
#include <map>
#include <memory>     //std::unique_ptr：BodyString 里的 Json::CharReader
#include <string>
#include <utility>
#include <vector>

#include "../FileShareApi.h"
#include "../Engine/ShareService.h"
#include "../Engine/ShareLocalStore.h"
#include "../Engine/ShareProtocolCodec.h"
#include "../UI/ShareCreateDialog.h"
#include "../UI/ShareConversationPopup.h"
#include "../UI/ShareCenterWindow.h"
#include "../UI/ShareWindowPlacement.h"   //SWP6~SWP10：位置配置文件的路径与写入
#include "../UI/ShareBrowserWindow.h"      //SWP14、SWP15：共享浏览窗口的位置（2026-10-07）
#include "../UI/ShareAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"   //SCD5 读表头勾选框的文字与字体
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   //SCW10 读详情滚动区的滚动位置
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiToast.h"   //SCW11、SCW12 读浮层提示
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   //SCW14 读最近操作的悬停提示
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSearchBox.h"   //SCW15 读搜索框的提示文字
#include "../Engine/ShareCenterModel.h"
#include "../Engine/ShareConversationModel.h"
#include "../Engine/ShareConversationCounter.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"   //SCD1、SCD2、SCD8：解析请求正文里的共享名称
#include "ShareTestFs.h"
#include "ShareTestSnapshot.h"

using namespace fileshare;

namespace {

//本套用例在测试程序目录下建的个人目录（每个用例开始前整个删掉）
const wchar_t* const kSsmTestDir = L"ssm_test";

//本人与几名成员的 userid
const int64_t kSelf = 30001;
const int64_t kMemberA = 30002;
const int64_t kMemberB = 30003;
const int64_t kMemberC = 30004;

//群 id
const int64_t kGroupId = 900100;

//服务端分配的 shareid
const int64_t kNewShareId = 7701;

//等待超时时用的毫秒数：超时设为 kShortTimeoutMs，每次检查前睡 kSleepPastTimeoutMs。
//管理请求的超时按 GetTickCount64 判断，它的精度是系统时钟中断间隔（默认约 15.6 毫秒，有程序调高时钟精度时约 1 毫秒）；
//睡的时长须超过这个间隔的两倍，否则睡完时计时值可能还没走到下一格，判断为没有超时
//（原先睡 5 毫秒时，SSM3 连跑 30 次失败 1 次）
const int64_t kShortTimeoutMs = 1;
const DWORD kSleepPastTimeoutMs = 40;

//浮层用例里的几个共享 id
const int64_t kShareMine = 8801;
const int64_t kShareOthers = 8802;
const int64_t kShareGone = 8803;

//「文件共享」窗口截图位置
const wchar_t* const kSnapshotCenter = L"\\..\\..\\..\\snapshots\\fileshare_center.png";
//「文件共享」窗口弹出浮层提示时的截图（2026-10-03 起）
const wchar_t* const kSnapshotCenterToast = L"\\..\\..\\..\\snapshots\\fileshare_center_toast.png";
//「文件共享」窗口「共享给我的」页签的截图（2026-10-03 起）
const wchar_t* const kSnapshotCenterShared = L"\\..\\..\\..\\snapshots\\fileshare_center_shared.png";

//浮层截图位置
const wchar_t* const kSnapshotPopup = L"\\..\\..\\..\\snapshots\\fileshare_conversation_popup.png";

//单文件共享用例里那个文件的字节数
const size_t kFileBytes = 64;

//截图前等窗口完成布局与绘制的时长，单位：毫秒
const DWORD kPaintWaitMs = 300;

//浮层一行里距行左边多远处投递双击，落在行首的类型图标上（图标边长 32），像素
const int kSmallIconProbe = 16;

//截图位置（测试程序目录 Debug_Win32/Tests 往上三级是仓库根）
const wchar_t* const kSnapshotGroup = L"\\..\\..\\..\\snapshots\\fileshare_create_dialog.png";
const wchar_t* const kSnapshotFile = L"\\..\\..\\..\\snapshots\\fileshare_create_dialog_file.png";

//截图用例里的群成员人数（含本人）：多于成员表一屏能放下的行数，滚动条会出现
const size_t kSnapshotMembers = 16;

//宿主桩给的字形头像：字形取 userid 除以 kStubGlyphModulo 的余数（两位数字），底色固定为 kStubAvatarColor
const int64_t kStubGlyphModulo = 100;
const COLORREF kStubAvatarColor = RGB(0x3A, 0x86, 0xD9);

//宿主桩：发信令时按顺序分配 seq 并记下命令与包体
class ManageHost : public IFileShareHost
{
public:
    ManageHost()
        : m_nextSeq(100)
    {
    }

    virtual void Log(int level, const char* text) override
    {
        (void)level;
        if (text != NULL)
            m_lines.push_back(text);
    }

    virtual uint32_t SendShareCommand(uint16_t cmd, const char* json, uint32_t len) override
    {
        m_cmds.push_back(cmd);
        m_bodies.push_back(std::string(json != NULL ? json : "", len));
        m_seqs.push_back(++m_nextSeq);
        return m_nextSeq;
    }

    virtual const unsigned char* GetServerPublicKey() override
    {
        return NULL;
    }

    virtual bool GetSessionInfo(FileShareSessionInfo* info) override
    {
        if (info == NULL || info->m_cbSize != sizeof(FileShareSessionInfo))
            return false;
        info->m_userId = kSelf;
        strcpy_s(info->m_account, kFileShareTextBytes, "ssmtester");
        strcpy_s(info->m_nickName, kFileShareTextBytes, "ssmtester");
        strcpy_s(info->m_deviceId, kFileShareTextBytes, "PC-SSM");
        wcsncpy_s(info->m_personalDir, kFileSharePathChars, m_personalDir.c_str(), _TRUNCATE);
        return true;
    }

    virtual bool RequestConversationMembers(int ctype, int64_t cid) override
    {
        m_memberRequests.push_back(std::make_pair(ctype, cid));
        return true;
    }

    virtual bool GetUserDisplayName(int64_t userId, char* buf, uint32_t bufBytes) override
    {
        if (buf == NULL || bufBytes == 0)
            return false;
        _snprintf_s(buf, bufBytes, _TRUNCATE, "user%lld", static_cast<long long>(userId));
        return true;
    }

    virtual void NotifyShareState(int64_t shareId, int state, int reason, int myPerms) override
    {
        Notified n;
        n.m_shareId = shareId;
        n.m_state = state;
        n.m_reason = reason;
        n.m_perms = myPerms;
        m_notified.push_back(n);
    }

    virtual bool GetConversationName(int ctype, int64_t cid, char* buf, uint32_t bufBytes) override
    {
        if (buf == NULL || bufBytes == 0)
            return false;
        _snprintf_s(buf, bufBytes, _TRUNCATE, "conv%d-%lld", ctype, static_cast<long long>(cid));
        return true;
    }

    virtual bool OpenConversation(int ctype, int64_t cid) override
    {
        m_openedConversations.push_back(std::make_pair(ctype, cid));
        return true;
    }

    virtual bool OpenImageViewer(HWND owner, const wchar_t* imagePath, bool navigable) override
    {
        (void)owner;
        m_openedImages.push_back(imagePath != NULL ? std::wstring(imagePath) : std::wstring());
        m_lastImageNavigable = navigable;
        return imagePath != NULL;
    }

    virtual void NotifyConversationShareCount(int ctype, int64_t cid, int count) override
    {
        m_shareCounts.push_back(std::make_pair(std::make_pair(ctype, cid), count));
    }

    virtual bool PickShareTargets(HWND owner) override
    {
        (void)owner;
        ++m_pickRequests;
        return true;
    }

    virtual bool CreateShareGroup(const char* groupName, const int64_t* memberIds, uint32_t count) override
    {
        if (!m_groupRequestOk || memberIds == NULL || groupName == NULL)
            return false;
        m_groupNames.push_back(groupName);
        m_groupRequests.push_back(std::vector<int64_t>(memberIds, memberIds + count));
        return true;
    }

    //字形头像见 kStubGlyphModulo、kStubAvatarColor；m_avatarImages 里登记过的用户另给照片路径
    virtual bool GetUserAvatar(int64_t userId, FileShareAvatarInfo* out) override
    {
        if (out == NULL || out->m_cbSize != sizeof(FileShareAvatarInfo))
            return false;
        ++m_avatarRequests;
        _snwprintf_s(out->m_glyph, kFileShareGlyphChars, _TRUNCATE, L"%02d", static_cast<int>(userId % kStubGlyphModulo));
        out->m_color = kStubAvatarColor;
        out->m_imagePath[0] = L'\0';
        std::map<int64_t, std::wstring>::const_iterator it = m_avatarImages.find(userId);
        if (it != m_avatarImages.end())
            wcsncpy_s(out->m_imagePath, kFileSharePathChars, it->second.c_str(), _TRUNCATE);
        return true;
    }

    //第 11 版接口（多语言）：不翻译，原样返回原文
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override
    {
        (void)context;
        return source;
    }

    //第 11 版接口（多语言）：报告简体中文与默认字体，DLL 里的 TR 因此直接返回原文
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override
    {
        if (langBuf == NULL || langChars == 0 || fontBuf == NULL || fontChars == 0)
            return false;
        wcsncpy_s(langBuf, langChars, L"zh-CN", _TRUNCATE);
        wcsncpy_s(fontBuf, fontChars, L"Microsoft YaHei", _TRUNCATE);
        return true;
    }

    //P2P 服务：本组用例不走 P2P，返回 NULL（文件共享的传输全部走中转）
    virtual p2p::IP2PService* GetP2PService() override
    {
        return NULL;
    }

    //第 12 版：弹窗通知照常弹出（本组用例不产生通知）
    virtual int QueryNoticePolicy() override
    {
        return fileshare_notice_show;
    }

    //第 12 版：不出声
    virtual void PlayNoticeSound() override
    {
    }

    std::map<int64_t, std::wstring>    m_avatarImages;            //登记了照片头像的用户：userid → 照片的本机路径（用例写入）
    int                                m_avatarRequests = 0;      //GetUserAvatar 被调用的次数

    std::vector<std::string>           m_groupNames;              //建群请求的群名（UTF-8），按请求顺序

    int                                m_pickRequests = 0;        //选择共享对象的请求次数
    bool                               m_groupRequestOk = true;   //CreateShareGroup 是否当作已发出（用例可改为 false）
    std::vector<std::vector<int64_t> > m_groupRequests;           //建群请求的成员，按请求顺序

    std::vector<std::pair<std::pair<int, int64_t>, int> > m_shareCounts;   //可用共享个数的通知：((会话类型, 会话 id), 个数)
    std::vector<std::pair<int, int64_t> > m_openedConversations;   //请宿主定位过的会话：(会话类型, 会话 id)
    std::vector<std::wstring>             m_openedImages;          //请宿主用看图窗打开过的图片，按顺序
    bool                                  m_lastImageNavigable = false;   //最近一次打开时是否可以翻页

    //一次卡片状态通知
    struct Notified
    {
        int64_t m_shareId;   //共享 id
        int     m_state;     //状态
        int     m_reason;    //原因
        int     m_perms;     //写权限位
    };

    //某个命令发送的次数
    int Count(uint16_t cmd) const
    {
        int n = 0;
        for (size_t i = 0; i < m_cmds.size(); ++i)
        {
            if (m_cmds[i] == cmd)
                ++n;
        }
        return n;
    }

    //某个命令第 index 次（从 0 起）发送的下标；没有时返回 -1
    int IndexOf(uint16_t cmd, int index) const
    {
        int n = 0;
        for (size_t i = 0; i < m_cmds.size(); ++i)
        {
            if (m_cmds[i] != cmd)
                continue;
            if (n == index)
                return static_cast<int>(i);
            ++n;
        }
        return -1;
    }

    uint32_t                              m_nextSeq;         //最近一次分配的 seq
    std::vector<uint16_t>                 m_cmds;            //发出的命令
    std::vector<std::string>              m_bodies;          //发出的包体，与 m_cmds 一一对应
    std::vector<uint32_t>                 m_seqs;            //分配的 seq，与 m_cmds 一一对应
    std::vector<std::string>              m_lines;           //日志
    std::vector<std::pair<int, int64_t> > m_memberRequests;  //取会话成员的请求
    std::wstring                          m_personalDir;     //个人目录（带结尾反斜杠）
    std::vector<Notified>                 m_notified;        //卡片状态通知，按到达顺序
};

//观察者桩：记下每一次结果。析构时从业务总控的待决请求上注销自己：断言失败提前退出时，局部的观察者先于夹具析构，
//夹具析构时的 Detach 会把还没有结果的请求按失败回调，不注销就会调用已析构的对象
class ManageObserver : public IShareManageObserver
{
public:
    explicit ManageObserver(CShareService& service)
        : m_service(service)
    {
    }

    ~ManageObserver()
    {
        m_service.DetachManageObserver(this);
    }

    virtual void OnShareManageResult(const ShareManageResult& result) override
    {
        m_results.push_back(result);
    }

    CShareService&                 m_service;   //发起请求的业务总控（夹具的成员，比本对象后析构）
    std::vector<ShareManageResult> m_results;   //收到的结果，按到达顺序
};

//一个已登录的业务总控：个人目录在测试程序目录下，Attach 之后登录（会发一条 2205）
struct ManageFixture
{
    ManageHost    m_host;      //宿主桩
    CShareService m_service;   //被测的业务总控

    ManageFixture()
    {
        const std::wstring base = sharetestfs::ExeDir() + L"\\" + kSsmTestDir;
        sharetestfs::DeleteTree(base);
        sharetestfs::MakeDirs(base);
        m_host.m_personalDir = base + L"\\";
        m_service.Attach(&m_host, NULL, 0);
    }

    ~ManageFixture()
    {
        m_service.Detach();
    }

    //本机库的路径
    std::wstring StorePath() const
    {
        return m_host.m_personalDir + L"ChatLog\\fileshare.db";
    }

    //另开一条连接读本机库里的全部记录
    std::vector<ShareLocalRecord> LocalRecords() const
    {
        std::vector<ShareLocalRecord> out;
        CShareLocalStore store;
        if (store.Open(StorePath().c_str()))
            store.ListShares(out);
        return out;
    }
};

//2201 的应答
std::string CreateResponse(int code, int64_t shareId, const std::string& clientShareId, const char* invalidJson)
{
    std::string s = "{\"code\":" + std::to_string(code) + ",\"msg\":\"x\",\"clientshareid\":\"" + clientShareId + "\"";
    if (shareId > 0)
        s += ",\"shareid\":\"" + std::to_string(shareId) + "\"";
    if (invalidJson != NULL)
        s += std::string(",\"invalidmembers\":") + invalidJson;
    return s + "}";
}

//只有结果码的应答
std::string SimpleResponse(int code, const char* invalidJson)
{
    std::string s = "{\"code\":" + std::to_string(code) + ",\"msg\":\"x\"";
    if (invalidJson != NULL)
        s += std::string(",\"invalidmembers\":") + invalidJson;
    return s + "}";
}

//请求正文里某个字符串字段的值（UTF-8）。正文按 jsoncpp 默认设置写出，中文被转义成反斜杠 u 加四位十六进制，
//直接在正文里找不到，要先解析。正文不是 JSON 对象或没有这个字符串字段时返回空串
std::string BodyString(const std::string& body, const char* key)
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(body.c_str(), body.c_str() + body.size(), &root, &errs) || !root.isObject()
        || !root[key].isString())
        return std::string();
    return root[key].asString();
}

//测试宿主给的本人昵称（ManageHost::GetSessionInfo）没起共享名称时的默认名称（UTF-8）
const char* const kDefaultShareNameUtf8 = u8"ssmtester的文件共享";

//一份群聊发起共享的请求
ShareCreateParams GroupParams()
{
    ShareCreateParams p;
    p.m_ctype = conversation_group;
    p.m_cid = kGroupId;
    p.m_kind = share_kind_folder;
    p.m_name = "root";
    p.m_displayPath = "x";
    ShareMemberPerm a;
    a.m_userId = kMemberA;
    a.m_perms = share_perm_upload;
    p.m_members.push_back(a);
    return p;
}

//建一个存在的共享根目录
std::wstring MakeRoot(const ManageFixture& f)
{
    const std::wstring root = f.m_host.m_personalDir + L"root";
    sharetestfs::MakeDirs(root);
    return root;
}

//发起一次并让它成功，返回 shareid
int64_t CreateOk(ManageFixture& f, ManageObserver& obs)
{
    const std::wstring root = MakeRoot(f);
    const uint32_t id = f.m_service.CreateShare(GroupParams(), root, &obs);
    REQUIRE(id != 0);
    const int idx = f.m_host.IndexOf(cmd_share_create, f.m_host.Count(cmd_share_create) - 1);
    const std::vector<ShareLocalRecord> recs = f.LocalRecords();
    REQUIRE(recs.size() == 1);
    f.m_service.HandleServerFrame(cmd_share_create, f.m_host.m_seqs[idx],
                                  CreateResponse(0, kNewShareId, recs[0].m_clientShareId, NULL));
    return kNewShareId;
}

//群成员数组：本人、A、B
std::vector<FileShareMemberInfo> GroupMembers()
{
    std::vector<FileShareMemberInfo> out;
    const int64_t ids[] = { kSelf, kMemberA, kMemberB };
    const char* const names[] = { "me", "Alice", "Bob" };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        FileShareMemberInfo m;
        memset(&m, 0, sizeof(m));
        m.m_userId = ids[i];
        strcpy_s(m.m_name, kFileShareTextBytes, names[i]);
        out.push_back(m);
    }
    return out;
}

//模块桩：记下浮层请模块打开的窗口
class FakeUiHost : public IShareUiHost
{
public:
    FakeUiHost()
        : m_createCtype(0)
        , m_createCid(0)
        , m_openShareId(0)
        , m_editShareId(0)
    {
    }

    virtual void UiOpenCreateShare(HWND owner, int ctype, int64_t cid, bool openConversationAfter) override
    {
        (void)owner;
        m_createCtype = ctype;
        m_createCid = cid;
        m_createOpenAfter = openConversationAfter;
    }

    virtual bool UiOpenShareCenter(HWND owner, int ctype, int64_t cid) override
    {
        (void)owner;
        m_centerCtype = ctype;
        m_centerCid = cid;
        return true;
    }

    virtual void UiOpenShare(HWND owner, int64_t shareId) override
    {
        (void)owner;
        m_openShareId = shareId;
    }

    virtual void UiEditMembers(HWND owner, const ShareListEntry& share) override
    {
        (void)owner;
        m_editShareId = share.m_shareId;
    }

    virtual bool UiOpenImage(HWND owner, const std::wstring& path, IShareImagePreviewSource* source) override
    {
        (void)owner;
        (void)path;
        (void)source;
        return false;
    }

    virtual void UiPreviewSourceGone(IShareImagePreviewSource* source) override
    {
        (void)source;
    }

    virtual bool UiStartDirectShare(HWND owner) override
    {
        (void)owner;
        ++m_directShareRequests;
        return m_directShareOk;
    }

    int     m_directShareRequests = 0;   //「发起共享」的请求次数
    bool    m_directShareOk = true;      //UiStartDirectShare 的返回值（用例可改为 false）
    bool    m_createOpenAfter = false;   //最近一次请求发起共享时，是否要求成功后打开聊天窗
    int     m_centerCtype = 0;           //最近一次请求打开「文件共享」窗口时记住的会话类型
    int64_t m_centerCid = 0;             //最近一次请求打开「文件共享」窗口时记住的会话 id
    int     m_createCtype;   //最近一次请求发起共享的会话类型
    int64_t m_createCid;     //最近一次请求发起共享的会话 id
    int64_t m_openShareId;   //最近一次请求打开的共享
    int64_t m_editShareId;   //最近一次请求调整成员的共享
};

//2205 应答里的一个共享（JSON 片段）
std::string ListItemJson(int64_t shareId, int64_t ownerId, int64_t createTime, int state, const char* name)
{
    return "{\"shareid\":\"" + std::to_string(shareId) + "\",\"ownerid\":\"" + std::to_string(ownerId)
        + "\",\"ctype\":2,\"cid\":\"" + std::to_string(kGroupId) + "\",\"kind\":2,\"name\":\"" + name
        + "\",\"displaypath\":\"D:\\\\x\",\"includehidden\":0,\"state\":" + std::to_string(state)
        + ",\"reason\":0,\"createtime\":" + std::to_string(createTime) + ",\"onthisdevice\":1,\"members\":[],\"myperms\":0}";
}

//2205 应答
std::string ListResponse(int scope, const std::string& itemsJson)
{
    return "{\"code\":0,\"msg\":\"ok\",\"scope\":" + std::to_string(scope) + ",\"shares\":[" + itemsJson + "]}";
}

//2205 应答里的一个共享，可指定成员与本人的权限（JSON 片段）
std::string ListItemJsonEx(int64_t shareId, int64_t ownerId, int state, const char* name, const char* membersJson,
                           int myPerms)
{
    return "{\"shareid\":\"" + std::to_string(shareId) + "\",\"ownerid\":\"" + std::to_string(ownerId)
        + "\",\"ctype\":2,\"cid\":\"" + std::to_string(kGroupId) + "\",\"kind\":2,\"name\":\"" + name
        + "\",\"displaypath\":\"D:\\\\x\",\"includehidden\":0,\"state\":" + std::to_string(state)
        + ",\"reason\":0,\"createtime\":100,\"onthisdevice\":1,\"members\":" + membersJson + ",\"myperms\":"
        + std::to_string(myPerms) + "}";
}

//打开「文件共享」窗口并让两份列表到达：我的两个（一个可用带两名成员、一个已失效），共享给我的一个
void OpenCenterWithRows(ManageFixture& f, CShareCenterWindow& center, FakeUiHost& ui)
{
    const size_t before = f.m_host.m_cmds.size();
    REQUIRE(center.Open(NULL, &f.m_service, &f.m_host, &ui));
    REQUIRE(f.m_host.m_cmds.size() == before + 2);
    CHECK(f.m_host.m_bodies[before] == ShareProtocolCodec::BuildListRequest(kShareListScopeMine, 0, 0));
    CHECK(f.m_host.m_bodies[before + 1] == ShareProtocolCodec::BuildListRequest(kShareListScopeSharedMe, 0, 0));
    const std::string members = "[{\"userid\":\"30002\",\"perms\":1},{\"userid\":\"30003\",\"perms\":0}]";
    f.m_service.HandleServerFrame(cmd_share_list, f.m_host.m_seqs[before],
                                  ListResponse(kShareListScopeMine,
                                               ListItemJsonEx(kShareMine, kSelf, share_state_available, "mine", members.c_str(), 0)
                                               + "," + ListItemJsonEx(kShareGone, kSelf, share_state_invalid, "gone", "[]", 0)));
    f.m_service.HandleServerFrame(cmd_share_list, f.m_host.m_seqs[before + 1],
                                  ListResponse(kShareListScopeSharedMe,
                                               ListItemJsonEx(kShareOthers, kMemberA, share_state_available, "others", "[]",
                                                              share_perm_upload)));
}

//详情里有没有某一行
bool HasLine(const std::vector<std::wstring>& lines, const std::wstring& line)
{
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i] == line)
            return true;
    }
    return false;
}

//弹出群聊浮层并让两份列表到达：我的一个、别人的一个、已失效的一个；返回发出两份 2205 之前的命令数
void OpenPopupWithRows(ManageFixture& f, CShareConversationPopup& popup, FakeUiHost& ui)
{
    const size_t before = f.m_host.m_cmds.size();
    REQUIRE(popup.Open(NULL, &f.m_service, &f.m_host, &ui, conversation_group, kGroupId, NULL));
    REQUIRE(f.m_host.m_cmds.size() == before + 2);
    CHECK(f.m_host.m_bodies[before] == ShareProtocolCodec::BuildListRequest(kShareListScopeMine, conversation_group, kGroupId));
    CHECK(f.m_host.m_bodies[before + 1]
          == ShareProtocolCodec::BuildListRequest(kShareListScopeSharedMe, conversation_group, kGroupId));
    CHECK(popup.GetStatusText().find(L"正在加载") != std::wstring::npos);
    f.m_service.HandleServerFrame(cmd_share_list, f.m_host.m_seqs[before],
                                  ListResponse(kShareListScopeMine, ListItemJson(kShareMine, kSelf, 100, share_state_available, "mine")));
    CHECK(popup.GetRowCount() == 0);   //两份都到了才显示
    f.m_service.HandleServerFrame(cmd_share_list, f.m_host.m_seqs[before + 1],
                                  ListResponse(kShareListScopeSharedMe,
                                               ListItemJson(kShareOthers, kMemberA, 200, share_state_unavailable, "others") + ","
                                               + ListItemJson(kShareGone, kMemberA, 300, share_state_invalid, "gone")));
}

} // namespace

TEST_CASE("SSM1 create share succeeds", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    ManageObserver obs(f.m_service);
    const std::wstring root = MakeRoot(f);

    const uint32_t id = f.m_service.CreateShare(GroupParams(), root, &obs);
    REQUIRE(id != 0);
    CHECK(f.m_service.GetPendingManageCount() == 1);
    REQUIRE(f.m_host.Count(cmd_share_create) == 1);
    //先写本机记录（还没有 shareid）再发请求
    std::vector<ShareLocalRecord> recs = f.LocalRecords();
    REQUIRE(recs.size() == 1);
    CHECK(recs[0].m_shareId == 0);
    CHECK(recs[0].m_rootPath == root);
    const std::string clientShareId = recs[0].m_clientShareId;
    CHECK(clientShareId.size() == 36);
    const int idx = f.m_host.IndexOf(cmd_share_create, 0);
    CHECK(f.m_host.m_bodies[idx].find(clientShareId) != std::string::npos);

    f.m_service.HandleServerFrame(cmd_share_create, f.m_host.m_seqs[idx], CreateResponse(0, kNewShareId, clientShareId, NULL));
    REQUIRE(obs.m_results.size() == 1);
    CHECK(obs.m_results[0].m_requestId == id);
    CHECK(obs.m_results[0].m_kind == share_manage_create);
    CHECK(obs.m_results[0].m_code == 0);
    CHECK(obs.m_results[0].m_shareId == kNewShareId);
    CHECK(f.m_service.GetPendingManageCount() == 0);
    recs = f.LocalRecords();
    REQUIRE(recs.size() == 1);
    CHECK(recs[0].m_shareId == kNewShareId);
    //立即报告可提供
    REQUIRE(f.m_host.Count(cmd_share_serve_report) == 1);
    const std::string report = f.m_host.m_bodies[f.m_host.IndexOf(cmd_share_serve_report, 0)];
    CHECK(report.find("\"7701\"") != std::string::npos);
    CHECK(report.find("\"available\":1") != std::string::npos);
    CHECK(f.m_service.GetLocalStatus(kNewShareId) == share_local_status_reported);
}

TEST_CASE("SSM2 create share rejected", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    ManageObserver obs(f.m_service);
    const std::wstring root = MakeRoot(f);
    REQUIRE(f.m_service.CreateShare(GroupParams(), root, &obs) != 0);
    const std::string clientShareId = f.LocalRecords()[0].m_clientShareId;
    const int idx = f.m_host.IndexOf(cmd_share_create, 0);

    f.m_service.HandleServerFrame(cmd_share_create, f.m_host.m_seqs[idx],
                                  CreateResponse(error_code_share_cross_enterprise, 0, clientShareId, "[\"30002\"]"));
    REQUIRE(obs.m_results.size() == 1);
    CHECK(obs.m_results[0].m_code == error_code_share_cross_enterprise);
    REQUIRE(obs.m_results[0].m_invalidMembers.size() == 1);
    CHECK(obs.m_results[0].m_invalidMembers[0] == kMemberA);
    CHECK(f.LocalRecords().empty());
    CHECK(f.m_host.Count(cmd_share_serve_report) == 0);

    //没有对应请求的应答（重复到达）不回调
    f.m_service.HandleServerFrame(cmd_share_create, f.m_host.m_seqs[idx], CreateResponse(0, kNewShareId, clientShareId, NULL));
    CHECK(obs.m_results.size() == 1);
}

TEST_CASE("SSM3 create share timeout resend", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    f.m_service.SetManageTimeoutMs(kShortTimeoutMs);
    ManageObserver obs(f.m_service);
    const std::wstring root = MakeRoot(f);

    //第一次超时后重发，包体逐字相同；随后重发前那一次的应答才到，按 clientshareid 配上
    REQUIRE(f.m_service.CreateShare(GroupParams(), root, &obs) != 0);
    ::Sleep(kSleepPastTimeoutMs);
    f.m_service.OnTimer();
    REQUIRE(f.m_host.Count(cmd_share_create) == 2);
    const int first = f.m_host.IndexOf(cmd_share_create, 0);
    const int second = f.m_host.IndexOf(cmd_share_create, 1);
    CHECK(f.m_host.m_bodies[first] == f.m_host.m_bodies[second]);
    CHECK(f.m_host.m_seqs[first] != f.m_host.m_seqs[second]);
    const std::string clientShareId = f.LocalRecords()[0].m_clientShareId;
    f.m_service.HandleServerFrame(cmd_share_create, f.m_host.m_seqs[first], CreateResponse(0, kNewShareId, clientShareId, NULL));
    REQUIRE(obs.m_results.size() == 1);
    CHECK(obs.m_results[0].m_code == 0);
    CHECK(f.LocalRecords()[0].m_shareId == kNewShareId);

    //一直没有应答：发满次数后以「没有应答」结束，删除本机记录
    ManageObserver lost(f.m_service);
    const std::wstring root2 = f.m_host.m_personalDir + L"root2";
    sharetestfs::MakeDirs(root2);
    REQUIRE(f.m_service.CreateShare(GroupParams(), root2, &lost) != 0);
    CHECK(f.LocalRecords().size() == 2);
    for (int i = 0; i < kShareCreateMaxAttempts; ++i)
    {
        ::Sleep(kSleepPastTimeoutMs);
        f.m_service.OnTimer();
    }
    CHECK(f.m_host.Count(cmd_share_create) == 2 + kShareCreateMaxAttempts);
    REQUIRE(lost.m_results.size() == 1);
    CHECK(lost.m_results[0].m_code == kShareManageNoResponse);
    CHECK(f.LocalRecords().size() == 1);
}

TEST_CASE("SSM4 cancel and update members", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    ManageObserver obs(f.m_service);
    const int64_t shareId = CreateOk(f, obs);
    REQUIRE(f.LocalRecords().size() == 1);

    //取消失败：本机记录保留
    const uint32_t cancel1 = f.m_service.CancelShare(shareId, &obs);
    REQUIRE(cancel1 != 0);
    int idx = f.m_host.IndexOf(cmd_share_cancel, 0);
    CHECK(f.m_host.m_bodies[idx] == ShareProtocolCodec::BuildShareIdRequest(shareId));
    f.m_service.HandleServerFrame(cmd_share_cancel, f.m_host.m_seqs[idx], SimpleResponse(error_code_share_permission_denied, NULL));
    REQUIRE(obs.m_results.size() == 2);
    CHECK(obs.m_results[1].m_kind == share_manage_cancel);
    CHECK(obs.m_results[1].m_code == error_code_share_permission_denied);
    CHECK(obs.m_results[1].m_shareId == shareId);
    CHECK(f.LocalRecords().size() == 1);

    //调整成员：被拒时带回不满足条件的成员
    std::vector<ShareMemberPerm> add(1);
    add[0].m_userId = kMemberC;
    std::vector<int64_t> remove(1, kMemberA);
    std::vector<ShareMemberPerm> update;
    REQUIRE(f.m_service.UpdateMembers(shareId, add, remove, update, &obs) != 0);
    idx = f.m_host.IndexOf(cmd_share_update_members, 0);
    CHECK(f.m_host.m_bodies[idx] == ShareProtocolCodec::BuildUpdateMembersRequest(shareId, add, remove, update));
    f.m_service.HandleServerFrame(cmd_share_update_members, f.m_host.m_seqs[idx],
                                  SimpleResponse(error_code_share_cross_enterprise, "[\"30004\"]"));
    REQUIRE(obs.m_results.size() == 3);
    CHECK(obs.m_results[2].m_kind == share_manage_update);
    REQUIRE(obs.m_results[2].m_invalidMembers.size() == 1);
    CHECK(obs.m_results[2].m_invalidMembers[0] == kMemberC);

    //三组都为空不发送
    CHECK(f.m_service.UpdateMembers(shareId, std::vector<ShareMemberPerm>(), std::vector<int64_t>(),
                                    std::vector<ShareMemberPerm>(), &obs) == 0);

    //观察者解除登记后不再回调，请求照常进行
    REQUIRE(f.m_service.UpdateMembers(shareId, add, std::vector<int64_t>(), update, &obs) != 0);
    f.m_service.DetachManageObserver(&obs);
    idx = f.m_host.IndexOf(cmd_share_update_members, 1);
    f.m_service.HandleServerFrame(cmd_share_update_members, f.m_host.m_seqs[idx], SimpleResponse(0, NULL));
    CHECK(obs.m_results.size() == 3);
    CHECK(f.m_service.GetPendingManageCount() == 0);

    //取消成功：删除本机记录
    REQUIRE(f.m_service.CancelShare(shareId, &obs) != 0);
    idx = f.m_host.IndexOf(cmd_share_cancel, 1);
    f.m_service.HandleServerFrame(cmd_share_cancel, f.m_host.m_seqs[idx], SimpleResponse(0, NULL));
    REQUIRE(obs.m_results.size() == 4);
    CHECK(obs.m_results[3].m_code == 0);
    CHECK(f.LocalRecords().empty());
}

TEST_CASE("SSM5 logout fails pending requests, login removes stale pending records", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    ManageObserver obs(f.m_service);
    const std::wstring root = MakeRoot(f);
    REQUIRE(f.m_service.CreateShare(GroupParams(), root, &obs) != 0);
    REQUIRE(f.m_service.CancelShare(kNewShareId, &obs) != 0);

    f.m_service.OnLoggingOut();
    REQUIRE(obs.m_results.size() == 2);
    CHECK(obs.m_results[0].m_code == kShareManageNoResponse);
    CHECK(obs.m_results[1].m_code == kShareManageNoResponse);
    CHECK(f.LocalRecords().empty());

    //上次进程在应答回来之前退出：留下一条没有 shareid 的记录，下次登录时清理
    {
        CShareLocalStore store;
        REQUIRE(store.Open(f.StorePath().c_str()));
        REQUIRE(store.AddPendingShare("stale-1", root, share_kind_folder, false, 1));
        store.Close();
    }
    CHECK(f.LocalRecords().size() == 1);
    f.m_service.OnLoggedIn();
    CHECK(f.LocalRecords().empty());
}

TEST_CASE("SCD1 group create dialog", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    CShareCreateDialog dialog;
    REQUIRE(dialog.OpenForCreate(NULL, &f.m_service, &f.m_host, conversation_group, kGroupId));
    REQUIRE(f.m_host.m_memberRequests.size() == 1);
    //程序图标：标题栏左侧的位图与任务栏 / Alt-Tab 的大、小图标都已设置
    CHECK(dialog.GetIcon() != NULL);
    CHECK(::SendMessage(dialog.m_hWnd, WM_GETICON, ICON_BIG, 0) != 0);
    CHECK(::SendMessage(dialog.m_hWnd, WM_GETICON, ICON_SMALL, 0) != 0);
    CHECK(f.m_host.m_memberRequests[0].first == conversation_group);
    CHECK(f.m_host.m_memberRequests[0].second == kGroupId);

    //候选不含本人
    const std::vector<FileShareMemberInfo> members = GroupMembers();
    dialog.OnConversationMembers(conversation_group, kGroupId, 0, &members[0], static_cast<uint32_t>(members.size()));
    REQUIRE(dialog.GetMemberTable().Rows().size() == 2);
    CHECK(dialog.GetMemberTable().Rows()[0].m_userId == kMemberA);

    //「共享名称」框：空着、可编辑，提示文字就是默认名称
    std::wstring nameText;
    std::wstring namePlaceholder;
    bool nameEnabled = false;
    REQUIRE(dialog.GetShareNameState(nameText, namePlaceholder, nameEnabled));
    CHECK(nameText.empty());
    CHECK(namePlaceholder == L"ssmtester的文件共享");
    CHECK(nameEnabled);

    //没选对象、没选成员
    CHECK_FALSE(dialog.Submit(true));
    CHECK(dialog.GetStatusText().find(L"请先选择") != std::wstring::npos);
    const std::wstring root = MakeRoot(f);
    dialog.SetShareObject(root, share_kind_folder, L"");
    CHECK_FALSE(dialog.Submit(true));
    CHECK(dialog.GetStatusText().find(L"至少勾选") != std::wstring::npos);

    //勾选两人，A 有上传权限
    dialog.GetMemberTable().SetChecked(kMemberA, true);
    dialog.GetMemberTable().SetChecked(kMemberB, true);
    dialog.GetMemberTable().SetPerm(kMemberA, share_perm_upload, true);
    dialog.RefreshMemberRows();
    REQUIRE(dialog.Submit(true));
    CHECK(dialog.IsBusy());
    int idx = f.m_host.IndexOf(cmd_share_create, 0);
    const std::string body = f.m_host.m_bodies[idx];
    CHECK(body.find("\"kind\":2") != std::string::npos);
    CHECK(body.find("\"cid\":\"900100\"") != std::string::npos);
    CHECK(body.find("{\"perms\":1,\"userid\":\"30002\"}") != std::string::npos);
    CHECK(body.find("{\"perms\":0,\"userid\":\"30003\"}") != std::string::npos);
    //没起名字：发出默认名称，不是文件夹名
    CHECK(BodyString(body, "name") == kDefaultShareNameUtf8);

    //被拒：标出 B，显示原因
    std::string clientShareId = f.LocalRecords()[0].m_clientShareId;
    f.m_service.HandleServerFrame(cmd_share_create, f.m_host.m_seqs[idx],
                                  CreateResponse(error_code_share_cross_enterprise, 0, clientShareId, "[\"30003\"]"));
    CHECK_FALSE(dialog.IsBusy());
    CHECK(dialog.GetMemberTable().Rows()[1].m_invalid);
    CHECK(dialog.GetStatusText().find(L"其他企业") != std::wstring::npos);
    CHECK(dialog.IsWindow());

    //去掉 B、起个名字再发起（首尾空白去掉），成功后关闭
    dialog.GetMemberTable().SetChecked(kMemberB, false);
    dialog.RefreshMemberRows();
    dialog.SetShareNameText(L"  Q3 docs  ");
    REQUIRE(dialog.Submit(true));
    idx = f.m_host.IndexOf(cmd_share_create, 1);
    CHECK(BodyString(f.m_host.m_bodies[idx], "name") == "Q3 docs");
    clientShareId = f.LocalRecords()[0].m_clientShareId;
    f.m_service.HandleServerFrame(cmd_share_create, f.m_host.m_seqs[idx], CreateResponse(0, kNewShareId, clientShareId, NULL));
    CHECK_FALSE(dialog.IsWindow());
}

TEST_CASE("SCD2 single chat file share", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    CShareCreateDialog dialog;
    REQUIRE(dialog.OpenForCreate(NULL, &f.m_service, &f.m_host, conversation_single, kMemberA));
    FileShareMemberInfo peer;
    memset(&peer, 0, sizeof(peer));
    peer.m_userId = kMemberA;
    strcpy_s(peer.m_name, kFileShareTextBytes, "Alice");
    dialog.OnConversationMembers(conversation_single, kMemberA, 0, &peer, 1);
    REQUIRE(dialog.GetMemberTable().Rows().size() == 1);
    CHECK(dialog.GetMemberTable().Rows()[0].m_fixed);
    CHECK(dialog.GetMemberTable().Rows()[0].m_checked);
    dialog.GetMemberTable().SetChecked(kMemberA, false);
    CHECK(dialog.GetMemberTable().Rows()[0].m_checked);

    //单个文件：没有权限，不带包含隐藏文件
    const std::wstring file = f.m_host.m_personalDir + L"a.txt";
    REQUIRE(sharetestfs::WriteFileBytes(file, kFileBytes));
    dialog.SetShareObject(file, share_kind_file, L"");
    CHECK(dialog.GetMemberTable().IsFileShare());
    dialog.GetMemberTable().SetPerm(kMemberA, share_perm_upload, true);
    REQUIRE(dialog.Submit(true));
    const std::string body = f.m_host.m_bodies[f.m_host.IndexOf(cmd_share_create, 0)];
    CHECK(body.find("\"kind\":3") != std::string::npos);
    CHECK(body.find("\"ctype\":1") != std::string::npos);
    CHECK(body.find("\"includehidden\":0") != std::string::npos);
    CHECK(body.find("{\"perms\":0,\"userid\":\"30002\"}") != std::string::npos);
    //共享名称不是文件名（文件名只在打开共享后、浏览窗口里看到）
    CHECK(BodyString(body, "name") == kDefaultShareNameUtf8);
    dialog.Close();
}

TEST_CASE("SCD3 edit members dialog", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    ShareListEntry share;
    share.m_shareId = kNewShareId;
    share.m_ctype = conversation_group;
    share.m_cid = kGroupId;
    share.m_kind = share_kind_folder;
    share.m_name = "root";
    share.m_displayPath = "D:\\root";
    ShareMemberPerm a;
    a.m_userId = kMemberA;
    a.m_perms = share_perm_upload;
    ShareMemberPerm c;
    c.m_userId = kMemberC;   //已不在群里
    share.m_members.push_back(a);
    share.m_members.push_back(c);

    CShareCreateDialog dialog;
    REQUIRE(dialog.OpenForEdit(NULL, &f.m_service, &f.m_host, share));
    //「共享名称」框显示当前名称，不可编辑
    std::wstring nameText;
    std::wstring namePlaceholder;
    bool nameEnabled = true;
    REQUIRE(dialog.GetShareNameState(nameText, namePlaceholder, nameEnabled));
    CHECK(nameText == L"root");
    CHECK_FALSE(nameEnabled);
    const std::vector<FileShareMemberInfo> members = GroupMembers();
    dialog.OnConversationMembers(conversation_group, kGroupId, 0, &members[0], static_cast<uint32_t>(members.size()));
    const std::vector<ShareMemberRow>& rows = dialog.GetMemberTable().Rows();
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].m_userId == kMemberA);
    CHECK(rows[0].m_checked);
    CHECK(rows[0].m_perms == share_perm_upload);
    CHECK_FALSE(rows[1].m_checked);
    CHECK(rows[2].m_userId == kMemberC);
    CHECK(rows[2].m_name == "user30004");
    CHECK(rows[2].m_checked);

    //没有变化
    CHECK_FALSE(dialog.Submit(true));
    CHECK(dialog.GetStatusText().find(L"没有变化") != std::wstring::npos);

    //移除 C、追加 B、A 加上删除权限
    dialog.GetMemberTable().SetChecked(kMemberC, false);
    dialog.GetMemberTable().SetChecked(kMemberB, true);
    dialog.GetMemberTable().SetPerm(kMemberA, share_perm_delete, true);
    dialog.RefreshMemberRows();
    REQUIRE(dialog.Submit(true));
    const int idx = f.m_host.IndexOf(cmd_share_update_members, 0);
    std::vector<ShareMemberPerm> add(1);
    add[0].m_userId = kMemberB;
    std::vector<ShareMemberPerm> update(1);
    update[0].m_userId = kMemberA;
    update[0].m_perms = share_perm_upload | share_perm_delete;
    CHECK(f.m_host.m_bodies[idx]
          == ShareProtocolCodec::BuildUpdateMembersRequest(kNewShareId, add, std::vector<int64_t>(1, kMemberC), update));
    f.m_service.HandleServerFrame(cmd_share_update_members, f.m_host.m_seqs[idx], SimpleResponse(0, NULL));
    CHECK_FALSE(dialog.IsWindow());
}

TEST_CASE("SCD8 share name rejected for a sensitive word", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    CShareCreateDialog dialog;
    REQUIRE(dialog.OpenForCreate(NULL, &f.m_service, &f.m_host, conversation_group, kGroupId));
    const std::vector<FileShareMemberInfo> members = GroupMembers();
    dialog.OnConversationMembers(conversation_group, kGroupId, 0, &members[0], static_cast<uint32_t>(members.size()));
    dialog.SetShareObject(MakeRoot(f), share_kind_folder, L"");
    dialog.GetMemberTable().SetChecked(kMemberA, true);
    dialog.RefreshMemberRows();
    dialog.SetShareNameText(L"bad name");
    REQUIRE(dialog.Submit(true));
    int idx = f.m_host.IndexOf(cmd_share_create, 0);
    CHECK(BodyString(f.m_host.m_bodies[idx], "name") == "bad name");

    //服务端回 822：说明原因，对话框不关、不再忙碌
    std::string clientShareId = f.LocalRecords()[0].m_clientShareId;
    f.m_service.HandleServerFrame(cmd_share_create, f.m_host.m_seqs[idx],
                                  CreateResponse(error_code_share_name_sensitive, 0, clientShareId, NULL));
    CHECK(dialog.IsWindow());
    CHECK_FALSE(dialog.IsBusy());
    CHECK(dialog.GetStatusText().find(L"敏感词") != std::wstring::npos);

    //改名后重新发起，成功后关闭
    dialog.SetShareNameText(L"good name");
    REQUIRE(dialog.Submit(true));
    idx = f.m_host.IndexOf(cmd_share_create, 1);
    CHECK(BodyString(f.m_host.m_bodies[idx], "name") == "good name");
    clientShareId = f.LocalRecords()[0].m_clientShareId;
    f.m_service.HandleServerFrame(cmd_share_create, f.m_host.m_seqs[idx], CreateResponse(0, kNewShareId, clientShareId, NULL));
    CHECK_FALSE(dialog.IsWindow());
}

namespace {

//对话框里表头各权限列的第一个 ctrlId，与 ShareCreateDialog.cpp 的 kIdColumnBase 相同
const UINT kDlgColumnBase = 211;
//对话框里成员行控件的起始 ctrlId 与每行占用的个数，与 ShareCreateDialog.cpp 的 kIdMemberBase、kIdsPerRow 相同
const UINT kDlgMemberBase = 1000;
const UINT kDlgIdsPerRow = 8;
//Checkbox 风格的 DuiButton 在文字左侧留出的宽度，单位：像素：左内边距 6 + 勾选框 16 + 与文字的间距 6（balloonui DuiButton.cpp 的
//kGlyphPadPx、kGlyphSizePx，不随 DPI 缩放）
const int kDlgCheckGlyphWidth = 28;

//用控件实际使用的字体量一段文字的宽度，单位：像素
int TextWidthWithFont(HFONT font, const wchar_t* text)
{
    HDC hdc = ::GetDC(NULL);
    HGDIOBJ oldFont = ::SelectObject(hdc, font);
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &size);
    ::SelectObject(hdc, oldFont);
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

//检查表头五个权限列都放得下「勾选框 + 文字」，且每个成员行的权限勾选框与表头同一列左对齐、同宽
void CheckPermColumnsFit(CShareCreateDialog& dialog, size_t memberRows)
{
    balloonwjui::DuiControl* root = dialog.GetRoot();
    REQUIRE(root != NULL);
    for (size_t c = 0; c < kShareMemberPermColumnCount; ++c)
    {
        balloonwjui::DuiButton* column =
            static_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kDlgColumnBase + static_cast<UINT>(c)));
        REQUIRE(column != NULL);
        const RECT rc = column->GetRect();
        const int need = kDlgCheckGlyphWidth + TextWidthWithFont(column->GetDefaultFont(), column->GetText());
        INFO("column " << c << " width " << (rc.right - rc.left) << " need " << need);
        CHECK(rc.right - rc.left >= need);
        for (size_t r = 0; r < memberRows; ++r)
        {
            const UINT id = kDlgMemberBase + static_cast<UINT>(r) * kDlgIdsPerRow + 1 + static_cast<UINT>(c);
            balloonwjui::DuiControl* perm = root->FindCtrlById(id);
            REQUIRE(perm != NULL);
            CHECK(perm->GetRect().left == rc.left);
            CHECK(perm->GetRect().right == rc.right);
        }
    }
}

} // namespace

TEST_CASE("SCD5 permission columns fit their header text at any DPI", "[fileshare][sharemanage]")
{
    //模拟窗口挪到另一块显示器时 DPI 放大的倍数
    const int kDpiFactor = 2;

    ManageFixture f;
    f.m_service.OnLoggedIn();
    CShareCreateDialog dialog;
    REQUIRE(dialog.OpenForCreate(NULL, &f.m_service, &f.m_host, conversation_group, kGroupId));
    const std::vector<FileShareMemberInfo> members = GroupMembers();
    dialog.OnConversationMembers(conversation_group, kGroupId, 0, &members[0], static_cast<uint32_t>(members.size()));
    const size_t rows = dialog.GetMemberTable().Rows().size();
    REQUIRE(rows == 2);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CheckPermColumnsFit(dialog, rows);

    //窗口挪到 DPI 更高的显示器：字体变大，「新建文件夹」等表头文字仍然完整显示，成员行的勾选框仍与表头对齐
    balloonwjui::DuiControl* firstColumn = dialog.GetRoot()->FindCtrlById(kDlgColumnBase);
    REQUIRE(firstColumn != NULL);
    const int largerDpi = firstColumn->GetDpi() * kDpiFactor;
    RECT wnd;
    ::GetWindowRect(dialog.m_hWnd, &wnd);
    ::SendMessage(dialog.m_hWnd, WM_DPICHANGED, MAKEWPARAM(largerDpi, largerDpi), reinterpret_cast<LPARAM>(&wnd));
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    REQUIRE(firstColumn->GetDpi() == largerDpi);
    CheckPermColumnsFit(dialog, rows);
    dialog.Close();
}

TEST_CASE("SCD4 create dialog snapshots", "[.snapshot][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();

    //群聊共享文件夹：两人有勾选与权限，一人被标为不满足条件
    {
        CShareCreateDialog dialog;
        REQUIRE(dialog.OpenForCreate(NULL, &f.m_service, &f.m_host, conversation_group, kGroupId));
        const char* const names[kSnapshotMembers] = { "me", "张三", "李四", "王五", "赵六", "孙七", "周八", "吴九",
                                                      "郑十", "钱一", "冯二", "陈三", "褚四", "卫五", "蒋六", "沈七" };
        std::vector<FileShareMemberInfo> members;
        for (size_t i = 0; i < kSnapshotMembers; ++i)
        {
            FileShareMemberInfo m;
            memset(&m, 0, sizeof(m));
            m.m_userId = kSelf + static_cast<int64_t>(i);
            strcpy_s(m.m_name, kFileShareTextBytes, names[i]);
            members.push_back(m);
        }
        dialog.OnConversationMembers(conversation_group, kGroupId, 0, &members[0], static_cast<uint32_t>(members.size()));
        dialog.SetShareObject(L"D:\\Projects\\Flamingo", share_kind_folder, L"");
        dialog.GetMemberTable().SetChecked(kMemberA, true);
        dialog.GetMemberTable().SetChecked(kMemberB, true);
        dialog.GetMemberTable().SetChecked(kMemberC, true);
        dialog.GetMemberTable().SetPerm(kMemberA, share_perm_upload, true);
        dialog.GetMemberTable().SetPerm(kMemberA, share_perm_delete, true);
        dialog.GetMemberTable().SetColumn(share_perm_mkdir, true);
        dialog.GetMemberTable().MarkInvalid(std::vector<int64_t>(1, kMemberC));
        dialog.RefreshMemberRows();
        sharesnapshot::PumpMessagesFor(kPaintWaitMs);
        CHECK(sharesnapshot::SaveWindowPng(dialog.m_hWnd, sharetestfs::ExeDir() + kSnapshotGroup));
        dialog.Close();
    }

    //单聊共享单个文件：没有权限列与「包含隐藏文件」
    {
        CShareCreateDialog dialog;
        REQUIRE(dialog.OpenForCreate(NULL, &f.m_service, &f.m_host, conversation_single, kMemberA));
        FileShareMemberInfo peer;
        memset(&peer, 0, sizeof(peer));
        peer.m_userId = kMemberA;
        strcpy_s(peer.m_name, kFileShareTextBytes, "张三");
        dialog.OnConversationMembers(conversation_single, kMemberA, 0, &peer, 1);
        dialog.SetShareObject(L"D:\\docs\\方案.docx", share_kind_file, L"");
        sharesnapshot::PumpMessagesFor(kPaintWaitMs);
        CHECK(sharesnapshot::SaveWindowPng(dialog.m_hWnd, sharetestfs::ExeDir() + kSnapshotFile));
        dialog.Close();
    }
}

TEST_CASE("SCS1 card state cache and host notify", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    ShareCardState st;
    CHECK_FALSE(f.m_service.GetCardState(kShareOthers, st));

    //2205 应答补缺，不通知宿主
    REQUIRE(f.m_service.RequestList(kShareListScopeSharedMe, 0, 0, NULL) != 0);
    f.m_service.HandleServerFrame(cmd_share_list, f.m_host.m_seqs.back(),
                                  ListResponse(kShareListScopeSharedMe, ListItemJson(kShareOthers, kMemberA, 200, share_state_available, "x")));
    REQUIRE(f.m_service.GetCardState(kShareOthers, st));
    CHECK(st.m_state == share_state_available);
    CHECK(st.m_myPerms == 0);
    CHECK(f.m_host.m_notified.empty());

    //2203 变为暂不可用：更新并通知
    f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8802\",\"state\":2,\"reason\":1}");
    REQUIRE(f.m_host.m_notified.size() == 1);
    CHECK(f.m_host.m_notified[0].m_shareId == kShareOthers);
    CHECK(f.m_host.m_notified[0].m_state == share_state_unavailable);
    CHECK(f.m_host.m_notified[0].m_reason == share_unavailable_owner_offline);
    //同样的状态再来一次：没有变化，不再通知
    f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8802\",\"state\":2,\"reason\":1}");
    CHECK(f.m_host.m_notified.size() == 1);

    //推给本人的 2206：权限变化，状态保留
    f.m_service.HandleServerFrame(cmd_share_perms_notify, 0, "{\"shareid\":\"8802\",\"userid\":\"30001\",\"perms\":5}");
    REQUIRE(f.m_host.m_notified.size() == 2);
    CHECK(f.m_host.m_notified[1].m_perms == 5);
    CHECK(f.m_host.m_notified[1].m_state == share_state_unavailable);
    REQUIRE(f.m_service.GetCardState(kShareOthers, st));
    CHECK(st.m_myPerms == 5);
    //推给分享者的 2206（别人的权限）不改本人的卡片
    f.m_service.HandleServerFrame(cmd_share_perms_notify, 0, "{\"shareid\":\"8802\",\"userid\":\"30002\",\"perms\":1}");
    CHECK(f.m_host.m_notified.size() == 2);

    //退出登录清空
    f.m_service.OnLoggingOut();
    CHECK_FALSE(f.m_service.GetCardState(kShareOthers, st));
}

TEST_CASE("SCS2 canceled card state is final, member removal is not", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    ShareCardState st;

    //群主或管理员取消之后迟到的「可用」（服务端处理 2210 与处理取消在不同线程上）：卡片保持已取消、不再通知宿主
    f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8802\",\"state\":1,\"reason\":0}");
    f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8802\",\"state\":3,\"reason\":2}");
    const size_t notified = f.m_host.m_notified.size();
    f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8802\",\"state\":1,\"reason\":0}");
    f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8802\",\"state\":2,\"reason\":1}");
    REQUIRE(f.m_service.GetCardState(kShareOthers, st));
    CHECK(st.m_state == share_state_invalid);
    CHECK(st.m_reason == share_invalid_group_admin_canceled);
    CHECK(f.m_host.m_notified.size() == notified);
    //2205 应答里的旧状态同样不改回
    REQUIRE(f.m_service.RequestList(kShareListScopeSharedMe, 0, 0, NULL) != 0);
    f.m_service.HandleServerFrame(cmd_share_list, f.m_host.m_seqs.back(),
                                  ListResponse(kShareListScopeSharedMe, ListItemJson(kShareOthers, kMemberA, 200, share_state_available, "x")));
    REQUIRE(f.m_service.GetCardState(kShareOthers, st));
    CHECK(st.m_state == share_state_invalid);

    //本人被移出成员的「已失效」不是终态：重新被加入后恢复为可用
    const int64_t removed = kShareOthers + 1;
    f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8803\",\"state\":3,\"reason\":6}");
    f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8803\",\"state\":1,\"reason\":0}");
    REQUIRE(f.m_service.GetCardState(removed, st));
    CHECK(st.m_state == share_state_available);
}

TEST_CASE("SCP1 conversation popup lists and cancels", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareConversationPopup popup;
    OpenPopupWithRows(f, popup, ui);
    REQUIRE(f.m_host.m_memberRequests.size() == 1);
    CHECK(f.m_host.m_memberRequests[0].second == kGroupId);

    //未失效的在前按时间从新到旧，已失效的在最后
    REQUIRE(popup.GetRowCount() == 3);
    CHECK(popup.GetRow(0).m_shareId == kShareOthers);
    CHECK(popup.GetRow(1).m_shareId == kShareMine);
    CHECK(popup.GetRow(2).m_shareId == kShareGone);
    CHECK(popup.GetRowActions(0) == share_row_action_open);
    CHECK(popup.GetRowActions(1) == (share_row_action_edit | share_row_action_cancel));
    CHECK(popup.GetRowActions(2) == 0);
    CHECK(popup.GetStatusText().empty());

    //本人是群管理员：别人的共享多出「取消共享」
    FileShareMemberInfo members[2];
    memset(members, 0, sizeof(members));
    members[0].m_userId = kSelf;
    members[0].m_role = kShareRoleAdmin;
    members[1].m_userId = kMemberA;
    popup.OnConversationMembers(conversation_group, kGroupId, 0, members, 2);
    CHECK(popup.GetMyRole() == kShareRoleAdmin);
    CHECK(popup.GetRowActions(0) == (share_row_action_open | share_row_action_cancel));

    //取消别人的共享：发 2202，成功后重新拉两份列表
    CHECK_FALSE(popup.DoRowAction(2, share_row_action_cancel, true));
    REQUIRE(popup.DoRowAction(0, share_row_action_cancel, true));
    const int idx = static_cast<int>(f.m_host.m_cmds.size()) - 1;
    CHECK(f.m_host.m_cmds[idx] == cmd_share_cancel);
    CHECK(f.m_host.m_bodies[idx] == ShareProtocolCodec::BuildShareIdRequest(kShareOthers));
    CHECK(popup.GetStatusText().find(L"正在取消") != std::wstring::npos);
    f.m_service.HandleServerFrame(cmd_share_cancel, f.m_host.m_seqs[idx], SimpleResponse(0, NULL));
    CHECK(f.m_host.Count(cmd_share_list) == 1 + 2 + 2);   //登录时的一份、弹出时的两份、取消后的两份

    //取消失败时显示原因
    REQUIRE(popup.DoRowAction(1, share_row_action_cancel, true));
    const int idx2 = static_cast<int>(f.m_host.m_cmds.size()) - 1;
    f.m_service.HandleServerFrame(cmd_share_cancel, f.m_host.m_seqs[idx2], SimpleResponse(error_code_share_not_exist, NULL));
    CHECK(popup.GetStatusText().find(L"已被取消") != std::wstring::npos);
    popup.Close();
}

TEST_CASE("SCP2 conversation popup opens other windows", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    {
        CShareConversationPopup popup;
        OpenPopupWithRows(f, popup, ui);
        REQUIRE(popup.DoRowAction(0, share_row_action_open, false));
        CHECK(ui.m_openShareId == kShareOthers);
        //关闭是投递的 WM_CLOSE，处理消息后窗口才销毁
        sharesnapshot::PumpMessagesFor(kPaintWaitMs);
        CHECK_FALSE(popup.IsWindow());
    }
    {
        CShareConversationPopup popup;
        OpenPopupWithRows(f, popup, ui);
        CHECK_FALSE(popup.DoRowAction(0, share_row_action_edit, false));   //别人的共享不能调整成员
        REQUIRE(popup.DoRowAction(1, share_row_action_edit, false));
        CHECK(ui.m_editShareId == kShareMine);
        popup.Close();
    }
    {
        CShareConversationPopup popup;
        REQUIRE(popup.Open(NULL, &f.m_service, &f.m_host, &ui, conversation_single, kMemberA, NULL));
        const size_t n = f.m_host.m_seqs.size();
        f.m_service.HandleServerFrame(cmd_share_list, f.m_host.m_seqs[n - 2], ListResponse(kShareListScopeMine, ""));
        f.m_service.HandleServerFrame(cmd_share_list, f.m_host.m_seqs[n - 1], ListResponse(kShareListScopeSharedMe, ""));
        CHECK(popup.GetRowCount() == 0);
        CHECK(popup.GetStatusText().find(L"还没有共享") != std::wstring::npos);
        popup.OnCreateClicked();
        CHECK(ui.m_createCtype == conversation_single);
        CHECK(ui.m_createCid == kMemberA);
        popup.Close();
    }
}

TEST_CASE("SCP4 conversation popup rows show type icons and open on double click", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareConversationPopup popup;
    OpenPopupWithRows(f, popup, ui);
    REQUIRE(popup.GetRowCount() == 3);
    //每一行都取到了本机的类型图标
    for (size_t r = 0; r < popup.GetRowCount(); ++r)
        CHECK(popup.RowHasIcon(r));
    CHECK_FALSE(popup.RowHasIcon(popup.GetRowCount()));

    //按坐标找行：行的中点落在这一行；浮层左上角（内边距与标题处）不在任何行上
    const RECT rc0 = popup.GetRowRect(0);
    REQUIRE(rc0.bottom > rc0.top);
    const POINT center0 = { (rc0.left + rc0.right) / 2, (rc0.top + rc0.bottom) / 2 };
    CHECK(popup.RowAtPoint(center0) == 0);
    const POINT corner = { 1, 1 };
    CHECK(popup.RowAtPoint(corner) == -1);

    //没有「打开」的行（本人的共享、已失效的共享）双击不做任何事
    CHECK_FALSE(popup.OnRowDoubleClick(1));
    CHECK_FALSE(popup.OnRowDoubleClick(2));
    CHECK_FALSE(popup.OnRowDoubleClick(popup.GetRowCount()));
    CHECK(ui.m_openShareId == 0);

    //在第 0 行的图标处真的投一个双击：打开这个共享，浮层随后关闭
    const POINT iconPt = { rc0.left + kSmallIconProbe, center0.y };
    ::SendMessage(popup.m_hWnd, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(iconPt.x, iconPt.y));
    CHECK(ui.m_openShareId == kShareOthers);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK_FALSE(popup.IsWindow());
}

TEST_CASE("SCP3 conversation popup snapshot", "[.snapshot][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareConversationPopup popup;
    OpenPopupWithRows(f, popup, ui);
    FileShareMemberInfo self;
    memset(&self, 0, sizeof(self));
    self.m_userId = kSelf;
    self.m_role = kShareRoleOwner;
    popup.OnConversationMembers(conversation_group, kGroupId, 0, &self, 1);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK(sharesnapshot::SaveWindowPng(popup.m_hWnd, sharetestfs::ExeDir() + kSnapshotPopup));
    popup.Close();
}

TEST_CASE("SCW1 share center window lists and details", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);
    REQUIRE(center.GetRowCount(share_center_tab_mine) == 2);
    REQUIRE(center.GetRowCount(share_center_tab_shared) == 1);
    CHECK(center.GetTab() == share_center_tab_mine);
    CHECK(center.GetActionMask() == 0);
    //程序图标：标题栏左侧的位图与任务栏 / Alt-Tab 的大、小图标都已设置
    CHECK(center.GetIcon() != NULL);
    CHECK(::SendMessage(center.m_hWnd, WM_GETICON, ICON_BIG, 0) != 0);
    CHECK(::SendMessage(center.m_hWnd, WM_GETICON, ICON_SMALL, 0) != 0);

    center.SelectRow(0);
    const std::vector<std::wstring> lines = center.GetDetailLines();
    CHECK(HasLine(lines, L"类型：文件夹"));
    CHECK(HasLine(lines, L"路径：D:\\x"));
    CHECK(HasLine(lines, L"所在会话：conv2-900100"));
    CHECK(HasLine(lines, L"状态：可用"));
    CHECK(HasLine(lines, L"基本信息"));
    CHECK(HasLine(lines, L"成员与权限（2）"));
    CHECK(HasLine(lines, L"user30002：下载、上传"));
    CHECK(HasLine(lines, L"user30003：浏览、下载"));
    CHECK(HasLine(lines, L"正在访问（0）"));
    CHECK(HasLine(lines, L"没有人正在访问"));
    CHECK(HasLine(lines, L"最近操作"));
    CHECK(HasLine(lines, L"还没有操作记录"));
    //本机没有这条记录（没有做过上线比对）：不能在本机打开
    CHECK(center.GetActionMask() == (share_center_action_edit | share_center_action_cancel | share_center_action_locate));

    //已失效的只能定位
    center.SelectRow(1);
    CHECK(center.GetActionMask() == share_center_action_locate);
    center.Close();
}

TEST_CASE("SCW2 share center window actions", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);

    center.SelectRow(0);
    REQUIRE(center.DoAction(share_center_action_edit, false));
    CHECK(ui.m_editShareId == kShareMine);
    REQUIRE(center.DoAction(share_center_action_locate, false));
    REQUIRE(f.m_host.m_openedConversations.size() == 1);
    CHECK(f.m_host.m_openedConversations[0].first == conversation_group);
    CHECK(f.m_host.m_openedConversations[0].second == kGroupId);
    CHECK_FALSE(center.DoAction(share_center_action_open, false));   //我的共享没有「打开」

    //取消：发 2202，成功后重新拉取两份列表
    REQUIRE(center.DoAction(share_center_action_cancel, true));
    const int idx = static_cast<int>(f.m_host.m_cmds.size()) - 1;
    CHECK(f.m_host.m_cmds[idx] == cmd_share_cancel);
    CHECK(center.GetToastText().find(L"正在取消") != std::wstring::npos);
    const int listsBefore = f.m_host.Count(cmd_share_list);
    f.m_service.HandleServerFrame(cmd_share_cancel, f.m_host.m_seqs[idx], SimpleResponse(0, NULL));
    CHECK(f.m_host.Count(cmd_share_list) == listsBefore + 2);

    //共享给我的：分享者与我的权限，可以打开
    center.SetTab(share_center_tab_shared);
    CHECK(center.GetSelectedRow() == -1);
    center.SelectRow(0);
    const std::vector<std::wstring> lines = center.GetDetailLines();
    CHECK(HasLine(lines, L"分享者：user30002"));
    CHECK(HasLine(lines, L"我的权限：下载、上传"));
    CHECK(center.GetActionMask() == (share_center_action_open | share_center_action_locate));
    REQUIRE(center.DoAction(share_center_action_open, false));
    CHECK(ui.m_openShareId == kShareOthers);
    center.Close();
}

TEST_CASE("SCW4 share center top bar and cards", "[fileshare][sharemanage]")
{
    //控件 id，与 ShareCenterWindow.cpp 的 kIdTabMine、kIdTabShared、kIdSearch 相同
    const UINT kTabMineId = 409;
    const UINT kTabSharedId = 410;
    const UINT kSearchId = 411;
    //胶囊页签与顶栏按钮文字两侧的内边距，与 ShareCenterWindow.cpp 的 kPillPadding 相同
    const int kPillPad = 14;
    //窗口浅灰底与卡片白底，与 ShareCenterWindow.cpp 的 kWindowBg、kBgColor 相同
    const COLORREF kGreyBg = RGB(0xF5, 0xF6, 0xF8);
    const COLORREF kWhite = RGB(0xFF, 0xFF, 0xFF);
    //取色点离卡片边缘的距离，单位：像素：两张卡片之间的间距 12 的一半，以及卡片圆角之外的高度
    const int kProbeGap = 6;
    const int kProbeDown = 20;
    //模拟窗口挪到另一块显示器时 DPI 放大的倍数
    const int kDpiFactor = 2;

    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    balloonwjui::DuiControl* root = center.GetRoot();
    REQUIRE(root != NULL);
    balloonwjui::DuiButton* tabMine = static_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kTabMineId));
    balloonwjui::DuiButton* tabShared = static_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kTabSharedId));
    REQUIRE(tabMine != NULL);
    REQUIRE(tabShared != NULL);
    CHECK(root->FindCtrlById(kSearchId) != NULL);

    //胶囊页签：选中的品牌蓝实心，另一个白底灰边；文字带共享个数（我的两个、共享给我的一个）
    CHECK(tabMine->GetVariant() == balloonwjui::DuiButton::Variant::Primary);
    CHECK(tabShared->GetVariant() == balloonwjui::DuiButton::Variant::Default);
    CHECK(tabMine->GetText() == CString(L"我的共享 2"));
    CHECK(tabShared->GetText() == CString(L"共享给我的 1"));
    center.SetTab(share_center_tab_shared);
    CHECK(tabMine->GetVariant() == balloonwjui::DuiButton::Variant::Default);
    CHECK(tabShared->GetVariant() == balloonwjui::DuiButton::Variant::Primary);

    //宽度正好放下文字：文字宽度加两侧内边距
    CHECK(tabMine->GetRect().right - tabMine->GetRect().left
          == TextWidthWithFont(tabMine->GetDefaultFont(), tabMine->GetText()) + 2 * kPillPad);

    //窗口浅灰底，列表放在白色卡片里，列表自己的灰边已被盖掉
    const RECT rcTabs = tabMine->GetParent()->GetRect();
    const int windowBgY = rcTabs.bottom + kProbeDown;
    //顶栏下方、两张卡片之间那条缝：先找到左侧卡片的右边缘（卡片是顶栏下一行的第一个子控件）
    balloonwjui::DuiControl* body = NULL;
    //顶栏与下面的卡片行同属窗口的内容根容器（GetRoot 返回的是外层带标题栏的根，内容根在它下面）
    REQUIRE(tabMine->GetParent()->GetParent() != NULL);
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& rootKids = tabMine->GetParent()->GetParent()->Children();
    for (size_t i = 0; i + 1 < rootKids.size(); ++i)
    {
        if (rootKids[i].get() == tabMine->GetParent())
            body = rootKids[i + 1].get();
    }
    REQUIRE(body != NULL);
    REQUIRE_FALSE(body->Children().empty());
    const RECT rcListCard = body->Children()[0]->GetRect();
    CHECK(sharesnapshot::WindowPixel(center.m_hWnd, rcListCard.right + kProbeGap, windowBgY) == kGreyBg);
    REQUIRE_FALSE(body->Children()[0]->Children().empty());
    const RECT rcList = body->Children()[0]->Children()[0]->GetRect();
    CHECK(sharesnapshot::WindowPixel(center.m_hWnd, rcList.left, windowBgY) == kWhite);

    //窗口挪到 DPI 更高的显示器：字体变大，页签随之加宽，仍然正好放下文字
    const RECT before = tabMine->GetRect();
    const int largerDpi = tabMine->GetDpi() * kDpiFactor;
    RECT wnd;
    ::GetWindowRect(center.m_hWnd, &wnd);
    ::SendMessage(center.m_hWnd, WM_DPICHANGED, MAKEWPARAM(largerDpi, largerDpi), reinterpret_cast<LPARAM>(&wnd));
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    REQUIRE(tabMine->GetDpi() == largerDpi);
    CHECK(tabMine->GetRect().right - tabMine->GetRect().left > before.right - before.left);
    CHECK(tabMine->GetRect().right - tabMine->GetRect().left
          == TextWidthWithFont(tabMine->GetDefaultFont(), tabMine->GetText()) + 2 * kPillPad);
    center.Close();
}

TEST_CASE("SCW3 share center window snapshot", "[.snapshot][sharemanage]")
{
    //截图里的最近操作条数：下载与上传交替，最新一条失败
    const int kShotActivities = 4;
    //第一条操作记录的时间（Unix 毫秒）与相邻两条的间隔（毫秒）
    const int64_t kShotTime0 = 1790000000000LL;
    const int64_t kShotStep = 600000;

    ManageFixture f;
    f.m_service.OnLoggedIn();
    {
        CShareLocalStore store;
        REQUIRE(store.Open(f.StorePath().c_str()));
        for (int i = 0; i < kShotActivities; ++i)
        {
            ShareActivityRecord a;
            a.m_shareId = kShareMine;
            a.m_visitorId = (i % 2 == 0) ? kMemberA : kMemberB;
            a.m_action = (i % 2 == 0) ? share_audit_download_file : share_audit_upload_file;
            a.m_path = "docs/report" + std::to_string(static_cast<long long>(i)) + ".docx";
            a.m_result = (i + 1 == kShotActivities) ? static_cast<int>(error_code_share_unavailable) : 0;
            a.m_time = kShotTime0 + kShotStep * i;
            REQUIRE(store.AddActivity(a));
        }
    }
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);
    center.SelectRow(0);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK(sharesnapshot::SaveWindowPng(center.m_hWnd, sharetestfs::ExeDir() + kSnapshotCenter));

    //取消共享时顶栏下方的浮层提示
    REQUIRE(center.DoAction(share_center_action_cancel, true));
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK(sharesnapshot::SaveWindowPng(center.m_hWnd, sharetestfs::ExeDir() + kSnapshotCenterToast));

    //「共享给我的」页签
    center.SetTab(share_center_tab_shared);
    center.SelectRow(0);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK(sharesnapshot::SaveWindowPng(center.m_hWnd, sharetestfs::ExeDir() + kSnapshotCenterShared));
    center.Close();
}

namespace {

//另一个群：用来验证不属于登记会话的共享不影响个数
const int64_t kOtherGroupId = 900200;

//个数的接收方桩：记下每一次通知
class CountSink : public IShareCountSink
{
public:
    virtual void OnConversationShareCount(int ctype, int64_t cid, int count) override
    {
        Item item;
        item.m_ctype = ctype;
        item.m_cid = cid;
        item.m_count = count;
        m_items.push_back(item);
    }

    //一次通知
    struct Item
    {
        int     m_ctype;   //会话类型
        int64_t m_cid;     //会话 id
        int     m_count;   //个数
    };

    std::vector<Item> m_items;   //收到的通知，按到达顺序
};

//计数类与业务总控的组合：计数类订阅卡片状态变化，析构时先解除订阅再解除关联
struct CountFixture
{
    ManageFixture             m_f;         //已登录的业务总控
    CountSink                 m_sink;      //个数的接收方
    CShareConversationCounter m_counter;   //被测的计数类

    CountFixture()
    {
        m_f.m_service.OnLoggedIn();
        m_counter.Attach(&m_f.m_service, &m_sink);
        m_f.m_service.SetCardStateListener(&m_counter);
    }

    ~CountFixture()
    {
        m_f.m_service.SetCardStateListener(NULL);
        m_counter.Detach();
    }

    //登记群 kGroupId，返回两份 2205 在宿主桩命令表里的起始下标
    size_t WatchGroup()
    {
        const size_t before = m_f.m_host.m_cmds.size();
        REQUIRE(m_counter.Watch(conversation_group, kGroupId));
        REQUIRE(m_f.m_host.m_cmds.size() == before + 2);
        return before;
    }

    //让一份 2205 应答到达
    void Respond(size_t index, int scope, const std::string& items)
    {
        m_f.m_service.HandleServerFrame(cmd_share_list, m_f.m_host.m_seqs[index], ListResponse(scope, items));
    }
};

} // namespace

TEST_CASE("SCC1 watch counts available shares of the conversation", "[fileshare][sharecount]")
{
    CountFixture c;
    const size_t at = c.WatchGroup();
    //两份请求都按会话筛选
    CHECK(c.m_f.m_host.m_bodies[at] == ShareProtocolCodec::BuildListRequest(kShareListScopeMine, conversation_group, kGroupId));
    CHECK(c.m_f.m_host.m_bodies[at + 1]
          == ShareProtocolCodec::BuildListRequest(kShareListScopeSharedMe, conversation_group, kGroupId));
    CHECK(c.m_counter.GetCount(conversation_group, kGroupId) == -1);

    //只到一份：不数、不通知
    c.Respond(at, kShareListScopeMine,
              ListItemJson(kShareMine, kSelf, 100, share_state_available, "mine") + ","
              + ListItemJson(kShareGone, kSelf, 300, share_state_invalid, "gone"));
    CHECK(c.m_sink.m_items.empty());

    //两份都到：我的一个可用 + 别人的一个可用、一个暂不可用 → 2
    c.Respond(at + 1, kShareListScopeSharedMe,
              ListItemJson(kShareOthers, kMemberA, 200, share_state_available, "others") + ","
              + ListItemJson(kNewShareId, kMemberB, 400, share_state_unavailable, "offline"));
    REQUIRE(c.m_sink.m_items.size() == 1);
    CHECK(c.m_sink.m_items[0].m_ctype == conversation_group);
    CHECK(c.m_sink.m_items[0].m_cid == kGroupId);
    CHECK(c.m_sink.m_items[0].m_count == 2);
    CHECK(c.m_counter.GetCount(conversation_group, kGroupId) == 2);
}

TEST_CASE("SCC2 state changes update the count", "[fileshare][sharecount]")
{
    CountFixture c;
    const size_t at = c.WatchGroup();
    c.Respond(at, kShareListScopeMine, ListItemJson(kShareMine, kSelf, 100, share_state_available, "mine"));
    c.Respond(at + 1, kShareListScopeSharedMe, ListItemJson(kShareOthers, kMemberA, 200, share_state_unavailable, "others"));
    REQUIRE(c.m_sink.m_items.size() == 1);
    CHECK(c.m_sink.m_items[0].m_count == 1);

    //别人的共享变为可用：2
    c.m_f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8802\",\"state\":1,\"reason\":0}");
    REQUIRE(c.m_sink.m_items.size() == 2);
    CHECK(c.m_sink.m_items[1].m_count == 2);

    //本人的共享被取消（已失效）：1
    c.m_f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8801\",\"state\":3,\"reason\":1}");
    REQUIRE(c.m_sink.m_items.size() == 3);
    CHECK(c.m_sink.m_items[2].m_count == 1);

    //不属于本会话的共享变了、或同样的状态再来一次：个数不变，不通知
    c.m_f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8803\",\"state\":1,\"reason\":0}");
    CHECK(c.m_sink.m_items.size() == 3);
    c.m_f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8802\",\"state\":1,\"reason\":0}");
    CHECK(c.m_sink.m_items.size() == 3);
}

TEST_CASE("SCC3 rewatch refreshes, unwatch drops late results", "[fileshare][sharecount]")
{
    CountFixture c;
    size_t at = c.WatchGroup();
    c.Respond(at, kShareListScopeMine, "");
    c.Respond(at + 1, kShareListScopeSharedMe, "");
    REQUIRE(c.m_sink.m_items.size() == 1);
    CHECK(c.m_sink.m_items[0].m_count == 0);

    //聊天窗收到新的共享卡片后再登记一次：重新拉取，新共享计入
    at = c.WatchGroup();
    CHECK(c.m_counter.GetWatchedCount() == 1);
    c.Respond(at, kShareListScopeMine, "");
    c.Respond(at + 1, kShareListScopeSharedMe, ListItemJson(kShareOthers, kMemberA, 200, share_state_available, "others"));
    REQUIRE(c.m_sink.m_items.size() == 2);
    CHECK(c.m_sink.m_items[1].m_count == 1);

    //另一个群也登记；本群注销后，本群迟到的结果与状态变化都不再通知
    REQUIRE(c.m_counter.Watch(conversation_group, kOtherGroupId));
    at = c.WatchGroup();
    c.m_counter.Unwatch(conversation_group, kGroupId);
    CHECK(c.m_counter.GetWatchedCount() == 1);
    CHECK(c.m_counter.GetCount(conversation_group, kGroupId) == -1);
    c.Respond(at, kShareListScopeMine, ListItemJson(kShareMine, kSelf, 100, share_state_available, "mine"));
    c.Respond(at + 1, kShareListScopeSharedMe, "");
    c.m_f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8802\",\"state\":2,\"reason\":1}");
    CHECK(c.m_sink.m_items.size() == 2);
}

TEST_CASE("SCC4 list failure keeps the last list, refresh and reset", "[fileshare][sharecount]")
{
    CountFixture c;
    size_t at = c.WatchGroup();
    c.Respond(at, kShareListScopeMine, ListItemJson(kShareMine, kSelf, 100, share_state_available, "mine"));
    c.Respond(at + 1, kShareListScopeSharedMe, "");
    REQUIRE(c.m_sink.m_items.size() == 1);
    CHECK(c.m_sink.m_items[0].m_count == 1);

    //重连后 RefreshAll：再发两份；其中一份失败时保留上一次的列表，个数不变、不通知
    const size_t before = c.m_f.m_host.m_cmds.size();
    c.m_counter.RefreshAll();
    REQUIRE(c.m_f.m_host.m_cmds.size() == before + 2);
    c.m_f.m_service.HandleServerFrame(cmd_share_list, c.m_f.m_host.m_seqs[before], SimpleResponse(error_code_server_internal, NULL));
    c.Respond(before + 1, kShareListScopeSharedMe, "");
    CHECK(c.m_sink.m_items.size() == 1);
    CHECK(c.m_counter.GetCount(conversation_group, kGroupId) == 1);

    //退出登录：丢弃登记，之后的状态变化不再通知
    c.m_counter.Reset();
    CHECK(c.m_counter.GetWatchedCount() == 0);
    c.m_f.m_service.HandleServerFrame(cmd_share_state_notify, 0, "{\"shareid\":\"8801\",\"state\":2,\"reason\":1}");
    CHECK(c.m_sink.m_items.size() == 1);
}

namespace {

//代为建出的新群
const int64_t kNewGroupId = 900300;

//建群失败的错误码（Protocol.h 的 error_code_create_group_fail）
const int kCreateGroupFailed = error_code_create_group_fail;

//选好的共享对象：userid 与显示名
std::vector<ShareMemberRow> Targets(const std::vector<int64_t>& ids)
{
    std::vector<ShareMemberRow> out;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        ShareMemberRow row;
        row.m_userId = ids[i];
        row.m_name = "user" + std::to_string(static_cast<long long>(ids[i]));
        out.push_back(row);
    }
    return out;
}

//两个人：A、B
std::vector<ShareMemberRow> TwoTargets()
{
    std::vector<int64_t> ids;
    ids.push_back(kMemberA);
    ids.push_back(kMemberB);
    return Targets(ids);
}

//让最近一次 2201 成功或被拒
void RespondLastCreate(ManageFixture& f, int code, int64_t shareId)
{
    const int idx = f.m_host.IndexOf(cmd_share_create, f.m_host.Count(cmd_share_create) - 1);
    REQUIRE(idx >= 0);
    const std::vector<ShareLocalRecord> recs = f.LocalRecords();
    REQUIRE_FALSE(recs.empty());
    f.m_service.HandleServerFrame(cmd_share_create, f.m_host.m_seqs[idx],
                                  CreateResponse(code, shareId, recs.back().m_clientShareId, NULL));
}

} // namespace

TEST_CASE("STD1 direct share to one person", "[fileshare][sharemanage][sharedirect]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    CShareCreateDialog dialog;
    REQUIRE(dialog.OpenForTargets(NULL, &f.m_service, &f.m_host, Targets(std::vector<int64_t>(1, kMemberA))));
    //不向宿主要会话成员：成员就是选好的人
    CHECK(f.m_host.m_memberRequests.empty());
    REQUIRE(dialog.GetMemberTable().Rows().size() == 1);
    CHECK(dialog.GetMemberTable().Rows()[0].m_checked);
    CHECK(dialog.GetMemberTable().Rows()[0].m_fixed);
    CHECK(dialog.GetCtype() == conversation_single);
    CHECK(dialog.GetCid() == kMemberA);

    dialog.SetShareObject(MakeRoot(f), share_kind_folder, L"");
    REQUIRE(dialog.Submit(true));
    CHECK(f.m_host.m_groupRequests.empty());
    const std::string body = f.m_host.m_bodies[f.m_host.IndexOf(cmd_share_create, 0)];
    CHECK(body.find("\"ctype\":1") != std::string::npos);
    CHECK(body.find("\"cid\":\"30002\"") != std::string::npos);

    //成功：打开与他的单聊，关闭对话框
    RespondLastCreate(f, 0, kNewShareId);
    REQUIRE(f.m_host.m_openedConversations.size() == 1);
    CHECK(f.m_host.m_openedConversations[0].first == conversation_single);
    CHECK(f.m_host.m_openedConversations[0].second == kMemberA);
    CHECK_FALSE(dialog.IsWindow());
}

TEST_CASE("STD2 direct share to several people creates a group first", "[fileshare][sharemanage][sharedirect]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    CShareCreateDialog dialog;
    REQUIRE(dialog.OpenForTargets(NULL, &f.m_service, &f.m_host, TwoTargets()));
    REQUIRE(dialog.GetMemberTable().Rows().size() == 2);
    CHECK(dialog.GetMemberTable().Rows()[0].m_checked);
    CHECK(dialog.GetMemberTable().Rows()[1].m_checked);
    CHECK_FALSE(dialog.GetMemberTable().Rows()[0].m_fixed);

    //没选共享对象：不建群
    CHECK_FALSE(dialog.Submit(true));
    CHECK(f.m_host.m_groupRequests.empty());

    //逐人权限：A 可以上传
    dialog.SetShareObject(MakeRoot(f), share_kind_folder, L"");
    dialog.GetMemberTable().SetPerm(kMemberA, share_perm_upload, true);
    REQUIRE(dialog.Submit(true));
    REQUIRE(f.m_host.m_groupRequests.size() == 1);
    REQUIRE(f.m_host.m_groupRequests[0].size() == 2);
    CHECK(f.m_host.m_groupRequests[0][0] == kMemberA);
    CHECK(f.m_host.m_groupRequests[0][1] == kMemberB);
    //群名：「共享 <共享名> 给 <成员名>」（UTF-8）
    REQUIRE(f.m_host.m_groupNames.size() == 1);
    //群名所有成员都看得到，用共享名称而不是文件夹名（2026-10-04 起）
    CHECK(f.m_host.m_groupNames[0] == u8"共享 ssmtester的文件共享 给 user30002、user30003");
    CHECK(f.m_host.Count(cmd_share_create) == 0);
    CHECK(dialog.IsBusy());
    CHECK(dialog.GetStatusText().find(L"正在建群") != std::wstring::npos);

    //群建好：在新群里发起，成员与权限照表
    dialog.OnShareGroupCreated(0, kNewGroupId);
    REQUIRE(f.m_host.Count(cmd_share_create) == 1);
    const std::string body = f.m_host.m_bodies[f.m_host.IndexOf(cmd_share_create, 0)];
    CHECK(body.find("\"ctype\":2") != std::string::npos);
    CHECK(body.find("\"cid\":\"900300\"") != std::string::npos);
    CHECK(body.find("{\"perms\":1,\"userid\":\"30002\"}") != std::string::npos);
    CHECK(body.find("{\"perms\":0,\"userid\":\"30003\"}") != std::string::npos);
    CHECK(dialog.GetCtype() == conversation_group);
    CHECK(dialog.GetCid() == kNewGroupId);

    //成功：打开群聊，关闭对话框
    RespondLastCreate(f, 0, kNewShareId);
    REQUIRE(f.m_host.m_openedConversations.size() == 1);
    CHECK(f.m_host.m_openedConversations[0].first == conversation_group);
    CHECK(f.m_host.m_openedConversations[0].second == kNewGroupId);
    CHECK_FALSE(dialog.IsWindow());
}

TEST_CASE("STD3 group creation fails", "[fileshare][sharemanage][sharedirect]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    CShareCreateDialog dialog;
    REQUIRE(dialog.OpenForTargets(NULL, &f.m_service, &f.m_host, TwoTargets()));
    dialog.SetShareObject(MakeRoot(f), share_kind_folder, L"");

    //服务端建群失败：显示原因，可以重试，不发 2201
    REQUIRE(dialog.Submit(true));
    dialog.OnShareGroupCreated(kCreateGroupFailed, 0);
    CHECK_FALSE(dialog.IsBusy());
    CHECK(dialog.GetStatusText().find(L"建群失败") != std::wstring::npos);
    CHECK(f.m_host.Count(cmd_share_create) == 0);
    CHECK(dialog.IsWindow());
    REQUIRE(dialog.Submit(true));
    CHECK(f.m_host.m_groupRequests.size() == 2);

    //宿主没发出建群请求（离线）：提示，不进入等待
    dialog.OnShareGroupCreated(kCreateGroupFailed, 0);
    f.m_host.m_groupRequestOk = false;
    CHECK_FALSE(dialog.Submit(true));
    CHECK_FALSE(dialog.IsBusy());
    CHECK(dialog.GetStatusText().find(L"建群请求") != std::wstring::npos);
    dialog.Close();
}

TEST_CASE("STD4 group kept when the share is rejected", "[fileshare][sharemanage][sharedirect]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    CShareCreateDialog dialog;
    REQUIRE(dialog.OpenForTargets(NULL, &f.m_service, &f.m_host, TwoTargets()));
    dialog.SetShareObject(MakeRoot(f), share_kind_folder, L"");
    REQUIRE(dialog.Submit(true));
    dialog.OnShareGroupCreated(0, kNewGroupId);
    REQUIRE(f.m_host.Count(cmd_share_create) == 1);

    //被拒（名额已满）：说明群已建好
    RespondLastCreate(f, error_code_share_too_many, 0);
    CHECK_FALSE(dialog.IsBusy());
    CHECK(dialog.GetStatusText().find(L"群已建好") != std::wstring::npos);
    CHECK(f.m_host.m_openedConversations.empty());

    //重试：不再建群，仍在这个群里发起
    REQUIRE(dialog.Submit(true));
    CHECK(f.m_host.m_groupRequests.size() == 1);
    REQUIRE(f.m_host.Count(cmd_share_create) == 2);
    const std::string body = f.m_host.m_bodies[f.m_host.IndexOf(cmd_share_create, 1)];
    CHECK(body.find("\"cid\":\"900300\"") != std::string::npos);
    dialog.Close();
}

TEST_CASE("STD5 several people picked but one checked shares in the single chat", "[fileshare][sharemanage][sharedirect]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    CShareCreateDialog dialog;
    REQUIRE(dialog.OpenForTargets(NULL, &f.m_service, &f.m_host, TwoTargets()));
    dialog.SetShareObject(MakeRoot(f), share_kind_folder, L"");
    dialog.GetMemberTable().SetChecked(kMemberA, false);
    REQUIRE(dialog.Submit(true));
    CHECK(f.m_host.m_groupRequests.empty());
    const std::string body = f.m_host.m_bodies[f.m_host.IndexOf(cmd_share_create, 0)];
    CHECK(body.find("\"ctype\":1") != std::string::npos);
    CHECK(body.find("\"cid\":\"30003\"") != std::string::npos);
    RespondLastCreate(f, 0, kNewShareId);
    REQUIRE(f.m_host.m_openedConversations.size() == 1);
    CHECK(f.m_host.m_openedConversations[0].first == conversation_single);
    CHECK(f.m_host.m_openedConversations[0].second == kMemberB);
}

TEST_CASE("STD6 no double submit while waiting for the group", "[fileshare][sharemanage][sharedirect]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    CShareCreateDialog dialog;
    REQUIRE(dialog.OpenForTargets(NULL, &f.m_service, &f.m_host, TwoTargets()));
    //不在等待建群时收到结果：不做任何事
    dialog.OnShareGroupCreated(0, kNewGroupId);
    CHECK(f.m_host.Count(cmd_share_create) == 0);
    CHECK(dialog.GetCid() == 0);

    dialog.SetShareObject(MakeRoot(f), share_kind_folder, L"");
    REQUIRE(dialog.Submit(true));
    CHECK_FALSE(dialog.Submit(true));
    CHECK(f.m_host.m_groupRequests.size() == 1);
    dialog.Close();
}

TEST_CASE("SCW6 share center start direct share", "[fileshare][sharemanage][sharedirect]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);
    CHECK(center.StartDirectShare());
    CHECK(ui.m_directShareRequests == 1);

    //弹不出选人窗口：浮层提示说明原因
    ui.m_directShareOk = false;
    CHECK_FALSE(center.StartDirectShare());
    CHECK(ui.m_directShareRequests == 2);
    CHECK(center.GetToastText().find(L"选择共享对象") != std::wstring::npos);
    center.Close();
}

namespace {

//头像位图的边长，单位：像素（与成员行头像的尺寸同一量级）
const int kAvatarTestPx = 28;
//测试照片的边长与颜色：纯红，与宿主桩字形头像的蓝色底区分得开
const int kPhotoTestPx = 64;
const COLORREF kPhotoColor = RGB(0xE0, 0x20, 0x20);
//判定颜色时每个通道允许的误差（缩放与抗锯齿会让颜色有轻微出入）
const int kColorTolerance = 24;
//取色点：头像顶部中间往下几个像素，落在圆内、又在字形文字的上方，只会是照片或字形的底色
const int kProbeY = 4;

//两种颜色在每个通道上都相差不超过 kColorTolerance
bool ColorNear(COLORREF a, COLORREF b)
{
    return abs(GetRValue(a) - GetRValue(b)) <= kColorTolerance && abs(GetGValue(a) - GetGValue(b)) <= kColorTolerance
        && abs(GetBValue(a) - GetBValue(b)) <= kColorTolerance;
}

} // namespace

TEST_CASE("SAV1 avatar query from the host", "[fileshare][shareavatar]")
{
    //没有登记宿主：问号与中性灰
    CShareAvatarSource bare;
    FileShareAvatarInfo info = bare.Query(kMemberA);
    CHECK(info.m_cbSize == sizeof(FileShareAvatarInfo));
    CHECK(std::wstring(info.m_glyph) == kUnknownAvatarGlyph);
    CHECK(info.m_color == static_cast<uint32_t>(kUnknownAvatarColor));

    //宿主给的字形、底色与照片路径原样取回，调用方填好了结构体大小
    ManageHost host;
    host.m_avatarImages[kMemberB] = L"D:\\avatar\\b.png";
    CShareAvatarSource source;
    source.SetHost(&host);
    info = source.Query(kMemberA);
    CHECK(std::wstring(info.m_glyph) == L"02");
    CHECK(info.m_color == static_cast<uint32_t>(kStubAvatarColor));
    CHECK(std::wstring(info.m_imagePath).empty());
    info = source.Query(kMemberB);
    CHECK(std::wstring(info.m_imagePath) == L"D:\\avatar\\b.png");
    CHECK(host.m_avatarRequests == 2);
}

TEST_CASE("SAV2 avatar bitmaps: glyph, photo and cache", "[fileshare][shareavatar]")
{
    const std::wstring photo = sharetestfs::ExeDir() + L"\\sav2_photo.png";
    REQUIRE(sharesnapshot::SaveSolidColorPng(photo, kPhotoTestPx, kPhotoColor));

    ManageHost host;
    CShareAvatarSource source;
    source.SetHost(&host);

    //字形头像：底色是宿主给的；同一个人再取一次用的是同一张位图，不重新生成
    HBITMAP glyph = source.GetBitmap(kMemberA, kAvatarTestPx);
    REQUIRE(glyph != NULL);
    CHECK(ColorNear(sharesnapshot::BitmapPixel(glyph, kAvatarTestPx / 2, kProbeY), kStubAvatarColor));
    CHECK(source.GetBitmap(kMemberA, kAvatarTestPx) == glyph);
    CHECK(source.BitmapCount() == 1);

    //照片头像：照片在本机就画照片
    host.m_avatarImages[kMemberB] = photo;
    HBITMAP photoBmp = source.GetBitmap(kMemberB, kAvatarTestPx);
    REQUIRE(photoBmp != NULL);
    CHECK(photoBmp != glyph);
    CHECK(ColorNear(sharesnapshot::BitmapPixel(photoBmp, kAvatarTestPx / 2, kProbeY), kPhotoColor));

    //照片路径指向的文件不存在（或解码不了）：改画字形
    host.m_avatarImages[kMemberC] = sharetestfs::ExeDir() + L"\\sav2_missing.png";
    HBITMAP fallback = source.GetBitmap(kMemberC, kAvatarTestPx);
    REQUIRE(fallback != NULL);
    CHECK(ColorNear(sharesnapshot::BitmapPixel(fallback, kAvatarTestPx / 2, kProbeY), kStubAvatarColor));

    //Clear 释放全部位图
    source.Clear();
    CHECK(source.BitmapCount() == 0);
    ::DeleteFileW(photo.c_str());
}

TEST_CASE("SAV3 avatar control", "[fileshare][shareavatar]")
{
    ManageHost host;
    CShareAvatarSource source;
    source.SetHost(&host);
    std::unique_ptr<balloonwjui::DuiAvatar> avatar = source.MakeAvatar(kMemberA, kAvatarTestPx);
    REQUIRE(avatar.get() != NULL);
    //圆形，挂着位图；字形与底色也设好了，位图生成失败时 DuiAvatar 自己画它们
    CHECK(avatar->GetShape() == balloonwjui::DuiAvatar::ShapeCircle);
    CHECK(avatar->GetBitmap() == source.GetBitmap(kMemberA, kAvatarTestPx));
    CHECK(avatar->GetName() == CString(L"02"));
    CHECK(avatar->GetFallbackBgColor() == kStubAvatarColor);
}

TEST_CASE("SCD6 member rows show avatars and clicking the name toggles the member", "[fileshare][sharemanage][shareavatar]")
{
    //成员行里名字、头像的列号与头像边长，与 ShareCreateDialog.cpp 的 kMemberNameColumn、kMemberAvatarColumn、kMemberAvatarPx 相同
    const UINT kDlgNameColumn = 6;
    const UINT kDlgAvatarColumn = 7;
    const int kDlgAvatarPx = 28;

    const std::wstring photo = sharetestfs::ExeDir() + L"\\scd6_photo.png";
    REQUIRE(sharesnapshot::SaveSolidColorPng(photo, kPhotoTestPx, kPhotoColor));
    ManageFixture f;
    f.m_service.OnLoggedIn();
    f.m_host.m_avatarImages[kMemberB] = photo;
    CShareCreateDialog dialog;
    REQUIRE(dialog.OpenForCreate(NULL, &f.m_service, &f.m_host, conversation_group, kGroupId));
    const std::vector<FileShareMemberInfo> members = GroupMembers();
    dialog.OnConversationMembers(conversation_group, kGroupId, 0, &members[0], static_cast<uint32_t>(members.size()));
    REQUIRE(dialog.GetMemberTable().Rows().size() == 2);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    balloonwjui::DuiControl* root = dialog.GetRoot();
    REQUIRE(root != NULL);

    //第一行 Alice：字形头像（宿主给的字形与底色），正方形；名字在头像右边
    balloonwjui::DuiAvatar* avatarA = static_cast<balloonwjui::DuiAvatar*>(root->FindCtrlById(kDlgMemberBase + kDlgAvatarColumn));
    REQUIRE(avatarA != NULL);
    CHECK(avatarA->GetName() == CString(L"02"));
    REQUIRE(avatarA->GetBitmap() != NULL);
    CHECK(ColorNear(sharesnapshot::BitmapPixel(avatarA->GetBitmap(), kDlgAvatarPx / 2, kProbeY), kStubAvatarColor));
    const RECT rcAvatar = avatarA->GetRect();
    CHECK(rcAvatar.right - rcAvatar.left == kDlgAvatarPx);
    CHECK(rcAvatar.bottom - rcAvatar.top == kDlgAvatarPx);
    balloonwjui::DuiLabel* nameA = static_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kDlgMemberBase + kDlgNameColumn));
    REQUIRE(nameA != NULL);
    CHECK(nameA->GetText() == CString(L"Alice"));
    CHECK(nameA->GetRect().left > rcAvatar.right);

    //第二行 Bob：照片在本机，画照片
    balloonwjui::DuiAvatar* avatarB =
        static_cast<balloonwjui::DuiAvatar*>(root->FindCtrlById(kDlgMemberBase + kDlgIdsPerRow + kDlgAvatarColumn));
    REQUIRE(avatarB != NULL);
    REQUIRE(avatarB->GetBitmap() != NULL);
    CHECK(ColorNear(sharesnapshot::BitmapPixel(avatarB->GetBitmap(), kDlgAvatarPx / 2, kProbeY), kPhotoColor));

    //用鼠标消息点 Alice 的名字：勾选上，勾选框跟着变；再点一次取消
    const RECT rcName = nameA->GetRect();
    const LPARAM pt = MAKELPARAM((rcName.left + rcName.right) / 2, (rcName.top + rcName.bottom) / 2);
    ::SendMessageW(dialog.m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    ::SendMessageW(dialog.m_hWnd, WM_LBUTTONUP, 0, pt);
    CHECK(dialog.GetMemberTable().Rows()[0].m_checked);
    balloonwjui::DuiButton* checkA = static_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kDlgMemberBase));
    REQUIRE(checkA != NULL);
    CHECK(checkA->IsChecked());
    ::SendMessageW(dialog.m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    ::SendMessageW(dialog.m_hWnd, WM_LBUTTONUP, 0, pt);
    CHECK_FALSE(dialog.GetMemberTable().Rows()[0].m_checked);
    CHECK_FALSE(checkA->IsChecked());

    //宿主通知头像变了（Alice 的照片下载到了本机）：成员行重建，Alice 换成照片，勾选与权限保留
    dialog.GetMemberTable().SetChecked(kMemberB, true);
    dialog.GetMemberTable().SetPerm(kMemberB, share_perm_upload, true);
    f.m_host.m_avatarImages[kMemberA] = photo;
    const int requestsBefore = f.m_host.m_avatarRequests;
    dialog.RefreshAvatars();
    CHECK(f.m_host.m_avatarRequests > requestsBefore);
    avatarA = static_cast<balloonwjui::DuiAvatar*>(root->FindCtrlById(kDlgMemberBase + kDlgAvatarColumn));
    REQUIRE(avatarA != NULL);
    REQUIRE(avatarA->GetBitmap() != NULL);
    CHECK(ColorNear(sharesnapshot::BitmapPixel(avatarA->GetBitmap(), kDlgAvatarPx / 2, kProbeY), kPhotoColor));
    CHECK(dialog.GetMemberTable().Rows()[1].m_checked);
    CHECK(dialog.GetMemberTable().Rows()[1].m_perms == share_perm_upload);
    dialog.Close();
    ::DeleteFileW(photo.c_str());
}

TEST_CASE("SCW7 share center detail lists people with avatars", "[fileshare][sharemanage][shareavatar]")
{
    //详情里头像的第一个 id 与头像边长，与 ShareCenterWindow.cpp 的 kIdDetailAvatarBase、kDetailAvatarPx 相同
    const UINT kCenterAvatarBase = 1000;
    const int kCenterAvatarPx = 28;

    const std::wstring photo = sharetestfs::ExeDir() + L"\\scw7_photo.png";
    REQUIRE(sharesnapshot::SaveSolidColorPng(photo, kPhotoTestPx, kPhotoColor));
    ManageFixture f;
    f.m_service.OnLoggedIn();
    f.m_host.m_avatarImages[kMemberB] = photo;
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);

    //「我的共享」：两名成员各一行，第一个是字形头像，第二个有照片；没有人正在访问，不多出头像
    center.SelectRow(0);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    balloonwjui::DuiControl* root = center.GetRoot();
    REQUIRE(root != NULL);
    balloonwjui::DuiAvatar* first = static_cast<balloonwjui::DuiAvatar*>(root->FindCtrlById(kCenterAvatarBase));
    balloonwjui::DuiAvatar* second = static_cast<balloonwjui::DuiAvatar*>(root->FindCtrlById(kCenterAvatarBase + 1));
    REQUIRE(first != NULL);
    REQUIRE(second != NULL);
    CHECK(root->FindCtrlById(kCenterAvatarBase + 2) == NULL);
    CHECK(first->GetName() == CString(L"02"));
    REQUIRE(first->GetBitmap() != NULL);
    CHECK(ColorNear(sharesnapshot::BitmapPixel(first->GetBitmap(), kCenterAvatarPx / 2, kProbeY), kStubAvatarColor));
    REQUIRE(second->GetBitmap() != NULL);
    CHECK(ColorNear(sharesnapshot::BitmapPixel(second->GetBitmap(), kCenterAvatarPx / 2, kProbeY), kPhotoColor));
    CHECK(first->GetRect().right - first->GetRect().left == kCenterAvatarPx);
    CHECK(first->GetRect().bottom - first->GetRect().top == kCenterAvatarPx);
    //详情的文字照旧（测试与日志读取的是整行文字）
    const std::vector<std::wstring> lines = center.GetDetailLines();
    CHECK(HasLine(lines, L"user30002：下载、上传"));
    CHECK(HasLine(lines, L"user30003：浏览、下载"));

    //宿主通知头像变了（第一个人的照片下载到了本机）：详情重建，换成照片
    f.m_host.m_avatarImages[kMemberA] = photo;
    center.RefreshAvatars();
    first = static_cast<balloonwjui::DuiAvatar*>(root->FindCtrlById(kCenterAvatarBase));
    REQUIRE(first != NULL);
    REQUIRE(first->GetBitmap() != NULL);
    CHECK(ColorNear(sharesnapshot::BitmapPixel(first->GetBitmap(), kCenterAvatarPx / 2, kProbeY), kPhotoColor));

    //「共享给我的」：分享者一行带头像
    center.SetTab(share_center_tab_shared);
    center.SelectRow(0);
    balloonwjui::DuiAvatar* owner = static_cast<balloonwjui::DuiAvatar*>(root->FindCtrlById(kCenterAvatarBase));
    REQUIRE(owner != NULL);
    CHECK(owner->GetName() == CString(L"02"));
    CHECK(root->FindCtrlById(kCenterAvatarBase + 1) == NULL);
    CHECK(HasLine(center.GetDetailLines(), L"分享者：user30002"));
    center.Close();
    ::DeleteFileW(photo.c_str());
}

TEST_CASE("SCP4 conversation popup manages all shares of the conversation", "[fileshare][sharemanage][sharelaunch]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareConversationPopup popup;
    OpenPopupWithRows(f, popup, ui);

    //「管理全部共享」：请模块打开「文件共享」窗口并记住本会话
    CHECK(popup.OnManageAllClicked());
    CHECK(ui.m_centerCtype == conversation_group);
    CHECK(ui.m_centerCid == kGroupId);
    popup.Close();

    //浮层自己的「发起共享」：在本会话里发起，聊天窗就在眼前，成功后不另开
    CShareConversationPopup again;
    OpenPopupWithRows(f, again, ui);
    again.OnCreateClicked();
    CHECK(ui.m_createCid == kGroupId);
    CHECK_FALSE(ui.m_createOpenAfter);
    again.Close();
}

TEST_CASE("SCW8 share center opened from a conversation shares into it without picking people", "[fileshare][sharemanage][sharelaunch]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);

    //记住了会话：「发起共享」直接打开那个会话的发起共享对话框，成功后前置聊天窗；不弹选人窗口
    center.SetLaunchConversation(conversation_group, kGroupId);
    CHECK(center.GetLaunchCid() == kGroupId);
    CHECK(center.StartDirectShare());
    CHECK(ui.m_createCtype == conversation_group);
    CHECK(ui.m_createCid == kGroupId);
    CHECK(ui.m_createOpenAfter);
    CHECK(ui.m_directShareRequests == 0);

    //从系统菜单、主面板再次打开（清除记住的会话）：回到先选人的流程
    center.SetLaunchConversation(0, 0);
    CHECK(center.GetLaunchCid() == 0);
    CHECK(center.StartDirectShare());
    CHECK(ui.m_directShareRequests == 1);
    center.Close();
}

TEST_CASE("SCD7 create dialog opens the conversation after success only when asked", "[fileshare][sharemanage][sharelaunch]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    const std::wstring root = MakeRoot(f);
    const std::vector<FileShareMemberInfo> members = GroupMembers();

    //从「文件共享」窗口发起：成功后经宿主打开所在会话
    {
        CShareCreateDialog dialog;
        dialog.SetOpenConversationOnSuccess(true);
        REQUIRE(dialog.OpenForCreate(NULL, &f.m_service, &f.m_host, conversation_group, kGroupId));
        dialog.OnConversationMembers(conversation_group, kGroupId, 0, &members[0], static_cast<uint32_t>(members.size()));
        dialog.SetShareObject(root, share_kind_folder, L"");
        dialog.GetMemberTable().SetChecked(kMemberA, true);
        dialog.RefreshMemberRows();
        REQUIRE(dialog.Submit(true));
        const int idx = f.m_host.IndexOf(cmd_share_create, 0);
        f.m_service.HandleServerFrame(cmd_share_create, f.m_host.m_seqs[idx],
                                      CreateResponse(0, kNewShareId, f.LocalRecords()[0].m_clientShareId, NULL));
        CHECK_FALSE(dialog.IsWindow());
        REQUIRE(f.m_host.m_openedConversations.size() == 1);
        CHECK(f.m_host.m_openedConversations[0] == std::make_pair(static_cast<int>(conversation_group), kGroupId));
    }

    //从聊天窗浮层发起（默认）：成功后不另开聊天窗
    {
        CShareCreateDialog dialog;
        REQUIRE(dialog.OpenForCreate(NULL, &f.m_service, &f.m_host, conversation_group, kGroupId));
        dialog.OnConversationMembers(conversation_group, kGroupId, 0, &members[0], static_cast<uint32_t>(members.size()));
        dialog.SetShareObject(root, share_kind_folder, L"");
        dialog.GetMemberTable().SetChecked(kMemberA, true);
        dialog.RefreshMemberRows();
        REQUIRE(dialog.Submit(true));
        const int idx = f.m_host.IndexOf(cmd_share_create, 1);
        //刚发起、还没有 shareid 的那条本机记录
        std::string pendingId;
        const std::vector<ShareLocalRecord> recs = f.LocalRecords();
        for (size_t i = 0; i < recs.size(); ++i)
        {
            if (recs[i].m_shareId == 0)
                pendingId = recs[i].m_clientShareId;
        }
        REQUIRE_FALSE(pendingId.empty());
        f.m_service.HandleServerFrame(cmd_share_create, f.m_host.m_seqs[idx],
                                      CreateResponse(0, kNewShareId + 1, pendingId, NULL));
        CHECK_FALSE(dialog.IsWindow());
        CHECK(f.m_host.m_openedConversations.size() == 1);
    }
}

TEST_CASE("SCW9 share center list rows, search filter and empty list", "[fileshare][sharemanage][sharecenterlist]")
{
    //与 ShareCenterWindow.cpp 一致的尺寸与颜色：行高、行内左右内边距、胶囊高度、选中底色收进的距离、竖条宽度
    const int kListRowH = 64;
    const int kListCellPad = 10;
    const int kListChipH = 20;
    const int kListRowInset = 3;
    const COLORREF kOkChipBg = RGB(0xE7, 0xF6, 0xEC);
    const COLORREF kGreyChipBg = RGB(0xF0, 0xF0, 0xF0);
    const COLORREF kSelBg = RGB(0xEA, 0xF1, 0xFE);
    const COLORREF kAccent = RGB(45, 108, 223);
    //取色点离胶囊右边缘、离行顶部的距离，单位：像素（落在胶囊的内边距里，避开文字；落在选中底色里，避开圆角）
    const int kChipProbe = 4;
    const int kSelProbe = 8;

    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);
    REQUIRE(center.GetVisibleCount() == 2);

    //搜索：名称包含关键字的才列出；选中的共享仍在结果里时保持选中
    center.SelectRow(1);
    REQUIRE(center.GetRow(share_center_tab_mine, 1).m_name == "gone");
    center.SetFilter(L"GON");
    CHECK(center.GetVisibleCount() == 1);
    CHECK(center.GetSelectedRow() == 0);
    center.SetFilter(L"");
    CHECK(center.GetVisibleCount() == 2);
    CHECK(center.GetSelectedRow() == 1);
    //没有匹配：列表为空、清除选择；切换页签后关键字仍然生效
    center.SetFilter(L"zzz");
    CHECK(center.GetVisibleCount() == 0);
    CHECK(center.GetSelectedRow() == -1);
    center.SetTab(share_center_tab_shared);
    CHECK(center.GetVisibleCount() == 0);
    center.SetFilter(L"");
    CHECK(center.GetVisibleCount() == 1);
    center.SetTab(share_center_tab_mine);

    //画行：第一行可用（绿色胶囊），第二行已失效（灰色胶囊）；选中第一行时浅蓝圆角底色与左侧品牌蓝竖条
    center.SelectRow(0);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    balloonwjui::DuiControl* root = center.GetRoot();
    REQUIRE(root != NULL);
    balloonwjui::DuiButton* tabMine = static_cast<balloonwjui::DuiButton*>(root->FindCtrlById(409));
    REQUIRE(tabMine != NULL);
    balloonwjui::DuiControl* body = NULL;
    //顶栏与下面的卡片行同属窗口的内容根容器（GetRoot 返回的是外层带标题栏的根，内容根在它下面）
    REQUIRE(tabMine->GetParent()->GetParent() != NULL);
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& rootKids = tabMine->GetParent()->GetParent()->Children();
    for (size_t i = 0; i + 1 < rootKids.size(); ++i)
    {
        if (rootKids[i].get() == tabMine->GetParent())
            body = rootKids[i + 1].get();
    }
    REQUIRE(body != NULL);
    const RECT rcList = body->Children()[0]->Children()[0]->GetRect();
    const int mid0 = rcList.top + kListRowH / 2;
    const int chipX = rcList.right - kListCellPad - kChipProbe;
    CHECK(sharesnapshot::WindowPixel(center.m_hWnd, chipX, mid0 - kListChipH / 2) == kOkChipBg);
    CHECK(sharesnapshot::WindowPixel(center.m_hWnd, chipX, mid0 + kListRowH - kListChipH / 2) == kGreyChipBg);
    CHECK(sharesnapshot::WindowPixel(center.m_hWnd, rcList.left + kListRowInset + kSelProbe * 2, rcList.top + kSelProbe) == kSelBg);
    CHECK(sharesnapshot::WindowPixel(center.m_hWnd, rcList.left + kListRowInset + 2, mid0) == kAccent);
    center.Close();
}

namespace {

//「文件共享」窗口的默认整窗宽度，与 ShareCenterWindow.cpp 的 kContentWidth 相同
const int kCenterDefaultWidth = 1080;
//窗口比默认宽时多出的宽度分给详情的比例的分母，与 ShareCenterWindow.cpp 的 kDetailExtraDiv 相同
const int kCenterExtraDiv = 2;

//从详情的「调整成员」按钮找到详情卡片：按钮在操作按钮行里，按钮行是卡片的第二个子控件
balloonwjui::DuiControl* DetailCardOf(balloonwjui::DuiControl* editButton)
{
    if (editButton == NULL || editButton->GetParent() == NULL)
        return NULL;
    return editButton->GetParent()->GetParent();
}

//控件矩形的宽度
int RectWidth(const RECT& rc)
{
    return rc.right - rc.left;
}

} // namespace

TEST_CASE("SCW10 share center detail header, action buttons and sections", "[fileshare][sharemanage][sharecenterdetail]")
{
    //控件 id，与 ShareCenterWindow.cpp 的 kIdOpenLocal、kIdOpen、kIdEdit、kIdCancel、kIdLocate、kIdDetailAvatarBase 相同
    const UINT kOpenLocalId = 403;
    const UINT kOpenId = 404;
    const UINT kEditId = 405;
    const UINT kCancelId = 406;
    const UINT kLocateId = 407;
    const UINT kAvatarBase = 1000;
    //按钮文字两侧的内边距、操作按钮之间的间距、状态胶囊文字两侧的内边距、详情卡片的内边距与设计宽度，
    //与 ShareCenterWindow.cpp 的 kPillPadding、kTopGap、kChipPadding、kDetailCardPadding、kDetailWidth 相同
    const int kPillPad = 14;
    const int kButtonGap = 8;
    const int kChipPad = 8;
    const int kCardPad = 16;
    const int kCardWidth = 560;
    //详情左列文字与右列内容之间的最小间距，与 ShareCenterWindow.cpp 的 kColumnGap 相同
    const int kColumnGapPx = 12;
    //访问者刷新定时器的 id，与 ShareCenterWindow.cpp 的 kVisitorTimerId 相同
    const UINT_PTR kVisitorTimer = 1;
    //写进本机库的操作记录条数：比详情一屏能放下的多，详情可以滚动
    const int kActivityCount = 20;
    //第一条操作记录的时间（Unix 毫秒）与相邻两条的间隔（毫秒）
    const int64_t kActivityTime0 = 1790000000000LL;
    const int64_t kActivityStep = 60000;
    //滚动测试用的位置，单位：像素
    const int kScrolledPos = 60;
    //白底取色点离滚动区底边的距离，单位：像素
    const int kProbeUp = 10;
    //模拟窗口挪到另一块显示器时 DPI 放大的倍数
    const int kDpiFactor = 2;
    //提示文字，与 ShareCenterWindow.cpp 的 kNoSelection 相同
    const CString kNoSelectionText(L"选中左侧的一个共享查看详情");

    ManageFixture f;
    f.m_service.OnLoggedIn();
    //分享者一端的最近操作：最新一条是失败的
    {
        CShareLocalStore store;
        REQUIRE(store.Open(f.StorePath().c_str()));
        for (int i = 0; i < kActivityCount; ++i)
        {
            ShareActivityRecord a;
            a.m_shareId = kShareMine;
            a.m_visitorId = kMemberA;
            a.m_action = share_audit_download_file;
            a.m_path = "file" + std::to_string(static_cast<long long>(i)) + ".txt";
            a.m_result = (i + 1 == kActivityCount) ? static_cast<int>(error_code_share_unavailable) : 0;
            a.m_time = kActivityTime0 + kActivityStep * i;
            REQUIRE(store.AddActivity(a));
        }
    }
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    balloonwjui::DuiControl* root = center.GetRoot();
    REQUIRE(root != NULL);
    balloonwjui::DuiButton* openLocal = static_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kOpenLocalId));
    balloonwjui::DuiButton* open = static_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kOpenId));
    balloonwjui::DuiButton* edit = static_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kEditId));
    balloonwjui::DuiButton* cancel = static_cast<balloonwjui::DuiButton*>(root->FindCtrlById(kCancelId));
    REQUIRE(openLocal != NULL);
    REQUIRE(open != NULL);
    REQUIRE(edit != NULL);
    REQUIRE(cancel != NULL);
    balloonwjui::DuiControl* card = DetailCardOf(edit);
    REQUIRE(card != NULL);
    REQUIRE(card->Children().size() == 3);
    //头部：类型图标 | 名称与路径 | 状态胶囊
    balloonwjui::DuiControl* header = card->Children()[0].get();
    REQUIRE(header->Children().size() == 3);
    balloonwjui::DuiAvatar* icon = static_cast<balloonwjui::DuiAvatar*>(header->Children()[0].get());
    REQUIRE(header->Children()[1]->Children().size() == 2);
    balloonwjui::DuiLabel* title = static_cast<balloonwjui::DuiLabel*>(header->Children()[1]->Children()[0].get());
    balloonwjui::DuiLabel* path = static_cast<balloonwjui::DuiLabel*>(header->Children()[1]->Children()[1].get());
    balloonwjui::DuiControl* chip = header->Children()[2].get();
    REQUIRE(chip->Children().size() == 1);
    balloonwjui::DuiLabel* chipText = static_cast<balloonwjui::DuiLabel*>(chip->Children()[0].get());
    balloonwjui::DuiScrollView* scroll = static_cast<balloonwjui::DuiScrollView*>(card->Children()[2].get());

    //没选中：头部只有提示文字（不加粗），没有图标与胶囊，也没有操作按钮
    CHECK(title->GetText() == kNoSelectionText);
    CHECK_FALSE(title->IsTextBold());
    CHECK_FALSE(icon->IsVisible());
    CHECK_FALSE(chip->IsVisible());
    CHECK_FALSE(openLocal->IsVisible());
    CHECK_FALSE(open->IsVisible());
    CHECK_FALSE(edit->IsVisible());
    CHECK_FALSE(cancel->IsVisible());
    CHECK(root->FindCtrlById(kLocateId) == NULL);

    //选中「我的共享」里可用的那个：头部有图标、名称、路径与绿色的「可用」
    center.SelectRow(0);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK(icon->IsVisible());
    CHECK(icon->GetBitmap() != NULL);
    CHECK(title->GetText() == CString(L"mine"));
    CHECK(title->IsTextBold());
    CHECK(path->GetText() == CString(L"D:\\x"));
    CHECK(chip->IsVisible());
    CHECK(chipText->GetText() == CString(L"可用"));
    CHECK(RectWidth(chip->GetRect()) == TextWidthWithFont(chipText->GetDefaultFont(), chipText->GetText()) + 2 * kChipPad);
    CHECK(chip->GetRect().right == header->GetRect().right);

    //操作按钮：调整成员（白底灰边）与取消共享（红色，靠右）；宽度按文字定
    CHECK_FALSE(openLocal->IsVisible());
    CHECK_FALSE(open->IsVisible());
    REQUIRE(edit->IsVisible());
    REQUIRE(cancel->IsVisible());
    CHECK(edit->GetVariant() == balloonwjui::DuiButton::Variant::Default);
    CHECK(cancel->GetVariant() == balloonwjui::DuiButton::Variant::Danger);
    CHECK(openLocal->GetVariant() == balloonwjui::DuiButton::Variant::Primary);
    CHECK(open->GetVariant() == balloonwjui::DuiButton::Variant::Primary);
    const RECT rcRow = edit->GetParent()->GetRect();
    CHECK(RectWidth(rcRow) == RectWidth(card->GetRect()) - 2 * kCardPad);
    CHECK(RectWidth(edit->GetRect()) == TextWidthWithFont(edit->GetDefaultFont(), edit->GetText()) + 2 * kPillPad);
    CHECK(edit->GetRect().left == rcRow.left);
    CHECK(cancel->GetRect().right == rcRow.right);
    CHECK(edit->GetRect().right + kButtonGap <= cancel->GetRect().left);
    //按钮最多的组合（在本机打开、调整成员、取消共享）也放得下
    const int worst = TextWidthWithFont(openLocal->GetDefaultFont(), openLocal->GetText())
                      + TextWidthWithFont(edit->GetDefaultFont(), edit->GetText())
                      + TextWidthWithFont(cancel->GetDefaultFont(), cancel->GetText()) + 3 * 2 * kPillPad + 2 * kButtonGap;
    CHECK(worst <= RectWidth(rcRow));
    //设计宽度放得下时卡片保持设计宽度，放不下时正好加宽到放下
    CHECK(RectWidth(card->GetRect()) == (std::max)(kCardWidth, worst + 2 * kCardPad));

    //小节：基本信息、成员与权限（带个数）、正在访问、最近操作；最新一条记录是失败的
    const std::vector<std::wstring> lines = center.GetDetailLines();
    CHECK(HasLine(lines, L"基本信息"));
    CHECK(HasLine(lines, L"成员与权限（2）"));
    CHECK(HasLine(lines, L"正在访问（0）"));
    CHECK(HasLine(lines, L"没有人正在访问"));
    CHECK(HasLine(lines, L"最近操作"));
    CHECK_FALSE(HasLine(lines, L"还没有操作记录"));
    bool failedLine = false;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i].find(L"file19.txt") != std::wstring::npos && lines[i].find(L"失败") != std::wstring::npos)
            failedLine = true;
    }
    CHECK(failedLine);

    //成员名字下面是权限胶囊：头像 | 间距 | 名字与胶囊
    balloonwjui::DuiControl* firstAvatar = root->FindCtrlById(kAvatarBase);
    balloonwjui::DuiControl* secondAvatar = root->FindCtrlById(kAvatarBase + 1);
    REQUIRE(firstAvatar != NULL);
    REQUIRE(secondAvatar != NULL);
    balloonwjui::DuiControl* firstRow = firstAvatar->GetParent();
    balloonwjui::DuiControl* secondRow = secondAvatar->GetParent();
    REQUIRE(firstRow->Children().size() >= 3);
    REQUIRE(secondRow->Children().size() >= 3);
    REQUIRE(firstRow->Children()[2]->Children().size() == 2);
    REQUIRE(secondRow->Children()[2]->Children().size() == 2);
    balloonwjui::DuiControl* firstChips = firstRow->Children()[2]->Children()[1].get();
    balloonwjui::DuiControl* secondChips = secondRow->Children()[2]->Children()[1].get();
    REQUIRE_FALSE(firstChips->Children().empty());
    REQUIRE_FALSE(secondChips->Children().empty());
    REQUIRE_FALSE(firstChips->Children()[0]->Children().empty());
    REQUIRE_FALSE(secondChips->Children()[0]->Children().empty());
    //第一个胶囊都是默认的「下载」：第一名成员后面还有「上传」，第二名成员只有「下载」
    REQUIRE(firstChips->Children().size() >= 2);
    REQUIRE_FALSE(firstChips->Children()[1]->Children().empty());
    CHECK(static_cast<balloonwjui::DuiLabel*>(firstChips->Children()[0]->Children()[0].get())->GetText() == CString(L"下载"));
    CHECK(static_cast<balloonwjui::DuiLabel*>(firstChips->Children()[1]->Children()[0].get())->GetText() == CString(L"上传"));
    CHECK(static_cast<balloonwjui::DuiLabel*>(secondChips->Children()[0]->Children()[0].get())->GetText() == CString(L"下载"));

    //「定位到会话」是信息表里的链接，在卡片之内；用鼠标消息点它，打开所在的群聊
    balloonwjui::DuiLabel* locate = static_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kLocateId));
    REQUIRE(locate != NULL);
    CHECK(locate->GetMode() == balloonwjui::DuiLabel::ModeLink);
    const RECT rcLocate = locate->GetRect();
    CHECK(rcLocate.left >= card->GetRect().left);
    CHECK(rcLocate.right <= card->GetRect().right);
    const LPARAM pt = MAKELPARAM((rcLocate.left + rcLocate.right) / 2, (rcLocate.top + rcLocate.bottom) / 2);
    ::SendMessageW(center.m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    ::SendMessageW(center.m_hWnd, WM_LBUTTONUP, 0, pt);
    REQUIRE(f.m_host.m_openedConversations.size() == 1);
    CHECK(f.m_host.m_openedConversations[0].first == conversation_group);
    CHECK(f.m_host.m_openedConversations[0].second == kGroupId);

    //内容比一屏高，可以滚动；定时刷新正在访问的人时保持滚动位置
    REQUIRE(scroll->GetContentHeight() > scroll->GetRect().bottom - scroll->GetRect().top);
    //滚动条是自动隐藏的悬浮式细条（2026-10-04 起不占内容宽度）；没有滚动时它那一条透出底下的白色内容，
    //不露出 DuiScrollView 的浅灰底
    REQUIRE(scroll->GetScrollBar() != NULL);
    CHECK(scroll->GetScrollBar()->IsAutoHide());
    const RECT rcBar = scroll->GetScrollBar()->GetRect();
    CHECK(sharesnapshot::WindowPixel(center.m_hWnd, (rcBar.left + rcBar.right) / 2, (rcBar.top + rcBar.bottom) / 2)
          == RGB(0xFF, 0xFF, 0xFF));
    scroll->SetScrollPos(kScrolledPos);
    REQUIRE(scroll->GetScrollPos() == kScrolledPos);
    ::SendMessageW(center.m_hWnd, WM_TIMER, kVisitorTimer, 0);
    CHECK(scroll->GetScrollPos() == kScrolledPos);

    //换选已失效的那个：回到顶部，灰色「已失效」，没有操作按钮，仍可定位；内容不满一屏时下面是白底
    center.SelectRow(1);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK(scroll->GetScrollPos() == 0);
    CHECK(chipText->GetText() == CString(L"已失效"));
    CHECK_FALSE(edit->IsVisible());
    CHECK_FALSE(cancel->IsVisible());
    CHECK(root->FindCtrlById(kLocateId) != NULL);
    const RECT rcScroll = scroll->GetRect();
    CHECK(sharesnapshot::WindowPixel(center.m_hWnd, (rcScroll.left + rcScroll.right) / 2, rcScroll.bottom - kProbeUp)
          == RGB(0xFF, 0xFF, 0xFF));

    //窗口挪到 DPI 加倍的显示器（系统建议的窗口尺寸同样加倍）：按钮按新字体定宽，详情卡片按需加宽，最多的组合仍放得下
    const int largerDpi = edit->GetDpi() * kDpiFactor;
    RECT wnd;
    ::GetWindowRect(center.m_hWnd, &wnd);
    wnd.right = wnd.left + RectWidth(wnd) * kDpiFactor;
    wnd.bottom = wnd.top + (wnd.bottom - wnd.top) * kDpiFactor;
    ::SendMessage(center.m_hWnd, WM_DPICHANGED, MAKEWPARAM(largerDpi, largerDpi), reinterpret_cast<LPARAM>(&wnd));
    center.SelectRow(0);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    REQUIRE(edit->GetDpi() == largerDpi);
    const RECT rcRow2 = edit->GetParent()->GetRect();
    CHECK(RectWidth(edit->GetRect()) == TextWidthWithFont(edit->GetDefaultFont(), edit->GetText()) + 2 * kPillPad);
    CHECK(cancel->GetRect().right == rcRow2.right);
    const int worst2 = TextWidthWithFont(openLocal->GetDefaultFont(), openLocal->GetText())
                       + TextWidthWithFont(edit->GetDefaultFont(), edit->GetText())
                       + TextWidthWithFont(cancel->GetDefaultFont(), cancel->GetText()) + 3 * 2 * kPillPad + 2 * kButtonGap;
    CHECK(worst2 <= RectWidth(rcRow2));
    //窗口按系统建议加宽了：卡片在最小宽度之上再分到多出默认宽度那部分的一半
    RECT wnd2;
    ::GetWindowRect(center.m_hWnd, &wnd2);
    const int extra2 = RectWidth(wnd2) - kCenterDefaultWidth;
    CHECK(RectWidth(card->GetRect())
          == (std::max)(kCardWidth, worst2 + 2 * kCardPad) + ((extra2 > 0) ? extra2 / kCenterExtraDiv : 0));
    CHECK(RectWidth(chip->GetRect()) == TextWidthWithFont(chipText->GetDefaultFont(), chipText->GetText()) + 2 * kChipPad);
    //信息表左列按新字体加宽：「所在会话」与右边的值之间留有间距
    balloonwjui::DuiControl* locate2 = root->FindCtrlById(kLocateId);
    REQUIRE(locate2 != NULL);
    balloonwjui::DuiControl* convRow = locate2->GetParent();
    REQUIRE(convRow->Children().size() >= 2);
    balloonwjui::DuiLabel* convLabel = static_cast<balloonwjui::DuiLabel*>(convRow->Children()[0].get());
    CHECK(convLabel->GetText() == CString(L"所在会话"));
    CHECK(convRow->Children()[1]->GetRect().left - convRow->GetRect().left
          >= TextWidthWithFont(convLabel->GetDefaultFont(), convLabel->GetText()) + kColumnGapPx);
    //最近操作的时间列放得下时间文字（最后一行是最早的那条记录）
    REQUIRE(scroll->GetContent() != NULL);
    REQUIRE_FALSE(scroll->GetContent()->Children().empty());
    balloonwjui::DuiControl* lastActivity = scroll->GetContent()->Children().back().get();
    REQUIRE(lastActivity->Children().size() == 2);
    balloonwjui::DuiLabel* timeLabel = static_cast<balloonwjui::DuiLabel*>(lastActivity->Children()[0].get());
    CHECK(RectWidth(timeLabel->GetRect()) >= TextWidthWithFont(timeLabel->GetDefaultFont(), timeLabel->GetText()) + kColumnGapPx);
    center.Close();
}

namespace {

//浮层提示的控件 id，与 ShareCenterWindow.cpp 的 kIdToast 相同
const UINT kCenterToastId = 412;
//普通与出错两种浮层提示的底色、显示时长（毫秒），与 ShareCenterWindow.cpp 的 kToastInfoBg、kErrorColor、kToastMs、kErrorToastMs 相同
const COLORREF kToastInfoBgColor = RGB(0x32, 0x32, 0x32);
const COLORREF kToastErrorBgColor = RGB(0xD9, 0x3B, 0x3B);
const int kToastInfoMs = 2000;
const int kToastErrorMs = 3000;

} // namespace

TEST_CASE("SCW11 share center toast: no toast while loading, red toast when a list fails", "[fileshare][sharemanage][sharecenterdetail]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareCenterWindow center;
    //打开时两份列表还没到：在加载，但不弹提示（列表里画「正在加载…」）
    const size_t before = f.m_host.m_cmds.size();
    REQUIRE(center.Open(NULL, &f.m_service, &f.m_host, &ui));
    REQUIRE(f.m_host.m_cmds.size() == before + 2);
    balloonwjui::DuiControl* root = center.GetRoot();
    REQUIRE(root != NULL);
    balloonwjui::DuiToast* toast = static_cast<balloonwjui::DuiToast*>(root->FindCtrlById(kCenterToastId));
    REQUIRE(toast != NULL);
    CHECK(center.IsLoading());
    CHECK_FALSE(toast->IsActive());
    CHECK(center.GetToastText().empty());

    //「我的共享」那份失败：红底提示、多显示一秒；另一份还没到，仍在加载
    f.m_service.HandleServerFrame(cmd_share_list, f.m_host.m_seqs[before], SimpleResponse(error_code_share_disabled, NULL));
    CHECK(center.IsLoading());
    CHECK(toast->IsActive());
    CHECK(center.GetToastText().find(L"加载失败") != std::wstring::npos);
    CHECK(toast->GetBgColor() == kToastErrorBgColor);
    CHECK(toast->GetDurationMs() == kToastErrorMs);

    //另一份成功到达：加载结束，成功不另外弹提示
    f.m_service.HandleServerFrame(cmd_share_list, f.m_host.m_seqs[before + 1], ListResponse(kShareListScopeSharedMe, ""));
    CHECK_FALSE(center.IsLoading());
    CHECK(center.GetToastText().find(L"加载失败") != std::wstring::npos);
    center.Close();
}

TEST_CASE("SCW12 share center toast: canceling and canceled, placed below the top bar", "[fileshare][sharemanage][sharecenterdetail]")
{
    //控件 id，与 ShareCenterWindow.cpp 的 kIdTabMine 相同
    const UINT kTabMineId = 409;
    //浮层提示内容盒距内容区顶边的距离，与 ShareCenterWindow.cpp 的 kToastTop 相同
    const int kToastTopPx = 64;
    //水平居中允许的误差，单位：像素（宽度为奇数时差 1）
    const int kCenterTolerance = 1;

    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);
    balloonwjui::DuiControl* root = center.GetRoot();
    REQUIRE(root != NULL);
    balloonwjui::DuiToast* toast = static_cast<balloonwjui::DuiToast*>(root->FindCtrlById(kCenterToastId));
    REQUIRE(toast != NULL);
    CHECK(center.GetToastText().empty());

    //取消共享：发 2202 时弹「正在取消…」（深灰底）
    center.SelectRow(0);
    REQUIRE(center.DoAction(share_center_action_cancel, true));
    const int idx = static_cast<int>(f.m_host.m_cmds.size()) - 1;
    REQUIRE(f.m_host.m_cmds[idx] == cmd_share_cancel);
    CHECK(toast->IsActive());
    CHECK(center.GetToastText() == L"正在取消…");
    CHECK(toast->GetBgColor() == kToastInfoBgColor);
    CHECK(toast->GetDurationMs() == kToastInfoMs);

    //位置：内容盒在顶栏下方、在内容区里水平居中（控件矩形四周含阴影留边）
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    balloonwjui::DuiControl* tabMine = root->FindCtrlById(kTabMineId);
    REQUIRE(tabMine != NULL);
    const RECT rcTop = tabMine->GetParent()->GetRect();
    const RECT rcContent = tabMine->GetParent()->GetParent()->GetRect();
    const RECT rcToast = toast->GetRect();
    const int margin = toast->GetShadowBlur() + std::abs(toast->GetShadowOffsetY());
    CHECK(rcToast.top + margin == rcContent.top + kToastTopPx);
    CHECK(rcToast.top + margin >= rcTop.bottom);
    CHECK(std::abs((rcToast.left + rcToast.right) / 2 - (rcContent.left + rcContent.right) / 2) <= kCenterTolerance);

    //取消成功：弹「已取消共享」，随后重新拉取两份列表（加载中不另外弹提示）
    f.m_service.HandleServerFrame(cmd_share_cancel, f.m_host.m_seqs[idx], SimpleResponse(0, NULL));
    CHECK(center.GetToastText() == L"已取消共享");
    CHECK(toast->GetBgColor() == kToastInfoBgColor);
    CHECK(center.IsLoading());
    center.Close();
}

TEST_CASE("SCW13 share center fits its minimum size", "[fileshare][sharemanage][sharecenterdetail]")
{
    //整窗最小尺寸，与 ShareCenterWindow.cpp 的 kMinWindowWidth、kMinWindowHeight 相同
    const int kMinW = 820;
    const int kMinH = 420;
    //搜索框的设计宽度，与 ShareCenterWindow.cpp 的 kSearchWidth 相同
    const int kSearchW = 220;
    //左侧列表卡片至少要有的宽度，单位：像素：放得下 16 像素图标、名称前几个字与右端的状态胶囊
    const int kMinListCardW = 200;
    //详情滚动区至少要有的高度，单位：像素：放得下一个小节标题与两行信息
    const int kMinDetailScrollH = 80;
    //控件 id，与 ShareCenterWindow.cpp 的 kIdTabMine、kIdTabShared、kIdSearch、kIdNewShare、kIdRefresh、kIdEdit、kIdCancel 相同
    const UINT kTabMineId = 409;
    const UINT kTabSharedId = 410;
    const UINT kSearchId = 411;
    const UINT kNewShareId = 408;
    const UINT kRefreshId = 402;
    const UINT kEditId = 405;
    const UINT kCancelId = 406;

    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);
    center.SelectRow(0);
    ::SetWindowPos(center.m_hWnd, NULL, 0, 0, kMinW, kMinH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    RECT wnd;
    ::GetWindowRect(center.m_hWnd, &wnd);
    REQUIRE(RectWidth(wnd) == kMinW);
    REQUIRE(wnd.bottom - wnd.top == kMinH);
    balloonwjui::DuiControl* root = center.GetRoot();
    REQUIRE(root != NULL);

    //顶栏：页签、搜索框、「发起共享」「刷新」依次排开、互不重叠，都在顶栏之内；搜索框保持设计宽度
    const UINT order[] = { kTabMineId, kTabSharedId, kSearchId, kNewShareId, kRefreshId };
    std::vector<RECT> rects;
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
    {
        balloonwjui::DuiControl* ctrl = root->FindCtrlById(order[i]);
        REQUIRE(ctrl != NULL);
        rects.push_back(ctrl->GetRect());
    }
    const RECT rcTop = root->FindCtrlById(kTabMineId)->GetParent()->GetRect();
    CHECK(rects[0].left >= rcTop.left);
    for (size_t i = 0; i + 1 < rects.size(); ++i)
        CHECK(rects[i].right <= rects[i + 1].left);
    CHECK(rects.back().right <= rcTop.right);
    CHECK(RectWidth(rects[2]) == kSearchW);

    //两张卡片：左侧列表留有足够宽度；详情的操作按钮在卡片之内，下面的滚动区留有足够高度
    balloonwjui::DuiControl* edit = root->FindCtrlById(kEditId);
    balloonwjui::DuiControl* cancel = root->FindCtrlById(kCancelId);
    REQUIRE(edit != NULL);
    REQUIRE(cancel != NULL);
    balloonwjui::DuiControl* card = DetailCardOf(edit);
    REQUIRE(card != NULL);
    REQUIRE(card->GetParent() != NULL);
    REQUIRE_FALSE(card->GetParent()->Children().empty());
    const RECT rcListCard = card->GetParent()->Children()[0]->GetRect();
    CHECK(RectWidth(rcListCard) >= kMinListCardW);
    CHECK(card->GetRect().right <= rcTop.right);
    CHECK(edit->GetRect().left >= card->GetRect().left);
    CHECK(cancel->GetRect().right <= card->GetRect().right);
    CHECK(edit->GetRect().right <= cancel->GetRect().left);
    REQUIRE(card->Children().size() == 3);
    const RECT rcScroll = card->Children()[2]->GetRect();
    CHECK(rcScroll.bottom - rcScroll.top >= kMinDetailScrollH);
    CHECK(rcScroll.bottom <= wnd.bottom - wnd.top);
    center.Close();
}

TEST_CASE("SCW14 share center detail grows with the window, activity tooltips, timer rebuilds only on change",
          "[fileshare][sharemanage][sharecenterdetail]")
{
    //控件 id，与 ShareCenterWindow.cpp 的 kIdEdit 相同
    const UINT kEditId = 405;
    //详情卡片的设计宽度，与 ShareCenterWindow.cpp 的 kDetailWidth 相同
    const int kCardWidth = 560;
    //把窗口拉宽的像素数
    const int kGrowBy = 400;
    //访问者刷新定时器的 id，与 ShareCenterWindow.cpp 的 kVisitorTimerId 相同
    const UINT_PTR kVisitorTimer = 1;
    //两条操作记录的时间（Unix 毫秒）
    const int64_t kFirstTime = 1790000000000LL;
    const int64_t kSecondTime = 1790000060000LL;

    ManageFixture f;
    f.m_service.OnLoggedIn();
    CShareLocalStore store;
    REQUIRE(store.Open(f.StorePath().c_str()));
    ShareActivityRecord first;
    first.m_shareId = kShareMine;
    first.m_visitorId = kMemberA;
    first.m_action = share_audit_download_file;
    first.m_path = "projects/2026/very-long-folder-name/another-level/design-review-final-version.docx";
    first.m_time = kFirstTime;
    REQUIRE(store.AddActivity(first));
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);
    center.SelectRow(0);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    balloonwjui::DuiControl* root = center.GetRoot();
    REQUIRE(root != NULL);
    balloonwjui::DuiControl* card = DetailCardOf(root->FindCtrlById(kEditId));
    REQUIRE(card != NULL);
    REQUIRE(card->GetParent() != NULL);
    balloonwjui::DuiControl* listCard = card->GetParent()->Children()[0].get();

    //默认宽度时是最小宽度；拉宽后详情分到多出部分的一半，其余给列表；缩回后复原
    RECT wnd;
    ::GetWindowRect(center.m_hWnd, &wnd);
    REQUIRE(RectWidth(wnd) == kCenterDefaultWidth);
    const int cardBefore = RectWidth(card->GetRect());
    const int listBefore = RectWidth(listCard->GetRect());
    CHECK(cardBefore >= kCardWidth);
    ::SetWindowPos(center.m_hWnd, NULL, 0, 0, kCenterDefaultWidth + kGrowBy, wnd.bottom - wnd.top,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK(RectWidth(card->GetRect()) == cardBefore + kGrowBy / kCenterExtraDiv);
    CHECK(RectWidth(listCard->GetRect()) == listBefore + kGrowBy - kGrowBy / kCenterExtraDiv);
    ::SetWindowPos(center.m_hWnd, NULL, 0, 0, kCenterDefaultWidth, wnd.bottom - wnd.top,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    sharesnapshot::PumpMessagesFor(kPaintWaitMs);
    CHECK(RectWidth(card->GetRect()) == cardBefore);

    //最近操作的文字挂着悬停提示，内容是全文
    balloonwjui::DuiScrollView* scroll = static_cast<balloonwjui::DuiScrollView*>(card->Children()[2].get());
    REQUIRE(scroll->GetContent() != NULL);
    balloonwjui::DuiControl* lastRow = scroll->GetContent()->Children().back().get();
    REQUIRE(lastRow->Children().size() == 2);
    balloonwjui::DuiLabel* content = static_cast<balloonwjui::DuiLabel*>(lastRow->Children()[1].get());
    CHECK(content->GetText().Find(L"design-review-final-version.docx") >= 0);
    CHECK(balloonwjui::DuiToolTipMgr::Inst().GetText(content) == content->GetText());

    //定时刷新：正在访问的人与最近操作都没变，不重建详情
    const int builds = center.GetDetailBuildCount();
    ::SendMessageW(center.m_hWnd, WM_TIMER, kVisitorTimer, 0);
    CHECK(center.GetDetailBuildCount() == builds);

    //新增一条操作记录：定时刷新时重建详情并列出它
    ShareActivityRecord second = first;
    second.m_path = "notes.txt";
    second.m_time = kSecondTime;
    REQUIRE(store.AddActivity(second));
    ::SendMessageW(center.m_hWnd, WM_TIMER, kVisitorTimer, 0);
    CHECK(center.GetDetailBuildCount() == builds + 1);
    const std::vector<std::wstring> lines = center.GetDetailLines();
    bool listed = false;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i].find(L"notes.txt") != std::wstring::npos)
            listed = true;
    }
    CHECK(listed);
    center.Close();
}

TEST_CASE("SCW15 share center search hint follows the tab", "[fileshare][sharemanage][sharecenterlist]")
{
    //与 ShareCenterWindow.cpp 的 kIdSearch 相同：顶栏搜索框的控件 id
    const UINT kCenterSearchId = 411;

    //「共享给我的」里的条目不带分享者的真实路径（2026-10-04 起），这个页签的提示文字不再写「路径」，改写「分享者」
    ManageFixture f;
    f.m_service.OnLoggedIn();
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterWithRows(f, center, ui);
    balloonwjui::DuiControl* root = center.GetRoot();
    REQUIRE(root != NULL);
    balloonwjui::DuiSearchBox* search = static_cast<balloonwjui::DuiSearchBox*>(root->FindCtrlById(kCenterSearchId));
    REQUIRE(search != NULL);
    CHECK(search->GetPlaceholder() == CString(L"搜索名称、路径、会话"));
    center.SetTab(share_center_tab_shared);
    CHECK(search->GetPlaceholder() == CString(L"搜索名称、分享者、会话"));
    center.SetTab(share_center_tab_mine);
    CHECK(search->GetPlaceholder() == CString(L"搜索名称、路径、会话"));
    center.Close();
}

namespace {

//SBN1 用的访问者观察者：本用例只看访问者手里的共享名称，回调一律忽略
class NameVisitorObserver : public IShareVisitorObserver
{
public:
    void OnVisitorStateChanged(int /*state*/, int /*errCode*/) override
    {
    }
    void OnVisitorListed(const std::string& /*path*/, int /*code*/, const std::vector<ShareEntry>& /*entries*/) override
    {
    }
    void OnVisitorPermsChanged(int /*perms*/) override
    {
    }
    void OnVisitorShareState(int /*state*/, int /*reason*/) override
    {
    }
};

} // namespace

TEST_CASE("SBN1 share name from the 2220 response goes to the visitor", "[fileshare][sharemanage]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    NameVisitorObserver observer;
    CShareVisitor* visitor = f.m_service.AcquireVisitor(kNewShareId, &observer);
    REQUIRE(visitor != NULL);
    CHECK(visitor->GetShareName().empty());

    //第一次列目录时发 2220；成功应答带共享名称，浏览窗口用它作标题（2026-10-04 起）
    visitor->List("", 0);
    REQUIRE(f.m_host.Count(cmd_share_open) == 1);
    const int idx = f.m_host.IndexOf(cmd_share_open, 0);
    f.m_service.HandleServerFrame(cmd_share_open, f.m_host.m_seqs[idx],
                                  u8"{\"accessid\":\"share-3-100\",\"code\":0,\"msg\":\"ok\",\"name\":\"张勇的文件共享\"}");
    CHECK(visitor->GetShareName() == u8"张勇的文件共享");
    f.m_service.ReleaseVisitor(kNewShareId);
}

namespace {

//判定「居中」时允许的左右、上下留白之差，单位：像素（工作区与窗口尺寸之差为奇数时，整数除法使两侧相差 1）
const int kCenterTolerancePx = 1;

//SWP7~SWP9 把窗口挪到的还原态位置：相对主显示器工作区左上角的偏移，单位：像素。取不整齐的值，避免与居中的落点巧合
const int kMoveOffsetX = 31;
const int kMoveOffsetY = 17;

//SWP7~SWP9 挪动时把窗口改小的量，单位：像素（打开时是默认大小 1080 宽，改小后仍大于最小尺寸 820 × 420）
const int kShrinkWidth = 60;
const int kShrinkHeight = 40;

//SWP10 写进配置文件的位置离虚拟屏幕右缘的距离，单位：像素；保证与任何一台显示器的工作区都不相交
const int kOffscreenGapPx = 2000;

//SWP10 写进配置文件的窗口大小，单位：像素
const int kOffscreenWidth = 900;
const int kOffscreenHeight = 600;

//配置文件里缺这个键时 GetPrivateProfileInt 的返回值；用来区分「没写」与「写了 0」
const int kIniKeyMissing = -12345;

//主显示器的工作区，屏幕坐标。「文件共享」窗口的 owner 为 NULL 时，没有可用的保存位置就在这里居中
RECT PrimaryWorkArea()
{
    RECT work;
    ::SetRectEmpty(&work);
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    return work;
}

//窗口当前的屏幕矩形
RECT CenterWindowRect(const CShareCenterWindow& center)
{
    RECT rc;
    ::SetRectEmpty(&rc);
    ::GetWindowRect(center.m_hWnd, &rc);
    return rc;
}

//rc 完整落在 area 之内
bool RectInside(const RECT& rc, const RECT& area)
{
    return rc.left >= area.left && rc.top >= area.top && rc.right <= area.right && rc.bottom <= area.bottom;
}

//rc 在 area 里居中：左右留白之差、上下留白之差都不超过 kCenterTolerancePx
bool RectCenteredIn(const RECT& rc, const RECT& area)
{
    const int leftGap = rc.left - area.left;
    const int rightGap = area.right - rc.right;
    const int topGap = rc.top - area.top;
    const int bottomGap = area.bottom - rc.bottom;
    return std::abs(leftGap - rightGap) <= kCenterTolerancePx && std::abs(topGap - bottomGap) <= kCenterTolerancePx;
}

//两个矩形四条边都相同
bool SameRect(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

//夹具个人目录下保存「文件共享」窗口位置的配置文件
std::wstring CenterPlacementIni(const ManageFixture& f)
{
    return SharePlacementIniPath(f.m_host.m_personalDir.c_str());
}

//直接读配置文件里 [ShareCenter] 小节的一个整数键；缺这个键时返回 kIniKeyMissing
int ReadCenterIniInt(const std::wstring& ini, const wchar_t* key)
{
    return (int)::GetPrivateProfileIntW(kShareCenterPlacementSection, key, kIniKeyMissing, ini.c_str());
}

//打开「文件共享」窗口（不送列表应答，窗口停在「加载中」，不影响位置与大小）
void OpenCenterForPlacement(ManageFixture& f, CShareCenterWindow& center, FakeUiHost& ui)
{
    REQUIRE(center.Open(NULL, &f.m_service, &f.m_host, &ui));
    REQUIRE(center.IsWindow());
}

//把还原态的窗口挪到主显示器工作区左上角附近并改小一些，返回挪动后的实际窗口矩形
RECT MoveCenterToTestRect(CShareCenterWindow& center)
{
    const RECT work = PrimaryWorkArea();
    const RECT cur = CenterWindowRect(center);
    ::SetWindowPos(center.m_hWnd, NULL, work.left + kMoveOffsetX, work.top + kMoveOffsetY,
                   RectWidth(cur) - kShrinkWidth, (cur.bottom - cur.top) - kShrinkHeight,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    const RECT moved = CenterWindowRect(center);
    //挪动之后的位置必须与打开时不同，否则后面「重开时原样恢复」的检查说明不了问题
    REQUIRE_FALSE(SameRect(moved, cur));
    return moved;
}

//配置文件里记下的矩形与是否最大化同 expected / maximized 一致
void CheckSavedPlacement(const std::wstring& ini, const RECT& expected, int maximized)
{
    CHECK(ReadCenterIniInt(ini, L"X") == expected.left);
    CHECK(ReadCenterIniInt(ini, L"Y") == expected.top);
    CHECK(ReadCenterIniInt(ini, L"Width") == expected.right - expected.left);
    CHECK(ReadCenterIniInt(ini, L"Height") == expected.bottom - expected.top);
    CHECK(ReadCenterIniInt(ini, L"Maximized") == maximized);
}

} // namespace

TEST_CASE("SWP6 share center opens centered on the primary work area when nothing is saved",
          "[fileshare][sharemanage][shareplacement]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    const std::wstring ini = CenterPlacementIni(f);
    REQUIRE(::GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES);

    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterForPlacement(f, center, ui);
    const RECT work = PrimaryWorkArea();
    const RECT rc = CenterWindowRect(center);
    CHECK(RectInside(rc, work));
    CHECK(RectCenteredIn(rc, work));
    CHECK_FALSE(::IsZoomed(center.m_hWnd));
    center.Close();
}

TEST_CASE("SWP7 share center reopens where it was moved and resized", "[fileshare][sharemanage][shareplacement]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    const std::wstring ini = CenterPlacementIni(f);
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterForPlacement(f, center, ui);
    const RECT moved = MoveCenterToTestRect(center);
    center.Close();

    //关窗时写下还原态矩形，未最大化
    CheckSavedPlacement(ini, moved, 0);

    //再次打开：按保存的矩形原样恢复
    OpenCenterForPlacement(f, center, ui);
    const RECT reopened = CenterWindowRect(center);
    CHECK(SameRect(reopened, moved));
    CHECK_FALSE(::IsZoomed(center.m_hWnd));
    center.Close();
}

TEST_CASE("SWP8 share center closed maximized reopens maximized and restores to its normal rect",
          "[fileshare][sharemanage][shareplacement]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    const std::wstring ini = CenterPlacementIni(f);
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterForPlacement(f, center, ui);
    const RECT normal = MoveCenterToTestRect(center);
    ::ShowWindow(center.m_hWnd, SW_MAXIMIZE);
    REQUIRE(::IsZoomed(center.m_hWnd));
    center.Close();

    //记下的是最大化之前的还原态矩形，并标记最大化
    CheckSavedPlacement(ini, normal, 1);

    //再次打开以最大化显示；还原后回到保存的还原态矩形
    OpenCenterForPlacement(f, center, ui);
    CHECK(::IsZoomed(center.m_hWnd));
    ::ShowWindow(center.m_hWnd, SW_RESTORE);
    REQUIRE_FALSE(::IsZoomed(center.m_hWnd));
    CHECK(SameRect(CenterWindowRect(center), normal));
    center.Close();
}

TEST_CASE("SWP9 share center minimized from maximized is saved as maximized", "[fileshare][sharemanage][shareplacement]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    const std::wstring ini = CenterPlacementIni(f);
    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterForPlacement(f, center, ui);
    const RECT normal = MoveCenterToTestRect(center);
    ::ShowWindow(center.m_hWnd, SW_MAXIMIZE);
    REQUIRE(::IsZoomed(center.m_hWnd));
    ::ShowWindow(center.m_hWnd, SW_MINIMIZE);
    REQUIRE(::IsIconic(center.m_hWnd));
    //对应在任务栏上右键关闭一个最小化的窗口
    center.Close();

    CheckSavedPlacement(ini, normal, 1);
}

TEST_CASE("SWP10 share center saved off screen reopens centered on the primary work area",
          "[fileshare][sharemanage][shareplacement]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    const std::wstring ini = CenterPlacementIni(f);
    ::CreateDirectoryW((f.m_host.m_personalDir + L"ChatLog").c_str(), NULL);

    //位置在虚拟屏幕右缘之外，与任何一台显示器都不相交（典型是那台显示器已经拔掉）
    const int virtualRight = ::GetSystemMetrics(SM_XVIRTUALSCREEN) + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    winplace::WindowPlacementPrefs saved;
    saved.m_hasSaved = true;
    saved.m_rect.left = virtualRight + kOffscreenGapPx;
    saved.m_rect.top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    saved.m_rect.right = saved.m_rect.left + kOffscreenWidth;
    saved.m_rect.bottom = saved.m_rect.top + kOffscreenHeight;
    saved.m_maximized = false;
    REQUIRE(SaveSharePlacement(ini.c_str(), kShareCenterPlacementSection, saved));

    FakeUiHost ui;
    CShareCenterWindow center;
    OpenCenterForPlacement(f, center, ui);
    const RECT work = PrimaryWorkArea();
    const RECT rc = CenterWindowRect(center);
    CHECK(RectInside(rc, work));
    CHECK(RectCenteredIn(rc, work));
    center.Close();
}

namespace {

//SWP14、SWP15 打开的共享 id（只用来取访问状态机，不送应答，窗口停在「加载中」，不影响位置与大小）
const int64_t kSwpBrowseShareA = 9101;
const int64_t kSwpBrowseShareB = 9102;

//浏览窗口当前的屏幕矩形
RECT BrowserWindowRect(const CShareBrowserWindow& window)
{
    RECT rc;
    ::SetRectEmpty(&rc);
    ::GetWindowRect(window.m_hWnd, &rc);
    return rc;
}

//直接读配置文件里 [ShareBrowser] 小节的一个整数键；缺这个键时返回 kIniKeyMissing
int ReadBrowserIniInt(const std::wstring& ini, const wchar_t* key)
{
    return (int)::GetPrivateProfileIntW(kShareBrowserPlacementSection, key, kIniKeyMissing, ini.c_str());
}

//按模块的做法打开一个浏览窗口：先交给它配置文件路径与相对谁错开，再打开
void OpenBrowserForPlacement(ManageFixture& f, CShareBrowserWindow& window, int64_t shareId, HWND cascadeFrom)
{
    window.SetPlacement(CenterPlacementIni(f), cascadeFrom);
    REQUIRE(window.Open(NULL, &f.m_service, shareId));
    REQUIRE(window.IsWindow());
}

} // namespace

TEST_CASE("SWP14 share browser opens centered when nothing is saved and reopens where it was moved",
          "[fileshare][sharemanage][shareplacement]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();
    const std::wstring ini = CenterPlacementIni(f);
    REQUIRE(::GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES);

    //没有保存过：在主显示器工作区居中（不再以参照窗口为中心）
    CShareBrowserWindow window;
    OpenBrowserForPlacement(f, window, kSwpBrowseShareA, NULL);
    const RECT work = PrimaryWorkArea();
    const RECT first = BrowserWindowRect(window);
    CHECK(RectInside(first, work));
    CHECK(RectCenteredIn(first, work));

    //挪到左上角附近并改小，关窗时写进 [ShareBrowser]，[ShareCenter] 不受影响
    ::SetWindowPos(window.m_hWnd, NULL, work.left + kMoveOffsetX, work.top + kMoveOffsetY,
                   RectWidth(first) - kShrinkWidth, (first.bottom - first.top) - kShrinkHeight,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    const RECT moved = BrowserWindowRect(window);
    REQUIRE_FALSE(SameRect(moved, first));
    window.Close();
    CHECK(ReadBrowserIniInt(ini, L"X") == moved.left);
    CHECK(ReadBrowserIniInt(ini, L"Y") == moved.top);
    CHECK(ReadBrowserIniInt(ini, L"Width") == moved.right - moved.left);
    CHECK(ReadBrowserIniInt(ini, L"Height") == moved.bottom - moved.top);
    CHECK(ReadBrowserIniInt(ini, L"Maximized") == 0);
    CHECK(ReadCenterIniInt(ini, L"X") == kIniKeyMissing);

    //再次打开（另一个共享也一样，所有共享共用一份）：按保存的矩形原样恢复
    CShareBrowserWindow other;
    OpenBrowserForPlacement(f, other, kSwpBrowseShareB, NULL);
    CHECK(SameRect(BrowserWindowRect(other), moved));
    CHECK_FALSE(::IsZoomed(other.m_hWnd));
    other.Close();
}

TEST_CASE("SWP15 a second share browser opens offset from the first one", "[fileshare][sharemanage][shareplacement]")
{
    ManageFixture f;
    f.m_service.OnLoggedIn();

    CShareBrowserWindow first;
    OpenBrowserForPlacement(f, first, kSwpBrowseShareA, NULL);
    const RECT rcFirst = BrowserWindowRect(first);

    //第二个窗口相对第一个右下各错开 kShareBrowserCascadeStep，大小相同
    CShareBrowserWindow second;
    OpenBrowserForPlacement(f, second, kSwpBrowseShareB, first.m_hWnd);
    const RECT rcSecond = BrowserWindowRect(second);
    CHECK(rcSecond.left == rcFirst.left + kShareBrowserCascadeStep);
    CHECK(rcSecond.top == rcFirst.top + kShareBrowserCascadeStep);
    CHECK(RectWidth(rcSecond) == RectWidth(rcFirst));
    CHECK(rcSecond.bottom - rcSecond.top == rcFirst.bottom - rcFirst.top);
    CHECK(RectInside(rcSecond, PrimaryWorkArea()));
    second.Close();
    first.Close();
}
