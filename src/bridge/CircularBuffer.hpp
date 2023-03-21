/*
    SlimeVR Code is placed under the MIT license
    Copyright (c) 2022 Eiren Rain and SlimeVR Contributors

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/
#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <cstring>

class CircularBuffer {
public:
    CircularBuffer(size_t size) :
        size_(size),
        buffer_(std::make_unique<char[]>(size)),
        head_(0), tail_(0), count_(0)
        { }
    ~CircularBuffer() = default;

    bool push(const char* data, size_t size) {
        if (size > bytes_free()) return false;
        size_t size1 = std::min<size_t>(size, size_ - (head_ % size_));
        size_t size2 = size - size1;
        std::memcpy(buffer_.get() + (head_ % size_), data, size1);
        std::memcpy(buffer_.get(), data + size1, size2);
        head_ += size;
        count_ += size;
        return true;
    }

    bool pop(char* data, size_t size) {
        if (size > bytes_available()) return false;
        size_t size1 = std::min<size_t>(size, size_ - (tail_ % size_));
        size_t size2 = size - size1;
        std::memcpy(data, buffer_.get() + (tail_ % size_ ), size1);
        std::memcpy(data + size1, buffer_.get(), size2);
        tail_ += size;
        count_ -= size;
        return true;
    }

    size_t peek(char* data, size_t size) {
        size_t available = bytes_available();
        size_t size1 = std::min<size_t>(size, size_ - (tail_ % size_));
        size_t size2 = std::min<size_t>(size - size1, available - size1);
        std::memcpy(data, buffer_.get() + (tail_ % size_), size1);
        std::memcpy(data + size1, buffer_.get(), size2);
        return size1 + size2;
    }

    bool skip(size_t n) {
        if (n > bytes_available()) return false;
        tail_ += n;
        count_ -= n;
        return true;
    }

    void clear() {
        head_ = 0;
        tail_ = 0;
        count_ = 0;
    }

    size_t bytes_available() const {
        return count_;
    }

    size_t bytes_free() const {
        return size_ - bytes_available();
    }

private:
    const size_t size_;
    std::unique_ptr<char[]> buffer_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
    std::atomic<size_t> count_;
};