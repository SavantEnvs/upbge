// Fuzzes Blender's PLY importer front half: header parsing + ASCII/binary element decoding.
// Mirrors source/blender/io/ply/tests/io_ply_importer_test.cc (read_header -> import_ply_data).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <unistd.h>

#include "CLG_log.h"
#include "ply_import.hh"
#include "ply_import_buffer.hh"
#include "ply_import_data.hh"

namespace ply = blender::io::ply;

static int g_fd = -1;
static std::string g_path;

extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
  /* The importer logs through CLOG, which aborts unless CLG_init() ran first (clog.cc). Log output
   * goes to /dev/null so the warning paths don't flood stderr; the context is never changed per input. */
  CLG_init();
  FILE *devnull = fopen("/dev/null", "w");
  if (devnull) {
    CLG_output_set(devnull);
  }
  const char *tmp = getenv("TMPDIR");
  g_path = std::string(tmp && *tmp ? tmp : "/tmp") + "/fuzz_ply_XXXXXX";
  g_fd = mkstemp(g_path.data());
  if (g_fd < 0) {
    perror("mkstemp");
    abort();
  }
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if (ftruncate(g_fd, 0) != 0 || (size && pwrite(g_fd, data, size, 0) != ssize_t(size))) {
    abort();
  }

  /* The importer reports malformed files by throwing std::exception (ply_import_buffer.cc); its only
   * caller, read_ply_to_data() in ply_import.cc, catches it, so an exception is the documented
   * "bad file" outcome, not a crash. */
  try {
    ply::PlyReadBuffer file(g_path.c_str());
    ply::PlyHeader header;
    if (ply::read_header(file, header) != nullptr) {
      return -1;
    }
    std::unique_ptr<ply::PlyData> ply_data = ply::import_ply_data(file, header);
    (void)ply_data;
  }
  catch (const std::exception &) {
    return -1;
  }
  return 0;
}
