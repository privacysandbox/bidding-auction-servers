# Privacy Sandbox - Tensorflow Sidecar

`services/inference_sidecar/modules/tensorflow_v2_14_0` provides the Tensorflow specific code to
build the inference sidecar with Tensorflow version 2.14.0 as the inference backend.

## How to build the Tensorflow sidecar binary

The directory has its own `WORKSPACE` file. You should change the current working directory before
running bazel.

```sh
cd services/inference_sidecar/modules/tensorflow_v2_14_0
./builders/tools/bazel-debian build //:inference_sidecar_bin
./builders/tools/bazel-debian test //:tensorflow_test
```

To enable packaging of the inference sidecar with the bidding server, we need to generate the
inference sidecar binary to a local artifact directory. Specifically, we need to run this following
command before `build_and_test_all_in_docker`.

```sh
cd services/inference_sidecar/modules/tensorflow_v2_14_0
./builders/tools/bazel-debian run //:generate_artifacts
```
