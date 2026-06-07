#ifndef TFV_NN_BRAIN_HPP
#define TFV_NN_BRAIN_HPP

#include "agents/Brain.hpp"
#include "agents/IdmParams.hpp"

#include <string>
#include <vector>

namespace tfv
{
    /**
     * Built-in configurable feed-forward neural-net brain (kind "nn"). Arbitrary
     * depth/width from config; can degenerate to a single linear layer. Pure-float,
     * single-threaded, FIXED matmul loop order, and weights from a self-contained
     * splitmix64 seeded by the master seed (NO std random distributions) — so it is
     * deterministic same-build and reset(seed) regenerates identical weights.
     *
     * Maps the 24-float Observation -> Action: output[0] -> accel (tanh-squashed into
     * [-b_max, a_max]); output[1] -> lane change (thresholded); brake light derived
     * from accel. A random/untrained net therefore drives erratically but SAFELY (the
     * ActionValidator and Phase-B0 gap re-check bound every request).
     *
     * The first layer must be OBS_LEN (24) and the last must be 4; an invalid
     * architecture marks the brain not-valid (the registry then falls back to rule).
     */
    class NNBrain final : public IBrain
    {
      public:
        // `population` is the size of a bank of independent seeded weight sets;
        // vehicles are deterministically assigned to members by id hash, giving
        // heterogeneous drivers. population==1 (default) == a single shared net
        // (member 0 reproduces the seed's weights bit-for-bit). Clamped to [1,256].
        NNBrain(std::vector<int> layers, const std::string& activation, uint64_t seed,
                const IdmParams& idm, float laneThreshold = 0.5f, int population = 1);

        void decideBatch(const Observation* obs, int n, Action* out) override;
        void decideBatchIds(const Observation* obs, const uint64_t* ids, int n,
                            Action* out) override;
        void reset(uint64_t seed) override;
        std::string weightsHash() const override;
        const char* kindName() const override { return "nn"; }
        bool drivesLaneChange() const override { return true; }

        bool valid() const { return m_valid; }
        int population() const { return m_population; }

      private:
        void initWeights(uint64_t seed);
        // Forward one observation through bank member `member` -> Action.
        void forwardOne(int member, const Observation& obs, Action& out) const;

        std::vector<int> m_layers; // layer sizes; front==OBS_LEN, back==4
        int m_activation{0};       // 0 tanh, 1 relu, 2 identity (hidden layers)
        // Bank of weight sets: m_W[member][transition] row-major (out x in); m_b[member][transition].
        std::vector<std::vector<std::vector<float>>> m_W;
        std::vector<std::vector<std::vector<float>>> m_b;
        int m_population{1};
        IdmParams m_idm;
        float m_laneThreshold{0.5f};
        uint64_t m_seed{0};
        bool m_valid{false};
    };

} // namespace tfv

#endif // TFV_NN_BRAIN_HPP
