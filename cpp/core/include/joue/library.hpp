#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace joue {

// Plain tree mirror of the library XML (tags Library, String, Array, Folder, Module, Element, Setting).
struct Node {
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<std::unique_ptr<Node>> children;

    const std::string* attr(const std::string& name) const;
    std::string& attrRef(const std::string& name);
    std::string name() const;
    std::unique_ptr<Node> clone() const;
};

bool equalTrees(const Node& a, const Node& b);

// Both throw std::runtime_error; only the library tags and the name/value attributes are accepted, and version 3.
std::unique_ptr<Node> parseLibrary(const std::string& xml);
std::string serializeLibrary(const Node& root);

std::unique_ptr<Node> emptyLibrary();

}  // namespace joue
