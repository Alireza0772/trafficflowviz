#include "agents/BrainRegistry.hpp"

#include "agents/NNBrain.hpp"
#include "agents/RuleBasedBrain.hpp"
#include "core/Configuration.hpp"
#include "utils/LoggingManager.hpp"

#include <sstream>
#include <vector>

namespace tfv
{
    namespace
    {
        std::vector<int> parseLayers(const std::string& csv)
        {
            std::vector<int> out;
            std::stringstream ss(csv);
            std::string tok;
            while(std::getline(ss, tok, ','))
            {
                try
                {
                    out.push_back(std::stoi(tok));
                }
                catch(...)
                {
                    return {}; // malformed -> empty -> invalid arch -> fall back to rule
                }
            }
            return out;
        }
    } // namespace

    std::unique_ptr<IBrain> makeBrain(const std::string& kind, const IdmParams& idm)
    {
        if(kind.empty() || kind == "rule")
            return std::make_unique<RuleBasedBrain>(idm);

        if(kind == "nn")
        {
            auto& cfg = TFV_CONFIG();
            std::vector<int> layers = parseLayers(cfg.getString("sim.nn.layers", "24,16,8,4"));
            const std::string act = cfg.getString("sim.nn.activation", "tanh");
            const float thr = cfg.getFloat("sim.nn.lane_threshold", 0.5f);
            auto nn = std::make_unique<NNBrain>(layers, act, cfg.getMasterSeed(), idm, thr);
            if(nn->valid())
            {
                LOG_INFO("Using neural-net brain (weights {hash})", PARAM(hash, nn->weightsHash()));
                return nn;
            }
            LOG_WARN("Invalid nn architecture (need first==24, last==4); falling back to 'rule'");
            return std::make_unique<RuleBasedBrain>(idm);
        }

        // Phase-7 follow-on: "dll:...", "python:...", "onnx:..." route through VtableBrain.
        LOG_WARN("Unknown brain kind '{kind}', falling back to 'rule'", PARAM(kind, kind));
        return std::make_unique<RuleBasedBrain>(idm);
    }

} // namespace tfv
