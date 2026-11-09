# Cross-compile duckling for 64-bit Windows from Linux with MinGW-w64.
#   cmake --preset windows-mingw-release
# Uses the default (win32 thread model) compilers; GCC >= 13 supports
# std::thread/std::mutex natively there, so no libwinpthread is needed.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(DUCKLING_MINGW_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${DUCKLING_MINGW_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${DUCKLING_MINGW_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${DUCKLING_MINGW_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${DUCKLING_MINGW_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
