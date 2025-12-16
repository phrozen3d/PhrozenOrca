#ifndef slic3r_PhrozenMachineDatas_hpp_
#define slic3r_PhrozenMachineDatas_hpp_

#include <atomic>
#include <vector>
#include <shared_mutex>
#include <type_traits>
#include <utility>   // for std::swap, std::move

namespace Slic3r {

#pragma region DoubleBuffer
template <typename T>
class DoubleBuffer {
    // check it can do copy & move assignable (for efficacy)
    static_assert(std::is_copy_assignable_v<T> || std::is_move_assignable_v<T>,
                  "T must be copy-assignable or move-assignable.");

private:

    T  bufferA{};            
    T  bufferB{};            
    T* readBuffer{ &bufferA };   
    T* writeBuffer{ &bufferB };  

    // support multi read, and write make more limit
    mutable std::shared_mutex m_;

public:
    DoubleBuffer() = default;

    explicit DoubleBuffer(const T& init) {
        bufferA = init;
        bufferB = init;
    }

    // copy & write data to buffer
    void write(const T& src) {
        std::unique_lock lk(m_);         // Exclusive locks : prevent multiple writes and indicator reversals.
        *writeBuffer = src;              
    }

    // Move the data directly to the buffer
    void write(T&& src) {
        std::unique_lock lk(m_);
        *writeBuffer = std::move(src);   
    }

    // read data from readBuffer 
    //Sending back a "copy" is secure and does not require holding a lock for an extended period of time.
    T read() const {
        std::shared_lock lk(m_);         // Shared lock: Allows multiple reads
        return *readBuffer;              // Copy it, release the lock, and then use it.
    }

    // Swap readBuffer and writeBuffer (flip)
    void flip() noexcept {
        std::unique_lock lk(m_);
        std::swap(readBuffer, writeBuffer);
        // After the exchange, the new reader will see the material that has just been written.
        // Existing readers using read() (posting back a copy) will not be affected.
    }

    // Non-blocking version: Attempts to write/read; returns false if the lock cannot be acquired.
    bool try_write(const T& src) {
        std::unique_lock lk(m_, std::try_to_lock);
        if (!lk.owns_lock()) return false;
        *writeBuffer = src;
        return true;
    }

    bool try_write(T&& src) {
        std::unique_lock lk(m_, std::try_to_lock);
        if (!lk.owns_lock()) return false;
        *writeBuffer = std::move(src);
        return true;
    }

    bool try_read(T& out) const {
        std::shared_lock lk(m_, std::try_to_lock);
        if (!lk.owns_lock()) return false;
        out = *readBuffer;
        return true;
    }

    bool try_flip() noexcept {
        std::unique_lock lk(m_, std::try_to_lock);
        if (!lk.owns_lock()) return false;
        std::swap(readBuffer, writeBuffer);
        return true;
    }
};
#pragma endregion
} // namespace Slic3r

#endif //  slic3r_PhrozenMachineDatas_hpp_
