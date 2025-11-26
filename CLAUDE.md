# Claude

- dont include estimates, either time or lines of code, in any plans
- use opengameart.org for texture asset placeholders
- keep an emphasis when planning on incremental progress, each state should be working and look decent when rendered
- always ensure that the build both compiles and runs without crashing before considering it done
- shaders can be compiled by running cmake
- compile with `cmake --preset debug && cmake --build build/debug`
- run with `./run-debug.sh` do not add timers or kill to this, you run it for the logs, but let the user close the app

