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
"""Module for sql based execution and expression evaluation."""

import sqlite3
import tempfile
import re
import numbers
import logging

logger = logging.getLogger(__name__)


class SqlExecutor:
    """Executes sql statements statefully in a db"""

    def __init__(self):
        self._db_file_name = tempfile.NamedTemporaryFile().name
        self._db = sqlite3.connect(self._db_file_name)
        logger.info("Sqlite db name: %s", self._db_file_name)

    def execute_in_db(self, query: str, *params):
        cur = self._db.cursor()
        res = cur.execute(query, params)
        return res.fetchall()

    def get_db_file_name(self):
        return self._db_file_name

    def __del__(self):
        self._db.commit()
        self._db.close()


class ExpressionEvaluator:
    """Evaluates expressions that possibly use variables defined earlier"""

    def __init__(self, sql_executor: SqlExecutor):
        self._sql_executor = sql_executor
        self._defined_columns = {"rowid"}
        self._sql_executor.execute_in_db("CREATE TABLE CostComputations(rowid INTEGER)")
        self._sql_executor.execute_in_db(
            "INSERT INTO CostComputations(rowid) values (0)"
        )

    @staticmethod
    def quote_variables(inp: str):
        """quotes variables so they can be a column name in sql"""
        return re.sub(r"([A-Za-z][A-Za-z0-9_.:]*)", "`\\1`", inp)

    def add_to_context(self, name: str, value_expr: str):
        """Add a variable (id) to the context. The value can be a number or an expression.
        If the value is an expression, it is computed immediately and the resulting value is saved.
        Subsequent calls can use this computed value."""
        quoted_name = ExpressionEvaluator.quote_variables(name)
        if isinstance(value_expr, numbers.Real):
            quoted_value_expr = value_expr  # no need to quote numbers
        else:
            # quote variables if it's not a number
            quoted_value_expr = ExpressionEvaluator.quote_variables(value_expr)
        if quoted_name not in self._defined_columns:
            self._sql_executor.execute_in_db(
                f"ALTER TABLE CostComputations ADD {quoted_name} FLOAT"
            )
            self._defined_columns.add(quoted_name)
        self._sql_executor.execute_in_db(
            f"UPDATE CostComputations SET {quoted_name} = {quoted_value_expr}"
        )

    def get_value(self, expression: str):
        """Computes the value of an expression, given all of the variables that
        are already defined."""
        quoted_expression = ExpressionEvaluator.quote_variables(expression)
        return self._sql_executor.execute_in_db(
            f"SELECT {quoted_expression} as result from CostComputations"
        )
