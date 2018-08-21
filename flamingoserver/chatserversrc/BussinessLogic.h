/** 
 * 即时通讯的业务逻辑都统一放在这里，BusinessLogic.h
 * zhangyl 2018.05.16
 */
#ifndef __BUSSINESS_LOGIC_H__
#define __BUSSINESS_LOGIC_H__

#include <memory>
#include "../net/TcpConnection.h"

using namespace net;

class BussinessLogic final
{
private:
    BussinessLogic() = delete;
    ~BussinessLogic() = delete;

    BussinessLogic(const BussinessLogic& rhs) = delete;
    BussinessLogic& operator =(const BussinessLogic& rhs) = delete;

public:
    static void RegisterUser(const std::string& data, const std::shared_ptr<TcpConnection>& conn, bool keepalive, std::string& retData);

};



#endif //!__BUSSINESS_LOGIC_H__