load("//tools:upr_schema_defs.bzl", "upr_schema_library")

package(default_visibility = ["//visibility:public"])

# Raw schema files referenced by label from the TypeScript and Python example
# codegen, which feed them directly to upr-gen.
exports_files(glob([
    "*.upr",
    "*.yaml",
]))

upr_schema_library(
    name = "market_data_schema",
    src = "market_data.upr",
    deps = ["order_types.upr"],
)

upr_schema_library(
    name = "market_data_yaml_schema",
    src = "market_data.yaml",
    deps = ["order_types.yaml"],
)

upr_schema_library(
    name = "advanced_market_data_schema",
    src = "advanced_market_data.upr",
)

upr_schema_library(
    name = "advanced_market_data_yaml_schema",
    src = "advanced_market_data.yaml",
)

upr_schema_library(
    name = "hardware_telemetry_schema",
    src = "hardware_telemetry.upr",
)

upr_schema_library(
    name = "hardware_telemetry_yaml_schema",
    src = "hardware_telemetry.yaml",
)
