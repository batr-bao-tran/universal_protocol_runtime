"""Bazel rules for UPR schema libraries."""

def upr_schema_library(name, src, deps = [], visibility = None):
    native.filegroup(
        name = name,
        srcs = [src] + deps,
        visibility = visibility,
    )
