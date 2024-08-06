load("@cddl_crate_index//:defs.bzl", cddl_aliases = "aliases", cddl_all_create_deps = "all_crate_deps")
load("@rules_rust//rust:defs.bzl", "rust_library", "rust_shared_library")

package(default_visibility = ["//visibility:private"])

licenses(["notice"])

exports_files(["LICENSE"])

rust_library(
    name = "cddl_src_lib",
    srcs = glob([
        "src/*.rs",
        "src/**/*.rs",
    ]),
    aliases = cddl_aliases(),
    crate_features = [
        "std",
        "ast-span",
        "ast-comments",
        "json",
        "cbor",
        "additional-controls",
        "ast-parent",
    ],
    proc_macro_deps = cddl_all_create_deps(
        package_name = "third_party",
        proc_macro = True,
    ),
    visibility = ["//visibility:public"],
    deps = cddl_all_create_deps(package_name = "third_party"),
)

rust_shared_library(
    name = "cddl",
    srcs = glob([
        "src/lib.rs",
        "src/**/*.rs",
    ]),
    crate_features = [
        "std",
        "ast-span",
        "ast-comments",
        "json",
        "cbor",
        "additional-controls",
        "ast-parent",
    ],
    proc_macro_deps = cddl_all_create_deps(
        package_name = "third_party",
        proc_macro = True,
    ),
    visibility = ["//visibility:public"],
    deps = cddl_all_create_deps(package_name = "third_party") + [
        ":cddl_src_lib",
    ],
)
