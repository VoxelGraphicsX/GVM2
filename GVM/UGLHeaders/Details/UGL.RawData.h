#pragma once

namespace UGL
{

    template <typename T>
    const T *RawData(const T &data)
    {
        return &data;
    }

    // 右值版本 = delete
    template <class T>
    const T *RawData(T &&) = delete;
}