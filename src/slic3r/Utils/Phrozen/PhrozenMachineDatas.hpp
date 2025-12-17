#ifndef slic3r_PhrozenMachineDatas_hpp_
#define slic3r_PhrozenMachineDatas_hpp_

#include <memory>
#include <atomic>
#include <mutex>
#include <type_traits>

namespace Slic3r {

#pragma region DoubleBuffer
template <typename T>
class DoubleBufferSP {
    static_assert(std::is_copy_assignable_v<T> || std::is_move_assignable_v<T>,
                  "T must be copy-assignable or move-assignable.");

private:
    std::shared_ptr<T> bufferA = std::make_shared<T>();
    std::shared_ptr<T> bufferB = std::make_shared<T>();

    std::shared_ptr<T> writeBuffer = bufferB; // initialize: A for reader, B for writer

    //An immutable snapshot made public to readers.
    std::shared_ptr<const T> readSnapshot;

    // Write-side mutual exclusion (protects writeBuffer content updates and flip switching)
    mutable std::mutex m_write;

public:
    DoubleBufferSP() {
        // Initialization: Showing the reader the content of A
        std::shared_ptr<const T> initSnap(bufferA, bufferA.get()); // aliasing: const view
        std::atomic_store(&readSnapshot, std::move(initSnap));
    }

    explicit DoubleBufferSP(const T& init) : DoubleBufferSP() {
        *bufferA = init;
        *bufferB = init;
    }

    // ======== Read (lock-free, atomic snapshot loading) ========

    std::shared_ptr<const T> read() const {
        return std::atomic_load(&readSnapshot);
    }

    bool try_read(std::shared_ptr<const T>& out) const {
        out = std::atomic_load(&readSnapshot);
        return (out != nullptr);
    }

    bool try_read(T& out) const {
        auto p = std::atomic_load(&readSnapshot);
        if (!p) return false;                    
        out = *p;                               
        return true;
    }


    // ======== Write (lock only the write end) ========

    void write(const T& src) {
        std::lock_guard<std::mutex> guard(m_write);
        *writeBuffer = src;
    }

    void write(T&& src) {
        std::lock_guard<std::mutex> guard(m_write);
        *writeBuffer = std::move(src);
    }

    bool move_and_write(T& src) {
        return try_write( std::move( src ) );
    }

    bool try_write(const T& src) {
        if (m_write.try_lock()) {
            *writeBuffer = src;
            m_write.unlock();
            return true;
        }
        return false;
    }

    bool try_write(T&& src) {
        if (m_write.try_lock()) {
            *writeBuffer = std::move(src);
            m_write.unlock();
            return true;
        }
        return false;
    }

    // ======== flip (Atomic release of new snapshot + switching to the next write buffer) ========

    void flip() {
        std::lock_guard<std::mutex> guard(m_write);

        // Publish a new snapshot (const view)
        std::shared_ptr<const T> newSnap(writeBuffer, writeBuffer.get());
        std::atomic_store(&readSnapshot, std::move(newSnap));

        // Switch write target
        writeBuffer = (writeBuffer == bufferA) ? bufferB : bufferA;
    }

    bool try_flip() {
        if (!m_write.try_lock()) return false;

        std::shared_ptr<const T> newSnap(writeBuffer, writeBuffer.get());
        std::atomic_store(&readSnapshot, std::move(newSnap));

        writeBuffer = (writeBuffer == bufferA) ? bufferB : bufferA;

        m_write.unlock();
        return true;
    }
};






#pragma endregion
} // namespace Slic3r

#endif //  slic3r_PhrozenMachineDatas_hpp_
