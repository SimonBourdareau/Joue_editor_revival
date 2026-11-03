#pragma once
#include <string>
#include <vector>

#include "joue/library.hpp"

namespace joue {

// The differences between two modules of a library, setting by setting: "ElementList/KEY1/Channel: 1 -> 10", or "(only in the first)" / "(only in the second)"
// for a setting that one of them does not have. The names of the modules are not compared.
std::vector<std::string> compareModules(const Node& first, const Node& second);

}  // namespace joue
