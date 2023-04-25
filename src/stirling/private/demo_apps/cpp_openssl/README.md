# OpenSSL/BoringSSL client-server (C++)

To run, execute the following commands in two separate terminals:

`bazel run //demos/client_server_apps/cpp_tls/client:client`

`bazel run //demos/client_server_apps/cpp_tls/server:server`

These 2 binaries actually statically link borgingssl instead of OpenSSL.
Pixie's existing tracing does not work correctly with boringssl yet, because the data structure
internals are incompatible.
