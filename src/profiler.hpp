#pragma once

#include <cstdint>
#include <vector>

struct FrameProfiler {
    uint32_t frame_count = 0;
    double total_time = 0.0;
    double last_fps_time = 0.0;
    int last_fps = 0;

    void tick(double dt);
    int fps() const { return last_fps; }
};
