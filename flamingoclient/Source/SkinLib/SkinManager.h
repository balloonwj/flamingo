#pragma once

#include <vector>
#include <map>
//#include "../Path.h"
#include "../ImageEx.h"
#include "../XmlDocument.h"

struct SKIN_INFO		// 皮肤信息
{
	int nSkinID;			// 皮肤ID
	CString strSkinName;	// 皮肤名称
	CString strSkinPath;	// 皮肤路径
};

struct SKIN_IMAGE_INFO	// 皮肤图片信息
{
	CImageEx* lpImage;		// 图片对象
	int nRef;				// 引用计数
};

class CSkinManager
{
public:
	CSkinManager(void);
	~CSkinManager(void);

public:
	static BOOL Init();
	static void UnInit();
	static CSkinManager* GetInstance();

	void SetSkinPath(LPCTSTR lpszPath);
	BOOL LoadConfigXml();
	BOOL SaveConfigXml();
	BOOL SetCurSkin(int nSkinID);
	std::vector<SKIN_INFO*>& GetSkinList();
	BOOL LoadImage(LPCTSTR lpszFileName);
	BOOL DestroyImage(LPCTSTR lpszFileName);
	CImageEx* GetImage(LPCTSTR lpszFileName);
	void ReleaseImage(CImageEx*& lpImgEx);

private:
	CString GetAbsolutePath(LPCTSTR lpFileName);
	SKIN_INFO* GetSkinInfo(int nSkinID);
	void ClearSkinList();
	void ClearImage();

private:
	static CSkinManager* m_lpSkinMgr;
	std::vector<SKIN_INFO*> m_arrSkinList;
	std::map<CString, SKIN_IMAGE_INFO*> m_mapImage;
	CString m_strPath;
	int m_nCurSkinID;
};
