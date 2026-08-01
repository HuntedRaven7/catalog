#pragma once

#include <string>
#include <vector>

namespace catalog {

struct Package {
    std::string name;
    std::string version;
    std::string architecture;
    std::string description;
    std::string depends;
    std::string kind;
    std::string repo;
};

bool parse_packages(const char *json, std::vector<Package> &out, std::string &error);

}
