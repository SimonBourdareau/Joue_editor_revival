#pragma once
#include <string>

namespace joue {

// True when the version of a release tag ("v0.34.0") is newer than the running version ("0.33.1"): dotted numbers compared one by one, a leading v and
// anything after a dash ignored. A text that is not a version is never newer.
bool isNewerVersion(const std::string& latest, const std::string& current);

// The first string value of a key in a piece of JSON ("tag_name": "v0.34.0"), or an empty string.
std::string jsonStringValue(const std::string& json, const std::string& key);

}  // namespace joue
