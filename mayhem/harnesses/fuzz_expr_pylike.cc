// Fuzzes the Python-like expression parser/evaluator used for animation drivers
// (source/blender/blenlib/intern/expr_pylike_eval.cc).
#include <cmath>
#include <cstdint>
#include <string>

#include "BLI_expr_pylike_eval.hh"

namespace bl = blender;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if (size > 4096) {
    return -1;
  }
  const std::string expression(reinterpret_cast<const char *>(data), size);

  const char *param_names[] = {"x", "y", "t"};
  bl::ExprPyLike_Parsed *parsed = bl::BLI_expr_pylike_parse(expression.c_str(), param_names, 3);
  if (parsed == nullptr) {
    return -1;
  }
  if (!bl::BLI_expr_pylike_is_valid(parsed)) {
    bl::BLI_expr_pylike_free(parsed);
    return -1;
  }

  (void)bl::BLI_expr_pylike_is_constant(parsed);
  for (int i = 0; i < 3; i++) {
    (void)bl::BLI_expr_pylike_is_using_param(parsed, i);
  }

  const double params[3] = {1.5, -2.0, 0.25};
  double result = 0.0;
  (void)bl::BLI_expr_pylike_eval(parsed, params, 3, &result);

  const double zeros[3] = {0.0, 0.0, 0.0};
  (void)bl::BLI_expr_pylike_eval(parsed, zeros, 3, &result);

  bl::BLI_expr_pylike_free(parsed);
  return 0;
}
