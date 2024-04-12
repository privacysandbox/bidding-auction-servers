# Privacy Sandbox - PyTorch Sidecar

`services/inference_sidecar/modules/pytorch_v2_1_1` provides the PyTorch specific code to build the
inference sidecar with PyTorch version 2.1.1 as the inference backend.

## How to build the PyTorch sidecar binary

The directory has its own `WORKSPACE` file. You should change the current working directory before
running bazel.

```sh
cd services/inference_sidecar/modules/pytorch_v2_1_1
./builders/tools/bazel-debian build //:inference_sidecar_bin
./builders/tools/bazel-debian test //:pytorch_test
```

To enable packaging of the inference sidecar with the bidding server, we need to generate the
inference sidecar binary to a local artifact directory. Specifically, we need to run this following
command before `build_and_test_all_in_docker`.

```sh
cd services/inference_sidecar/modules/pytorch_v2_1_1
./builders/tools/bazel-debian run //:generate_artifacts
```
