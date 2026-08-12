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
    /**
     *  构造一个尚未加载任何文件的读取器，之后须调用 load() 指定配置文件。
     *  供「按多个候选路径依次尝试」的场景使用，见 utils/ServerConfigLoader.h。
     */
    CConfigFileReader();

    /**
     *  构造并立即加载配置文件。
     *  @param filename 配置文件路径，相对路径按进程的工作目录解析，不得为 NULL。
     *                  文件打不开时构造照常完成，此时 isLoadOk() 返回 false、
     *                  getConfigName() 一律返回 NULL —— 调用方必须检查 isLoadOk()，
     *                  否则会把 NULL 当成有效配置值使用。
     */
	CConfigFileReader(const char* filename);
	~CConfigFileReader();

    /**
     *  加载（或重新加载）配置文件，加载前会清空上一次的内容。
     *  @param filename 配置文件路径，不得为 NULL。
     *  @return 文件成功打开并解析完毕返回 true，打不开返回 false。
     */
    bool  load(const char* filename);

    /**
     *  上一次加载是否成功。
     *  @return true 表示配置文件已成功打开；从未加载过、或文件打不开均返回 false。
     */
    bool  isLoadOk() const;

    /**
     *  取上一次加载时使用的文件路径，用于在报错信息里回显到底找的是哪个文件。
     *  @return 文件路径；从未加载过时为空串。
     */
    const std::string& getFilePath() const;

    char* getConfigName(const char* name);
    int setConfigValue(const char* name, const char*  value);

private:
    void  loadFile(const char* filename);
    int   writeFile(const char* filename = NULL);
    void  parseLine(char* line);
    char* trimSpace(char* name);

    //配置文件是否已成功加载。必须在构造函数里显式置 false ——
    //以前没有初始化，文件打不开时它保留栈上的残留值，取值全看运气
    bool                                m_load_ok;
    std::map<std::string, std::string>  m_config_map;
    std::string                         m_config_file;
};


#endif //!__CONFIG_FILE_READER_H__
