#ifndef TFV_ONNX_BRAIN_HPP
#define TFV_ONNX_BRAIN_HPP

#include "agents/Brain.hpp"
#include "agents/IdmParams.hpp"

#include <memory>
#include <string>

namespace tfv
{
    /**
     * Load a brain backed by an ONNX Runtime model (a trained TF/PyTorch net exported
     * via tf2onnx / torch.onnx). The model must have exactly ONE float32 input of shape
     * [N,24] (dynamic batch N; the 24 channels are the locked Observation layout) and
     * ONE float32 output [N,K] (1<=K<=6); column 0 is the acceleration request, columns
     * 1..5 optionally map to laneChange/turn/lightCmd/reserved0/reserved1. The engine's
     * ActionValidator clamps/scrubs the result, so a model emits raw requests safely.
     *
     * `config` is the opaque "?<config>" suffix (key=val on '&'). Returns nullptr if ONNX
     * support is not compiled in (TFV_WITH_ONNX=OFF) or on ANY load failure (missing/bad
     * model, wrong I/O) — the caller falls back to the rule brain. NEVER throws.
     */
    std::unique_ptr<IBrain> loadOnnxBrain(const std::string& path, const std::string& config,
                                          const IdmParams& idm);

} // namespace tfv

#endif // TFV_ONNX_BRAIN_HPP
