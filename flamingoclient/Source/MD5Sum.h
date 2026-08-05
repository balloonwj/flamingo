/*
* $Id: MD5Sum.h 4483 2008-01-02 09:19:06Z soarchin $
* 
* this file is part of easyMule
* Copyright (C)2002-2008 VeryCD Dev Team ( strEmail.Format("%s@%s", "emuledev", "verycd.com") / http:* www.easymule.org )
* 
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
#pragma once

//MD5 的纯 C 接口（MD5_CTX / MD5Init / MD5Update / MD5Final）已拆到 MD5Core.h，
//以便不链接 MFC 的模块也能用；本头文件在其之上追加依赖 CString 的 MD5Sum 类。
#include "MD5Core.h"

class MD5Sum
{
public:
	MD5Sum();
	MD5Sum(const CString& sSource);
	MD5Sum(const unsigned char* pachSource, UINT nLen);

	CString Calculate(const CString& sSource);
	CString Calculate(const unsigned char* pachSource, UINT nLen);

	CString GetHash() const;
	const unsigned char* GetRawHash() const { return m_rawHash; }

private:
	CString			m_sHash;
	unsigned char	m_rawHash[16];
};
