#include <cstdio>
#include <format>
#include <debugapi.h>
#include <winbase.h>
#include <winuser.h>
#include <stdexcept>

#define DBG_PRINT(fmt, ...)                                                                        \
    do                                                                                             \
    {                                                                                              \
        char _buf[256];                                                                            \
        std::snprintf(_buf, sizeof(_buf), fmt "\n", __VA_ARGS__);                                  \
        OutputDebugStringA(_buf);                                                                  \
    } while (0)

#define FSTR(fmt, ...) std::format(fmt, __VA_ARGS__)

extern "C" WINUSERAPI int WINAPI MessageBoxTimeoutW(IN HWND hWnd, IN PCWSTR lpText,
                                                    IN PCWSTR lpCaption, IN UINT uType,
                                                    IN WORD wLanguageId, IN DWORD dwMilliseconds);

#ifdef _DEBUG
#define HANDLE_LOGIC_ERROR(msg) throw std::logic_error(msg)
#else
#define HANDLE_LOGIC_ERROR(msg) DBG_PRINT("%s", msg)
#endif

#define THROW_IF_FALSE(cond)                                                                       \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
            throw std::runtime_error(#cond " failed");                                             \
    } while (0)
