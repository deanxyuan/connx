/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_INCLUDE_EXPORT_H_
#define CONNX_INCLUDE_EXPORT_H_

#if !defined(CONNX_API)

#    if defined(CONNX_SHARED_LIBRARY)
#        if defined(_WIN32)

#            if defined(CONNX_COMPILE_LIBRARY)
#                define CONNX_API __declspec(dllexport)
#            else
#                define CONNX_API __declspec(dllimport)
#            endif // defined(CONNX_COMPILE_LIBRARY)

#        else // defined(_WIN32)
#            if defined(CONNX_COMPILE_LIBRARY)
#                define CONNX_API __attribute__((visibility("default")))
#            else
#                define CONNX_API
#            endif
#        endif // defined(_WIN32)

#    else // defined(CONNX_SHARED_LIBRARY)
#        define CONNX_API
#    endif

#endif // !defined(CONNX_API)

#endif // CONNX_INCLUDE_EXPORT_H_
