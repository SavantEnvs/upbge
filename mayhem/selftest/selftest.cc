// Known-answer checks over the Blender code the fuzz targets exercise: the PLY importer front
// half, the pylike expression evaluator and the SDNA decoder. Built with normal flags (no
// sanitizers); mayhem/test.sh runs it and diffs its stdout against mayhem/selftest/expected.txt.
// Run from the repo root (fixtures are read from mayhem/*/testsuite).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "BLI_expr_pylike_eval.hh"
#include "DNA_genfile.h"
#include "DNA_sdna_types.h"
#include "ply_import.hh"
#include "ply_import_buffer.hh"
#include "ply_import_data.hh"

namespace bl = blender;
namespace ply = blender::io::ply;

static void report(const char *name, bool ok, const char *detail = "")
{
  if (ok) {
    printf("PASS %s\n", name);
  }
  else {
    printf("FAIL %s %s\n", name, detail);
  }
}

static std::unique_ptr<ply::PlyData> load_ply(const char *path, bool *header_ok)
{
  ply::PlyReadBuffer file(path);
  ply::PlyHeader header;
  *header_ok = ply::read_header(file, header) == nullptr;
  if (!*header_ok) {
    return nullptr;
  }
  return ply::import_ply_data(file, header);
}

static void test_ply()
{
  bool ok = false;
  auto cube = load_ply("mayhem/fuzz_ply/testsuite/cube_ascii.ply", &ok);
  report("ply.cube_ascii.header", ok);
  if (cube) {
    report("ply.cube_ascii.counts",
           cube->vertices.size() == 24 && cube->face_sizes.size() == 6 &&
               cube->face_vertices.size() == 24 && cube->edges.size() == 0 &&
               cube->error.empty());
    report("ply.cube_ascii.vertex0",
           cube->vertices.size() > 0 && cube->vertices[0].x == 1.0f &&
               cube->vertices[0].y == 1.0f && cube->vertices[0].z == -1.0f);
    report("ply.cube_ascii.normal0",
           cube->vertex_normals.size() == 24 && cube->vertex_normals[0].z == -1.0f);
    report("ply.cube_ascii.uv1",
           cube->uv_coordinates.size() == 24 && std::fabs(cube->uv_coordinates[1].x - 0.30085f) < 1e-5f &&
               cube->uv_coordinates[1].y == 1.0f);
    report("ply.cube_ascii.color0",
           cube->vertex_colors.size() == 24 && cube->vertex_colors[0].x == 1.0f &&
               cube->vertex_colors[0].z == 0.0f && cube->vertex_colors[0].w == 1.0f);
  }

  auto wire = load_ply("mayhem/fuzz_ply/testsuite/ASCII_wireframe_cube.ply", &ok);
  report("ply.wireframe.header", ok);
  if (wire) {
    report("ply.wireframe.counts",
           wire->vertices.size() == 8 && wire->edges.size() == 12 && wire->face_sizes.size() == 0);
    report("ply.wireframe.vertex7",
           wire->vertices.size() == 8 && wire->vertices[7].x == 1.0f && wire->vertices[7].y == 1.0f &&
               wire->vertices[7].z == 1.0f);
    report("ply.wireframe.edge0", wire->edges.size() == 12 && wire->edges[0].first == 2 &&
                                      wire->edges[0].second == 0);
  }

  load_ply("mayhem/fuzz_sdna/testsuite/sdna_64bit_min.bin", &ok);
  report("ply.not_a_ply_rejected", !ok);
}

static bool eval_ok(const char *expr, const char **names, int n, const double *params,
                    double expected)
{
  bl::ExprPyLike_Parsed *p = bl::BLI_expr_pylike_parse(expr, names, n);
  bool ok = bl::BLI_expr_pylike_is_valid(p);
  double result = 0;
  if (ok) {
    ok = bl::BLI_expr_pylike_eval(p, params, n, &result) == bl::EXPR_PYLIKE_SUCCESS &&
         result == expected;
  }
  bl::BLI_expr_pylike_free(p);
  return ok;
}

static bool parse_fails(const char *expr)
{
  bl::ExprPyLike_Parsed *p = bl::BLI_expr_pylike_parse(expr, nullptr, 0);
  const bool bad = !bl::BLI_expr_pylike_is_valid(p);
  bl::BLI_expr_pylike_free(p);
  return bad;
}

static bool eval_status(const char *expr, double x, bl::eExprPyLike_EvalStatus status)
{
  const char *names[] = {"x"};
  bl::ExprPyLike_Parsed *p = bl::BLI_expr_pylike_parse(expr, names, 1);
  double result = 0;
  const bool ok = bl::BLI_expr_pylike_is_valid(p) &&
                  bl::BLI_expr_pylike_eval(p, &x, 1, &result) == status;
  bl::BLI_expr_pylike_free(p);
  return ok;
}

static void test_expr()
{
  const char *names[] = {"x", "y"};
  const double p1[] = {3.0, 4.0};
  const double p2[] = {3.0, 7.0};
  report("expr.linear", eval_ok("x*2+y", names, 2, p1, 10.0));
  report("expr.minmax", eval_ok("max(x, y) - min(x, y)", names, 2, p2, 4.0));
  report("expr.ternary", eval_ok("x if x > y else y", names, 2, p2, 7.0));
  report("expr.pow_sqrt", eval_ok("sqrt(x*x + y*y)", names, 2, p1, 5.0));
  report("expr.chain_compare", eval_ok("1 if 0 < x < y else 0", names, 2, p1, 1.0));
  report("expr.const_fold", eval_ok("2*3+4", nullptr, 0, nullptr, 10.0));

  bl::ExprPyLike_Parsed *c = bl::BLI_expr_pylike_parse("2*3+4", nullptr, 0);
  report("expr.is_constant", bl::BLI_expr_pylike_is_constant(c));
  bl::BLI_expr_pylike_free(c);
  bl::ExprPyLike_Parsed *u = bl::BLI_expr_pylike_parse("y", names, 2);
  report("expr.param_usage",
         !bl::BLI_expr_pylike_is_using_param(u, 0) && bl::BLI_expr_pylike_is_using_param(u, 1));
  bl::BLI_expr_pylike_free(u);

  report("expr.err_div_zero", eval_status("1/x", 0.0, bl::EXPR_PYLIKE_DIV_BY_ZERO));
  report("expr.err_math", eval_status("sqrt(x)", -1.0, bl::EXPR_PYLIKE_MATH_ERROR));
  report("expr.parse_fail",
         parse_fails("") && parse_fails("0x0") && parse_fails("1 +") && parse_fails("(1+2") &&
             parse_fails("sqrt(1,2)"));
}

static bool read_file(const char *path, std::vector<char> *out)
{
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return false;
  }
  out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

static void test_sdna()
{
  struct Case {
    const char *file;
    int list_size;
    int id_size;
    int structs;
  };
  const Case cases[] = {
      {"mayhem/fuzz_sdna/testsuite/sdna_64bit_mesh.bin", 16, 88, 5},
      {"mayhem/fuzz_sdna/testsuite/sdna_32bit_mesh.bin", 8, 80, 5},
      {"mayhem/fuzz_sdna/testsuite/sdna_64bit_min.bin", 16, 88, 4},
  };
  for (const Case &c : cases) {
    std::vector<char> blob;
    if (!read_file(c.file, &blob)) {
      report(c.file, false, "cannot read fixture");
      continue;
    }
    const char *error = nullptr;
    std::unique_ptr<bl::SDNA> sdna = bl::DNA_sdna_from_data(blob.data(), int(blob.size()), true, true, &error);
    report(c.file, sdna != nullptr && error == nullptr);
    if (!sdna) {
      continue;
    }
    const int list = bl::DNA_struct_find_with_alias(sdna.get(), "ListBase");
    const int id = bl::DNA_struct_find_with_alias(sdna.get(), "ID");
    printf("sdna structs=%d listbase_size=%d id_size=%d\n", int(sdna->structs.size()),
           list >= 0 ? bl::DNA_struct_size(sdna.get(), list) : -1,
           id >= 0 ? bl::DNA_struct_size(sdna.get(), id) : -1);
    report("sdna.structs_num", int(sdna->structs.size()) == c.structs);
    report("sdna.listbase_size", list >= 0 && bl::DNA_struct_size(sdna.get(), list) == c.list_size);
    report("sdna.id_size", id >= 0 && bl::DNA_struct_size(sdna.get(), id) == c.id_size);
    report("sdna.missing_struct", bl::DNA_struct_find_with_alias(sdna.get(), "NoSuchStruct") == -1);
  }

  std::vector<char> blob;
  read_file(cases[0].file, &blob);
  std::vector<char> bad(blob.begin(), blob.begin() + blob.size() / 2);
  const char *error = nullptr;
  std::unique_ptr<bl::SDNA> truncated = bl::DNA_sdna_from_data(bad.data(), int(bad.size()), true, true, &error);
  report("sdna.truncated_rejected", truncated == nullptr && error != nullptr);
}

int main()
{
  test_ply();
  test_expr();
  test_sdna();
  return 0;
}
