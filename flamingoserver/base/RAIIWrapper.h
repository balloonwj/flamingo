#ifndef __RAII_WRAPPER_H__
#define __RAII_WRAPPER_H__

template<class T>
class RAIIWrapper
{
public:
    RAIIWrapper(T* p) :ptr(p) {}

    virtual ~RAIIWrapper()
    {
        if (ptr != NULL)
        {
            delete ptr;
            ptr = NULL;
        }
    }
private:
    T* ptr;
};

#endif //!__RAII_WRAPPER_H__
