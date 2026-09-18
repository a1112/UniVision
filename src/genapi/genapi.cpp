#include "genapi.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace univision::genapi {
namespace {

struct XmlElement {
  std::string name;
  std::map<std::string, std::string> attributes;
  std::vector<XmlElement> children;
  std::string text;
};

std::string local_name(std::string_view value) {
  const auto separator = value.find(':');
  return std::string(value.substr(separator == std::string_view::npos ? 0 : separator + 1));
}

std::string trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

std::string decode_entities(std::string value) {
  const std::pair<std::string_view, std::string_view> entities[] = {
      {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
      {"&quot;", "\""}, {"&apos;", "'"},
  };
  for (const auto& [encoded, decoded] : entities) {
    std::size_t position = 0;
    while ((position = value.find(encoded, position)) != std::string::npos) {
      value.replace(position, encoded.size(), decoded);
      position += decoded.size();
    }
  }
  return value;
}

class XmlParser {
 public:
  explicit XmlParser(std::string_view source) : source_(source) {
    if (source_.size() >= 3 && static_cast<unsigned char>(source_[0]) == 0xefU &&
        static_cast<unsigned char>(source_[1]) == 0xbbU &&
        static_cast<unsigned char>(source_[2]) == 0xbfU) {
      position_ = 3;
    }
  }

  Result<XmlElement> parse() {
    skip_misc();
    if (position_ >= source_.size()) {
      return Status{ErrorCode::invalid_argument, "GenApi XML is empty"};
    }
    auto root = parse_element();
    if (!root) {
      return root.status();
    }
    skip_misc();
    if (position_ != source_.size()) {
      return error("unexpected content after root element");
    }
    return std::move(root).value();
  }

 private:
  Status error(std::string message) const {
    return {ErrorCode::invalid_argument,
            "invalid GenApi XML at byte " + std::to_string(position_) + ": " + message};
  }

  bool starts_with(std::string_view token) const {
    return source_.substr(position_, token.size()) == token;
  }

  void skip_space() {
    while (position_ < source_.size() &&
           (source_[position_] == ' ' || source_[position_] == '\t' ||
            source_[position_] == '\r' || source_[position_] == '\n')) {
      ++position_;
    }
  }

  void skip_misc() {
    while (true) {
      skip_space();
      if (starts_with("<?")) {
        const auto end = source_.find("?>", position_ + 2);
        position_ = end == std::string_view::npos ? source_.size() : end + 2;
      } else if (starts_with("<!--")) {
        const auto end = source_.find("-->", position_ + 4);
        position_ = end == std::string_view::npos ? source_.size() : end + 3;
      } else if (starts_with("<!DOCTYPE")) {
        const auto end = source_.find('>', position_ + 9);
        position_ = end == std::string_view::npos ? source_.size() : end + 1;
      } else {
        break;
      }
    }
  }

  std::string parse_name() {
    const auto begin = position_;
    while (position_ < source_.size()) {
      const char value = source_[position_];
      const bool valid = (value >= 'a' && value <= 'z') ||
                         (value >= 'A' && value <= 'Z') ||
                         (value >= '0' && value <= '9') || value == '_' ||
                         value == '-' || value == ':' || value == '.';
      if (!valid) {
        break;
      }
      ++position_;
    }
    return local_name(source_.substr(begin, position_ - begin));
  }

  Result<std::string> parse_quoted() {
    if (position_ >= source_.size() ||
        (source_[position_] != '\'' && source_[position_] != '\"')) {
      return error("expected quoted attribute value");
    }
    const char quote = source_[position_++];
    const auto begin = position_;
    const auto end = source_.find(quote, position_);
    if (end == std::string_view::npos) {
      return error("unterminated attribute value");
    }
    position_ = end + 1;
    return decode_entities(std::string(source_.substr(begin, end - begin)));
  }

  Result<XmlElement> parse_element() {
    if (position_ >= source_.size() || source_[position_] != '<' ||
        starts_with("</")) {
      return error("expected opening tag");
    }
    ++position_;
    XmlElement element;
    element.name = parse_name();
    if (element.name.empty()) {
      return error("element name is empty");
    }

    while (true) {
      skip_space();
      if (starts_with("/>")) {
        position_ += 2;
        return element;
      }
      if (position_ < source_.size() && source_[position_] == '>') {
        ++position_;
        break;
      }
      auto attribute = parse_name();
      if (attribute.empty()) {
        return error("expected attribute name");
      }
      skip_space();
      if (position_ >= source_.size() || source_[position_++] != '=') {
        return error("expected '=' after attribute name");
      }
      skip_space();
      auto value = parse_quoted();
      if (!value) {
        return value.status();
      }
      element.attributes.emplace(std::move(attribute), std::move(value).value());
    }

    while (position_ < source_.size()) {
      if (starts_with("</")) {
        position_ += 2;
        const auto closing_name = parse_name();
        skip_space();
        if (position_ >= source_.size() || source_[position_++] != '>') {
          return error("unterminated closing tag");
        }
        if (closing_name != element.name) {
          return error("closing tag does not match " + element.name);
        }
        element.text = trim(decode_entities(element.text));
        return element;
      }
      if (starts_with("<!--")) {
        const auto end = source_.find("-->", position_ + 4);
        if (end == std::string_view::npos) {
          return error("unterminated comment");
        }
        position_ = end + 3;
        continue;
      }
      if (starts_with("<![CDATA[")) {
        const auto end = source_.find("]]>", position_ + 9);
        if (end == std::string_view::npos) {
          return error("unterminated CDATA section");
        }
        element.text.append(source_.substr(position_ + 9, end - position_ - 9));
        position_ = end + 3;
        continue;
      }
      if (source_[position_] == '<') {
        auto child = parse_element();
        if (!child) {
          return child.status();
        }
        element.children.push_back(std::move(child).value());
      } else {
        const auto end = source_.find('<', position_);
        const auto length = (end == std::string_view::npos ? source_.size() : end) -
                            position_;
        element.text.append(source_.substr(position_, length));
        position_ += length;
      }
    }
    return error("unterminated element " + element.name);
  }

  std::string_view source_;
  std::size_t position_{};
};

const XmlElement* child(const XmlElement& element, std::string_view name) {
  const auto found = std::find_if(
      element.children.begin(), element.children.end(),
      [&](const XmlElement& value) { return value.name == name; });
  return found == element.children.end() ? nullptr : &*found;
}

std::string child_text(const XmlElement& element, std::string_view name) {
  const auto* value = child(element, name);
  return value == nullptr ? std::string{} : value->text;
}

std::optional<std::uint64_t> parse_unsigned(std::string_view text) {
  auto value = trim(text);
  if (value.empty()) {
    return std::nullopt;
  }
  int base = 10;
  std::string_view digits = value;
  if (digits.size() > 2 && digits[0] == '0' &&
      (digits[1] == 'x' || digits[1] == 'X')) {
    base = 16;
    digits.remove_prefix(2);
  }
  std::uint64_t result{};
  const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), result,
                                      base);
  if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return result;
}

std::optional<std::int64_t> parse_integer(std::string_view text) {
  const auto value = trim(text);
  if (value.empty()) {
    return std::nullopt;
  }
  if (value.front() != '-') {
    const auto parsed = parse_unsigned(value);
    if (!parsed || *parsed > static_cast<std::uint64_t>(
                               std::numeric_limits<std::int64_t>::max())) {
      return std::nullopt;
    }
    return static_cast<std::int64_t>(*parsed);
  }
  std::int64_t result{};
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
    return std::nullopt;
  }
  return result;
}

std::optional<double> parse_floating(std::string_view text) {
  const auto value = trim(text);
  if (value.empty()) {
    return std::nullopt;
  }
  double result{};
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
    return std::nullopt;
  }
  return result;
}

AccessMode parse_access(std::string value) {
  if (value == "RO") {
    return AccessMode::read_only;
  }
  if (value == "WO") {
    return AccessMode::write_only;
  }
  if (value == "RW") {
    return AccessMode::read_write;
  }
  return AccessMode::unavailable;
}

bool readable(AccessMode access) {
  return access == AccessMode::read_only || access == AccessMode::read_write;
}

bool writable(AccessMode access) {
  return access == AccessMode::write_only || access == AccessMode::read_write;
}

bool standard_name(std::string_view name) {
  constexpr std::string_view names[] = {
      "Width",          "Height",          "OffsetX",       "OffsetY",
      "ExposureTime",   "Gain",            "PixelFormat",   "TriggerMode",
      "TriggerSource",  "TriggerSoftware", "AcquisitionStart",
      "AcquisitionStop", "DeviceUserID",   "DeviceTemperature",
  };
  return std::find(std::begin(names), std::end(names), name) != std::end(names);
}

enum class NodeType {
  integer,
  int_register,
  floating_point,
  float_register,
  boolean,
  enumeration,
  enum_entry,
  string,
  string_register,
  command,
};

struct Node {
  std::string name;
  NodeType type{NodeType::integer};
  FeatureInfo info;
  std::string value_reference;
  std::vector<std::string> entry_references;
  std::optional<std::int64_t> integer_value;
  std::optional<double> floating_value;
  std::string string_value;
  std::uint64_t address{};
  std::size_t length{};
  bool little_endian{};
  bool signed_value{};
  std::int64_t on_value{1};
  std::int64_t off_value{};
  std::int64_t command_value{1};
  bool exposed{};
};

Result<NodeType> node_type(std::string_view name) {
  if (name == "Integer") return NodeType::integer;
  if (name == "IntReg") return NodeType::int_register;
  if (name == "Float") return NodeType::floating_point;
  if (name == "FloatReg") return NodeType::float_register;
  if (name == "Boolean") return NodeType::boolean;
  if (name == "Enumeration") return NodeType::enumeration;
  if (name == "EnumEntry") return NodeType::enum_entry;
  if (name == "String") return NodeType::string;
  if (name == "StringReg") return NodeType::string_register;
  if (name == "Command") return NodeType::command;
  return Status{ErrorCode::not_found, "not a supported GenApi node type"};
}

FeatureKind feature_kind(NodeType type) {
  switch (type) {
    case NodeType::integer:
      return FeatureKind::integer;
    case NodeType::floating_point:
      return FeatureKind::floating_point;
    case NodeType::boolean:
      return FeatureKind::boolean;
    case NodeType::enumeration:
      return FeatureKind::enumeration;
    case NodeType::string:
      return FeatureKind::string;
    case NodeType::command:
      return FeatureKind::command;
    default:
      return FeatureKind::string;
  }
}

bool is_exposed(NodeType type) {
  return type == NodeType::integer || type == NodeType::floating_point ||
         type == NodeType::boolean || type == NodeType::enumeration ||
         type == NodeType::string || type == NodeType::command;
}

std::uint64_t decode_unsigned(const std::vector<std::byte>& bytes, bool little_endian) {
  std::uint64_t value{};
  if (little_endian) {
    for (std::size_t index = bytes.size(); index > 0; --index) {
      value = (value << 8U) | std::to_integer<unsigned char>(bytes[index - 1]);
    }
  } else {
    for (const auto byte : bytes) {
      value = (value << 8U) | std::to_integer<unsigned char>(byte);
    }
  }
  return value;
}

std::vector<std::byte> encode_unsigned(std::uint64_t value, std::size_t length,
                                       bool little_endian) {
  std::vector<std::byte> bytes(length);
  for (std::size_t index = 0; index < length; ++index) {
    const auto target = little_endian ? index : length - index - 1;
    bytes[target] = static_cast<std::byte>(value & 0xffU);
    value >>= 8U;
  }
  return bytes;
}

}  // namespace

class NodeMap::Impl {
 public:
  static Result<std::shared_ptr<Impl>> create(std::string xml, PortRead read,
                                               PortWrite write) {
    if (!read || !write) {
      return Status{ErrorCode::invalid_argument,
                    "GenApi NodeMap requires port read and write callbacks"};
    }
    XmlParser parser(xml);
    auto root = parser.parse();
    if (!root) {
      return root.status();
    }
    auto implementation = std::shared_ptr<Impl>(new Impl(std::move(read), std::move(write)));
    auto status = implementation->collect(root.value());
    if (!status) {
      return status;
    }
    status = implementation->finalize();
    if (!status) {
      return status;
    }
    return implementation;
  }

  std::vector<FeatureInfo> features() const {
    std::lock_guard lock(mutex_);
    std::vector<FeatureInfo> result;
    result.reserve(feature_order_.size());
    for (const auto& name : feature_order_) {
      result.push_back(nodes_.at(name).info);
    }
    return result;
  }

  Result<FeatureInfo> feature_info(const std::string& name) const {
    std::lock_guard lock(mutex_);
    const auto found = nodes_.find(name);
    if (found == nodes_.end() || !found->second.exposed) {
      return Status{ErrorCode::not_found, "GenApi feature not found: " + name};
    }
    return found->second.info;
  }

  Result<FeatureValue> read(const std::string& name) const {
    std::lock_guard lock(mutex_);
    const auto found = nodes_.find(name);
    if (found == nodes_.end() || !found->second.exposed) {
      return Status{ErrorCode::not_found, "GenApi feature not found: " + name};
    }
    if (!readable(found->second.info.access)) {
      return Status{ErrorCode::access_denied, "GenApi feature is not readable: " + name};
    }
    if (found->second.type == NodeType::command) {
      return Status{ErrorCode::invalid_argument,
                    "GenApi command cannot be read: " + name};
    }
    return read_node(found->second, 0);
  }

  Status write(const std::string& name, const FeatureValue& value) {
    std::lock_guard lock(mutex_);
    const auto found = nodes_.find(name);
    if (found == nodes_.end() || !found->second.exposed) {
      return {ErrorCode::not_found, "GenApi feature not found: " + name};
    }
    const auto& node = found->second;
    if (!writable(node.info.access)) {
      return {ErrorCode::access_denied, "GenApi feature is not writable: " + name};
    }
    if (node.type == NodeType::command) {
      return {ErrorCode::invalid_argument,
              "use execute_command for GenApi command: " + name};
    }
    return write_feature_node(node, value);
  }

  Status execute(const std::string& name) {
    std::lock_guard lock(mutex_);
    const auto found = nodes_.find(name);
    if (found == nodes_.end() || !found->second.exposed) {
      return {ErrorCode::not_found, "GenApi command not found: " + name};
    }
    const auto& node = found->second;
    if (node.type != NodeType::command) {
      return {ErrorCode::invalid_argument, "GenApi feature is not a command: " + name};
    }
    if (!writable(node.info.access)) {
      return {ErrorCode::access_denied, "GenApi command is not executable: " + name};
    }
    return write_reference(node, FeatureValue{node.command_value}, 0);
  }

  bool contains(const std::string& name) const {
    std::lock_guard lock(mutex_);
    const auto found = nodes_.find(name);
    return found != nodes_.end() && found->second.exposed;
  }

 private:
  Impl(PortRead read, PortWrite write)
      : read_port_(std::move(read)), write_port_(std::move(write)) {}

  Status collect(const XmlElement& element) {
    auto type = node_type(element.name);
    if (type) {
      const auto name_attribute = element.attributes.find("Name");
      if (name_attribute == element.attributes.end() || name_attribute->second.empty()) {
        return {ErrorCode::invalid_argument,
                "GenApi " + element.name + " node is missing Name"};
      }
      Node node;
      node.name = name_attribute->second;
      node.type = type.value();
      node.exposed = is_exposed(node.type);
      node.info.name = node.name;
      node.info.display_name = child_text(element, "DisplayName");
      if (node.info.display_name.empty()) {
        node.info.display_name = node.name;
      }
      node.info.description = child_text(element, "ToolTip");
      if (node.info.description.empty()) {
        node.info.description = child_text(element, "Description");
      }
      node.info.unit = child_text(element, "Unit");
      node.info.kind = feature_kind(node.type);
      node.info.access = parse_access(child_text(element, "AccessMode"));
      const auto namespace_attribute = element.attributes.find("NameSpace");
      node.info.standard_feature =
          (namespace_attribute != element.attributes.end() &&
           namespace_attribute->second == "Standard") ||
          standard_name(node.name);
      node.info.minimum = parse_floating(child_text(element, "Min"));
      node.info.maximum = parse_floating(child_text(element, "Max"));
      node.info.increment = parse_floating(child_text(element, "Inc"));
      node.value_reference = child_text(element, "pValue");
      node.integer_value = parse_integer(child_text(element, "Value"));
      node.floating_value = parse_floating(child_text(element, "Value"));
      node.string_value = child_text(element, "Value");
      if (node.type == NodeType::enum_entry) {
        const auto symbolic = child_text(element, "Symbolic");
        if (!symbolic.empty()) {
          node.string_value = symbolic;
        }
      }
      node.address = parse_unsigned(child_text(element, "Address")).value_or(0);
      node.length = static_cast<std::size_t>(
          parse_unsigned(child_text(element, "Length")).value_or(0));
      node.little_endian = child_text(element, "Endianess") == "LittleEndian";
      node.signed_value = child_text(element, "Sign") == "Signed";
      node.on_value = parse_integer(child_text(element, "OnValue")).value_or(1);
      node.off_value = parse_integer(child_text(element, "OffValue")).value_or(0);
      node.command_value =
          parse_integer(child_text(element, "CommandValue")).value_or(1);
      for (const auto& value : element.children) {
        if (value.name == "pEnumEntry" && !value.text.empty()) {
          node.entry_references.push_back(value.text);
        }
      }
      if (!nodes_.emplace(node.name, node).second) {
        return {ErrorCode::already_exists, "duplicate GenApi node: " + node.name};
      }
      if (node.exposed && child_text(element, "Visibility") != "Invisible") {
        feature_order_.push_back(node.name);
      }
    }
    for (const auto& value : element.children) {
      auto status = collect(value);
      if (!status) {
        return status;
      }
    }
    return Status::success();
  }

  Status finalize() {
    if (nodes_.empty()) {
      return {ErrorCode::invalid_argument,
              "GenApi XML contains no supported feature nodes"};
    }
    for (auto& [name, node] : nodes_) {
      if ((node.type == NodeType::int_register ||
           node.type == NodeType::float_register ||
           node.type == NodeType::string_register) &&
          (node.length == 0 || node.length > 1024)) {
        return {ErrorCode::invalid_argument,
                "GenApi register has invalid Length: " + name};
      }
      if (node.exposed && node.info.access == AccessMode::unavailable &&
          !node.value_reference.empty()) {
        const auto target = nodes_.find(node.value_reference);
        if (target != nodes_.end()) {
          node.info.access = effective_access(target->second, 0);
        }
      }
      if (node.exposed && node.info.access == AccessMode::unavailable) {
        node.info.access = node.value_reference.empty() ? AccessMode::read_only
                                                        : AccessMode::unavailable;
      }
      if (node.type == NodeType::enumeration) {
        for (const auto& reference : node.entry_references) {
          const auto entry = nodes_.find(reference);
          if (entry == nodes_.end() || entry->second.type != NodeType::enum_entry ||
              !entry->second.integer_value) {
            return {ErrorCode::invalid_argument,
                    "GenApi Enumeration references invalid EnumEntry: " + reference};
          }
          const auto symbolic = entry->second.string_value.empty()
                                    ? entry->second.name
                                    : entry->second.string_value;
          node.info.enum_entries.push_back(symbolic);
        }
      }
    }
    return Status::success();
  }

  AccessMode effective_access(const Node& node, unsigned depth) const {
    if (depth > 32) {
      return AccessMode::unavailable;
    }
    if (node.info.access != AccessMode::unavailable) {
      return node.info.access;
    }
    const auto found = nodes_.find(node.value_reference);
    return found == nodes_.end() ? AccessMode::unavailable
                                 : effective_access(found->second, depth + 1);
  }

  Result<FeatureValue> read_node(const Node& node, unsigned depth) const {
    if (depth > 32) {
      return Status{ErrorCode::invalid_argument, "GenApi node reference cycle detected"};
    }
    switch (node.type) {
      case NodeType::integer:
        if (!node.value_reference.empty()) return read_reference(node, depth);
        if (node.integer_value) return FeatureValue{*node.integer_value};
        break;
      case NodeType::int_register:
        return read_integer_register(node);
      case NodeType::floating_point:
        if (!node.value_reference.empty()) {
          auto value = read_reference(node, depth);
          if (!value) return value.status();
          if (const auto* floating = std::get_if<double>(&value.value())) {
            return FeatureValue{*floating};
          }
          if (const auto* integer = std::get_if<std::int64_t>(&value.value())) {
            return FeatureValue{static_cast<double>(*integer)};
          }
          return Status{ErrorCode::invalid_argument,
                        "GenApi Float value reference is not numeric: " + node.name};
        }
        if (node.floating_value) return FeatureValue{*node.floating_value};
        break;
      case NodeType::float_register:
        return read_float_register(node);
      case NodeType::boolean: {
        auto value = read_reference(node, depth);
        if (!value) return value.status();
        const auto* integer = std::get_if<std::int64_t>(&value.value());
        if (integer == nullptr) {
          return Status{ErrorCode::invalid_argument,
                        "GenApi Boolean value reference is not integer: " + node.name};
        }
        return FeatureValue{*integer == node.on_value};
      }
      case NodeType::enumeration: {
        auto value = read_reference(node, depth);
        if (!value) return value.status();
        const auto* integer = std::get_if<std::int64_t>(&value.value());
        if (integer == nullptr) {
          return Status{ErrorCode::invalid_argument,
                        "GenApi Enumeration value reference is not integer: " + node.name};
        }
        for (const auto& reference : node.entry_references) {
          const auto& entry = nodes_.at(reference);
          if (entry.integer_value == *integer) {
            return FeatureValue{entry.string_value.empty() ? entry.name
                                                           : entry.string_value};
          }
        }
        return Status{ErrorCode::not_found,
                      "GenApi Enumeration register has no matching entry: " + node.name};
      }
      case NodeType::string:
        if (!node.value_reference.empty()) return read_reference(node, depth);
        return FeatureValue{node.string_value};
      case NodeType::string_register:
        return read_string_register(node);
      case NodeType::enum_entry:
        if (node.integer_value) return FeatureValue{*node.integer_value};
        break;
      case NodeType::command:
        break;
    }
    return Status{ErrorCode::unsupported, "unsupported GenApi node value: " + node.name};
  }

  Result<FeatureValue> read_reference(const Node& node, unsigned depth) const {
    const auto found = nodes_.find(node.value_reference);
    if (found == nodes_.end()) {
      return Status{ErrorCode::not_found,
                    "GenApi value reference not found: " + node.value_reference};
    }
    return read_node(found->second, depth + 1);
  }

  Result<FeatureValue> read_integer_register(const Node& node) const {
    if (node.length == 0 || node.length > sizeof(std::uint64_t)) {
      return Status{ErrorCode::unsupported,
                    "GenApi integer register length is unsupported: " + node.name};
    }
    std::vector<std::byte> bytes(node.length);
    auto status = read_port_(node.address, bytes.data(), bytes.size());
    if (!status) return status;
    auto value = decode_unsigned(bytes, node.little_endian);
    if (node.signed_value && node.length < sizeof(std::uint64_t) &&
        (value & (std::uint64_t{1} << (node.length * 8U - 1U))) != 0) {
      value |= (~std::uint64_t{0}) << (node.length * 8U);
    }
    return FeatureValue{static_cast<std::int64_t>(value)};
  }

  Result<FeatureValue> read_float_register(const Node& node) const {
    if (node.length != 4 && node.length != 8) {
      return Status{ErrorCode::unsupported,
                    "GenApi floating register must be 4 or 8 bytes: " + node.name};
    }
    std::vector<std::byte> bytes(node.length);
    auto status = read_port_(node.address, bytes.data(), bytes.size());
    if (!status) return status;
    const auto bits = decode_unsigned(bytes, node.little_endian);
    if (node.length == 4) {
      return FeatureValue{static_cast<double>(
          std::bit_cast<float>(static_cast<std::uint32_t>(bits)))};
    }
    return FeatureValue{std::bit_cast<double>(bits)};
  }

  Result<FeatureValue> read_string_register(const Node& node) const {
    std::vector<char> bytes(node.length);
    auto status = read_port_(node.address, bytes.data(), bytes.size());
    if (!status) return status;
    const auto end = std::find(bytes.begin(), bytes.end(), '\0');
    return FeatureValue{std::string(bytes.begin(), end)};
  }

  Status write_feature_node(const Node& node, const FeatureValue& value) {
    switch (node.type) {
      case NodeType::integer: {
        const auto* integer = std::get_if<std::int64_t>(&value);
        if (integer == nullptr) return type_error(node);
        auto status = validate_numeric(node, static_cast<double>(*integer));
        return status ? write_reference(node, value, 0) : status;
      }
      case NodeType::floating_point: {
        const auto* floating = std::get_if<double>(&value);
        if (floating == nullptr) return type_error(node);
        auto status = validate_numeric(node, *floating);
        return status ? write_reference(node, value, 0) : status;
      }
      case NodeType::boolean: {
        const auto* boolean = std::get_if<bool>(&value);
        if (boolean == nullptr) return type_error(node);
        return write_reference(
            node, FeatureValue{*boolean ? node.on_value : node.off_value}, 0);
      }
      case NodeType::enumeration: {
        const auto* symbolic = std::get_if<std::string>(&value);
        if (symbolic == nullptr) return type_error(node);
        for (const auto& reference : node.entry_references) {
          const auto& entry = nodes_.at(reference);
          const auto name = entry.string_value.empty() ? entry.name : entry.string_value;
          if (name == *symbolic) {
            return write_reference(node, FeatureValue{*entry.integer_value}, 0);
          }
        }
        return {ErrorCode::invalid_argument,
                "invalid GenApi Enumeration entry for " + node.name + ": " + *symbolic};
      }
      case NodeType::string: {
        if (std::get_if<std::string>(&value) == nullptr) return type_error(node);
        return write_reference(node, value, 0);
      }
      default:
        return {ErrorCode::unsupported, "unsupported GenApi write node: " + node.name};
    }
  }

  Status write_reference(const Node& node, const FeatureValue& value, unsigned depth) {
    if (depth > 32) {
      return {ErrorCode::invalid_argument, "GenApi node reference cycle detected"};
    }
    const auto found = nodes_.find(node.value_reference);
    if (found == nodes_.end()) {
      return {ErrorCode::not_found,
              "GenApi value reference not found: " + node.value_reference};
    }
    return write_node(found->second, value, depth + 1);
  }

  Status write_node(const Node& node, const FeatureValue& value, unsigned depth) {
    if (depth > 32) {
      return {ErrorCode::invalid_argument, "GenApi node reference cycle detected"};
    }
    if (!writable(effective_access(node, depth))) {
      return {ErrorCode::access_denied, "GenApi node is not writable: " + node.name};
    }
    if (!node.value_reference.empty()) {
      return write_reference(node, value, depth);
    }
    if (node.type == NodeType::int_register) {
      const auto* integer = std::get_if<std::int64_t>(&value);
      if (integer == nullptr) return type_error(node);
      const auto bytes = encode_unsigned(static_cast<std::uint64_t>(*integer), node.length,
                                         node.little_endian);
      return write_port_(node.address, bytes.data(), bytes.size());
    }
    if (node.type == NodeType::float_register) {
      const auto* floating = std::get_if<double>(&value);
      if (floating == nullptr) return type_error(node);
      const auto bits = node.length == 4
                            ? static_cast<std::uint64_t>(
                                  std::bit_cast<std::uint32_t>(static_cast<float>(*floating)))
                            : std::bit_cast<std::uint64_t>(*floating);
      const auto bytes = encode_unsigned(bits, node.length, node.little_endian);
      return write_port_(node.address, bytes.data(), bytes.size());
    }
    if (node.type == NodeType::string_register) {
      const auto* string = std::get_if<std::string>(&value);
      if (string == nullptr) return type_error(node);
      if (string->size() >= node.length) {
        return {ErrorCode::invalid_argument,
                "GenApi string exceeds register capacity: " + node.name};
      }
      std::vector<char> bytes(node.length, '\0');
      std::copy(string->begin(), string->end(), bytes.begin());
      return write_port_(node.address, bytes.data(), bytes.size());
    }
    return {ErrorCode::unsupported, "unsupported GenApi write target: " + node.name};
  }

  Status validate_numeric(const Node& node, double value) const {
    if (!std::isfinite(value)) {
      return {ErrorCode::invalid_argument,
              "GenApi numeric value is not finite: " + node.name};
    }
    if (node.info.minimum && value < *node.info.minimum) {
      return {ErrorCode::invalid_argument,
              "GenApi value is below minimum: " + node.name};
    }
    if (node.info.maximum && value > *node.info.maximum) {
      return {ErrorCode::invalid_argument,
              "GenApi value is above maximum: " + node.name};
    }
    if (node.info.increment && *node.info.increment > 0.0) {
      const auto origin = node.info.minimum.value_or(0.0);
      const auto steps = (value - origin) / *node.info.increment;
      if (std::abs(steps - std::round(steps)) > 1e-9) {
        return {ErrorCode::invalid_argument,
                "GenApi value does not match increment: " + node.name};
      }
    }
    return Status::success();
  }

  static Status type_error(const Node& node) {
    return {ErrorCode::invalid_argument,
            "GenApi feature value type does not match: " + node.name};
  }

  PortRead read_port_;
  PortWrite write_port_;
  std::unordered_map<std::string, Node> nodes_;
  std::vector<std::string> feature_order_;
  mutable std::mutex mutex_;
};

NodeMap::NodeMap(std::shared_ptr<Impl> implementation)
    : implementation_(std::move(implementation)) {}

Result<std::shared_ptr<NodeMap>> NodeMap::parse(std::string xml, PortRead read,
                                                PortWrite write) {
  auto implementation = Impl::create(std::move(xml), std::move(read), std::move(write));
  if (!implementation) {
    return implementation.status();
  }
  return std::shared_ptr<NodeMap>(new NodeMap(std::move(implementation).value()));
}

std::vector<FeatureInfo> NodeMap::features() const { return implementation_->features(); }

Result<FeatureInfo> NodeMap::feature_info(const std::string& name) const {
  return implementation_->feature_info(name);
}

Result<FeatureValue> NodeMap::read(const std::string& name) const {
  return implementation_->read(name);
}

Status NodeMap::write(const std::string& name, const FeatureValue& value) {
  return implementation_->write(name, value);
}

Status NodeMap::execute(const std::string& name) {
  return implementation_->execute(name);
}

bool NodeMap::contains(const std::string& name) const {
  return implementation_->contains(name);
}

}  // namespace univision::genapi
