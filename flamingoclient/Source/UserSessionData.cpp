#include "stdafx.h"
#include "UserSessionData.h"
#include "UserSessionData.h"
#include "net/IUProtocolData.h"
#include "EncodeUtil.h"
#include "Utils.h"
#include "Path.h"
#include "Buffer.h"

//主程序目录
TCHAR g_szHomePath[MAX_PATH] = {0};
char g_szHomePathAscii[MAX_PATH] = {0};

std::wstring g_strAppTitle;

//系统库中用户头像的个数
const UINT USER_THUMB_COUNT = 36;

CBuddyInfo::CBuddyInfo(void)
{
    Reset();
    //FillFakeData();
}

CBuddyInfo::~CBuddyInfo(void)
{

}

void CBuddyInfo::Reset()
{
    m_uUserID = 0;
    m_strAccount = _T("");
    m_nTeamIndex = 0;
    m_strNickName = _T("");
    m_strMarkName = _T("");
    m_nFace = 0;
    m_strCustomFace = _T("");
    m_nStatus = STATUS_OFFLINE;
    m_nClientType = CLIENT_TYPE_UNKNOWN;
    m_strSign = _T("");
    m_nGender = 1;
    m_nBirthday = 0;
    m_strMobile = _T("");
    m_strEmail = _T("");
    m_strAddress = _T("");
    m_uMsgID = 0;
    m_bUseCustomFace = FALSE;
    m_bCustomFaceAvailable = FALSE;

    m_nTeamIndex = 0;
}

void CBuddyInfo::FillFakeData()
{
    m_uUserID = 0;
    m_nTeamIndex = 0;
    m_strNickName = _T("孙悟空");
    m_strMarkName = _T("");
    m_nFace = 0;
    m_nStatus = STATUS_OFFLINE;
    m_strSign = _T("观乎其上,得乎其中,观乎其中,得乎其下。");
    m_nBirthday = 0;
    m_strMobile = _T("");
    m_strEmail = _T("balloonwj@UTalk.com");
    m_strAddress = _T("上海市闵行区东川路555号");
    m_uMsgID = 0;
    m_bUseCustomFace = FALSE;
    m_bCustomFaceAvailable = FALSE;
}

void CBuddyInfo::SetBuddyInfo(CBuddyInfo* lpBuddyInfo)
{
    if (NULL == lpBuddyInfo)
        return;

    m_strAccount = lpBuddyInfo->m_strAccount;
    m_strNickName = lpBuddyInfo->m_strNickName;
    m_nFace = lpBuddyInfo->m_nFace;
    m_strCustomFace = lpBuddyInfo->m_strCustomFace;
    m_nGender = lpBuddyInfo->m_nGender;
    m_nBirthday = lpBuddyInfo->m_nBirthday;
    m_strMobile = lpBuddyInfo->m_strMobile;
    m_strEmail = lpBuddyInfo->m_strEmail;
    m_strAddress = lpBuddyInfo->m_strAddress;
    m_strSign = lpBuddyInfo->m_strSign;
}

CBuddyTeamInfo::CBuddyTeamInfo(void)
{
    Reset();
}

CBuddyTeamInfo::~CBuddyTeamInfo(void)
{

}

void CBuddyTeamInfo::Reset()
{
    m_nIndex = 0;
    m_nSort = 0;
    m_strName = _T("");

    m_arrBuddyInfo.clear();
}

int CBuddyTeamInfo::GetBuddyCount()
{
    return m_arrBuddyInfo.size();
}

int CBuddyTeamInfo::GetOnlineBuddyCount()
{
    int nCount = 0;
    for (int i = 0; i < (int)m_arrBuddyInfo.size(); i++)
    {
        CBuddyInfo* lpBuddyInfo = m_arrBuddyInfo[i];
        if (lpBuddyInfo != NULL && lpBuddyInfo->m_nStatus != STATUS_OFFLINE && lpBuddyInfo->m_nStatus != STATUS_INVISIBLE)
            nCount++;
    }
    return nCount;
}

CBuddyInfo* CBuddyTeamInfo::GetBuddy(int nIndex)
{
    if (nIndex >= 0 && nIndex < (int)m_arrBuddyInfo.size())
        return m_arrBuddyInfo[nIndex];
    else
        return NULL;
}

void CBuddyTeamInfo::Sort()
{
    CBuddyInfo* lpBuddyInfo1,*lpBuddyInfo2;
    BOOL bExchange;
    int nCount;

    nCount = (int)m_arrBuddyInfo.size();
    for (int i = 0; i < nCount - 1; i++)
    {
        bExchange = FALSE;
        for (int j = nCount - 1; j > i; j--)
        {
            lpBuddyInfo1 = m_arrBuddyInfo[j - 1];
            lpBuddyInfo2 = m_arrBuddyInfo[j];
            if (lpBuddyInfo1 != NULL && lpBuddyInfo2 != NULL)
            {
                if (lpBuddyInfo2->m_nStatus > lpBuddyInfo1->m_nStatus)
                {
                    m_arrBuddyInfo[j - 1] = lpBuddyInfo2;
                    m_arrBuddyInfo[j] = lpBuddyInfo1;
                    bExchange = TRUE;
                }
            }
        }
        if (!bExchange)
            break;
    }
}

COnlineBuddyInfo::COnlineBuddyInfo(void)
{
    Reset();
}

COnlineBuddyInfo::~COnlineBuddyInfo(void)
{

}

void COnlineBuddyInfo::Reset()
{
    m_nUin = 0;
    m_nStatus = STATUS_OFFLINE;
    m_nClientType = 0;
}

CBuddyList::CBuddyList(void)
{
    Reset();
}

CBuddyList::~CBuddyList(void)
{
    Reset();
}

void CBuddyList::Reset()
{
    m_nRetCode = 0;

    for (int i = 0; i < (int)m_arrBuddyTeamInfo.size(); i++)
    {
        CBuddyTeamInfo* lpBuddyTeamInfo = m_arrBuddyTeamInfo[i];
        if (lpBuddyTeamInfo != NULL)
            delete lpBuddyTeamInfo;
    }
    m_arrBuddyTeamInfo.clear();
}

int CBuddyList::GetBuddyTeamCount()
{
    return m_arrBuddyTeamInfo.size();
}

CBuddyTeamInfo* CBuddyList::GetBuddyTeam(int nTeamIndex)
{
    if (nTeamIndex >= 0 && nTeamIndex < (int)m_arrBuddyTeamInfo.size())
        return m_arrBuddyTeamInfo[nTeamIndex];
    else
        return NULL;
}

CBuddyTeamInfo* CBuddyList::GetBuddyTeamByIndex(int nIndex)
{
    for (int i = 0; i < (int)m_arrBuddyTeamInfo.size(); i++)
    {
        CBuddyTeamInfo* lpBuddyTeamInfo = m_arrBuddyTeamInfo[i];
        if (lpBuddyTeamInfo != NULL && nIndex == lpBuddyTeamInfo->m_nIndex)
            return lpBuddyTeamInfo;
    }
    return NULL;
}

int CBuddyList::GetBuddyTotalCount()
{
    size_t nTotalBuddyCount = 0;

    size_t nTeamCount = GetBuddyTeamCount();
    for (size_t i = 0; i<nTeamCount; ++i)
    {
        nTotalBuddyCount += GetBuddyCount(i);
    }

    return nTotalBuddyCount;
}


int CBuddyList::GetBuddyCount(int nTeamIndex)
{
    CBuddyTeamInfo* lpBuddyTeamInfo = GetBuddyTeam(nTeamIndex);
    if (lpBuddyTeamInfo != NULL)
        return lpBuddyTeamInfo->GetBuddyCount();
    else
        return 0;
}

int CBuddyList::GetOnlineBuddyCount(int nTeamIndex)
{
    CBuddyTeamInfo* lpBuddyTeamInfo = GetBuddyTeam(nTeamIndex);
    if (lpBuddyTeamInfo != NULL)
        return lpBuddyTeamInfo->GetOnlineBuddyCount();
    else
        return 0;
}

CBuddyInfo* CBuddyList::GetBuddy(int nTeamIndex, int nIndex)
{
    CBuddyTeamInfo* lpBuddyTeamInfo = GetBuddyTeam(nTeamIndex);
    if (lpBuddyTeamInfo != NULL)
        return lpBuddyTeamInfo->GetBuddy(nIndex);
    else
        return NULL;
}

CBuddyInfo* CBuddyList::GetBuddy(UINT nUTalkUin)
{
    for (int i = 0; i < (int)m_arrBuddyTeamInfo.size(); i++)
    {
        CBuddyTeamInfo* lpBuddyTeamInfo = m_arrBuddyTeamInfo[i];
        if (lpBuddyTeamInfo != NULL)
        {
            for (int j = 0; j < (int)lpBuddyTeamInfo->m_arrBuddyInfo.size(); j++)
            {
                CBuddyInfo* lpBuddyInfo = lpBuddyTeamInfo->m_arrBuddyInfo[j];
                //if (lpBuddyInfo != NULL && lpBuddyInfo->m_nUTalkUin == nUTalkUin)
                if (lpBuddyInfo != NULL && lpBuddyInfo->m_uUserID == nUTalkUin)
                    return lpBuddyInfo;
            }
        }
    }
    return NULL;
}


void CBuddyList::SortBuddyTeam()
{
    CBuddyTeamInfo* lpBuddyTeamInfo1,*lpBuddyTeamInfo2;
    BOOL bExchange;
    int nCount;

    nCount = (int)m_arrBuddyTeamInfo.size();
    for (int i = 0; i < nCount - 1; i++)
    {
        bExchange = FALSE;
        for (int j = nCount - 1; j > i; j--)
        {
            lpBuddyTeamInfo1 = m_arrBuddyTeamInfo[j - 1];
            lpBuddyTeamInfo2 = m_arrBuddyTeamInfo[j];
            if (lpBuddyTeamInfo1 != NULL && lpBuddyTeamInfo2 != NULL)
            {
                if (lpBuddyTeamInfo2->m_nSort < lpBuddyTeamInfo1->m_nSort)
                {
                    m_arrBuddyTeamInfo[j - 1] = lpBuddyTeamInfo2;
                    m_arrBuddyTeamInfo[j] = lpBuddyTeamInfo1;
                    bExchange = TRUE;
                }
            }
        }
        if (!bExchange)
            break;
    }
}

void CBuddyList::SortBuddy()
{
    for (int i = 0; i < (int)m_arrBuddyTeamInfo.size(); i++)
    {
        CBuddyTeamInfo* lpTeamInfo = m_arrBuddyTeamInfo[i];
        if (lpTeamInfo != NULL)
            lpTeamInfo->Sort();
    }
}

BOOL CBuddyList::AddBuddyTeam(CBuddyTeamInfo* lpBuddyTeamInfo)
{
    if (NULL == lpBuddyTeamInfo)
        return FALSE;
    lpBuddyTeamInfo->m_nIndex = m_arrBuddyTeamInfo.size();
    m_arrBuddyTeamInfo.push_back(lpBuddyTeamInfo);
    return TRUE;
}

BOOL CBuddyList::IsTeamNameExist(PCTSTR pszTeamName)
{
    CBuddyTeamInfo* pTeamInfo = NULL;
    size_t nTeamCount = m_arrBuddyTeamInfo.size();
    for (size_t i = 0; i<nTeamCount; ++i)
    {
        pTeamInfo = m_arrBuddyTeamInfo[i];
        if (pTeamInfo == NULL)
            continue;;

        if (pTeamInfo->m_strName == pszTeamName)
            return TRUE;
    }

    return FALSE;
}

CGroupInfo::CGroupInfo(void)
{
    Reset();
}

CGroupInfo::~CGroupInfo(void)
{
    Reset();
}

void CGroupInfo::Reset()
{
    m_nGroupCode = 0;
    m_nGroupId = 0;
    m_nGroupNumber = 0;
    m_strName = _T("");
    m_strMemo = _T("");
    m_strFingerMemo = _T("");
    m_nOwnerUin = 0;
    m_nCreateTime = 0;
    m_nFace = 0;
    m_nLevel = 0;
    m_nClass = 0;
    m_nOption = 0;
    m_nFlag = 0;
    m_bHasGroupNumber = m_bHasGroupInfo = FALSE;

    DelAllMember();
}

// 获取群成员总人数
int CGroupInfo::GetMemberCount()
{
    return (int)m_arrMember.size();
}

// 获取群成员在线人数
int CGroupInfo::GetOnlineMemberCount()
{
    int nCount = 0;
    for (int i = 0; i < (int)m_arrMember.size(); i++)
    {
        CBuddyInfo* lpBuddyInfo = m_arrMember[i];
        if (lpBuddyInfo != NULL && lpBuddyInfo->m_nStatus != STATUS_OFFLINE)
            nCount++;
    }
    return nCount;
}

// 根据索引获取群成员信息
CBuddyInfo* CGroupInfo::GetMember(int nIndex)
{
    if (nIndex < 0 || nIndex >= (int)m_arrMember.size())
        return NULL;

    return m_arrMember[nIndex];
}

// 根据UTalkUin获取群成员信息
CBuddyInfo* CGroupInfo::GetMemberByUin(UINT nUTalkUin)
{
    for (int i = 0; i < (int)m_arrMember.size(); i++)
    {
        CBuddyInfo* lpBuddyInfo = m_arrMember[i];
        if (lpBuddyInfo != NULL && lpBuddyInfo->m_uUserID == nUTalkUin)
            return lpBuddyInfo;
    }
    return NULL;
}

// 根据UTalkNum获取群成员信息
CBuddyInfo* CGroupInfo::GetMemberByAccount(PCTSTR pszAccountName)
{
    for (int i = 0; i < (int)m_arrMember.size(); i++)
    {
        CBuddyInfo* lpBuddyInfo = m_arrMember[i];
        if (lpBuddyInfo != NULL && lpBuddyInfo->m_strAccount == pszAccountName)
            return lpBuddyInfo;
    }
    return NULL;
}

// 对群成员列表按在线状态进行排序
void CGroupInfo::Sort()
{
    CBuddyInfo* lpBuddyInfo1,*lpBuddyInfo2;
    BOOL bExchange;
    int nCount;

    nCount = (int)m_arrMember.size();
    for (int i = 0; i < nCount - 1; i++)
    {
        bExchange = FALSE;
        for (int j = nCount - 1; j > i; j--)
        {
            lpBuddyInfo1 = m_arrMember[j - 1];
            lpBuddyInfo2 = m_arrMember[j];
            if (lpBuddyInfo1 != NULL && lpBuddyInfo2 != NULL)
            {
                if (lpBuddyInfo2->m_nStatus > lpBuddyInfo1->m_nStatus)
                {
                    m_arrMember[j - 1] = lpBuddyInfo2;
                    m_arrMember[j] = lpBuddyInfo1;
                    bExchange = TRUE;
                }
            }
        }
        if (!bExchange)
            break;
    }
}

// 添加群成员
BOOL CGroupInfo::AddMember(CBuddyInfo* lpBuddyInfo)
{
    if (NULL == lpBuddyInfo)
        return FALSE;
    m_arrMember.push_back(lpBuddyInfo);
    return TRUE;
}

// 删除所有群成员
BOOL CGroupInfo::DelAllMember()
{
    for (int i = 0; i < (int)m_arrMember.size(); i++)
    {
        CBuddyInfo* lpBuddyInfo = m_arrMember[i];
        if (lpBuddyInfo != NULL)
            delete lpBuddyInfo;
    }
    m_arrMember.clear();
    return TRUE;
}

BOOL CGroupInfo::IsHasGroupNumber()
{
    return m_bHasGroupNumber;
}

BOOL CGroupInfo::IsHasGroupInfo()
{
    return m_bHasGroupInfo;
}

BOOL CGroupInfo::IsMember(UINT uAccountID)
{
    for (std::vector<CBuddyInfo*>::iterator iter = m_arrMember.begin(); iter != m_arrMember.end(); ++iter)
    {
        if ((*iter)->m_uUserID == uAccountID)
            return TRUE;
    }

    return FALSE;
}

CGroupList::CGroupList(void)
{
    Reset();
}

CGroupList::~CGroupList(void)
{
    Reset();
}

void CGroupList::Reset()
{
    for (int i = 0; i < (int)m_arrGroupInfo.size(); i++)
    {
        CGroupInfo* lpGroupInfo = m_arrGroupInfo[i];
        if (lpGroupInfo != NULL)
            delete lpGroupInfo;
    }
    m_arrGroupInfo.clear();
}

// 获取群总数
int CGroupList::GetGroupCount()
{
    return (int)m_arrGroupInfo.size();
}

// 获取群信息(根据索引)
CGroupInfo* CGroupList::GetGroup(int nIndex)
{
    if (nIndex >= 0 && nIndex < (int)m_arrGroupInfo.size())
        return m_arrGroupInfo[nIndex];
    else
        return NULL;
}

// 获取群信息(根据群代码)
CGroupInfo* CGroupList::GetGroupByCode(UINT nGroupCode)
{
    for (int i = 0; i < (int)m_arrGroupInfo.size(); i++)
    {
        CGroupInfo* lpGroupInfo = m_arrGroupInfo[i];
        if (lpGroupInfo != NULL && lpGroupInfo->m_nGroupCode == nGroupCode)
            return lpGroupInfo;
    }
    return NULL;
}

// 获取群信息(根据群Id)
CGroupInfo* CGroupList::GetGroupById(UINT nGroupId)
{
    for (int i = 0; i < (int)m_arrGroupInfo.size(); i++)
    {
        CGroupInfo* lpGroupInfo = m_arrGroupInfo[i];
        if (lpGroupInfo != NULL && lpGroupInfo->m_nGroupId == nGroupId)
            return lpGroupInfo;
    }
    return NULL;
}

// 根据群代码和群成员UTalkUin获取群成员信息
CBuddyInfo* CGroupList::GetGroupMemberByCode(UINT nGroupCode, UINT nUTalkUin)
{
    CGroupInfo* lpGroupInfo = GetGroupByCode(nGroupCode);
    if (lpGroupInfo != NULL)
        return lpGroupInfo->GetMemberByUin(nUTalkUin);
    else
        return NULL;
}

// 根据群Id和群成员UTalkUin获取群成员信息
CBuddyInfo* CGroupList::GetGroupMemberById(UINT nGroupId, UINT nUTalkUin)
{
    CGroupInfo* lpGroupInfo = GetGroupById(nGroupId);
    if (lpGroupInfo != NULL)
        return lpGroupInfo->GetMemberByUin(nUTalkUin);
    else
        return NULL;
}

// 添加群
BOOL CGroupList::AddGroup(CGroupInfo* lpGroupInfo)
{
    if (NULL == lpGroupInfo)
        return FALSE;
    m_arrGroupInfo.push_back(lpGroupInfo);
    return TRUE;
}

// 由群Id获取群代码
UINT CGroupList::GetGroupCodeById(UINT nGroupId)
{
    CGroupInfo* lpGroupInfo = GetGroupById(nGroupId);
    return ((lpGroupInfo != NULL) ? lpGroupInfo->m_nGroupCode : 0);
}

// 由群代码获取群Id
UINT CGroupList::GetGroupIdByCode(UINT nGroupCode)
{
    CGroupInfo* lpGroupInfo = GetGroupByCode(nGroupCode);
    return ((lpGroupInfo != NULL) ? lpGroupInfo->m_nGroupId : 0);
}

CRecentInfo::CRecentInfo(void)
{
    Reset();
}

CRecentInfo::~CRecentInfo(void)
{

}

void CRecentInfo::Reset()
{
    m_nType = FMG_MSG_TYPE_BUDDY;
    m_uUserID = 0;
    m_uFaceID = 0;
    m_MsgTime = 0;
    memset(m_szName, 0, sizeof(m_szName));
    memset(m_szLastMsgText, 0, sizeof(m_szLastMsgText));
}

CRecentList::CRecentList(void)
{
    Reset();
}

CRecentList::~CRecentList(void)
{
    Reset();
}

void CRecentList::Reset()
{
    m_nRetCode = 0;

    for (int i = 0; i < (int)m_arrRecentInfo.size(); i++)
    {
        if (m_arrRecentInfo[i] != NULL)
            delete m_arrRecentInfo[i];
    }
    m_arrRecentInfo.clear();
}

BOOL CRecentList::DeleteRecentItem(UINT uAccountID)
{
    for (std::vector<CRecentInfo*>::const_iterator iter = m_arrRecentInfo.begin(); iter != m_arrRecentInfo.end(); ++iter)
    {
        CRecentInfo* pRecentInfo =*iter;
        if (pRecentInfo != NULL && pRecentInfo->m_uUserID == uAccountID)
        {
            delete pRecentInfo;
            m_arrRecentInfo.erase(iter);
            return TRUE;
        }
    }

    return FALSE;
}

BOOL CRecentList::AddRecent(CRecentInfo* lpRecentInfo)
{
    if (NULL == lpRecentInfo)
        return FALSE;

    //先删除原来的最近联系人信息
    for (std::vector<CRecentInfo*>::iterator iter = m_arrRecentInfo.begin(); iter != m_arrRecentInfo.end(); ++iter)
    {
        //TODO: 这里有问题
        if (*iter == NULL)
            continue;

        if ((*iter)->m_uUserID == lpRecentInfo->m_uUserID)
        {
            m_arrRecentInfo.erase(iter);
            break;
        }
    }


    //再将最新的存入进去
    m_arrRecentInfo.push_back(lpRecentInfo);

    //最多五十项
    if (m_arrRecentInfo.size() >= 50)
    {
        CRecentInfo* pLast = m_arrRecentInfo[m_arrRecentInfo.size() - 1];
        if (pLast != NULL)
        {
            m_arrRecentInfo.erase(m_arrRecentInfo.end() - 1);
            delete pLast;
        }
    }

    return TRUE;
}

int CRecentList::GetRecentCount()
{
    return (int)m_arrRecentInfo.size();
}

CRecentInfo* CRecentList::GetRecent(int nIndex)
{
    if (nIndex >= 0 && nIndex < (int)m_arrRecentInfo.size())
        return m_arrRecentInfo[nIndex];
    else
        return NULL;
}

CBuddyMessage::CBuddyMessage(void)
{
    m_nMsgType = CONTENT_TYPE_UNKNOWN;
    m_nMsgId = 0;
    m_nMsgId2 = 0;
    m_nFromUin = 0;
    m_nToUin = 0;
    m_nReplyIp = 0;
    m_nTime = 0;
    m_hwndFrom = NULL;
}

CBuddyMessage::~CBuddyMessage(void)
{
    Reset();
}

BOOL CBuddyMessage::IsType(LPCTSTR lpType)
{
    return (_tcsicmp(lpType, _T("message")) == 0);
}

void CBuddyMessage::Reset()
{
    m_nMsgId = 0;
    m_nMsgId2 = 0;
    m_nFromUin = 0;
    m_nToUin = 0;
    m_nMsgType = CONTENT_TYPE_UNKNOWN;
    m_nReplyIp = 0;
    m_nTime = 0;

    for (int i = 0; i < (int)m_arrContent.size(); i++)
    {
        CContent* lpContent = m_arrContent[i];
        if (lpContent != NULL)
            delete lpContent;
    }
    m_arrContent.clear();
}

BOOL CBuddyMessage::Parse(Json::Value& JsonValue)
{
    std::string strValue;
    int nCount;

    if (JsonValue["msgType"].isNull() || !JsonValue["msgType"].isInt())
        return FALSE;

    m_nMsgType = (CONTENT_TYPE)JsonValue["msgType"].asInt();

    if (JsonValue["time"].isNull())
        return false;


    m_nTime = JsonValue["time"].asUInt();

    CString strTmp;

    CContent* pContent = new CContent;
    //先解析字体信息
    //字体名
    if (!JsonValue["font"][(UINT)0].isNull())
    {
        strValue = JsonValue["font"][(UINT)0].asString();
        EncodeUtil::Utf8ToUnicode(strValue.data(), strTmp.GetBuffer(strValue.length()* 2), strValue.length()* 2);
        strTmp.ReleaseBuffer();
        pContent->m_FontInfo.m_strName = strTmp.GetString();
    }
    else
        pContent->m_FontInfo.m_strName = _T("微软雅黑");

    //字体大小
    if (!JsonValue["font"][(UINT)1].isNull())
        pContent->m_FontInfo.m_nSize = JsonValue["font"][(UINT)1].asUInt();
    else
        pContent->m_FontInfo.m_nSize = 12;

    //字体颜色
    if (!JsonValue["font"][(UINT)2].isNull())
        pContent->m_FontInfo.m_clrText = JsonValue["font"][(UINT)2].asUInt();
    else
        pContent->m_FontInfo.m_clrText = RGB(0, 0, 0);

    //是否粗体
    if (!JsonValue["font"][(UINT)3].isNull())
        pContent->m_FontInfo.m_bBold = JsonValue["font"][(UINT)3].asInt() != 0 ? TRUE : FALSE;
    else
        pContent->m_FontInfo.m_bBold = FALSE;

    //是否斜体
    if (!JsonValue["font"][(UINT)4].isNull())
        pContent->m_FontInfo.m_bItalic = JsonValue["font"][(UINT)4].asInt() != 0 ? TRUE : FALSE;
    else
        pContent->m_FontInfo.m_bItalic = FALSE;

    //是否下划线
    if (!JsonValue["font"][(UINT)5].isNull())
        pContent->m_FontInfo.m_bUnderLine = JsonValue["font"][(UINT)5].asInt() != 0 ? TRUE : FALSE;
    else
        pContent->m_FontInfo.m_bUnderLine = FALSE;

    pContent->m_nType = CONTENT_TYPE_FONT_INFO;
    m_arrContent.push_back(pContent);

    std::string strNodeValue;
    nCount = (int)JsonValue["content"].size();
    for (int i = 0; i < nCount; ++i)
    {
        if (JsonValue["content"][i].isNull())
            continue;

        if (!JsonValue["content"][i]["msgText"].isNull())
        {
            pContent = new CContent();
            pContent->m_nType = CONTENT_TYPE_TEXT;
            strNodeValue = JsonValue["content"][i]["msgText"].asString();
            EncodeUtil::Utf8ToUnicode(strNodeValue.data(), strTmp.GetBuffer(strNodeValue.length()* 2), strNodeValue.length()* 2);
            strTmp.ReleaseBuffer();
            pContent->m_strText = strTmp;
            m_arrContent.push_back(pContent);
        }
        else if (!JsonValue["content"][i]["faceID"].isNull())
        {
            pContent = new CContent();
            pContent->m_nType = CONTENT_TYPE_FACE;
            pContent->m_nFaceId = JsonValue["content"][i]["faceID"].asInt();
            m_arrContent.push_back(pContent);
        }
        else if (!JsonValue["content"][i]["shake"].isNull())
        {
            pContent = new CContent();
            pContent->m_nType = CONTENT_TYPE_SHAKE_WINDOW;
            pContent->m_nShakeTimes = JsonValue["content"][i]["shake"].asInt();
            m_arrContent.push_back(pContent);
        }
        else if (!JsonValue["content"][i]["pic"].isNull())
        {
            pContent = new CContent();
            pContent->m_nType = CONTENT_TYPE_CHAT_IMAGE;
            strNodeValue = JsonValue["content"][i]["pic"][(UINT)0].asString();;
            EncodeUtil::Utf8ToUnicode(strNodeValue.data(), strTmp.GetBuffer(strNodeValue.length()* 2), strNodeValue.length()* 2);
            strTmp.ReleaseBuffer();
            pContent->m_CFaceInfo.m_strFileName = strTmp;

            strNodeValue = JsonValue["content"][i]["pic"][(UINT)1].asString();;
            if (!strNodeValue.empty())
            {
                EncodeUtil::Utf8ToUnicode(strNodeValue.data(), strTmp.GetBuffer(strNodeValue.length()* 2), strNodeValue.length()* 2);
                strTmp.ReleaseBuffer();
                pContent->m_CFaceInfo.m_strFilePath = strTmp;
            }

            pContent->m_CFaceInfo.m_dwFileSize = JsonValue["content"][i]["pic"][(UINT)2].asUInt();
            m_arrContent.push_back(pContent);
        }
        else if (!JsonValue["content"][i]["file"].isNull())
        {
            pContent = new CContent();
            pContent->m_nType = CONTENT_TYPE_FILE;
            strNodeValue = JsonValue["content"][i]["file"][(UINT)0].asString();;
            EncodeUtil::Utf8ToUnicode(strNodeValue.data(), strTmp.GetBuffer(strNodeValue.length()* 2), strNodeValue.length()* 2);
            strTmp.ReleaseBuffer();
            pContent->m_CFaceInfo.m_strFileName = strTmp;

            strNodeValue = JsonValue["content"][i]["file"][(UINT)1].asString();;
            EncodeUtil::Utf8ToUnicode(strNodeValue.data(), strTmp.GetBuffer(strNodeValue.length()* 2), strNodeValue.length()* 2);
            strTmp.ReleaseBuffer();
            pContent->m_CFaceInfo.m_strFilePath = strTmp;

            pContent->m_CFaceInfo.m_dwFileSize = JsonValue["content"][i]["file"][2].asUInt();

            pContent->m_CFaceInfo.m_bOnline = JsonValue["content"][i]["file"][3].asUInt();
            m_arrContent.push_back(pContent);
        }
        else if (!JsonValue["content"][i]["remotedesktop"].isNull())
        {
            pContent = new CContent();
            pContent->m_nType = CONTENT_TYPE_REMOTE_DESKTOP;
            m_arrContent.push_back(pContent);
        }

    }

    return TRUE;
}

BOOL CBuddyMessage::IsShakeWindowMsg()
{
    long nSize = m_arrContent.size();
    if (nSize <= 0)
        return FALSE;

    for (long i = nSize - 1; i >= 0; --i)
    {
        if (m_arrContent[i]->m_nType == CONTENT_TYPE_SHAKE_WINDOW)
            return TRUE;
    }

    return FALSE;
}


CGroupMessage::CGroupMessage(void)
{
    Reset();
}

CGroupMessage::~CGroupMessage(void)
{

}

BOOL CGroupMessage::IsType(LPCTSTR lpType)
{
    return (_tcsicmp(lpType, _T("group_message")) == 0);
}

void CGroupMessage::Reset()
{
    m_nMsgId = 0;
    m_nMsgId2 = 0;
    m_nFromUin = 0;
    m_nToUin = 0;
    m_nMsgType = 0;
    m_nReplyIp = 0;
    m_nGroupCode = 0;
    m_nSendUin = 0;
    m_nSeq = 0;
    m_nInfoSeq = 0;
    m_nTime = 0;
    m_hwndFrom = NULL;

    for (int i = 0; i < (int)m_arrContent.size(); i++)
    {
        CContent* lpContent = m_arrContent[i];
        if (lpContent != NULL)
            delete lpContent;
    }
    m_arrContent.clear();
}

BOOL CGroupMessage::Parse(Json::Value& JsonValue)
{
    std::string strValue;
    int nCount, nCount2;

    Reset();

    if (!JsonValue["msg_id"].isNull())
        m_nMsgId = JsonValue["msg_id"].asUInt();

    if (!JsonValue["msg_id2"].isNull())
        m_nMsgId2 = JsonValue["msg_id2"].asUInt();

    if (!JsonValue["from_uin"].isNull())
        m_nFromUin = JsonValue["from_uin"].asUInt();

    if (!JsonValue["to_uin"].isNull())
        m_nToUin = JsonValue["to_uin"].asUInt();

    if (!JsonValue["msg_type"].isNull())
        m_nMsgType = JsonValue["msg_type"].asUInt();

    if (!JsonValue["reply_ip"].isNull())
        m_nReplyIp = JsonValue["reply_ip"].asUInt();

    if (!JsonValue["time"].isNull())
        m_nTime = JsonValue["time"].asUInt();

    if (!JsonValue["group_code"].isNull())
        m_nGroupCode = JsonValue["group_code"].asUInt();

    if (!JsonValue["send_uin"].isNull())
        m_nSendUin = JsonValue["send_uin"].asUInt();

    if (!JsonValue["seq"].isNull())
        m_nSeq = JsonValue["seq"].asUInt();

    if (!JsonValue["info_seq"].isNull())
        m_nInfoSeq = JsonValue["info_seq"].asUInt();

    nCount = (int)JsonValue["content"].size();
    for (int i = 0; i < nCount; ++i)
    {
        if (JsonValue["content"][i].isNull())
            continue;

        if (JsonValue["content"][i].isString())		// 纯文本
        {
            CContent* lpContent = new CContent;
            if (NULL == lpContent)
                continue;

            strValue = JsonValue["content"][i].asString();

            if (i == nCount - 1)					// 去除消息最后多余的一个空格
            {
                nCount2 = strValue.size();
                if (nCount2 > 0 && strValue[nCount2 - 1] == ' ')
                    strValue.erase(strValue.end() - 1);
            }

            Replace(strValue, "\r", "\r\n");

            lpContent->m_nType = CONTENT_TYPE_TEXT;
            lpContent->m_strText = EncodeUtil::Utf8ToUnicode(strValue);
            m_arrContent.push_back(lpContent);
        }
        else if (JsonValue["content"][i].isArray())
        {
            if (JsonValue["content"][i][(UINT)0].isNull())
                continue;

            strValue = JsonValue["content"][i][(UINT)0].asString();	// 内容类型
            if (strValue == "font")			// 字体信息
            {
                CContent* lpContent = new CContent;
                if (NULL == lpContent)
                    continue;

                lpContent->m_nType = CONTENT_TYPE_FONT_INFO;

                if (!JsonValue["content"][i][(UINT)1]["size"].isNull())				// 字体大小
                    lpContent->m_FontInfo.m_nSize = JsonValue["content"][i][(UINT)1]["size"].asInt();

                if (!JsonValue["content"][i][(UINT)1]["color"].isNull())			// 字体颜色
                {
                    strValue = JsonValue["content"][i][(UINT)1]["color"].asString();
                    lpContent->m_FontInfo.m_clrText = HexStrToRGB(EncodeUtil::Utf8ToUnicode(strValue).c_str());
                }

                if (!JsonValue["content"][i][(UINT)1]["style"][(UINT)0].isNull())	// 字体风格(加粗)
                    lpContent->m_FontInfo.m_bBold = JsonValue["content"][i][(UINT)1]["style"][(UINT)0].asBool();

                if (!JsonValue["content"][i][(UINT)1]["style"][(UINT)1].isNull())	// 字体风格(倾斜)
                    lpContent->m_FontInfo.m_bItalic = JsonValue["content"][i][(UINT)1]["style"][(UINT)1].asBool();

                if (!JsonValue["content"][i][(UINT)1]["style"][(UINT)2].isNull())	// 字体风格(下划线)
                    lpContent->m_FontInfo.m_bUnderLine = JsonValue["content"][i][(UINT)1]["style"][(UINT)2].asBool();

                if (!JsonValue["content"][i][(UINT)1]["name"].isNull())				// 字体名称
                {
                    strValue = JsonValue["content"][i][(UINT)1]["name"].asString();
                    lpContent->m_FontInfo.m_strName = EncodeUtil::Utf8ToUnicode(strValue);
                }

                m_arrContent.push_back(lpContent);
            }
            else if (strValue == "face")	// 系统表情
            {
                CContent* lpContent = new CContent;
                if (NULL == lpContent)
                    continue;
                lpContent->m_nType = CONTENT_TYPE_FACE;
                if (!JsonValue["content"][i][(UINT)1].isNull())
                    lpContent->m_nFaceId = JsonValue["content"][i][(UINT)1].asInt();
                m_arrContent.push_back(lpContent);
            }
            else if (strValue == "cface")	// 自定义表情
            {
                CContent* lpContent = new CContent;
                if (NULL == lpContent)
                    continue;
                lpContent->m_nType = CONTENT_TYPE_CHAT_IMAGE;
                if (!JsonValue["content"][i][(UINT)1].isNull())
                {
                    if (!JsonValue["content"][i][(UINT)1]["name"].isNull())
                    {
                        strValue = JsonValue["content"][i][(UINT)1]["name"].asString();
                        lpContent->m_CFaceInfo.m_strName = EncodeUtil::Utf8ToUnicode(strValue);
                    }

                    if (!JsonValue["content"][i][(UINT)1]["file_id"].isNull())
                        lpContent->m_CFaceInfo.m_nFileId = JsonValue["content"][i][(UINT)1]["file_id"].asUInt();

                    if (!JsonValue["content"][i][(UINT)1]["key"].isNull())
                    {
                        strValue = JsonValue["content"][i][(UINT)1]["key"].asString();
                        lpContent->m_CFaceInfo.m_strKey = EncodeUtil::Utf8ToUnicode(strValue);
                    }

                    if (!JsonValue["content"][i][(UINT)1]["server"].isNull())
                    {
                        strValue = JsonValue["content"][i][(UINT)1]["server"].asString();
                        lpContent->m_CFaceInfo.m_strServer = EncodeUtil::Utf8ToUnicode(strValue);
                    }
                }
                m_arrContent.push_back(lpContent);
            }
            else if (strValue == "cface_idx")	// 未知
            {

            }
        }
    }

    return TRUE;
}

CSessMessage::CSessMessage(void)
{
    Reset();
}

CSessMessage::~CSessMessage(void)
{
    Reset();
}

BOOL CSessMessage::IsType(LPCTSTR lpType)
{
    return (_tcsicmp(lpType, _T("sess_message")) == 0);
}

void CSessMessage::Reset()
{
    m_nMsgId = 0;
    m_nMsgId2 = 0;
    m_nFromUin = 0;
    m_nToUin = 0;
    m_nMsgType = 0;
    m_nReplyIp = 0;
    m_nTime = 0;
    m_nGroupId = 0;
    m_nUTalkNum = 0;
    m_nServiceType = 0;
    m_nFlags = 0;

    for (int i = 0; i < (int)m_arrContent.size(); i++)
    {
        CContent* lpContent = m_arrContent[i];
        if (lpContent != NULL)
            delete lpContent;
    }
    m_arrContent.clear();
}

BOOL CSessMessage::Parse(Json::Value& JsonValue)
{
    std::string strValue;
    int nCount, nCount2;

    Reset();

    if (!JsonValue["msg_id"].isNull())
        m_nMsgId = JsonValue["msg_id"].asUInt();

    if (!JsonValue["msg_id2"].isNull())
        m_nMsgId2 = JsonValue["msg_id2"].asUInt();

    if (!JsonValue["from_uin"].isNull())
        m_nFromUin = JsonValue["from_uin"].asUInt();

    if (!JsonValue["to_uin"].isNull())
        m_nToUin = JsonValue["to_uin"].asUInt();

    if (!JsonValue["msg_type"].isNull())
        m_nMsgType = JsonValue["msg_type"].asUInt();

    if (!JsonValue["reply_ip"].isNull())
        m_nReplyIp = JsonValue["reply_ip"].asUInt();

    if (!JsonValue["time"].isNull())
        m_nTime = JsonValue["time"].asUInt();

    if (!JsonValue["id"].isNull())
        m_nGroupId = JsonValue["id"].asUInt();

    if (!JsonValue["ruin"].isNull())
        m_nUTalkNum = JsonValue["ruin"].asUInt();

    if (!JsonValue["service_type"].isNull())
        m_nServiceType = JsonValue["service_type"].asUInt();

    m_nFlags = 0;	// 待解析("flags":{"text":1,"pic":1,"file":1,"audio":1,"video":1})

    nCount = (int)JsonValue["content"].size();
    for (int i = 0; i < nCount; ++i)
    {
        if (JsonValue["content"][i].isNull())
            continue;

        if (JsonValue["content"][i].isString())		// 纯文本
        {
            CContent* lpContent = new CContent;
            if (NULL == lpContent)
                continue;

            strValue = JsonValue["content"][i].asString();

            if (i == nCount - 1)					// 去除消息最后多余的一个空格
            {
                nCount2 = strValue.size();
                if (nCount2 > 0 && (strValue[nCount2 - 1] == ' '))
                    strValue.erase(strValue.end() - 1);
            }

            Replace(strValue, "\r", "\r\n");

            lpContent->m_nType = CONTENT_TYPE_TEXT;
            lpContent->m_strText = EncodeUtil::Utf8ToUnicode(strValue);
            m_arrContent.push_back(lpContent);
        }
        else if (JsonValue["content"][i].isArray())
        {
            if (JsonValue["content"][i][(UINT)0].isNull())
                continue;

            strValue = JsonValue["content"][i][(UINT)0].asString();	// 内容类型
            if (strValue == "font")		// 字体信息
            {
                CContent* lpContent = new CContent;
                if (NULL == lpContent)
                    continue;

                lpContent->m_nType = CONTENT_TYPE_FONT_INFO;

                if (!JsonValue["content"][i][(UINT)1]["size"].isNull())				// 字体大小
                    lpContent->m_FontInfo.m_nSize = JsonValue["content"][i][(UINT)1]["size"].asInt();

                if (!JsonValue["content"][i][(UINT)1]["color"].isNull())			// 字体颜色
                {
                    strValue = JsonValue["content"][i][(UINT)1]["color"].asString();
                    lpContent->m_FontInfo.m_clrText = HexStrToRGB(EncodeUtil::Utf8ToUnicode(strValue).c_str());
                }

                if (!JsonValue["content"][i][(UINT)1]["style"][(UINT)0].isNull())	// 字体风格(加粗)
                    lpContent->m_FontInfo.m_bBold = JsonValue["content"][i][(UINT)1]["style"][(UINT)0].asBool();

                if (!JsonValue["content"][i][(UINT)1]["style"][(UINT)1].isNull())	// 字体风格(倾斜)
                    lpContent->m_FontInfo.m_bItalic = JsonValue["content"][i][(UINT)1]["style"][(UINT)1].asBool();

                if (!JsonValue["content"][i][(UINT)1]["style"][(UINT)2].isNull())	// 字体风格(下划线)
                    lpContent->m_FontInfo.m_bUnderLine = JsonValue["content"][i][(UINT)1]["style"][(UINT)2].asBool();

                if (!JsonValue["content"][i][(UINT)1]["name"].isNull())				// 字体名称
                {
                    strValue = JsonValue["content"][i][(UINT)1]["name"].asString();
                    lpContent->m_FontInfo.m_strName = EncodeUtil::Utf8ToUnicode(strValue);
                }
                m_arrContent.push_back(lpContent);
            }
            else if (strValue == "face")	// 系统表情
            {
                CContent* lpContent = new CContent;
                if (NULL == lpContent)
                    continue;
                lpContent->m_nType = CONTENT_TYPE_FACE;
                if (!JsonValue["content"][i][(UINT)1].isNull())
                    lpContent->m_nFaceId = JsonValue["content"][i][(UINT)1].asInt();
                m_arrContent.push_back(lpContent);
            }
        }
    }

    return TRUE;
}

CStatusChangeMessage::CStatusChangeMessage(void)
{
    Reset();
}

CStatusChangeMessage::~CStatusChangeMessage(void)
{

}

BOOL CStatusChangeMessage::IsType(LPCTSTR lpType)
{
    return (_tcsicmp(lpType, _T("buddies_status_change")) == 0);
}

void CStatusChangeMessage::Reset()
{
    m_nUTalkUin = 0;
    m_nStatus = STATUS_OFFLINE;
    m_nClientType = 0;
}

BOOL CStatusChangeMessage::Parse(Json::Value& JsonValue)
{
    std::string strValue;

    Reset();

    if (!JsonValue["uin"].isNull())
        m_nUTalkUin = JsonValue["uin"].asUInt();

    if (!JsonValue["status"].isNull())
    {
        strValue = JsonValue["status"].asString();
        /*m_nStatus = ConvertToUTalkStatus(EncodeUtil::Utf8ToUnicode(strValue).c_str());*/
    }

    if (!JsonValue["client_type"].isNull())
        m_nClientType = JsonValue["client_type"].asUInt();

    return TRUE;
}

CKickMessage::CKickMessage(void)
{
    Reset();
}

CKickMessage::~CKickMessage(void)
{

}

BOOL CKickMessage::IsType(LPCTSTR lpType)
{
    return (_tcsicmp(lpType, _T("kick_message")) == 0);
}

void CKickMessage::Reset()
{
    m_nMsgId = 0;
    m_nMsgId2 = 0;
    m_nFromUin = 0;
    m_nToUin = 0;
    m_nMsgType = 0;
    m_nReplyIp = 0;
    m_bShowReason = FALSE;
    m_strReason = _T("");
}

BOOL CKickMessage::Parse(Json::Value& JsonValue)
{
    std::string strValue;

    Reset();

    if (!JsonValue["msg_id"].isNull())
        m_nMsgId = JsonValue["msg_id"].asUInt();

    if (!JsonValue["from_uin"].isNull())
        m_nFromUin = JsonValue["from_uin"].asUInt();

    if (!JsonValue["to_uin"].isNull())
        m_nToUin = JsonValue["to_uin"].asUInt();

    if (!JsonValue["msg_id2"].isNull())
        m_nMsgId2 = JsonValue["msg_id2"].asUInt();

    if (!JsonValue["msg_type"].isNull())
        m_nMsgType = JsonValue["msg_type"].asUInt();

    if (!JsonValue["reply_ip"].isNull())
        m_nReplyIp = JsonValue["reply_ip"].asUInt();

    if (!JsonValue["show_reason"].isNull())
        m_bShowReason = JsonValue["show_reason"].asBool();

    if (!JsonValue["reason"].isNull())
    {
        strValue = JsonValue["reason"].asString();
        m_strReason = EncodeUtil::Utf8ToUnicode(strValue);
    }

    return TRUE;
}

CSysGroupMessage::CSysGroupMessage(void)
{
    Reset();
}

CSysGroupMessage::~CSysGroupMessage(void)
{

}

BOOL CSysGroupMessage::IsType(LPCTSTR lpType)
{
    return (_tcsicmp(lpType, _T("sys_g_msg")) == 0);
}

void CSysGroupMessage::Reset()
{
    m_nMsgId = 0;
    m_nMsgId2 = 0;
    m_nFromUin = 0;
    m_nToUin = 0;
    m_nMsgType = 0;
    m_nReplyIp = 0;
    m_strSubType = _T("");
    m_nGroupCode = 0;
    m_nGroupNumber = 0;
    m_nAdminUin = 0;
    m_strMsg = _T("");
    m_nOpType = 0;
    m_nOldMember = 0;
    m_strOldMember = _T("");
    m_strAdminUin = _T("");
    m_strAdminNickName = _T("");
}

BOOL CSysGroupMessage::Parse(Json::Value& JsonValue)
{
    std::string strValue;

    Reset();

    if (!JsonValue["msg_id"].isNull())
        m_nMsgId = JsonValue["msg_id"].asUInt();

    if (!JsonValue["from_uin"].isNull())
        m_nFromUin = JsonValue["from_uin"].asUInt();

    if (!JsonValue["to_uin"].isNull())
        m_nToUin = JsonValue["to_uin"].asUInt();

    if (!JsonValue["msg_id2"].isNull())
        m_nMsgId2 = JsonValue["msg_id2"].asUInt();

    if (!JsonValue["msg_type"].isNull())
        m_nMsgType = JsonValue["msg_type"].asUInt();

    if (!JsonValue["reply_ip"].isNull())
        m_nReplyIp = JsonValue["reply_ip"].asUInt();

    if (!JsonValue["type"].isNull())
    {
        strValue = JsonValue["type"].asString();
        m_strSubType = EncodeUtil::Utf8ToUnicode(strValue);
    }

    if (m_strSubType == _T("group_request_join_agree")
        || m_strSubType == _T("group_request_join_deny"))		// 加群同意通知或拒绝加群通知
    {
        if (!JsonValue["gcode"].isNull())
            m_nGroupCode = JsonValue["gcode"].asUInt();

        if (!JsonValue["t_gcode"].isNull())
            m_nGroupNumber = JsonValue["t_gcode"].asUInt();

        if (!JsonValue["admin_uin"].isNull())
            m_nAdminUin = JsonValue["admin_uin"].asUInt();

        if (!JsonValue["msg"].isNull())
        {
            strValue = JsonValue["msg"].asString();
            m_strMsg = EncodeUtil::Utf8ToUnicode(strValue);
        }
        return TRUE;
    }
    else if (m_strSubType == _T("group_leave"))	// 被移出群通知
    {
        if (!JsonValue["gcode"].isNull())
            m_nGroupCode = JsonValue["gcode"].asUInt();

        if (!JsonValue["t_gcode"].isNull())
            m_nGroupNumber = JsonValue["t_gcode"].asUInt();

        if (!JsonValue["op_type"].isNull())
            m_nOpType = JsonValue["op_type"].asUInt();

        if (!JsonValue["old_member"].isNull())
            m_nOldMember = JsonValue["old_member"].asUInt();

        if (!JsonValue["t_old_member"].isNull())
        {
            strValue = JsonValue["t_old_member"].asString();
            m_strOldMember = EncodeUtil::Utf8ToUnicode(strValue);
        }

        if (!JsonValue["admin_uin"].isNull())
            m_nAdminUin = JsonValue["admin_uin"].asUInt();

        if (!JsonValue["t_admin_uin"].isNull())
        {
            strValue = JsonValue["t_admin_uin"].asString();
            m_strAdminUin = EncodeUtil::Utf8ToUnicode(strValue);
        }

        if (!JsonValue["admin_nick"].isNull())
        {
            strValue = JsonValue["admin_nick"].asString();
            m_strAdminNickName = EncodeUtil::Utf8ToUnicode(strValue);
        }
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

CUpdateFriendInfoMsg::CUpdateFriendInfoMsg()
{
    Reset();
}

CUpdateFriendInfoMsg::~CUpdateFriendInfoMsg()
{

}

BOOL CUpdateFriendInfoMsg::IsType(LPCTSTR lpType)
{
    return (_tcsicmp(lpType, _T("update_friend_info_msg")) == 0);
}

void CUpdateFriendInfoMsg::Reset()
{
    m_uMsgType = 0;
    m_uMsgId = 0;
    m_uMsgId2 = 0;
    m_uFromUin = 0;
    m_uToUin = 0;
    m_uClientType = CLIENT_TYPE_PC;
    m_uTime = 0;
}

BOOL CUpdateFriendInfoMsg::Parse(Json::Value& JsonValue)
{
    Reset();

    if (!JsonValue["msgType"].isNull())
        m_uMsgType = JsonValue["msgType"].asUInt();

    if (!JsonValue["clientType"].isNull())
        m_uClientType = JsonValue["clientType"].asUInt();

    if (!JsonValue["msgID"].isNull())
        m_uMsgId = JsonValue["msgID"].asUInt();

    m_uMsgId2 = m_uMsgId;

    if (!JsonValue["from"].isNull())
        m_uFromUin = JsonValue["from"].asUInt();

    if (!JsonValue["to"].isNull())
        m_uToUin = JsonValue["to"].asUInt();

    if (!JsonValue["time"].isNull())
        m_uTime = JsonValue["time"].asUInt();

    return TRUE;
}


CMessageSender::CMessageSender(long nType, UINT nSenderId, UINT nGroupCode)
{
    m_nType = nType;
    m_nSenderId = nSenderId;
    m_nGroupCode = nGroupCode;
}

CMessageSender::~CMessageSender(void)
{
    DelAllMsg();
}

// 添加消息
BOOL CMessageSender::AddMsg(void* lpMsg)
{
    if (NULL == lpMsg)
        return FALSE;
    m_arrMsg.push_back(lpMsg);
    return TRUE;
}

// 删除消息(根据索引)
BOOL CMessageSender::DelMsg(int nIndex)
{
    if (nIndex >= 0 && nIndex < (int)m_arrMsg.size())
    {
        m_arrMsg.erase(m_arrMsg.begin() + nIndex);
        return TRUE;
    }
    else
        return FALSE;
}

// 删除消息(根据消息Id)
BOOL CMessageSender::DelMsgById(UINT nMsgId)
{
    for (int i = 0; i < (int)m_arrMsg.size(); i++)
    {
        switch (m_nType)
        {
        case FMG_MSG_TYPE_BUDDY:
        {
                                     CBuddyMessage* lpMsg = (CBuddyMessage*)m_arrMsg[i];
                                     if (lpMsg != NULL && lpMsg->m_nMsgId == nMsgId)
                                     {
                                         delete lpMsg;
                                         m_arrMsg.erase(m_arrMsg.begin() + i);
                                         return TRUE;
                                     }
        }
            break;
        case FMG_MSG_TYPE_GROUP:
        {
                                     //CGroupMessage* lpMsg = (CGroupMessage*)m_arrMsg[i];
                                     CBuddyMessage* lpMsg = (CBuddyMessage*)m_arrMsg[i];
                                     if (lpMsg != NULL && lpMsg->m_nMsgId == nMsgId)
                                     {
                                         delete lpMsg;
                                         m_arrMsg.erase(m_arrMsg.begin() + i);
                                         return TRUE;
                                     }
        }
            break;
        case FMG_MSG_TYPE_SESS:
        {
                                    CSessMessage* lpMsg = (CSessMessage*)m_arrMsg[i];
                                    if (lpMsg != NULL && lpMsg->m_nMsgId == nMsgId)
                                    {
                                        delete lpMsg;
                                        m_arrMsg.erase(m_arrMsg.begin() + i);
                                        return TRUE;
                                    }
        }
            break;
        }
    }
    return FALSE;
}

// 删除所有消息
BOOL CMessageSender::DelAllMsg()
{
    for (int i = 0; i < (int)m_arrMsg.size(); i++)
    {
        void* lpMsg = m_arrMsg[i];
        if (lpMsg != NULL)
        {
            switch (m_nType)
            {
            case FMG_MSG_TYPE_BUDDY:
                delete (CBuddyMessage*)lpMsg;
                break;
            case FMG_MSG_TYPE_GROUP:
                delete (CBuddyMessage*)lpMsg;
                break;
            case FMG_MSG_TYPE_SESS:
                delete (CSessMessage*)lpMsg;
                break;
            }
        }
    }
    m_arrMsg.clear();
    return TRUE;
}

// 获取消息类型
long CMessageSender::GetMsgType()
{
    return m_nType;
}

// 获取发送者Id
UINT CMessageSender::GetSenderId()
{
    return m_nSenderId;
}

// 获取消息总数
int CMessageSender::GetDisplayMsgCount()
{
    CBuddyMessage* pBuddyMessage = NULL;
    long nMsgCount = 0;
    size_t nCount = m_arrMsg.size();
    for (size_t i = 0; i<nCount; ++i)
    {
        pBuddyMessage = (CBuddyMessage*)m_arrMsg[i];
        if (pBuddyMessage != NULL && pBuddyMessage->m_nMsgType != CONTENT_TYPE_IMAGE_CONFIRM)
            ++nMsgCount;

    }
    return nMsgCount;
}

// 获取消息总数
int CMessageSender::GetMsgCount()
{
    return (int)m_arrMsg.size();
}

// 获取群代码
UINT CMessageSender::GetGroupCode()
{
    return m_nGroupCode;
}

// 获取好友消息(根据索引)
CBuddyMessage* CMessageSender::GetBuddyMsg(int nIndex)
{
    if (m_nType != FMG_MSG_TYPE_BUDDY)
        return NULL;

    if (nIndex >= 0 && nIndex < (int)m_arrMsg.size())
        return (CBuddyMessage*)m_arrMsg[nIndex];
    else
        return NULL;
}

// 获取群消息(根据索引)
CBuddyMessage* CMessageSender::GetGroupMsg(int nIndex)
{
    if (m_nType != FMG_MSG_TYPE_GROUP)
        return NULL;

    if (nIndex >= 0 && nIndex < (int)m_arrMsg.size())
        return (CBuddyMessage*)m_arrMsg[nIndex];
    else
        return NULL;
}

// 获取临时会话消息(根据索引)
CSessMessage* CMessageSender::GetSessMsg(int nIndex)
{
    if (m_nType != FMG_MSG_TYPE_SESS)
        return NULL;

    if (nIndex >= 0 && nIndex < (int)m_arrMsg.size())
        return (CSessMessage*)m_arrMsg[nIndex];
    else
        return NULL;
}

// 获取好友消息(根据消息Id)
CBuddyMessage* CMessageSender::GetBuddyMsgById(UINT nMsgId)
{
    if (m_nType != FMG_MSG_TYPE_BUDDY)
        return NULL;

    for (int i = 0; i < (int)m_arrMsg.size(); i++)
    {
        CBuddyMessage* lpMsg = (CBuddyMessage*)m_arrMsg[i];
        if (lpMsg != NULL && lpMsg->m_nMsgId == nMsgId)
            return lpMsg;
    }

    return NULL;
}

// 获取群消息(根据消息Id)
CBuddyMessage* CMessageSender::GetGroupMsgById(UINT nMsgId)
{
    if (m_nType != FMG_MSG_TYPE_GROUP)
        return NULL;

    for (int i = 0; i < (int)m_arrMsg.size(); i++)
    {
        CBuddyMessage* lpMsg = (CBuddyMessage*)m_arrMsg[i];
        if (lpMsg != NULL && lpMsg->m_nMsgId == nMsgId)
            return lpMsg;
    }

    return NULL;
}

// 获取临时会话消息(根据消息Id)
CSessMessage* CMessageSender::GetSessMsgById(UINT nMsgId)
{
    if (m_nType != FMG_MSG_TYPE_SESS)
        return NULL;

    for (int i = 0; i < (int)m_arrMsg.size(); i++)
    {
        CSessMessage* lpMsg = (CSessMessage*)m_arrMsg[i];
        if (lpMsg != NULL && lpMsg->m_nMsgId == nMsgId)
            return lpMsg;
    }

    return NULL;
}

// 获取群系统消息(根据消息Id)
CSysGroupMessage* CMessageSender::GetSysGroupMsgById(UINT nMsgId)
{
    //if (m_nType != FMG_MSG_TYPE_SYSGROUP)
    //	return NULL;

    //for (int i = 0; i < (int)m_arrMsg.size(); i++)
    //{
    //	CSysGroupMessage* lpMsg = (CSysGroupMessage*)m_arrMsg[i];
    //	if (lpMsg != NULL && lpMsg->m_nMsgId == nMsgId)
    //		return lpMsg;
    //}

    return NULL;
}

CMessageList::CMessageList(void)
{

}

CMessageList::~CMessageList(void)
{
    DelAllMsgSender();
}

// 添加消息
BOOL CMessageList::AddMsg(long nType, UINT nSenderId,
    UINT nGroupCode, void* lpMsg)
{
    if (NULL == lpMsg)
        return FALSE;

    CMessageSender* lpMsgSender = GetMsgSender(nType, nSenderId);
    if (NULL == lpMsgSender)
    {
        lpMsgSender = new CMessageSender(nType, nSenderId, nGroupCode);
        if (NULL == lpMsgSender)
            return FALSE;

        m_arrMsgSender.push_back(lpMsgSender);
    }
    AddMsgIdentity(nType, nSenderId);
    return lpMsgSender->AddMsg(lpMsg);
}

// 删除消息
BOOL CMessageList::DelMsg(long nType, UINT nMsgId)
{
    for (int i = 0; i < (int)m_arrMsgSender.size(); i++)
    {
        CMessageSender* lpMsgSender = m_arrMsgSender[i];
        if (lpMsgSender != NULL && lpMsgSender->GetMsgType() == nType)
        {
            if (lpMsgSender->DelMsgById(nMsgId))
                return TRUE;
        }
    }
    return FALSE;
}

// 删除消息发送者(根据索引)
BOOL CMessageList::DelMsgSender(int nIndex)
{
    if (nIndex >= 0 && nIndex < (int)m_arrMsgSender.size())
    {
        CMessageSender* lpMsgSender = m_arrMsgSender[nIndex];
        if (lpMsgSender != NULL)
        {
            DelMsgIdentity(lpMsgSender->GetMsgType(), lpMsgSender->GetSenderId());
            delete lpMsgSender;
        }
        m_arrMsgSender.erase(m_arrMsgSender.begin() + nIndex);
        return TRUE;
    }
    else
        return FALSE;
}

// 删除消息发送者(根据消息类型和发送者Id)
BOOL CMessageList::DelMsgSender(long nType, UINT nSenderId)
{
    for (int i = 0; i < (int)m_arrMsgSender.size(); i++)
    {
        CMessageSender* lpMsgSender = m_arrMsgSender[i];
        if (lpMsgSender != NULL && lpMsgSender->GetMsgType() == nType
            && lpMsgSender->GetSenderId() == nSenderId)
        {
            DelMsgIdentity(nType, nSenderId);
            delete lpMsgSender;
            m_arrMsgSender.erase(m_arrMsgSender.begin() + i);
            return TRUE;
        }
    }
    return FALSE;
}

// 删除所有消息发送者
BOOL CMessageList::DelAllMsgSender()
{
    for (int i = 0; i < (int)m_arrMsgSender.size(); i++)
    {
        CMessageSender* lpMsgSender = m_arrMsgSender[i];
        if (lpMsgSender != NULL)
        {
            DelMsgIdentity(lpMsgSender->GetMsgType(), lpMsgSender->GetSenderId());
            delete lpMsgSender;
        }
    }
    m_arrMsgSender.clear();
    return TRUE;
}

// 获取消息发送者总数
int CMessageList::GetMsgSenderCount()
{
    return (int)m_arrMsgSender.size();
}

// 获取消息发送者(根据索引)
CMessageSender* CMessageList::GetMsgSender(int nIndex)
{
    if (nIndex >= 0 && nIndex < (int)m_arrMsgSender.size())
        return m_arrMsgSender[nIndex];
    else
        return NULL;
}

// 获取消息发送者(根据消息类型和发送者Id)
CMessageSender* CMessageList::GetMsgSender(long nType, UINT nSenderId)
{
    for (int i = 0; i < (int)m_arrMsgSender.size(); i++)
    {
        CMessageSender* lpMsgSender = m_arrMsgSender[i];
        if (lpMsgSender != NULL && lpMsgSender->GetMsgType() == nType
            && lpMsgSender->GetSenderId() == nSenderId)
        {
            return lpMsgSender;
        }
    }
    return NULL;
}

// 获取最后一个发送消息过来的发送者
CMessageSender* CMessageList::GetLastMsgSender()
{
    int nCount = m_arrMsgIdentity.size();
    if (nCount <= 0)
        return NULL;

    MSG_IDENTITY stMsgId;
    stMsgId = m_arrMsgIdentity[nCount - 1];
    return GetMsgSender(stMsgId.nType, stMsgId.nSenderId);
}

// 获取好友消息(根据消息Id)
CBuddyMessage* CMessageList::GetBuddyMsg(UINT nMsgId)
{
    for (int i = 0; i < (int)m_arrMsgSender.size(); i++)
    {
        CMessageSender* lpMsgSender = m_arrMsgSender[i];
        if (lpMsgSender != NULL && lpMsgSender->GetMsgType() == FMG_MSG_TYPE_BUDDY)
        {
            CBuddyMessage* lpMsg = lpMsgSender->GetBuddyMsgById(nMsgId);
            if (lpMsg != NULL)
                return lpMsg;
        }
    }
    return NULL;
}

// 获取群消息(根据消息Id)
CBuddyMessage* CMessageList::GetGroupMsg(UINT nMsgId)
{
    for (int i = 0; i < (int)m_arrMsgSender.size(); i++)
    {
        CMessageSender* lpMsgSender = m_arrMsgSender[i];
        if (lpMsgSender != NULL && lpMsgSender->GetMsgType() == FMG_MSG_TYPE_GROUP)
        {
            CBuddyMessage* lpMsg = lpMsgSender->GetGroupMsgById(nMsgId);
            if (lpMsg != NULL)
                return lpMsg;
        }
    }
    return NULL;
}

// 获取临时会话消息(根据消息Id)
CSessMessage* CMessageList::GetSessMsg(UINT nMsgId)
{
    for (int i = 0; i < (int)m_arrMsgSender.size(); i++)
    {
        CMessageSender* lpMsgSender = m_arrMsgSender[i];
        if (lpMsgSender != NULL && lpMsgSender->GetMsgType() == FMG_MSG_TYPE_SESS)
        {
            CSessMessage* lpMsg = lpMsgSender->GetSessMsgById(nMsgId);
            if (lpMsg != NULL)
                return lpMsg;
        }
    }
    return NULL;
}

// 获取好友消息(根据发送者Id和消息Id)
CBuddyMessage* CMessageList::GetBuddyMsg(UINT nUTalkUin, UINT nMsgId)
{
    CMessageSender* lpMsgSender = GetMsgSender(FMG_MSG_TYPE_BUDDY, nUTalkUin);
    if (lpMsgSender != NULL)
        return lpMsgSender->GetBuddyMsgById(nMsgId);
    else
        return NULL;
}

// 获取群消息(根据发送者Id和消息Id)
CBuddyMessage* CMessageList::GetGroupMsg(UINT nGroupCode, UINT nMsgId)
{
    CMessageSender* lpMsgSender = GetMsgSender(FMG_MSG_TYPE_GROUP, nGroupCode);
    if (lpMsgSender != NULL)
        return lpMsgSender->GetGroupMsgById(nMsgId);
    else
        return NULL;
}

// 获取临时会话消息(根据发送者Id和消息Id)
CSessMessage* CMessageList::GetSessMsg(UINT nUTalkUin, UINT nMsgId)
{
    CMessageSender* lpMsgSender = GetMsgSender(FMG_MSG_TYPE_SESS, nUTalkUin);
    if (lpMsgSender != NULL)
        return lpMsgSender->GetSessMsgById(nMsgId);
    else
        return NULL;
}

void CMessageList::AddMsgIdentity(long nType, UINT nSenderId)
{
    DelMsgIdentity(nType, nSenderId);

    MSG_IDENTITY stMsgId;
    stMsgId.nType = nType;
    stMsgId.nSenderId = nSenderId;
    m_arrMsgIdentity.push_back(stMsgId);
}

void CMessageList::DelMsgIdentity(long nType, UINT nSenderId)
{
    for (int i = 0; i < (int)m_arrMsgIdentity.size(); i++)
    {
        if (m_arrMsgIdentity[i].nType == nType
            && m_arrMsgIdentity[i].nSenderId == nSenderId)
        {
            m_arrMsgIdentity.erase(m_arrMsgIdentity.begin() + i);
            break;
        }
    }
}

// CMessageLogger::CMessageLogger(void)
// {
// 
// }
// 
// CMessageLogger::~CMessageLogger(void)
// {
// 
// }
// 
// BOOL CMessageLogger::CreateMsgLogFile(LPCTSTR lpszFileName)
// {
// 	sqlite3*conn = NULL;
// 	char*err_msg = NULL;
// 
// 	int nRet = sqlite3_open16(lpszFileName, &conn);
// 	if (nRet != SQLITE_OK)
// 		return FALSE;
// 
// 	char* lpSql = "CREATE TABLE [tb_BuddyMsg] ([id] INTEGER PRIMARY KEY, [uin] INTEGER, [nickname] TEXT, [time] INTEGER, [sendflag] INTEGER, [content] TEXT)";
// 	nRet = sqlite3_exec(conn, lpSql, 0, 0, &err_msg);	// 创建好友消息表
// 	if (nRet != SQLITE_OK)
// 	{
// 		sqlite3_close(conn);
// 		return FALSE;
// 	}
// 
// 	lpSql = "CREATE TABLE [tb_GroupMsg] ([id] INTEGER PRIMARY KEY, [groupnum] INTEGER, [uin] INTEGER, [nickname] TEXT, [time] INTEGER, [content] TEXT)";
// 	nRet = sqlite3_exec(conn, lpSql, 0, 0, &err_msg);	// 创建群消息表
// 	if (nRet != SQLITE_OK)
// 	{
// 		sqlite3_close(conn);
// 		return FALSE;
// 	}
// 
// 	lpSql = "CREATE TABLE [tb_SessMsg] ([id] INTEGER PRIMARY KEY, [uin] INTEGER, [nickname] TEXT, [time] INTEGER, [sendflag] INTEGER, [content] TEXT)";
// 	nRet = sqlite3_exec(conn, lpSql, 0, 0, &err_msg);	// 创建临时会话(群成员)消息表
// 	if (nRet != SQLITE_OK)
// 	{
// 		sqlite3_close(conn);
// 		return FALSE;
// 	}
// 
// 	nRet = sqlite3_close(conn);
// 	return (nRet != SQLITE_OK) ? FALSE : TRUE;
// }