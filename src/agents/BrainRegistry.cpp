#include "agents/BrainRegistry.hpp"

#include "agents/RuleBasedBrain.hpp"
#include "utils/LoggingManager.hpp"

namespace tfv
{
    std::unique_ptr<IBrain> makeBrain(const std::string& kind, const IdmParams& idm)
    {
        if(kind.empty() || kind == "rule")
            return std::make_unique<RuleBasedBrain>(idm);

        LOG_WARN("Unknown brain kind '{kind}', falling back to 'rule'", PARAM(kind, kind));
        return std::make_unique<RuleBasedBrain>(idm);
    }

} // namespace tfv
