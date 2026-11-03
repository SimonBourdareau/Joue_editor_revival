#include "joue/library.hpp"

#include <set>
#include <sstream>
#include <stdexcept>

#include "pugixml.hpp"

namespace joue {

const std::string* Node::attr(const std::string& key) const {
    for (const auto& pair : attrs) {
        if (pair.first == key) return &pair.second;
    }
    return nullptr;
}

std::string& Node::attrRef(const std::string& key) {
    for (auto& pair : attrs) {
        if (pair.first == key) return pair.second;
    }
    attrs.emplace_back(key, "");
    return attrs.back().second;
}

std::string Node::name() const {
    const std::string* value = attr("name");
    return value ? *value : std::string();
}

std::unique_ptr<Node> Node::clone() const {
    auto copy = std::make_unique<Node>();
    copy->tag = tag;
    copy->attrs = attrs;
    for (const auto& child : children) copy->children.push_back(child->clone());
    return copy;
}

bool equalTrees(const Node& a, const Node& b) {
    if (a.tag != b.tag || a.attrs != b.attrs || a.children.size() != b.children.size()) return false;
    for (std::size_t i = 0; i < a.children.size(); ++i) {
        if (!equalTrees(*a.children[i], *b.children[i])) return false;
    }
    return true;
}

namespace {

const std::set<std::string>& allowedTags() {
    static const std::set<std::string> tags = {"Library", "String", "Array", "Folder", "Module", "Element", "Setting"};
    return tags;
}

std::unique_ptr<Node> fromXml(const pugi::xml_node& element) {
    if (!allowedTags().count(element.name())) throw std::runtime_error(std::string("Unexpected element <") + element.name() + ">.");
    auto node = std::make_unique<Node>();
    node->tag = element.name();
    for (const pugi::xml_attribute& attribute : element.attributes()) {
        std::string key = attribute.name();
        if (key != "name" && key != "value") throw std::runtime_error("Unexpected attribute '" + key + "' on <" + node->tag + ">.");
        node->attrs.emplace_back(key, attribute.value());
    }
    for (const pugi::xml_node& child : element.children()) {
        if (child.type() == pugi::node_element) node->children.push_back(fromXml(child));
    }
    return node;
}

void toXml(const Node& node, pugi::xml_node parent) {
    if (!allowedTags().count(node.tag)) throw std::runtime_error("Unexpected element <" + node.tag + ">.");
    pugi::xml_node element = parent.append_child(node.tag.c_str());
    for (const auto& pair : node.attrs) {
        if (pair.first != "name" && pair.first != "value") throw std::runtime_error("Unexpected attribute '" + pair.first + "' on <" + node.tag + ">.");
        element.append_attribute(pair.first.c_str()) = pair.second.c_str();
    }
    for (const auto& child : node.children) toXml(*child, element);
}

void requireVersion3(const Node& root) {
    if (root.tag != "Library") throw std::runtime_error("Root element must be <Library>.");
    for (const auto& child : root.children) {
        if (child->tag == "String" && child->name() == "Version") {
            const std::string* value = child->attr("value");
            if (value && *value == "3") return;
            throw std::runtime_error("Library version " + (value ? *value : std::string("?")) + " is not supported.");
        }
    }
    throw std::runtime_error("Library version is missing.");
}

}  // namespace

std::unique_ptr<Node> parseLibrary(const std::string& xml) {
    pugi::xml_document document;
    pugi::xml_parse_result result = document.load_buffer(xml.data(), xml.size(), pugi::parse_default, pugi::encoding_utf8);
    if (!result) throw std::runtime_error(std::string("Invalid XML: ") + result.description());
    pugi::xml_node root = document.document_element();
    if (!root) throw std::runtime_error("Root element must be <Library>.");
    auto tree = fromXml(root);
    requireVersion3(*tree);
    return tree;
}

std::string serializeLibrary(const Node& root) {
    requireVersion3(root);
    pugi::xml_document document;
    pugi::xml_node declaration = document.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";
    toXml(root, document);
    std::ostringstream out;
    document.save(out, "\t", pugi::format_default | pugi::format_write_bom, pugi::encoding_utf8);
    return out.str();
}

std::unique_ptr<Node> emptyLibrary() {
    auto root = std::make_unique<Node>();
    root->tag = "Library";
    auto version = std::make_unique<Node>();
    version->tag = "String";
    version->attrs = {{"name", "Version"}, {"value", "3"}};
    auto list = std::make_unique<Node>();
    list->tag = "Array";
    list->attrs = {{"name", "ModuleList"}};
    root->children.push_back(std::move(version));
    root->children.push_back(std::move(list));
    return root;
}

}  // namespace joue
