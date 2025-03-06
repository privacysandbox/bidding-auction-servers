# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# stage: runtime
#
FROM python:3.13-slim-bookworm AS runtime
LABEL org.opencontainers.image.title="Bidding & Auction Cost Tool"
LABEL org.opencontainers.image.description="Cloud cost estimator tool for the Bidding & Auction system"
WORKDIR /app
COPY requirements_lock.txt /app
RUN python3 -m venv /app/venv \
  && bash -c "source /app/venv/bin/activate && pip install --no-cache-dir -r requirements_lock.txt"
COPY *.py /app/
COPY cost.yaml /app/
COPY sku.json /app/
ENTRYPOINT ["/app/venv/bin/python", "main.py"]
