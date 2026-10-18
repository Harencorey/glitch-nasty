#pragma once
//==============================================================================
// Glitch Nasty - TripleBuffer
//
// Hands a whole struct (the pattern bank) from the UI to the audio thread
// without locks. Three copies: one the UI writes, one the audio thread reads,
// one in the middle. Publishing swaps the UI's copy into the middle; the
// audio thread swaps the middle into its slot at the start of a block if it's
// newer. Neither side ever waits for the other, and nothing allocates.
//
// One writer only (the plug-in serialises its writers with a lock that the
// audio thread never touches).
//==============================================================================

#include <array>
#include <atomic>

namespace gn
{

template <typename T>
class TripleBuffer
{
public:
    // Fill every slot. Only call while no reader is running (constructor/prepare).
    void reset (const T& value)
    {
        for (auto& s : slots)
            s = value;
        middle.store (1);
        writeIdx = 2;
        readIdx = 0;
    }

    // Writer: copy the value in and make it visible to the reader.
    void publish (const T& value) noexcept
    {
        slots[(size_t) writeIdx] = value;
        const int old = middle.exchange (writeIdx | kDirty, std::memory_order_acq_rel);
        writeIdx = old & kIndexMask;
    }

    // Reader (audio thread): newest published value.
    const T& read() noexcept
    {
        if ((middle.load (std::memory_order_acquire) & kDirty) != 0)
        {
            const int old = middle.exchange (readIdx, std::memory_order_acq_rel);
            readIdx = old & kIndexMask;
        }
        return slots[(size_t) readIdx];
    }

private:
    static constexpr int kDirty = 4, kIndexMask = 3;
    std::array<T, 3> slots {};
    std::atomic<int> middle { 1 };
    int writeIdx = 2, readIdx = 0;
};

} // namespace gn
