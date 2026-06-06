#ifndef TFV_RULE_BASED_BRAIN_HPP
#define TFV_RULE_BASED_BRAIN_HPP

#include "agents/Brain.hpp"
#include "agents/IdmParams.hpp"

namespace tfv
{
    /**
     * Deterministic baseline brain: longitudinal car-following via the Intelligent
     * Driver Model (Treiber). Pure float arithmetic, no RNG, so it is the
     * correctness oracle and is order-independent. It reconstructs absolute speed,
     * desired speed, and leader gap from the normalized Observation channels.
     *
     * Phase 2 scope: longitudinal only. Lane changes (MOBIL) and turn intent are
     * deferred to Phase 3 (no multi-lane geometry / cross-segment leader yet), so
     * Action.laneChange/turn are always 0.
     */
    class RuleBasedBrain : public IBrain
    {
      public:
        explicit RuleBasedBrain(const IdmParams& params) : m_p(params) {}

        void decideBatch(const Observation* obs, int n, Action* out) override;
        void reset(uint64_t /*seed*/) override {} // stateless / RNG-free
        std::string weightsHash() const override { return "rule-idm:v1"; }
        const char* kindName() const override { return "rule"; }

      private:
        IdmParams m_p;
    };

} // namespace tfv

#endif // TFV_RULE_BASED_BRAIN_HPP
