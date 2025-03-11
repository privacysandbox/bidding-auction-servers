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
"""Tests for sql_expr."""

import unittest
import sqlite3

from sql_expr import SqlExecutor, ExpressionEvaluator


class SqlExecutorTest(unittest.TestCase):
    """Tests for SqlExecutor."""

    def test_execute_in_db(self):
        """Tests execute_in_db"""
        executor = SqlExecutor()
        result = executor.execute_in_db("SELECT 1")
        self.assertEqual(result, [(1,)])

        result = executor.execute_in_db("CREATE TABLE Temp (rowid INTEGER) ")
        result = executor.execute_in_db(
            "INSERT INTO Temp(rowid) VALUES (1),(2),(3),(4)"
        )
        result = executor.execute_in_db("SELECT * FROM Temp ORDER BY (rowid)")
        self.assertEqual(result, [(1,), (2,), (3,), (4,)])


class ExpressionEvaluatorTest(unittest.TestCase):
    """Tests for ExpressionEvaluator."""

    def test_quote_variables(self):
        """Tests quote_variables."""
        self.assertEqual(ExpressionEvaluator.quote_variables("a"), "`a`")
        self.assertEqual(ExpressionEvaluator.quote_variables("a.b"), "`a.b`")
        self.assertEqual(ExpressionEvaluator.quote_variables("a_b"), "`a_b`")
        self.assertEqual(ExpressionEvaluator.quote_variables("a:b"), "`a:b`")
        self.assertEqual(ExpressionEvaluator.quote_variables("a1"), "`a1`")
        self.assertEqual(ExpressionEvaluator.quote_variables("abc.def"), "`abc.def`")
        self.assertEqual(
            ExpressionEvaluator.quote_variables("s:abc.def1"), "`s:abc.def1`"
        )
        self.assertEqual(
            ExpressionEvaluator.quote_variables("10 + 24 - s:abc.def1 * hello * 40"),
            "10 + 24 - `s:abc.def1` * `hello` * 40",
        )
        self.assertEqual(ExpressionEvaluator.quote_variables("1+a"), "1+`a`")

    def test_add_to_context_number(self):
        evaluator = ExpressionEvaluator(SqlExecutor())
        evaluator.add_to_context("a", 1)
        self.assertEqual(evaluator.get_value("a"), [(1.0,)])

    def test_add_to_context_expression(self):
        evaluator = ExpressionEvaluator(SqlExecutor())
        evaluator.add_to_context("a", 1)
        evaluator.add_to_context("b", "a + 1")
        self.assertEqual(evaluator.get_value("b"), [(2.0,)])

    def test_add_to_context_complex_expression(self):
        evaluator = ExpressionEvaluator(SqlExecutor())
        evaluator.add_to_context("a", 1)
        evaluator.add_to_context("b", 2)
        evaluator.add_to_context("c", "a + b * 2")
        self.assertEqual(evaluator.get_value("c"), [(5.0,)])

    def test_get_value_simple_expression(self):
        evaluator = ExpressionEvaluator(SqlExecutor())
        evaluator.add_to_context("a", 1)
        self.assertEqual(evaluator.get_value("a + 1"), [(2.0,)])

    def test_get_value_complex_expression(self):
        evaluator = ExpressionEvaluator(SqlExecutor())
        evaluator.add_to_context("abc", 1)
        evaluator.add_to_context("bbb", 2)
        self.assertEqual(evaluator.get_value("abc + bbb * 2"), [(5.0,)])
        self.assertEqual(evaluator.get_value("min(abc,bbb) * 2"), [(2.0,)])
        self.assertEqual(evaluator.get_value("abc*bbb/2 * 2"), [(2.0,)])
        self.assertEqual(evaluator.get_value("(50 + 2)*(abc+bbb)/2 * 2"), [(156.0,)])

    def test_get_value_with_undefined_variable(self):
        evaluator = ExpressionEvaluator(SqlExecutor())
        evaluator.add_to_context("a", 1)
        evaluator.add_to_context("b", 2)
        self.assertEqual(evaluator.get_value("a + b * 2"), [(5.0,)])
        with self.assertRaises(
            sqlite3.OperationalError
        ):  # Expect an error for undefined variable c
            evaluator.get_value("a + b * 2 + c")


if __name__ == "__main__":
    unittest.main()
