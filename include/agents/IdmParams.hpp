#ifndef TFV_IDM_PARAMS_HPP
#define TFV_IDM_PARAMS_HPP

namespace tfv
{
    /**
     * Parameters of the Intelligent Driver Model (Treiber et al.). Sourced from
     * Configuration (sim.idm.*) at Simulation::initialize and shared with the
     * RuleBasedBrain and the integrator's accel clamp.
     */
    struct IdmParams
    {
        float v0_cap{13.9f};   // desired-speed cap (m/s), ~50 km/h
        float a_max{1.5f};     // maximum acceleration (m/s^2)
        float b_comfort{2.0f}; // comfortable deceleration (m/s^2)
        float b_max{6.0f};     // hard-brake clamp (m/s^2)
        float s0{2.0f};        // minimum jam gap (m)
        float T{1.5f};         // safe time headway (s)
        float delta{4.0f};     // free-acceleration exponent (dimensionless)
    };

} // namespace tfv

#endif // TFV_IDM_PARAMS_HPP
