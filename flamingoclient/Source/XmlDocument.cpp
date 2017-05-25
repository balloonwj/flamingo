#include "stdafx.h"
#include "XmlDocument.h"

CXmlNode::CXmlNode(void)
{
	m_pXMLNode = NULL;
}

CXmlNode::~CXmlNode(void)
{
	Release();
}

BOOL CXmlNode::SelectSingleNode(LPCTSTR pNodeName, CXmlNode& objXmlNode)
{
	IXMLDOMNode* pXMLNode;
	BSTR bstrNodeName;
	HRESULT hr;

	if (!pNodeName || !m_pXMLNode)
		return FALSE;

	bstrNodeName = ::SysAllocString(pNodeName);

	pXMLNode = NULL;
	hr = m_pXMLNode->selectSingleNode(bstrNodeName, &pXMLNode);
	::SysFreeString(bstrNodeName);
	if (hr == S_OK)
	{
		objXmlNode.Attach(pXMLNode);
		return TRUE;
	}
	else
	{
		objXmlNode.Attach(NULL);
		return FALSE;
	}
}

BOOL CXmlNode::SelectNodes(LPCTSTR pNodeName, CXmlNodeList& objXmlNodeList)
{
	IXMLDOMNodeList* pXMLNodeList;
	BSTR bstrNodeName;
	HRESULT hr;

	if (!pNodeName || !m_pXMLNode)
		return FALSE;

	bstrNodeName = ::SysAllocString(pNodeName);

	pXMLNodeList = NULL;
	hr = m_pXMLNode->selectNodes(bstrNodeName, &pXMLNodeList);
	::SysFreeString(bstrNodeName);
	if (hr == S_OK)
	{
		objXmlNodeList.Attach(pXMLNodeList);
		return TRUE;
	}
	else
	{
		objXmlNodeList.Attach(NULL);
		return FALSE;
	}
}

BOOL CXmlNode::GetFirstChildNode(LPCTSTR pNodeName, CXmlNode& objXmlNode)
{
	IXMLDOMNode* pXMLNode,* pTmpXMLNode;
	BSTR bstrNodeName;
	HRESULT hr;

	if (!pNodeName || !m_pXMLNode)
		return FALSE;

	pXMLNode = NULL;
	hr = m_pXMLNode->get_firstChild(&pXMLNode);
	while (hr == S_OK)
	{
		bstrNodeName = NULL;
		hr = pXMLNode->get_nodeName(&bstrNodeName);
		if (hr == S_OK)
		{
			int nRet = wcscmp(bstrNodeName, pNodeName);
			::SysFreeString(bstrNodeName);
			if (nRet == 0)
			{
				objXmlNode.Attach(pXMLNode);
				return TRUE;
			}
		}

		pTmpXMLNode = NULL;
		hr = pXMLNode->get_nextSibling(&pTmpXMLNode);
		pXMLNode->Release();
		pXMLNode = pTmpXMLNode;
	}

	objXmlNode.Attach(NULL);
	return FALSE;
}

BOOL CXmlNode::GetNextSiblingNode(LPCTSTR pNodeName, CXmlNode& objXmlNode)
{
	IXMLDOMNode* pXMLNode,* pTmpXMLNode;
	BSTR bstrNodeName;
	HRESULT hr;

	if (!pNodeName || !m_pXMLNode)
		return FALSE;

	pXMLNode = NULL;
	hr = m_pXMLNode->get_nextSibling(&pXMLNode);
	while (hr == S_OK)
	{
		bstrNodeName = NULL;
		hr = pXMLNode->get_nodeName(&bstrNodeName);
		if (hr == S_OK)
		{
			int nRet = wcscmp(bstrNodeName, pNodeName);
			::SysFreeString(bstrNodeName);
			if (nRet == 0)
			{
				objXmlNode.Attach(pXMLNode);
				return TRUE;
			}
		}

		pTmpXMLNode = NULL;
		hr = pXMLNode->get_nextSibling(&pTmpXMLNode);
		pXMLNode->Release();
		pXMLNode = pTmpXMLNode;
	}

	objXmlNode.Attach(NULL);
	return FALSE;
}

BOOL CXmlNode::GetFirstChildNode(CXmlNode& objXmlNode)
{
	IXMLDOMNode* pXMLNode;
	HRESULT hr;

	if (NULL == m_pXMLNode)
		return FALSE;

	pXMLNode = NULL;
	hr = m_pXMLNode->get_firstChild(&pXMLNode);
	if (hr == S_OK)
	{
		objXmlNode.Attach(pXMLNode);
		return TRUE;
	}
	else
	{
		objXmlNode.Attach(NULL);
		return FALSE;
	}
}

BOOL CXmlNode::GetNextSiblingNode(CXmlNode& objXmlNode)
{
	IXMLDOMNode* pXMLNode;
	HRESULT hr;

	if (NULL == m_pXMLNode)
		return FALSE;

	pXMLNode = NULL;
	hr = m_pXMLNode->get_nextSibling(&pXMLNode);
	if (hr == S_OK)
	{
		objXmlNode.Attach(pXMLNode);
		return TRUE;
	}
	else
	{
		objXmlNode.Attach(NULL);
		return FALSE;
	}
}

std::wstring CXmlNode::GetNodeName()
{
	BSTR bstrNodeName;
	std::wstring strNodeName;
	HRESULT hr;

	if (NULL == m_pXMLNode)
		return _T("");

	bstrNodeName = NULL;
	hr = m_pXMLNode->get_nodeName(&bstrNodeName);
	if (hr != S_OK)
		return _T("");
	
	strNodeName = (WCHAR*)bstrNodeName;
	::SysFreeString(bstrNodeName);

	return strNodeName;
}

std::wstring CXmlNode::GetText()
{
	BSTR bstrText;
	std::wstring sText;
	HRESULT hr;

	if (!m_pXMLNode)
		return _T("");

	bstrText = NULL;
	hr = m_pXMLNode->get_text(&bstrText);
	if (hr != S_OK)
		return _T("");

	sText = (WCHAR*)bstrText;
	::SysFreeString(bstrText);

	return sText;
}

int CXmlNode::GetTextInt()
{
	std::wstring strText = GetText();
	return _ttoi(strText.c_str());
}

std::wstring CXmlNode::GetAttribute(LPCTSTR lpAttributeName)
{
	IXMLDOMNamedNodeMap* lpNamedNodeMap;
	IXMLDOMNode* lpXMLNode;
	BSTR bstrValue;
	std::wstring strValue;
	HRESULT hr;

	if (NULL == lpAttributeName || NULL == m_pXMLNode)
		return _T("");

	lpNamedNodeMap = NULL;
	hr = m_pXMLNode->get_attributes(&lpNamedNodeMap);
	if (hr == S_OK)
	{
		lpXMLNode = NULL;
		hr = lpNamedNodeMap->getNamedItem((WCHAR*)lpAttributeName, &lpXMLNode);
		if (hr == S_OK)
		{
			hr = lpXMLNode->get_text(&bstrValue);
			if (hr == S_OK)
			{
				strValue = (WCHAR*)bstrValue;
				::SysFreeString(bstrValue);
			}
			lpXMLNode->Release();
		}
		lpNamedNodeMap->Release();
	}

	return strValue;
}

int CXmlNode::GetAttributeInt(LPCTSTR lpAttributeName)
{
	std::wstring strValue = GetAttribute(lpAttributeName);
	return _ttoi(strValue.c_str());
}

void CXmlNode::Release()
{
	if (m_pXMLNode)
	{
		m_pXMLNode->Release();
		m_pXMLNode = NULL;
	}
}

void CXmlNode::Attach(IXMLDOMNode* pXMLNode)
{
	Release();
	m_pXMLNode = pXMLNode;
}

IXMLDOMNode* CXmlNode::Detach()
{
	IXMLDOMNode* pXMLNode;

	pXMLNode = m_pXMLNode;
	m_pXMLNode = NULL;
	return pXMLNode;
}

CXmlNodeList::CXmlNodeList(void)
{
	m_pXMLNodeList = NULL;
}

CXmlNodeList::~CXmlNodeList(void)
{
	Release();
}

int CXmlNodeList::GetLength()
{
	long lLength;
	HRESULT hr;

	if (!m_pXMLNodeList)
		return 0;

	lLength = 0;
	hr = m_pXMLNodeList->get_length(&lLength);
	if (hr == S_OK)
		return lLength;
	else
		return 0;
}

BOOL CXmlNodeList::GetItem(int nIndex, CXmlNode& objXmlNode)
{
	IXMLDOMNode* pXMLNode;
	HRESULT hr;

	if (!m_pXMLNodeList)
		return 0;

	pXMLNode = NULL;
	hr = m_pXMLNodeList->get_item(nIndex, &pXMLNode);
	if (hr == S_OK)
		return TRUE;
	else
		return FALSE;
}

void CXmlNodeList::Release()
{
	if (m_pXMLNodeList)
	{
		m_pXMLNodeList->Release();
		m_pXMLNodeList = NULL;
	}
}

void CXmlNodeList::Attach(IXMLDOMNodeList* pXMLNodeList)
{
	Release();
	m_pXMLNodeList = pXMLNodeList;
}

IXMLDOMNodeList* CXmlNodeList::Detach()
{
	IXMLDOMNodeList* pXMLNodeList;

	pXMLNodeList = m_pXMLNodeList;
	m_pXMLNodeList = NULL;
	return pXMLNodeList;
}

CXmlDocument::CXmlDocument(void)
{
	m_pXMLDoc = NULL;
}

CXmlDocument::~CXmlDocument(void)
{
	Release();
}

BOOL CXmlDocument::Load(LPCTSTR pPath)
{
	VARIANT varPath;
	VARIANT_BOOL isSuccessful = 0;
	BOOL bRet = FALSE;
	HRESULT hr;

	if (NULL == pPath || NULL ==*pPath || 
		::GetFileAttributes(pPath) == 0xFFFFFFFF)
		return FALSE;

	::VariantInit(&varPath);

	Release();

	hr = ::CoCreateInstance(__uuidof(DOMDocument), NULL, 
		CLSCTX_INPROC_SERVER, __uuidof(IXMLDOMDocument2), (void**)&m_pXMLDoc);
	if (FAILED(hr))
		return FALSE;

	hr = m_pXMLDoc->put_async(VARIANT_FALSE);	// 关闭异步调用
	if (FAILED(hr))
		goto Ret0;

	hr = m_pXMLDoc->put_resolveExternals(VARIANT_FALSE);	// 是否启用外部DTD分析
	if (FAILED(hr))
		goto Ret0;

	hr = m_pXMLDoc->put_validateOnParse(VARIANT_FALSE);		// 是否允许文档验证
	if (FAILED(hr))
		goto Ret0;

	varPath.vt = VT_BSTR;
	varPath.bstrVal = ::SysAllocString(pPath);

	hr = m_pXMLDoc->load(varPath, &isSuccessful);
	if (isSuccessful != VARIANT_TRUE)
		goto Ret0;

	bRet = TRUE;

Ret0:
	::VariantClear(&varPath);

	if (!bRet)
		Release();

	return bRet;
}

BOOL CXmlDocument::LoadXml(LPCTSTR pXml)
{
	VARIANT_BOOL isSuccessful = 0;
	BOOL bRet = FALSE;
	HRESULT hr;

	if (NULL == pXml)
		return FALSE;

	Release();

	hr = ::CoCreateInstance(__uuidof(DOMDocument), NULL, 
		CLSCTX_INPROC_SERVER, __uuidof(IXMLDOMDocument2), (void**)&m_pXMLDoc);
	if (FAILED(hr))
		return FALSE;

	hr = m_pXMLDoc->put_async(VARIANT_FALSE);	// 关闭异步调用
	if (FAILED(hr))
		goto Ret0;

	hr = m_pXMLDoc->put_resolveExternals(VARIANT_FALSE);	// 是否启用外部DTD分析
	if (FAILED(hr))
		goto Ret0;

	hr = m_pXMLDoc->put_validateOnParse(VARIANT_FALSE);		// 是否允许文档验证
	if (FAILED(hr))
		goto Ret0;

	BSTR bstrXml = ::SysAllocString(pXml);
	hr = m_pXMLDoc->loadXML(bstrXml, &isSuccessful);
	::SysFreeString(bstrXml);

	bRet = (isSuccessful != VARIANT_TRUE) ? FALSE : TRUE;

Ret0:
	if (!bRet)
		Release();

	return bRet;
}

BOOL CXmlDocument::SelectSingleNode(LPCTSTR pNodeName, CXmlNode& objXmlNode)
{
	IXMLDOMNode* pXMLNode;
	BSTR bstrNodeName;
	HRESULT hr;

	if (NULL == pNodeName || NULL == m_pXMLDoc)
		return FALSE;

	bstrNodeName = ::SysAllocString(pNodeName);

	pXMLNode = NULL;
	hr = m_pXMLDoc->selectSingleNode(bstrNodeName, &pXMLNode);
	::SysFreeString(bstrNodeName);
	if (hr == S_OK)
	{
		objXmlNode.Attach(pXMLNode);
		return TRUE;
	}
	else
	{
		objXmlNode.Attach(NULL);
		return FALSE;
	}
}

BOOL CXmlDocument::SelectNodes(LPCTSTR pNodeName, CXmlNodeList& objXmlNodeList)
{
	IXMLDOMNodeList* pXMLNodeList;
	BSTR bstrNodeName;
	HRESULT hr;

	if (!pNodeName || !m_pXMLDoc)
		return FALSE;

	bstrNodeName = ::SysAllocString(pNodeName);

	pXMLNodeList = NULL;
	hr = m_pXMLDoc->selectNodes(bstrNodeName, &pXMLNodeList);
	::SysFreeString(bstrNodeName);
	if (hr != S_OK)
	{
		objXmlNodeList.Attach(NULL);
		return FALSE;
	}
	else
	{
		objXmlNodeList.Attach(pXMLNodeList);
		return TRUE;
	}
}

void CXmlDocument::Release()
{
	if (m_pXMLDoc)
	{
		m_pXMLDoc->Release();
		m_pXMLDoc = NULL;
	}
}