#include "Trainer.h"

#include <SDL3/SDL_log.h>
#include <cstdlib>
#include <cstring>
#include <string>

static void printUsage(const char* argv0) {
    SDL_Log("Usage: %s [options]", argv0);
    SDL_Log("Options:");
    SDL_Log("  --motions <dir>      Motion data directory (default: assets/motions)");
    SDL_Log("  --output <dir>       Output directory (default: generated/unicon)");
    SDL_Log("  --envs <n>           Number of parallel environments (default: 32)");
    SDL_Log("  --iterations <n>     Training iterations (default: 1000)");
    SDL_Log("  --rollout-steps <n>  Steps per env per rollout (default: 64)");
    SDL_Log("  --lr <f>             Policy learning rate (default: 3e-4)");
    SDL_Log("  --resume <path>      Resume from checkpoint weights");
    SDL_Log("  --help               Show this message");
}

int main(int argc, char* argv[]) {
    TrainerConfig config;
    std::string resumePath;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--motions") == 0 && i + 1 < argc) {
            config.motionDir = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            config.outputDir = argv[++i];
        } else if (std::strcmp(argv[i], "--envs") == 0 && i + 1 < argc) {
            config.numEnvs = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            config.totalIterations = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--rollout-steps") == 0 && i + 1 < argc) {
            config.rolloutSteps = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--lr") == 0 && i + 1 < argc) {
            config.policyLR = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--resume") == 0 && i + 1 < argc) {
            resumePath = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown option: %s", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    SDL_Log("=== UniCon C++ Training ===");
    SDL_Log("Environments: %zu", config.numEnvs);
    SDL_Log("Iterations: %zu", config.totalIterations);
    SDL_Log("Rollout steps: %zu", config.rolloutSteps);
    SDL_Log("Policy LR: %g", config.policyLR);
    SDL_Log("Motion dir: %s", config.motionDir.c_str());
    SDL_Log("Output dir: %s", config.outputDir.c_str());

    Trainer trainer(config);

    if (!resumePath.empty()) {
        SDL_Log("Resuming from checkpoint: %s", resumePath.c_str());
        trainer.saveCheckpoint(resumePath); // TODO: loadCheckpoint
    }

    trainer.train();

    return 0;
}
