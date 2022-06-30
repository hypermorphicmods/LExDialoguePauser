#pragma once

#if defined GAMELE1
#include "LEASIMods/LE1-ASI-Plugins/LE1-SDK/Interface.h"
#elif defined GAMELE2
#include "LEASIMods/LE2-ASI-Plugins/LE2-SDK/Interface.h"
#elif defined GAMELE3
#include "LEASIMods/LE3-ASI-Plugins/LE3-SDK/Interface.h"
#else
#error Select a game configuration
#endif

#include "LEASIMods/Shared-ASI/Common.h"
#include "LEASIMods/Shared-ASI/ME3Tweaks/ME3TweaksHeader.h"

#ifdef ASI_DEBUG
FILE* logs;
DWORD64 starttime = GetTickCount64();
#define LOGFORMAT(...) \
{ \
    fprintf(logs, "[%f] %s\n", (GetTickCount64() - starttime)/1000.0f, string_format(__VA_ARGS__).c_str()) ; \
    fflush(logs); \
    printf("[%f] %s\n", (GetTickCount64() - starttime)/1000.0f, string_format(__VA_ARGS__).c_str()) ; \
}
#define LOGFORMATW(...) \
{ \
    fwprintf(logs, L"[%f] %s\n", (GetTickCount64() - starttime)/1000.0f, wstring_format(__VA_ARGS__).c_str()) ; \
    fflush(logs); \
}
#else
#define LOGFORMAT(...) ;
#endif

// fast comparison for events
template <class T>
class FunctionName : public FName
{
public:
    UClass* m_oClass = nullptr;
    char *m_szFunc = nullptr;
    char* m_szName = nullptr;

    FunctionName(char *funcName);
    T* Matches(UObject* context, UFunction* func);
};

template <class T>
FunctionName<T>::FunctionName(char *funcName)
{
    m_szFunc = funcName;
}

template <class T>
T* FunctionName<T>::Matches(UObject* context, UFunction* func)
{
    // these take time to initialize
    if (!m_oClass)
    {
        if (T::StaticClass())
            m_oClass = T::StaticClass();
        else
            return nullptr;
    }

    if (m_oClass == context->Class)
    {
        if (!m_szName)
        {
            if (m_szFunc[0] == 0)
                // match any, though not faster than a class compare
                return reinterpret_cast<T*>(context);

            if (auto thisname = func->Name.GetName(); thisname && !strcmp(m_szFunc, thisname))
                m_szName = thisname;
            else return nullptr;
        }

        return m_szName == func->Name.GetName() ? reinterpret_cast<T*>(context) : nullptr;
    }
    return nullptr;
}
