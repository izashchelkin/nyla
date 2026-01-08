#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "nyla/commons/assert.h"

namespace nyla
{

class Spirview
{
    static inline const unsigned int kMagicNumber = 0x07230203;
    static inline const unsigned int kOpCodeMask = 0xffff;
    static inline const unsigned int kWordCountShift = 16;

  public:
    static auto ParseStringLiteral(auto& it, auto& out)
    {
        for (;;)
        {
            uint32_t word = *it++;
            for (uint32_t i = 0; i < 32; i += 8)
            {
                uint32_t ch = (word >> i) & 0xFF;
                if (ch)
                    out.AppendChar(ch);
                else
                    return;
            }
        }
    }

    explicit Spirview(std::span<uint32_t> words) : m_Words(words)
    {
        NYLA_ASSERT(m_Words.size() >= 5);
        NYLA_ASSERT(m_Words[0] == kMagicNumber);

        const uint32_t version = m_Words[1];
        const uint32_t major = (version >> 16) & 0xFF;
        const uint32_t minor = (version >> 8) & 0xFF;
        const uint32_t generator = m_Words[2];
        const uint32_t bound = m_Words[3];
        const uint32_t reserved = m_Words[4];
        NYLA_ASSERT(reserved == 0);

        (void)major;
        (void)minor;
        (void)generator;
        (void)bound;
    }

    template <class WordT> struct BasicIterator
    {
        using pointer = WordT *;
        using span_type = std::span<WordT>;

        BasicIterator(pointer at, pointer end) : m_At(at), m_End(end)
        {
            Update();
        }

        [[nodiscard]]
        auto Op() const -> uint16_t
        {
            return m_Op;
        }

        [[nodiscard]]
        auto WordCount() const -> uint16_t
        {
            return m_WordCount;
        }

        [[nodiscard]]
        auto Words() const -> span_type
        {
            NYLA_ASSERT(m_At + m_WordCount <= m_End);
            return {m_At, m_WordCount};
        }

        [[nodiscard]]
        auto Operands() const -> span_type
        {
            auto w = Words();
            NYLA_ASSERT(w.size() >= 1);
            return w.subspan(1);
        }

        auto operator++() -> BasicIterator &
        {
            NYLA_ASSERT(m_At + m_WordCount <= m_End);
            m_At += m_WordCount;

            Update();
            return *this;
        }

        [[nodiscard]]
        auto operator==(const BasicIterator &rhs) const -> bool
        {
            return m_At == rhs.m_At;
        }

        [[nodiscard]]
        auto operator!=(const BasicIterator &rhs) const -> bool
        {
            return !(*this == rhs);
        }

        //

        void MakeNop() const
        {
            NYLA_DEBUGBREAK();
            memset(m_At, 1 << 16, m_WordCount);
        }

      private:
        void Update()
        {
            NYLA_ASSERT(m_At <= m_End);
            m_Op = (*m_At) & kOpCodeMask;
            m_WordCount = static_cast<uint16_t>((*m_At) >> kWordCountShift);
        }

        uint16_t m_Op{};
        uint16_t m_WordCount{};

        pointer m_At{};
        pointer m_End{};
    };

    using iterator = BasicIterator<uint32_t>;
    using const_iterator = BasicIterator<const uint32_t>;

    [[nodiscard]]
    auto begin() -> iterator
    {
        return {m_Words.data() + 5, m_Words.data() + m_Words.size()};
    }

    [[nodiscard]]
    auto end() -> iterator
    {
        return {m_Words.data() + m_Words.size(), m_Words.data() + m_Words.size()};
    }

    [[nodiscard]]
    auto begin() const -> const_iterator
    {
        return {m_Words.data() + 5, m_Words.data() + m_Words.size()};
    }

    [[nodiscard]]
    auto end() const -> const_iterator
    {
        return {m_Words.data() + m_Words.size(), m_Words.data() + m_Words.size()};
    }

    [[nodiscard]]
    auto cbegin() const -> const_iterator
    {
        return begin();
    }

    [[nodiscard]]
    auto cend() const -> const_iterator
    {
        return end();
    }

  private:
    std::span<uint32_t> m_Words;
};

} // namespace nyla