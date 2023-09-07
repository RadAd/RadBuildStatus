#pragma once
#include <memory>

#ifdef UNICODE
#define tstring wstring
inline std::wstring convert(const std::string& m)
{
    wchar_t msg[1024];
    const int sz = MultiByteToWideChar(CP_UTF8, 0, m.c_str(), int(m.length()), msg, ARRAYSIZE(msg));
    msg[sz] = L'\0';
    return msg;
}
#else
#define tstring string
inline const std::string convert(const std::string& m)
{
    return m;
}
#endif

inline DWORD RegGetDWORD(HKEY hKey, LPCTSTR lpValue, DWORD Value)
{
    DWORD DataSize = sizeof(DWORD);
    RegGetValue(hKey, nullptr, lpValue, REG_DWORD, nullptr, &Value, &DataSize);
    return Value;
}

inline void RegSetDWORD(HKEY hKey, LPCTSTR lpValue, DWORD Value)
{
    const DWORD DataSize = sizeof(DWORD);
    RegSetValueEx(hKey, lpValue, 0, REG_DWORD, (BYTE*) &Value, DataSize);
}

inline std::tstring RegGetString(HKEY hKey, LPCTSTR sValue, const std::tstring& strDef)
{
    TCHAR buf[1024];
    DWORD len = (ARRAYSIZE(buf) - 1) * sizeof(CHAR);
    if (RegGetValue(hKey, nullptr, sValue, RRF_RT_REG_SZ, nullptr, buf, &len) == ERROR_SUCCESS)
    {
        buf[len / sizeof(CHAR)] = TEXT('\0');
        return buf;
    }
    else
        return strDef;
}

template <class F, class A>
struct S1
{
    F f;
    A a;

    ~S1()
    {
        f(a);
    }
};

template <class F, class A, class B>
struct S2
{
    F f;
    A a;
    B b;

    ~S2()
    {
        f(a, b);
    }
};

template <class F, class A>
S1<F, A> Auto(F f, A a)
{
    return { f, f(a) };
}

template <class F, class A, class B>
S2<F, A, B> Auto(F f, A a, B b)
{
    return { f, a, f(a, b) };
}

template <class T>
class OutPtr
{
public:
    typedef typename T::pointer pointer;

    OutPtr(T& t)
        : tt(t)
    {}

    operator pointer* ()
    {
        return &pp;
    }

    pointer* get()
    {
        return &pp;
    }

    ~OutPtr()
    {
        tt.reset(pp);
    }

private:
    T& tt;
    pointer pp = {};
};

template <class P, class F>
struct HandleDeleter
{
    typedef P pointer;
    HandleDeleter(F f)
        : f(f)
    {}

    void operator()(P p) const
    {
        f(p);
    }

    F f;
};

template <class P, class F>
std::unique_ptr<P, HandleDeleter<P, F>> MakeHandleDeleter(P p, F f)
{
    return std::unique_ptr<P, HandleDeleter<P, F>>(p, HandleDeleter<P, F>(f));
}

template <class T>
auto out_ptr(T& t)
{
    return OutPtr<T>(t);
}

#if 0
#include <memory>

struct free_delete {
    constexpr free_delete() noexcept = default;

    free_delete(const free_delete&) noexcept {}

    void operator()(void* _Ptr) const noexcept /* strengthened */ {
        free(_Ptr);
    }
};

template <class _Ty>
auto unique_malloc(const size_t count = 1, const size_t extra = 0)
{
    const size_t sz = count * sizeof(_Ty) + extra;
    auto p = std::unique_ptr<_Ty, free_delete>((_Ty*) malloc(sz));
    memset(p.get(), 0, sz);
    return p;
}
#endif

