# Copyright 2024 Google LLC
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
"""Utils functions."""

import logging
import tempfile
import os
from pathlib import Path

_MISSING = object()
logger = logging.getLogger(__name__)


def read_from_yaml_or_die(yaml, heirarchy: list):
    """
    Traverses a YAML object (dict) following the provided hierarchy.
    Raises an exception if any key in the hierarchy is missing.

    Args:
      yaml: The YAML object to traverse.
      hierarchy: A list of keys representing the hierarchy to follow.

    Returns:
      The value at the end of the hierarchy.

    Raises:
      Exception: If any key in the hierarchy is missing.
    """
    cur = yaml
    for element in heirarchy:
        cur = cur.get(element, _MISSING)
        if cur is _MISSING:
            raise Exception(f"Model missing the following data hierarchy: {heirarchy}")
    return cur


def write_to_named_or_temp_file(filename_or_none, content, content_name):
    """Writes content to filename if given, or to a temp file."""
    filename = (
        filename_or_none if filename_or_none else tempfile.NamedTemporaryFile().name
    )
    log_and_get_file_path(filename, "Writing", content_name)
    with open(filename, "w") as file:
        file.write(content)


def log_and_get_file_path(filename: str, operation: str, content_name: str):
    """Gets a filepath (if given filename is absolute) or a filepath in the
    working directory (if given filename is not absolute) and creates a log entry
    at INFO level."""

    file_path = Path(filename)
    if not file_path.is_absolute():
        file_path = Path(os.getcwd()).joinpath(file_path)
    logger.info("%s %s file: %s", operation, content_name, file_path)
    return file_path


def dict_to_csv(content: dict) -> str:
    """Converts dict to a csv string, with one key-value per line."""
    rows = ""
    for key, value in content.items():
        rows += f"{key}, {value}\n"
    return rows
