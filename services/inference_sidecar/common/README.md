# Privacy Sandbox - Inference Common Libraries

`services/inference_sidecar/common` provides the shared libraries among B&A servers and inference
sidecars.

## How to build the inference common libraries

The directory has its own `WORKSPACE` file. You should change the current working directory before
running bazel.

```sh
cd services/inference_sidecar/common
./builders/tools/bazel-debian build //:inference_sidecar_bin
./builders/tools/bazel-debian test //...
```
