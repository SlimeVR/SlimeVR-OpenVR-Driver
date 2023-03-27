/*
    SlimeVR Code is placed under the MIT license
    Copyright (c) 2022 SlimeVR Contributors

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
#include <mutex>
#include <cstring>

/**
 * A fixed-size queue using contiguous memory ONLY for a single producer and a single consumer (SPSC).
 * 
 * @param size Size of the queue in bytes.
 */
class CircularBuffer {
public:
    /**
     * Constructs a fixed-size queue using contiguous memory.
     * 
     * @param size Size of the queue in bytes.
     */
    CircularBuffer(size_t size) :
        size_(size),
        buffer_(std::make_unique<char[]>(size))
        { }
    ~CircularBuffer() = default;

    /**
     * Pushes data into the queue.
     * 
     * @param data A pointer to the data to be pushed.
     * @param size Number of bytes to push.
     * @return True if the data was successfully pushed, false if the queue is full.
     */
    bool Push(const char* data, size_t size) {
        if (size > BytesFree()) return false;
        size_t size1 = std::min<size_t>(size, size_ - (head_ % size_));
        size_t size2 = size - size1;
        std::memcpy(buffer_.get() + (head_ % size_), data, size1);
        std::memcpy(buffer_.get(), data + size1, size2);
        head_ += size;
        count_ += size;
        return true;
    }

    /**
     * Pops data from the queue.
     * 
     * @param data A pointer to the location where the data should be stored.
     * @param size Number of bytes to pop.
     * @return True if the data was successfully popped, false if there is not enough data.
     */
    bool Pop(char* data, size_t size) {
        if (size > BytesAvailable()) return false;
        size_t size1 = std::min<size_t>(size, size_ - (tail_ % size_));
        size_t size2 = size - size1;
        std::memcpy(data, buffer_.get() + (tail_ % size_ ), size1);
        std::memcpy(data + size1, buffer_.get(), size2);
        tail_ += size;
        count_ -= size;
        return true;
    }

    /**
     * Copies data from the queue into the given data pointer, without removing it.
     * 
     * @param data A pointer to the location where the data should be copied to.
     * @param size Number of bytes to peek.
     * @return Number of bytes actually copied, 0 if there is not enough data.
     */
    size_t Peek(char* data, size_t size) {
        size_t available = BytesAvailable();
        if (size > available) return 0;
        size_t size1 = std::min<size_t>(size, size_ - (tail_ % size_));
        size_t size2 = std::min<size_t>(size - size1, available - size1);
        std::memcpy(data, buffer_.get() + (tail_ % size_), size1);
        std::memcpy(data + size1, buffer_.get(), size2);
        return size1 + size2;
    }

    /**
     * Skips n bytes in the queue.
     * 
     * @param n Number of bytes to skip.
     * @return True if n bytes were successfully skipped, false if n is greater than the number of bytes available in the queue.
     */
    bool Skip(size_t n) {
        if (n > BytesAvailable()) return false;
        tail_ += n;
        count_ -= n;
        return true;
    }

    /**
     * Clears the queue.
     */
    void Clear() {
        head_ = 0;
        tail_ = 0;
        count_ = 0;
    }

    /**
     * Returns the number of bytes available in the queue.
     * 
     * @return Number of bytes available in the queue.
     */
    size_t BytesAvailable() const {
        return count_;
    }

    /**
     * Returns the number of free bytes in the queue.
     * 
     * @return Number of free bytes in the queue.
     */
    size_t BytesFree() const {
        return size_ - BytesAvailable();
    }

private:
    const size_t size_;
    std::unique_ptr<char[]> buffer_ = nullptr;
    std::atomic<size_t> head_ = 0;
    std::atomic<size_t> tail_ = 0;
    std::atomic<size_t> count_ = 0;
};