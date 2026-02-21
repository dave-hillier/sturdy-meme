// pybind11 bindings exposing VecEnv as a Python module (jolt_training).
//
// Build: cmake builds this as a shared library that Python can import.
// Usage from Python:
//   import jolt_training
//   env = jolt_training.VecEnv(num_envs=4096, skeleton_path="data/characters/humanoid.glb")
//   obs = env.reset()
//   obs, rewards, dones = env.step(actions)

#ifdef BUILD_PYTHON_BINDINGS

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "VecEnv.h"
#include "CharacterEnv.h"
#include "MotionFrame.h"
#include "RewardComputer.h"
#include "../loaders/GLTFLoader.h"

#include <SDL.h>

namespace py = pybind11;

PYBIND11_MODULE(jolt_training, m) {
    m.doc() = "Jolt Physics training environment for AMP/CALM reinforcement learning";

    // EnvConfig
    py::class_<training::EnvConfig>(m, "EnvConfig")
        .def(py::init<>())
        .def_readwrite("sim_timestep", &training::EnvConfig::simTimestep)
        .def_readwrite("sim_substeps", &training::EnvConfig::simSubsteps)
        .def_readwrite("early_termination_height", &training::EnvConfig::earlyTerminationHeight)
        .def_readwrite("max_episode_steps", &training::EnvConfig::maxEpisodeSteps);

    // TaskType
    py::enum_<training::TaskType>(m, "TaskType")
        .value("HEADING", training::TaskType::Heading)
        .value("LOCATION", training::TaskType::Location)
        .value("STRIKE", training::TaskType::Strike)
        .export_values();

    // MotionFrame
    py::class_<training::MotionFrame>(m, "MotionFrame")
        .def(py::init<>())
        .def_static("from_numpy", [](py::array_t<float> root_pos,
                                      py::array_t<float> root_rot,
                                      py::array_t<float> joint_positions,
                                      py::array_t<float> joint_rotations) {
            training::MotionFrame frame;

            auto rp = root_pos.unchecked<1>();
            frame.rootPosition = glm::vec3(rp(0), rp(1), rp(2));

            auto rr = root_rot.unchecked<1>();
            // Input is (x, y, z, w)
            frame.rootRotation = glm::quat(rr(3), rr(0), rr(1), rr(2));

            auto jp = joint_positions.unchecked<2>();
            frame.jointPositions.resize(jp.shape(0));
            for (ssize_t i = 0; i < jp.shape(0); ++i) {
                frame.jointPositions[i] = glm::vec3(jp(i, 0), jp(i, 1), jp(i, 2));
            }

            auto jr = joint_rotations.unchecked<2>();
            frame.jointRotations.resize(jr.shape(0));
            for (ssize_t i = 0; i < jr.shape(0); ++i) {
                // (x, y, z, w) input
                frame.jointRotations[i] = glm::quat(jr(i, 3), jr(i, 0), jr(i, 1), jr(i, 2));
            }

            return frame;
        }, py::arg("root_pos"), py::arg("root_rot"),
           py::arg("joint_positions"), py::arg("joint_rotations"));

    // VecEnv
    py::class_<training::VecEnv>(m, "VecEnv")
        .def(py::init([](int numEnvs, const std::string& skeletonPath,
                         training::EnvConfig config) {
            // Load skeleton from glb file
            auto result = GLTFLoader::load(skeletonPath);
            if (!result.has_value()) {
                throw std::runtime_error("Failed to load skeleton: " + skeletonPath);
            }
            if (!result->skeleton.has_value()) {
                throw std::runtime_error("No skeleton in file: " + skeletonPath);
            }

            return std::make_unique<training::VecEnv>(
                numEnvs, config, result->skeleton.value());
        }), py::arg("num_envs"), py::arg("skeleton_path"),
            py::arg("config") = training::EnvConfig{})

        .def("reset", [](training::VecEnv& self) {
            self.reset();
            // Return observations as numpy array
            int n = self.numEnvs();
            int d = self.policyObsDim();
            return py::array_t<float>({n, d},
                                       {d * sizeof(float), sizeof(float)},
                                       self.observations(),
                                       py::cast(self));
        })

        .def("step", [](training::VecEnv& self, py::array_t<float> actions) {
            auto buf = actions.request();
            if (buf.ndim != 2) {
                throw std::runtime_error("Actions must be 2D array [num_envs, action_dim]");
            }
            self.step(static_cast<float*>(buf.ptr));

            int n = self.numEnvs();
            int pd = self.policyObsDim();
            int ad = self.ampObsDim();

            // Return (obs, rewards, dones) as numpy arrays
            // These are views into VecEnv's contiguous buffers — zero copy
            auto obs = py::array_t<float>(
                {n, pd}, {pd * sizeof(float), sizeof(float)},
                self.observations(), py::cast(self));
            auto rewards = py::array_t<float>(
                {n}, {sizeof(float)},
                self.rewards(), py::cast(self));

            // Convert bool* to numpy (need copy since numpy bool is 1 byte)
            auto dones_arr = py::array_t<bool>({n});
            auto dones_mut = dones_arr.mutable_unchecked<1>();
            const bool* d = self.dones();
            for (int i = 0; i < n; ++i) {
                dones_mut(i) = d[i];
            }

            return py::make_tuple(obs, rewards, dones_arr);
        }, py::arg("actions"))

        .def("amp_observations", [](training::VecEnv& self) {
            int n = self.numEnvs();
            int d = self.ampObsDim();
            return py::array_t<float>(
                {n, d}, {d * sizeof(float), sizeof(float)},
                self.ampObservations(), py::cast(self));
        })

        .def("set_task", [](training::VecEnv& self, training::TaskType task,
                            py::array_t<float> target) {
            auto t = target.unchecked<1>();
            self.setTask(task, glm::vec3(t(0), t(1), t(2)));
        }, py::arg("task"), py::arg("target"))

        // Motion library: load FBX animations for training
        .def("load_motions", &training::VecEnv::loadMotions,
             py::arg("directory"),
             "Load all FBX animation files from a directory. Returns number of clips loaded.")

        .def("load_motion_file", &training::VecEnv::loadMotionFile,
             py::arg("path"),
             "Load animations from a single FBX file. Returns number of clips loaded.")

        .def("reset_done_with_motions", &training::VecEnv::resetDoneWithMotions,
             "Reset done environments using random frames from loaded motion library.")

        .def_property_readonly("num_motions", [](const training::VecEnv& self) {
            return self.motionLibrary().numClips();
        }, "Number of loaded motion clips.")

        .def_property_readonly("motion_duration", [](const training::VecEnv& self) {
            return self.motionLibrary().totalDuration();
        }, "Total duration of all loaded motion clips in seconds.")

        .def_property_readonly("num_envs", &training::VecEnv::numEnvs)
        .def_property_readonly("policy_obs_dim", &training::VecEnv::policyObsDim)
        .def_property_readonly("amp_obs_dim", &training::VecEnv::ampObsDim)
        .def_property_readonly("action_dim", &training::VecEnv::actionDim);
}

#endif // BUILD_PYTHON_BINDINGS
