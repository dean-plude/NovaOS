// NovaOS: what Visual Studio gives WinUI 2's sources that clang in C++20
// does not: the coroutine types under std::experimental, where the
// project's /await code names them (C++20 moved them to std).
#pragma once
#ifdef __cplusplus
#include <coroutine>
namespace std::experimental {
using std::coroutine_handle;
using std::coroutine_traits;
using std::suspend_always;
using std::suspend_never;
}
#endif
