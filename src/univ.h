#pragma once

#include <functional>
#include <string>
#include <vector>

#include "model.h"

namespace catalog {

std::string univ_bin();

using QueryCallback = std::function<void(bool ok, std::vector<Package> packages,
                                         const std::string &message)>;

void query_packages(const std::vector<std::string> &args, QueryCallback cb);

using LineCallback = std::function<void(const std::string &line)>;
using ExitCallback = std::function<void(int exit_code)>;

void stream_task(const std::vector<std::string> &args, LineCallback on_line,
                 ExitCallback on_exit);

}
