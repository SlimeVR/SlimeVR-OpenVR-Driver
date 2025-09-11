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
    bool Push(const char* data, size_t size);

    /**
     * Pops data from the queue.
     * 
     * @param data A pointer to the location where the data should be stored.
     * @param size Number of bytes to pop.
     * @return True if the data was successfully popped, false if there is not enough data.
     */
    bool Pop(char* data, size_t size);

    /**
     * Copies data from the queue into the given data pointer, without removing it.
     * 
     * @param data A pointer to the location where the data should be copied to.
     * @param size Number of bytes to peek.
     * @return Number of bytes actually copied, 0 if there is not enough data.
     */
    size_t Peek(char* data, size_t size);

    /**
     * Skips n bytes in the queue.
     * 
     * @param n Number of bytes to skip.
     * @return True if n bytes were successfully skipped, false if n is greater than the number of bytes available in the queue.
     */
    bool Skip(size_t n);

    /**
     * Clears the queue.
     */
    void Clear();

    /**
     * Returns the number of bytes available in the queue.
     * 
     * @return Number of bytes available in the queue.
     */
    size_t BytesAvailable() const;

    /**
     * Returns the number of free bytes in the queue.
     * 
     * @return Number of free bytes in the queue.
     */
    size_t BytesFree() const;

private:
    const size_t size_;
    std::unique_ptr<char[]> buffer_ = nullptr;
    std::atomic<size_t> head_ = 0;
    std::atomic<size_t> tail_ = 0;
    std::atomic<size_t> count_ = 0;
};