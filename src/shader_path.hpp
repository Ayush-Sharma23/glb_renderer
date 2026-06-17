#pragma once

#include <string>
#include <cstring>
#include <unistd.h>
#include <climits>
#include <libgen.h>

inline std::string get_executable_dir() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        char* dir = dirname(buf);
        return std::string(dir) + "/";
    }
    return "./";
}

inline std::string shader_path(const std::string& name) {
    std::string exe_dir = get_executable_dir();
    std::string path = exe_dir + "shaders/" + name;
    return path;
}
