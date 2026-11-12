// Fuzzes the DNA type-description ("SDNA") decoder that every .blend file carries in its DNA1
// block (source/blender/makesdna/intern/dna_genfile.cc), plus the queries and the struct
// reconstruction machinery built on a decoded SDNA.
#include <cstdint>
#include <cstdlib>
#include <memory>

#include "MEM_guardedalloc.h"

#include "DNA_genfile.h"
#include "DNA_sdna_types.h"

namespace bl = blender;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if (size > (1 << 20)) {
    return -1;
  }
  const char *error = nullptr;
  std::unique_ptr<bl::SDNA> sdna = bl::DNA_sdna_from_data(data, int(size), true, true, &error);
  if (!sdna) {
    return -1;
  }

  const int structs_num = int(sdna->structs.size());
  for (int i = 0; i < structs_num; i++) {
    const int struct_size = bl::DNA_struct_size(sdna.get(), i);
    (void)bl::DNA_struct_alignment(sdna.get(), i);
    const bl::StringRef name = bl::DNA_struct_identifier(sdna.get(), i);
    (void)bl::DNA_struct_find_with_alias(sdna.get(), name);
    (void)bl::DNA_struct_find_index_without_alias(sdna.get(), name);
    const bl::SDNA_Struct *s = sdna->structs[i];
    for (int m = 0; m < s->members_num; m++) {
      (void)bl::DNA_struct_member_size(sdna.get(), s->members[m].type_index, s->members[m].member_index);
    }
    (void)struct_size;
  }

  const char *compare_flags = bl::DNA_struct_get_compareflags(sdna.get(), sdna.get());
  if (compare_flags == nullptr) {
    return 0;
  }
  bl::DNA_ReconstructInfo *info = bl::DNA_reconstruct_info_create(
      sdna.get(), sdna.get(), compare_flags);
  for (int i = 0; i < structs_num && i < 64; i++) {
    const int struct_size = bl::DNA_struct_size(sdna.get(), i);
    if (struct_size <= 0 || struct_size > (1 << 16)) {
      continue;
    }
    void *blocks = calloc(2, size_t(struct_size));
    void *out = bl::DNA_struct_reconstruct(info, i, 2, blocks, "fuzz_sdna");
    if (out) {
      MEM_delete_void(out);
    }
    free(blocks);
  }
  bl::DNA_reconstruct_info_free(info);
  MEM_delete(compare_flags);
  return 0;
}
