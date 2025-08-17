#include "executorch_module_utils.h"

#include <sstream>
#include <executorch/runtime/core/span.h>
#include <executorch/runtime/core/tag.h>
#include <executorch/runtime/core/result.h>

namespace etx = ::executorch;
namespace ext = ::executorch::extension;
using etx::runtime::Result;
using etx::runtime::Tag;

namespace executorch_utils {

// ---------- helpers ----------

inline const char* scalar_type_to_string(etx::aten::ScalarType t) {
  using T = etx::aten::ScalarType;
  switch (t) {
    case T::Float: return "Float";
    case T::Half: return "Half";
    case T::BFloat16: return "BFloat16";
    case T::Byte: return "Byte";
    case T::Char: return "Char";
    case T::Short: return "Short";
    case T::Int: return "Int";
    case T::Long: return "Long";
    case T::Double: return "Double";
    case T::Bool: return "Bool";
    default: return "Unknown";
  }
}

inline std::string span_ints_to_string(etx::runtime::Span<const int32_t> s) {
  std::ostringstream oss; oss << "[";
  for (size_t i = 0; i < s.size(); ++i) {
    oss << s[i];
    if (i + 1 < s.size()) oss << ",";
  }
  oss << "]";
  return oss.str();
}

inline std::string span_bytes_to_string(etx::runtime::Span<const uint8_t> s) {
  std::ostringstream oss; oss << "[";
  for (size_t i = 0; i < s.size(); ++i) {
    oss << int(s[i]);
    if (i + 1 < s.size()) oss << ",";
  }
  oss << "]";
  return oss.str();
}

// ---------- main utility ----------

std::string dump_method_meta(ext::Module& module, const std::string& method_name) {
  auto metaR = module.method_meta(method_name);
  if (!metaR.ok()) return "Failed to get method_meta(" + method_name + ").";
  const auto& meta = *metaR;

  std::ostringstream out;
  out << "Method: " << meta.name() << "\n";

  out << "Inputs: " << meta.num_inputs() << "\n";
  for (size_t i = 0; i < meta.num_inputs(); ++i) {
    auto tagR = meta.input_tag(i);
    out << "  Input[" << i << "]: ";
    if (!tagR.ok()) { out << "<tag error>\n"; continue; }
    auto tag = *tagR;
    out << "tag=" << tag_to_string(tag);
    if (tag == Tag::Tensor) {
      auto tinfR = meta.input_tensor_meta(i);
      if (!tinfR.ok()) { out << " <tensor meta error>\n"; continue; }
      const auto& ti = *tinfR;
      out << " dtype=" << scalar_type_to_string(ti.scalar_type())
          << " sizes=" << span_ints_to_string(ti.sizes())
          << " dim_order=" << span_bytes_to_string(ti.dim_order())
          << " nbytes=" << ti.nbytes()
          << " planned=" << (ti.is_memory_planned() ? "true" : "false");
      auto name = ti.name();
      if (!name.empty()) out << " name=" << name;
    }
    out << "\n";
  }

  out << "Outputs: " << meta.num_outputs() << "\n";
  for (size_t i = 0; i < meta.num_outputs(); ++i) {
    auto tagR = meta.output_tag(i);
    out << "  Output[" << i << "]: ";
    if (!tagR.ok()) { out << "<tag error>\n"; continue; }
    auto tag = *tagR;
    out << "tag=" << tag_to_string(tag);
    if (tag == Tag::Tensor) {
      auto tinfR = meta.output_tensor_meta(i);
      if (!tinfR.ok()) { out << " <tensor meta error>\n"; continue; }
      const auto& ti = *tinfR;
      out << " dtype=" << scalar_type_to_string(ti.scalar_type())
          << " sizes=" << span_ints_to_string(ti.sizes())
          << " dim_order=" << span_bytes_to_string(ti.dim_order())
          << " nbytes=" << ti.nbytes()
          << " planned=" << (ti.is_memory_planned() ? "true" : "false");
      auto name = ti.name();
      if (!name.empty()) out << " name=" << name;
    }
    out << "\n";
  }

  out << "Attributes: " << meta.num_attributes() << "\n";
  for (size_t i = 0; i < meta.num_attributes(); ++i) {
    auto ar = meta.attribute_tensor_meta(i);
    if (!ar.ok()) { out << "  Attr[" << i << "]: <meta error>\n"; continue; }
    const auto& ti = *ar;
    out << "  Attr[" << i << "]: dtype=" << scalar_type_to_string(ti.scalar_type())
        << " sizes=" << span_ints_to_string(ti.sizes())
        << " dim_order=" << span_bytes_to_string(ti.dim_order())
        << " nbytes=" << ti.nbytes()
        << " planned=" << (ti.is_memory_planned() ? "true" : "false");
    auto name = ti.name();
    if (!name.empty()) out << " name=" << name;
    out << "\n";
  }

  out << "Backends: " << meta.num_backends() << "\n";
  for (size_t i = 0; i < meta.num_backends(); ++i) {
    auto br = meta.get_backend_name(i);
    out << "  [" << i << "]: " << (br.ok() ? *br : "<invalid>") << "\n";
  }

  out << "MemoryPlannedBuffers: " << meta.num_memory_planned_buffers() << "\n";
  for (size_t i = 0; i < meta.num_memory_planned_buffers(); ++i) {
    auto sz = meta.memory_planned_buffer_size(i);
    out << "  [" << i << "]: " << (sz.ok() ? std::to_string(*sz) : "<error>") << " bytes\n";
  }

  return out.str();
}

} // namespace executorch_utils
