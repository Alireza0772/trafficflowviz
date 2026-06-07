#ifndef TFV_PYTHON_BRAIN_HPP
#define TFV_PYTHON_BRAIN_HPP

#include "agents/Brain.hpp"
#include "agents/IdmParams.hpp"

#include <memory>
#include <string>

namespace tfv
{
    /**
     * Load a brain backed by an embedded-CPython module exposing decide_batch(obs)->[n,K]
     * (see examples/brains/python/example_brain.py for the contract). The module is
     * imported once; per tick the batch observation is passed as a zero-copy float32
     * [n,24] NumPy view and the returned array is marshalled into Action[] (the
     * ActionValidator clamps it afterward, exactly like the nn/dll brains).
     *
     * `config` is the opaque "?<config>" suffix (supports key=val pairs, incl. path=<dir>
     * prepended to sys.path). Returns nullptr if Python support is not compiled in
     * (TFV_WITH_PYTHON=OFF) or on ANY load failure (no interpreter, import/callable
     * error) — the caller falls back to the rule brain. NEVER throws.
     */
    std::unique_ptr<IBrain> loadPythonBrain(const std::string& module, const std::string& func,
                                            const std::string& config, const IdmParams& idm);

} // namespace tfv

#endif // TFV_PYTHON_BRAIN_HPP
