
// Copyright (c) 2020-2023, ARM Limited.
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//         http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.

#ifndef _TOSA_NUMPY_UTILS_H
#define _TOSA_NUMPY_UTILS_H

#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "half.hpp"

class NumpyUtilities
{
public:
    enum NPError
    {
        NO_ERROR = 0,
        FILE_NOT_FOUND,
        FILE_IO_ERROR,
        FILE_TYPE_MISMATCH,
        HEADER_PARSE_ERROR,
        BUFFER_SIZE_MISMATCH,
        DATA_TYPE_NOT_SUPPORTED,
    };

    template <typename T>
    static const char* getDTypeString(bool& is_bool)
    {
        is_bool = false;
        if (std::is_same<T, bool>::value)
        {
            is_bool = true;
            return "'|b1'";
        }
        if (std::is_same<T, uint8_t>::value)
        {
            return "'|u1'";
        }
        if (std::is_same<T, int8_t>::value)
        {
            return "'|i1'";
        }
        if (std::is_same<T, uint16_t>::value)
        {
            return "'<u2'";
        }
        if (std::is_same<T, int16_t>::value)
        {
            return "'<i2'";
        }
        if (std::is_same<T, int32_t>::value)
        {
            return "'<i4'";
        }
        if (std::is_same<T, int64_t>::value)
        {
            return "'<i8'";
        }
        if (std::is_same<T, float>::value)
        {
            return "'<f4'";
        }
        if (std::is_same<T, double>::value)
        {
            return "'<f8'";
        }
        if (std::is_same<T, half_float::half>::value)
        {
            return "'<f2'";
        }
        assert(false && "unsupported Dtype");
    };

    template <typename T>
    static NPError writeToNpyFile(const char* filename, const uint32_t elems, const T* databuf)
    {
        std::vector<int32_t> shape = { static_cast<int32_t>(elems) };
        return writeToNpyFile(filename, shape, databuf);
    }

    template <typename T>
    static NPError writeToNpyFile(const char* filename, const std::vector<int32_t>& shape, const T* databuf)
    {
        bool is_bool;
        const char* dtype_str = getDTypeString<T>(is_bool);
        return writeToNpyFileCommon(filename, dtype_str, sizeof(T), shape, databuf, is_bool);
    }

    template <typename T>
    static NPError readFromNpyFile(const char* filename, const uint32_t elems, T* databuf)
    {
        bool is_bool;
        const char* dtype_str = getDTypeString<T>(is_bool);
        return readFromNpyFileCommon(filename, dtype_str, sizeof(T), elems, databuf, is_bool);
    }

    template <typename D, typename S>
    static void copyBufferByElement(D* dest_buf, S* src_buf, int num)
    {
        static_assert(sizeof(D) >= sizeof(S), "The size of dest_buf must be equal to or larger than that of src_buf");
        for (int i = 0; i < num; ++i)
        {
            dest_buf[i] = src_buf[i];
        }
    }

private:
    static NPError writeToNpyFileCommon(const char* filename,
                                        const char* dtype_str,
                                        const size_t elementsize,
                                        const std::vector<int32_t>& shape,
                                        const void* databuf,
                                        bool bool_translate);
    static NPError readFromNpyFileCommon(const char* filename,
                                         const char* dtype_str,
                                         const size_t elementsize,
                                         const uint32_t elems,
                                         void* databuf,
                                         bool bool_translate);
    static NPError checkNpyHeader(FILE* infile, const uint32_t elems, const char* dtype_str);
    static NPError getHeader(FILE* infile, bool& is_signed, int& bit_length, char& byte_order);
    static NPError writeNpyHeader(FILE* outfile, const std::vector<int32_t>& shape, const char* dtype_str);
};

template <>
NumpyUtilities::NPError NumpyUtilities::readFromNpyFile(const char* filename, const uint32_t elems, int32_t* databuf);

#endif    // _TOSA_NUMPY_UTILS_H
