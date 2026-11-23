#pragma once

#if defined(OMEGACONF_FORCE_CPP17)
    #define OMEGACONF_CPP20 0
#elif (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L) || (__cplusplus >= 202002L)
    #define OMEGACONF_CPP20 1
#else
    #define OMEGACONF_CPP20 0
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(OMEGACONF_BUILD_DLL)
        #define OMEGACONF_API __declspec(dllexport)
    #elif defined(OMEGACONF_USE_DLL)
        #define OMEGACONF_API __declspec(dllimport)
    #else
        #define OMEGACONF_API
    #endif
#else
    #define OMEGACONF_API __attribute__((visibility("default")))
#endif
