""""""

# Thanks to b7r6: https://gist.github.com/b7r6/316d18949ad508e15243ed4aa98c80d4

def _pkg_config_impl(ctx):
    alias = ctx.attr.alias
    pkg_name = ctx.attr.pkg_name

    result = ctx.execute([
        "pkg-config",
        "--cflags",
        "--libs",
        "--static",
        pkg_name,
    ])
    if result.return_code != 0:
        fail("pkg-config failed for %s: %s" % (pkg_name, result.stderr))

    flags_includes = []
    flags_libs = []
    flags_lib_dirs = []
    flags_defines = []
    flags_other = []
    flags_linkopts = []

    for flag in result.stdout.strip().split(" "):
        if not flag:
            continue

        if flag.startswith("-I"):
            val = flag[2:]
            flags_includes.append(val)
            continue

        if flag.startswith("-L"):
            val = flag[2:]
            flags_lib_dirs.append(val)
            continue

        if flag.startswith("-l"):
            val = flag[2:]
            flags_libs.append(val)
            continue

        if flag.startswith("-D"):
            val = flag[2:]
            flags_defines.append(val)
            continue

        flags_other.append(flag)

    ctx.execute(["rm", "-rf", "pkg_config_include"])
    ctx.execute(["mkdir", "-p", "pkg_config_include"])

    for dir in flags_includes:
        result = ctx.execute(["cp", "-aT", dir, "pkg_config_include"])

    lib_files = []
    found_libs = {}

    for i, lib_dir in enumerate(flags_lib_dirs):
        lib_dir_name = "lib_%d" % i
        ctx.symlink(lib_dir, lib_dir_name)

        for lib in flags_libs:
            if lib in found_libs:
                continue

            for ext in [".a", ".so"]:
                lib_path = "%s/lib%s%s" % (lib_dir_name, lib, ext)
                if ctx.path(lib_path).exists:
                    lib_files.append(lib_path)
                    found_libs[lib] = True
                    break

    for lib in flags_libs:
        if lib not in found_libs:
            flags_linkopts.append("-l" + lib)

    build_content = '''
cc_library(
    name = "%s",
    srcs = %s,
    hdrs = glob([
        "pkg_config_include/**/*.h",
    ]),
    includes = ["pkg_config_include"],
    linkopts = %s,
    defines = %s,
    visibility = ["//visibility:public"],
)
''' % (
        alias,
        repr(lib_files),
        repr(flags_linkopts + flags_other),
        repr(flags_defines),
    )

    ctx.file("BUILD.bazel", build_content)

pkg_config_repository = repository_rule(
    implementation = _pkg_config_impl,
    environ = ["PKG_CONFIG_PATH"],
    local = True,
    attrs = {
        "alias": attr.string(mandatory = False),
        "pkg_name": attr.string(mandatory = True),
    },
)

def _pkg_extension_impl(mctx):
    for mod in mctx.modules:
        for dep in mod.tags.dep:
            pkg_config_repository(
                name = dep.name,
                alias = dep.alias or dep.pkg_name,
                pkg_name = dep.pkg_name,
            )

dep = tag_class(attrs = {
    "name": attr.string(mandatory = True),
    "alias": attr.string(),
    "pkg_name": attr.string(mandatory = True),
})

pkg = module_extension(
    implementation = _pkg_extension_impl,
    tag_classes = {"dep": dep},
)
