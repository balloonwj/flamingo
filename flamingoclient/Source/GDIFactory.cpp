#include "stdafx.h"
#include "GDIFactory.h"

std::map<long, HFONT> CGDIFactory::m_mapFontCache;
std::map<long, HFONT> CGDIFactory::m_mapBoldFontCache;

HFONT CGDIFactory::GetFont(long nSize)
{
	std::map<long, HFONT>::iterator iter = m_mapFontCache.find(nSize);
	if(iter != m_mapFontCache.end())
		return iter->second;

	HFONT hFont = ::CreateFont(nSize,						// nHeight 
								 0,							// nWidth 
								 0,							// nEscapement 
								 0,							// nOrientation 
								 FW_NORMAL,					// nWeight 
								 FALSE,						// bItalic 
								 FALSE,						// bUnderline 
								 0,							// cStrikeOut 
								 DEFAULT_CHARSET,			// nCharSet 
								 OUT_DEFAULT_PRECIS,		// nOutPrecision 
								 CLIP_DEFAULT_PRECIS,		// nClipPrecision 
								 DEFAULT_QUALITY,			// nQuality 
								 DEFAULT_PITCH | FF_SWISS,	// nPitchAndFamily 
								 _T("Î¢ÈíÑÅºÚ"));

	m_mapFontCache.insert(std::pair<long, HFONT>(nSize, hFont));

	return hFont;
}

HFONT CGDIFactory::GetBoldFont(long nSize)
{
	std::map<long, HFONT>::iterator iter = m_mapBoldFontCache.find(nSize);
	if(iter != m_mapBoldFontCache.end())
		return iter->second;

	HFONT hFont = ::CreateFont(nSize,						// nHeight 
								 0,							// nWidth 
								 0,							// nEscapement 
								 0,							// nOrientation 
								 FW_BOLD,					// nWeight 
								 FALSE,						// bItalic 
								 FALSE,						// bUnderline 
								 0,							// cStrikeOut 
								 DEFAULT_CHARSET,			// nCharSet 
								 OUT_DEFAULT_PRECIS,		// nOutPrecision 
								 CLIP_DEFAULT_PRECIS,		// nClipPrecision 
								 DEFAULT_QUALITY,			// nQuality 
								 DEFAULT_PITCH | FF_SWISS,	// nPitchAndFamily 
								 _T("Î¢ÈíÑÅºÚ"));

	m_mapBoldFontCache.insert(std::pair<long, HFONT>(nSize, hFont));

	return hFont;
}

void CGDIFactory::Uninit()
{
	HFONT hFont = NULL;
	for(std::map<long, HFONT>::iterator iter=m_mapFontCache.begin(); iter!=m_mapFontCache.end(); ++iter)
	{
		hFont = iter->second;
		if(hFont != NULL)
		{
			::DeleteObject(hFont);
			iter->second = NULL;
		}
	}
	m_mapFontCache.clear();

	for(std::map<long, HFONT>::iterator iter=m_mapBoldFontCache.begin(); iter!=m_mapBoldFontCache.end(); ++iter)
	{
		hFont = iter->second;
		if(hFont != NULL)
		{
			::DeleteObject(hFont);
			iter->second = NULL;
		}
	}
	m_mapBoldFontCache.clear();
}