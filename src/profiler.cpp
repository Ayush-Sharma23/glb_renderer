#include "profiler.hpp"

void FrameProfiler::tick(double dt) {
    frame_count++;
    total_time += dt;
    last_fps_time += dt;

    if (last_fps_time >= 1.0) {
        last_fps = (int)(frame_count / total_time + 0.5);
        last_fps_time = 0.0;
        frame_count = 0;
        total_time = 0.0;
    }
}
