#ifndef TFV_OBSERVATION_HPP
#define TFV_OBSERVATION_HPP

#include <array>

namespace tfv
{
    /**
     * Fixed-length, normalized observation vector handed to every brain.
     *
     * The length and channel layout are locked by the design doc (§3.4/§6.3) so a
     * rule brain, a neural net, and an ONNX/Python model are interchangeable and
     * trained models never need re-export when channels are added. Phase 2 fills
     * only the longitudinal subset (the indices in `obs_idx` below) and leaves the
     * remaining perception channels zero; Phase 5 populates the rest.
     *
     * Channels are normalized by documented fixed scales so a brain can reconstruct
     * absolute physical quantities (speed/gap in model units) deterministically.
     */
    inline constexpr int OBS_LEN = 24;
    using Observation = std::array<float, OBS_LEN>;

    // Documented normalization scales (model units; pixels stand in for meters in
    // Phase 2 — see the units caveat in docs/agent-simulation-design.md).
    inline constexpr float OBS_SPEED_SCALE = 40.0f; // speed normalizer (m/s)
    inline constexpr float OBS_RANGE_SCALE = 60.0f; // distance normalizer (m, = front range)
    inline constexpr float OBS_ACCEL_SCALE = 10.0f; // acceleration normalizer (m/s^2)

    namespace obs_idx
    {
        // Self (idx 0-3)
        inline constexpr int SelfSpeed = 0;     // speed / OBS_SPEED_SCALE
        inline constexpr int SelfAccel = 1;     // last accel / OBS_ACCEL_SCALE
        inline constexpr int PositionAlong = 3; // position along segment, 0..1
        // Segment context (idx 4-7)
        inline constexpr int SpeedLimit = 4; // segment speed limit / OBS_SPEED_SCALE
        inline constexpr int Congestion = 5; // segment congestion, 0..1
        // Front sector (idx 11-13 of the 4-sector block 11..22)
        inline constexpr int FrontGap = 11;       // bumper gap (m) / OBS_RANGE_SCALE, clamped to 1
        inline constexpr int FrontRelSpeed = 12;  // (v_self - v_leader) / OBS_SPEED_SCALE
        inline constexpr int FrontHasLeader = 13; // 1.0 if a same-segment leader exists, else 0
    }                                             // namespace obs_idx

} // namespace tfv

#endif // TFV_OBSERVATION_HPP
