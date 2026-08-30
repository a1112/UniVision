#pragma once

#if defined(_WIN32) && defined(UNIVISION_SHARED)
#  if defined(UNIVISION_BUILDING_LIBRARY)
#    define UNIVISION_API __declspec(dllexport)
#  else
#    define UNIVISION_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && defined(UNIVISION_SHARED)
#  define UNIVISION_API __attribute__((visibility("default")))
#else
#  define UNIVISION_API
#endif
