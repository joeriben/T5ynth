#include "DiffusionScheduler.h"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

DiffusionScheduler::DiffusionScheduler()
    : config_{}
{
}

DiffusionScheduler::DiffusionScheduler(const Config& config)
    : config_(config)
{
}

DiffusionScheduler::Config DiffusionScheduler::loadConfig(const std::string& jsonPath)
{
    std::ifstream f(jsonPath);
    if (!f.is_open())
        throw std::runtime_error("Cannot open scheduler config: " + jsonPath);

    auto j = nlohmann::json::parse(f);

    Config c;
    c.sigmaMin      = j.value("sigma_min", 0.3f);
    c.sigmaMax      = j.value("sigma_max", 500.0f);
    c.sigmaData     = j.value("sigma_data", 1.0f);
    c.rho           = j.value("rho", 7.0f);
    c.solverOrder   = j.value("solver_order", 2);
    c.lowerOrderFinal = j.value("lower_order_final", true);
    c.eulerAtFinal  = j.value("euler_at_final", false);
    return c;
}

void DiffusionScheduler::setTimesteps(int numSteps)
{
    numSteps_ = numSteps;

    // Exponential sigma schedule: linspace(log(sigma_min), log(sigma_max), N).exp().flip()
    sigmas_.resize(numSteps + 1);
    float logMin = std::log(config_.sigmaMin);
    float logMax = std::log(config_.sigmaMax);
    for (int i = 0; i < numSteps; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(numSteps - 1);
        float logSigma = logMin + t * (logMax - logMin);
        // Flip: index 0 = sigma_max, index N-1 = sigma_min
        sigmas_[numSteps - 1 - i] = std::exp(logSigma);
    }
    // Final sigma = 0 (final_sigmas_type = "zero")
    sigmas_[numSteps] = 0.0f;

    // Timesteps: arctan(sigma) / pi * 2
    timesteps_.resize(numSteps);
    for (int i = 0; i < numSteps; ++i)
        timesteps_[i] = std::atan(sigmas_[i]) / static_cast<float>(M_PI) * 2.0f;
}

torch::Tensor DiffusionScheduler::scaleModelInput(const torch::Tensor& sample, int stepIndex) const
{
    float sigma = sigmas_[stepIndex];
    float cIn = 1.0f / std::sqrt(sigma * sigma + config_.sigmaData * config_.sigmaData);
    return sample * cIn;
}

torch::Tensor DiffusionScheduler::convertModelOutput(const torch::Tensor& modelOutput,
                                                     const torch::Tensor& sample,
                                                     int stepIndex) const
{
    float sigma = sigmas_[stepIndex];
    float sd = config_.sigmaData;

    // v_prediction preconditioning
    float cSkip = (sd * sd) / (sigma * sigma + sd * sd);
    float cOut  = -(sigma * sd) / std::sqrt(sigma * sigma + sd * sd);

    return cSkip * sample + cOut * modelOutput;
}

torch::Tensor DiffusionScheduler::firstOrderUpdate(const torch::Tensor& modelOutput,
                                                   const torch::Tensor& sample,
                                                   int stepIndex,
                                                   const torch::Tensor& /*noise*/) const
{
    float sigmaS = sigmas_[stepIndex];       // current sigma
    float sigmaT = sigmas_[stepIndex + 1];   // next sigma (target)

    // ODE DPM-Solver++ first order (no noise injection):
    // x_t = (sigma_t / sigma_s) * sample + (1 - sigma_t / sigma_s) * denoised
    // When sigma_t = 0 (final step): x_t = denoised
    float ratio = (sigmaS > 0) ? (sigmaT / sigmaS) : 0.0f;
    return ratio * sample + (1.0f - ratio) * modelOutput;
}

torch::Tensor DiffusionScheduler::secondOrderUpdate(const torch::Tensor& m0,
                                                    const torch::Tensor& m1,
                                                    const torch::Tensor& sample,
                                                    int stepIndex,
                                                    const torch::Tensor& /*noise*/) const
{
    float sigmaS0 = sigmas_[stepIndex];      // current
    float sigmaS1 = sigmas_[stepIndex - 1];  // previous
    float sigmaT  = sigmas_[stepIndex + 1];  // target

    // Lambdas (alpha_t=1 always)
    float lambdaT  = (sigmaT > 0)  ? -std::log(sigmaT)  : 100.0f;
    float lambdaS0 = -std::log(sigmaS0);
    float lambdaS1 = -std::log(sigmaS1);

    float h   = lambdaT - lambdaS0;
    float h_0 = lambdaS0 - lambdaS1;
    float r0  = h_0 / h;

    // D0 = m0, D1 = (1/r0) * (m0 - m1) — second-order correction
    auto D0 = m0;
    auto D1 = (1.0f / r0) * (m0 - m1);

    // ODE DPM-Solver++ midpoint second order (no noise injection):
    // x_t = (sigma_t/sigma_s0) * sample + (1 - sigma_t/sigma_s0) * D0
    //                                    + 0.5 * (1 - sigma_t/sigma_s0) * D1
    float ratio = (sigmaS0 > 0) ? (sigmaT / sigmaS0) : 0.0f;
    float coeff = 1.0f - ratio;

    return ratio * sample + coeff * D0 + 0.5f * coeff * D1;
}
