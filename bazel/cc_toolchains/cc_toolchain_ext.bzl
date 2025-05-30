load("//bazel/cc_toolchains:clang.bzl",    # ↙ existing repository_rule
     "clang_toolchain")
load("//bazel/cc_toolchains:gcc.bzl",      # ↙ ordinary BUILD-defined toolchain
     "gcc_x86_64_gnu")

def _impl(ctx):
    # --- Clang 15 variants -----------------------------------------------
    def _clang(name, target_arch, libc_version, use_for_host = False):
        ctx.call(                        # invoke the *repository_rule*
            clang_toolchain,
            name = name,
            repo_name = repo_name,
            toolchain_repo      = "com_llvm_clang_15",
            target_arch         = target_arch,
            clang_version       = "15.0.6",
            libc_version        = libc_version,
            host_arch           = "x86_64",
            host_libc_version   = "glibc_host",
            use_for_host_tools  = use_for_host,
        )
        ctx.register_toolchains("@%s//:toolchain" % name)

    _clang("clang-15.0-x86_64",                    "x86_64", "glibc_host")
    _clang("clang-15.0-x86_64-glibc2.36-sysroot",  "x86_64", "glibc2_36")
    _clang("clang-15.0-aarch64-glibc2.36-sysroot", "aarch64", "glibc2_36")
    # _clang("clang-15.0-exec",                      "x86_64", "glibc_host",
    #        use_for_host = True)

    # --- GCC host toolchain ----------------------------------------------
    #  gcc_x86_64_gnu() only **defines** the BUILD-file objects.
    #  We still need to expose them as a repo that the extension can return,
    #  so we synthesise a virtual repo that simply loads that BUILD file.
    # ctx.file("gcc/BUILD.bazel", 'load("//bazel/cc_toolchains:gcc.bzl", "gcc_x86_64_gnu"); gcc_x86_64_gnu()')
    # ctx.register_toolchains("@%s//gcc:cc-toolchain-gcc-x86_64-gnu"
    #                         % ctx.path("."))   # the “current” repo

cc_toolchain_ext = module_extension(
    implementation = _impl,
)

