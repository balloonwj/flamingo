/**
 *  文件读写类, File.h
 *  zhangyl 2017.07.26 
 */
#ifndef __FILE_H__
#define __FILE_H__

//#ifdef FILE_EXPORTS
//#define FILE_API __declspec(dllexport)
//#else
//#define FILE_API __declspec(dllimport)
//#endif

#define FILE_API

//不支持大文件（大于2G）
//TODO: 标识位需要只打开，只创建，后期加上
class FILE_API CFileX
{
public:
    CFileX();
    CFileX(PCTSTR pszFileName);
    ~CFileX();

    CFileX(const CFileX& rhs) = delete;
    CFileX& operator=(const CFileX& rhs) = delete;
    
    bool Open(PCTSTR pszFileName, bool bCreateIfNotExsit = true);
    bool Read(char* pszBuffer, int nBufferLength, int* nReadLength = NULL);
    bool Write(const char* pszBuffer, int nBufferLength);

    bool IsValid();
    int GetFileSize();

    void Close();

    /** 通过扩展名删除一类文件
     *@param pszDir所在文件夹 示例E:\\doc\\
     *@param pszExtension 扩展名 示例*.doc
     */
    static bool DeleteAllFileByExtension(PCTSTR pszDir, PCTSTR pszExtension);

    static bool IsDirectoryExist(LPCTSTR lpszPath);

private:
    HANDLE      m_hFile;
};

#endif //!__FILE_H__