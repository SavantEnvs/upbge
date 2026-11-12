// Stand-in for the one BKE symbol the PLY importer links against. Same rule as
// source/blender/blenkernel/intern/attribute_access.cc, whose TU drags in the whole
// geometry-function library through its static initializers.
#include "BKE_customdata.hh"
#include "BLI_string_ref.hh"

namespace blender::bke {

bool allow_procedural_attribute_access(StringRef attribute_name)
{
  if (attribute_name.startswith(".corner") || attribute_name.startswith(".edge") ||
      attribute_name.startswith(".select") || attribute_name.startswith(".sculpt") ||
      attribute_name.startswith(".hide") || attribute_name.startswith(".uv") ||
      attribute_name == ".reference_index" || attribute_name.startswith("." UV_PINNED_NAME "."))
  {
    return false;
  }
  return true;
}

}  // namespace blender::bke
