#ifndef TFV_LIGHT_CONTROLLER_HPP
#define TFV_LIGHT_CONTROLLER_HPP

namespace tfv
{
    class RoadNetwork;

    // Fixed-cycle timing ("the central brain's fixed weights").
    struct LightTiming
    {
        float greenSec{4.0f};
        float amberSec{1.0f};
        float allRedSec{0.5f};
    };

    /**
     * The single central traffic-light controller (Phase 4). One instance drives
     * EVERY signalized intersection in the network with a deterministic fixed cycle
     * + a safety governor (amber + all-red clearance before switching approach), so
     * no illegal phase flip can occur. Individual lights have no autonomy.
     *
     * Tick-based and RNG-free, so the statistical/same-build determinism holds.
     */
    class LightController
    {
      public:
        explicit LightController(LightTiming timing = {}) : m_t(timing) {}

        /** Reset all intersections to phase 0 / green / t=0. */
        void reset(RoadNetwork& net);

        /** Advance every intersection by one fixed tick of length dt. */
        void step(RoadNetwork& net, double dt);

        const LightTiming& timing() const { return m_t; }

      private:
        LightTiming m_t;
    };

} // namespace tfv

#endif // TFV_LIGHT_CONTROLLER_HPP
