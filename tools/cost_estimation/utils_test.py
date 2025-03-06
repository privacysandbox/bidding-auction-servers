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
"""Unit tests for utils."""
import unittest
from unittest.mock import patch, mock_open

from utils import (
    read_from_yaml_or_die,
    write_to_named_or_temp_file,
    dict_to_csv,
)


class TestReadFromYamlOrDie(unittest.TestCase):
    """Tests for read_from_yaml_or_die utils function."""

    def test_valid_hierarchy(self):
        yaml_data = {"a": {"b": {"c": 1}}}
        hierarchy = ["a", "b", "c"]
        result = read_from_yaml_or_die(yaml_data, hierarchy)
        self.assertEqual(result, 1)

    def test_missing_key(self):
        yaml_data = {"a": {"b": {"c": 1}}}
        hierarchy = ["a", "x", "c"]
        with self.assertRaisesRegex(
            Exception, r"Model missing the following data hierarchy: \['a', 'x', 'c'\]"
        ):
            read_from_yaml_or_die(yaml_data, hierarchy)

    def test_empty_hierarchy(self):
        yaml_data = {"a": {"b": {"c": 1}}}
        hierarchy = []
        result = read_from_yaml_or_die(yaml_data, hierarchy)
        self.assertEqual(result, yaml_data)

    def test_none_value(self):
        yaml_data = {"a": {"b": None}}
        hierarchy = ["a", "b"]
        result = read_from_yaml_or_die(yaml_data, hierarchy)
        self.assertIsNone(result)


class TestWriteToNamedOrTempFile(unittest.TestCase):
    """Tests the write_to_named_or_temp_file function."""

    @patch("builtins.open", new_callable=mock_open)
    def test_writes_to_named_file(self, mock_open_object):
        filename = "test_output.txt"
        content = "This is test content."
        content_name = "test content"
        write_to_named_or_temp_file(filename, content, content_name)
        mock_open_object.assert_called_once_with(filename, "w")
        mock_open_object().write.assert_called_once_with(content)

    @patch("builtins.open", new_callable=mock_open)
    def test_writes_to_temp_file(self, mock_open_object):
        filename = None
        content = "This is test content."
        content_name = "test content"
        write_to_named_or_temp_file(filename, content, content_name)
        mock_open_object.assert_called_once()
        mock_open_object().write.assert_called_once_with(content)


class TestDictToCsv(unittest.TestCase):
    """Tests the dict_to_csv function."""

    def test_dict_to_csv(self):
        data = {"name": "Alice", "age": 30, "city": "New York"}
        expected_csv = "name, Alice\nage, 30\ncity, New York\n"
        self.assertEqual(dict_to_csv(data), expected_csv)

    def test_dict_to_csv_empty(self):
        data = {}
        self.assertEqual(dict_to_csv(data), "")


if __name__ == "__main__":
    unittest.main()
