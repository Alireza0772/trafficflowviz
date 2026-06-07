#include "agents/BrainRegistry.hpp"

#include "agents/DllBrain.hpp"
#include "agents/NNBrain.hpp"
#include "agents/OnnxBrain.hpp"
#include "agents/PythonBrain.hpp"
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

        // External backend: "dll:<path>" or "dll:<path>?<config>" loads a shared library
        // implementing tfv_brain.h. (Python/ONNX backends will route here similarly.)
        if(kind.rfind("dll:", 0) == 0)
        {
            const std::string rest = kind.substr(4);
            std::string path = rest, config;
            const auto q = rest.find('?');
            if(q != std::string::npos)
            {
                path = rest.substr(0, q);
                config = rest.substr(q + 1);
            }
            if(auto b = loadDllBrain(path, config))
                return b;
            LOG_WARN("DLL brain load failed; falling back to 'rule'");
            return std::make_unique<RuleBasedBrain>(idm);
        }

        // ONNX Runtime: "onnx:<path>[?config]". Split only on the first '?' (the path may
        // contain ':' and '/'), exactly like the dll: branch.
        if(kind.rfind("onnx:", 0) == 0)
        {
            std::string rest = kind.substr(5);
            std::string path = rest, config;
            const auto q = rest.find('?');
            if(q != std::string::npos)
            {
                path = rest.substr(0, q);
                config = rest.substr(q + 1);
            }
            if(auto b = loadOnnxBrain(path, config, idm))
                return b;
            LOG_WARN("ONNX brain load failed; falling back to 'rule'");
            return std::make_unique<RuleBasedBrain>(idm);
        }

        // Embedded Python: "python:<module>[:func][?config]". Split ?config first, then
        // the LAST ':' of the remainder as the optional function (default decide_batch).
        if(kind.rfind("python:", 0) == 0)
        {
            std::string rest = kind.substr(7);
            std::string config;
            const auto q = rest.find('?');
            if(q != std::string::npos)
            {
                config = rest.substr(q + 1);
                rest = rest.substr(0, q);
            }
            std::string module = rest, func;
            const auto colon = rest.rfind(':');
            if(colon != std::string::npos)
            {
                module = rest.substr(0, colon);
                func = rest.substr(colon + 1);
            }
            if(auto b = loadPythonBrain(module, func, config, idm))
                return b;
            LOG_WARN("Python brain load failed; falling back to 'rule'");
            return std::make_unique<RuleBasedBrain>(idm);
        }

        LOG_WARN("Unknown brain kind '{kind}', falling back to 'rule'", PARAM(kind, kind));
        return std::make_unique<RuleBasedBrain>(idm);
    }

} // namespace tfv
