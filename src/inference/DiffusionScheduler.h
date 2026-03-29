#pragma once
#include <torch/torch.h>
#include <vector>
#include <cmath>

/**
 * CosineDPMSolverMultistepScheduler — C++ port of the diffusers scheduler.
 *
 * Implements the SDE variant of DPM-Solver++ (2nd order, midpoint) with
 * v_prediction and exponential sigma schedule, matching Stable Audio Open 1.0.
 *
 * Config (from scheduler_config.json):
 *   sigma_min=0.3, sigma_max=500, sigma_data=1.0, rho=7.0
 *   prediction_type=v_prediction, solver_type=midpoint, solver_order=2
 *   sigma_schedule=exponential, final_sigmas_type=zero, lower_order_final=true
 */
class DiffusionScheduler
{
public:
    struct Config
    {
        float sigmaMin = 0.3f;
        float sigmaMax = 500.0f;
        float sigmaData = 1.0f;
        float rho = 7.0f;
        int solverOrder = 2;
        bool lowerOrderFinal = true;
        bool eulerAtFinal = false;
    };

    DiffusionScheduler();
    explicit DiffusionScheduler(const Config& config);

    /** Load config from scheduler_config.json. */
    static Config loadConfig(const std::string& jsonPath);

    /** Compute sigma schedule for N inference steps. */
    void setTimesteps(int numSteps);

    /** Scale model input: x * c_in where c_in = 1/sqrt(sigma^2 + sigma_data^2). */
    torch::Tensor scaleModelInput(const torch::Tensor& sample, int stepIndex) const;

    /** Convert raw model output (v_prediction) to denoised sample. */
    torch::Tensor convertModelOutput(const torch::Tensor& modelOutput,
                                     const torch::Tensor& sample,
                                     int stepIndex) const;

    /** First-order DPM-Solver++ step (used for step 0 and final step). */
    torch::Tensor firstOrderUpdate(const torch::Tensor& modelOutput,
                                   const torch::Tensor& sample,
                                   int stepIndex,
                                   const torch::Tensor& noise) const;

    /** Second-order midpoint DPM-Solver++ step. */
    torch::Tensor secondOrderUpdate(const torch::Tensor& m0,
                                    const torch::Tensor& m1,
                                    const torch::Tensor& sample,
                                    int stepIndex,
                                    const torch::Tensor& noise) const;

    float getSigma(int stepIndex) const { return sigmas_[stepIndex]; }
    float getTimestep(int stepIndex) const { return timesteps_[stepIndex]; }
    int getNumSteps() const { return numSteps_; }

private:
    Config config_;
    int numSteps_ = 0;
    std::vector<float> sigmas_;     // length = numSteps + 1 (last is 0 or sigma_min)
    std::vector<float> timesteps_;  // length = numSteps
};
