# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

package(default_visibility = ["//visibility:public"])

exports_files([
    "models/tensorflow_1_mib_saved_model.pb",
    # This PyTorch model inputs a float and returns the same float.
    "models/pytorch_simple_model.pt",
    # This PyTorch model uses a realistic input/output format for e2e test flow.
    "models/pytorch_e2e_model.pt",
    # This pytorch model takes one int64 value and three floats as input. It
    # returns two tensors: a float embedding of size 4 and one int64 value.
    "models/pytorch_mixed_inputs_mixed_outputs_model.pt",
    # This PyTorch model returns the incrementing counter.
    "models/pytorch_stateful_model.pt",
    # This PyTorch model returns itself and is not freezable.
    "models/pytorch_unfreezable_model.pt",
    # This PyTorch model returns is already frozen.
    "models/pytorch_frozen_model.pt",
    # This PyTorch model performs external I/O from its submodule.
    "models/pytorch_submodule_model.pt",
    # This PyTorch model performs external I/O from an external function.
    "models/pytorch_external_function_model.pt",
    # This PyTorch model performs external I/O from an external class.
    "models/pytorch_external_class_model.pt",
    # This PyTorch model performs external I/O from nested blocks.
    "models/pytorch_nested_block_model.pt",
])
