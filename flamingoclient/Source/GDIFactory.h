#pragma once
#include <map>

class CGDIFactory
{
public:
	static HFONT GetFont(long nSize);
	static HFONT GetBoldFont(long nSize);

	static void Uninit();

private:
	static std::map<long, HFONT> m_mapFontCache;
	static std::map<long, HFONT> m_mapBoldFontCache;
};