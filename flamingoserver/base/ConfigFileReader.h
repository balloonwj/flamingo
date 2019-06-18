/** 
 *  简单的配置文件读取类，ConfigFileReader.h
 *  zhangyl 2017.05.27
 */
#ifndef __CONFIG_FILE_READER_H__
#define __CONFIG_FILE_READER_H__

#include <map>
#include <string>

class CConfigFileReader
{
public:
	CConfigFileReader(const char* filename);
	~CConfigFileReader();

    char* GetConfigName(const char* name);
    int SetConfigValue(const char* name, const char*  value);

private:
    void  _LoadFile(const char* filename);
    int   _WriteFile(const char* filename = NULL);
    void  _ParseLine(char* line);
    char* _TrimSpace(char* name);

    bool                                m_load_ok;
    std::map<std::string, std::string>  m_config_map;
    std::string                         m_config_file;
};


#endif //!__CONFIG_FILE_READER_H__
