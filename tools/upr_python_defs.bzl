"""Bazel macro for native-backed Python protocol bindings."""

load("@pybind11_bazel//:build_defs.bzl", "pybind_extension")
load("@rules_python//python:py_library.bzl", "py_library")

def upr_python_bindings(name, schema, module_name, schema_deps = [], visibility = None):
    """Generates a native-backed Python protocol module from a schema.

    Emits the `<module_name>` facade, compiles the generated pybind11 codec into
    the `_<module_name>_native` extension, and bundles both in the `name`
    py_library. Consumers depend on `name` and `import generated.<module_name>`.

    Args:
      name: Name of the generated py_library target.
      schema: Schema file (`.upr` or `.yaml`) to generate bindings from.
      module_name: Python module name for the generated facade and native codec.
      schema_deps: Additional schema files imported by `schema`.
      visibility: Visibility of the generated py_library target.
    """
    gen = "%s_gen" % name
    ext = "_%s_native" % module_name
    py_out = "generated/%s.py" % module_name
    cpp_out = "generated/_%s_native.cpp" % module_name
    hpp_out = "generated/%s_native.hpp" % module_name
    header_include = "%s/generated/%s_native.hpp" % (native.package_name(), module_name)

    native.genrule(
        name = gen,
        # MODULE.bazel anchors the workspace root so upr-gen can resolve
        # workspace-relative schema imports (e.g. examples/schema/*.upr).
        srcs = [schema] + schema_deps + ["//:MODULE.bazel"],
        outs = [py_out, cpp_out, hpp_out],
        cmd = "$(execpath //packages/upr_codegen:generate_bindings)" +
              " --lang python" +
              " --input $(execpath %s)" % schema +
              " --output $(RULEDIR)/%s" % py_out +
              " --native-output $(RULEDIR)/%s" % cpp_out +
              " --native-header-output $(RULEDIR)/%s" % hpp_out +
              " --native-header-include %s" % header_include +
              " --module-name %s" % module_name,
        tools = ["//packages/upr_codegen:generate_bindings"],
    )

    pybind_extension(
        name = ext,
        srcs = [cpp_out, hpp_out],
        copts = ["-std=c++20"],
        deps = ["//packages/upr_runtime"],
    )

    py_library(
        name = name,
        srcs = [py_out],
        imports = ["."],
        deps = [
            ":" + ext,
            "//packages/upr_python:universal_protocol_runtime",
        ],
        visibility = visibility,
    )
