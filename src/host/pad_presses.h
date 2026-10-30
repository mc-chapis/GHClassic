#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace gh2::input
{
    using ButtonPresses = std::array<uint32_t, 16>;

    // The host can poll several times between libpad reads. Keep presses
    // until the game reads them, inserting a release before another press
    // of a button it still holds. Other buttons and analog values stay live.
    class PadPresses
    {
    public:
        void publish(uint16_t held, const ButtonPresses &down)
        {
            for (int b = 0; b < 16; ++b)
            {
                const uint16_t bit = static_cast<uint16_t>(1u << b);
                m_pending[b] += std::max(down[b], (held & ~m_held & bit) ? 1u : 0u);
            }
            m_held = held;
        }

        uint16_t read()
        {
            uint16_t held = m_held;
            for (int b = 0; b < 16; ++b)
            {
                const uint16_t bit = static_cast<uint16_t>(1u << b);
                if (m_pending[b] == 0)
                    continue;
                if (m_read & bit)
                    held &= static_cast<uint16_t>(~bit);
                else
                {
                    held |= bit;
                    --m_pending[b];
                }
            }
            m_read = held;
            return held;
        }

    private:
        uint16_t m_held = 0, m_read = 0;
        std::array<uint64_t, 16> m_pending{};
    };
}
