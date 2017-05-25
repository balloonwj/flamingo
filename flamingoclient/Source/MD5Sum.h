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

/* Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
rights reserved.

License to copy and use this software is granted provided that it
is identified as the "RSA Data Security, Inc. MD5 Message-Digest
Algorithm" in all material mentioning or referencing this software
or this function.

License is also granted to make and use derivative works provided
that such works are identified as "derived from the RSA Data
Security, Inc. MD5 Message-Digest Algorithm" in all material
mentioning or referencing the derived work.

RSA Data Security, Inc. makes no representations concerning either
the merchantability of this software or the suitability of this
software for any particular purpose. It is provided "as is"
without express or implied warranty of any kind.

These notices must be retained in any copies of any part of this
documentation and/or software.
*/


typedef unsigned char*POINTER;
typedef unsigned short int UINT2;
typedef unsigned long int UINT4;

typedef struct {
  UINT4 state[4];
  UINT4 count[2];
  unsigned char buffer[64];
} MD5_CTX;

void MD5Init (MD5_CTX*);
void MD5Update (MD5_CTX*, const unsigned char*, unsigned int);
void MD5Final (unsigned char [16], MD5_CTX*);

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
