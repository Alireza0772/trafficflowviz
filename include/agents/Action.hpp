#ifndef TFV_ACTION_HPP
#define TFV_ACTION_HPP

#include <cstdint>

namespace tfv
{
    /** Observable vehicle light bits (read by other vehicles in later phases). */
    namespace light
    {
        inline constexpr uint8_t BRAKE = 1 << 0;
        inline constexpr uint8_t LEFT = 1 << 1;
        inline constexpr uint8_t RIGHT = 1 << 2;
        inline constexpr uint8_t HAZARD = 1 << 3;
        inline constexpr uint8_t FLASH = 1 << 4;
    } // namespace light

    /**
     * A brain's decision for one vehicle on one decision tick (design §3.4 / §13.2).
     * It is a REQUEST: the integrator validates and clamps it against physics.
     * Phase 2 brains drive only `accel` (and optionally the BRAKE light bit);
     * laneChange/turn are reserved for Phase 3 and validated to 0.
     */
    struct Action
    {
        float accel{0.0f};      // target longitudinal acceleration, m/s^2
        int8_t laneChange{0};   // -1 left, 0 none, +1 right (Phase 3)
        int8_t turn{0};         // 0 straight, -1 left, +1 right, 2 U-turn (Phase 3)
        uint8_t lightCmd{0};    // bitfield of light:: flags
        float reserved0{0.0f};  // future use (e.g. variable target speed)
        int32_t reserved1{0};   // future use (e.g. horn)
    };

} // namespace tfv

#endif // TFV_ACTION_HPP
