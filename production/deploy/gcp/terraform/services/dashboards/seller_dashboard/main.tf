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

resource "google_monitoring_dashboard" "environment_dashboard" {
  dashboard_json = <<EOF
{
  "displayName": "${var.environment} Seller Metrics",
  "mosaicLayout": {
    "columns": 48,
    "tiles": [
      {
        "height": 19,
        "widget": {
          "title": "request.count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/request.count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24
      },
      {
        "height": 19,
        "widget": {
          "title": "system.cpu.percent [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"label\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/system.cpu.percent\" resource.type=\"generic_task\" metric.label.\"label\"!=\"total cpu cores\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 38
      },
      {
        "height": 19,
        "widget": {
          "title": "request.duration_ms [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/request.duration_ms\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24
      },
      {
        "height": 19,
        "widget": {
          "title": "system.memory.usage_kb for main process [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/system.memory.usage_kb\" resource.type=\"generic_task\" metric.label.\"label\"=\"main process\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 57
      },
      {
        "height": 19,
        "widget": {
          "title": "system.memory.usage_kb for MemAvailable: [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"label\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/system.memory.usage_kb\" resource.type=\"generic_task\" metric.label.\"label\"=\"MemAvailable:\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 76
      },
      {
        "height": 19,
        "widget": {
          "title": "request.size_bytes [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/request.size_bytes\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 19
      },
      {
        "height": 19,
        "widget": {
          "title": "response.size_bytes [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/response.size_bytes\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 38
      },
      {
        "height": 19,
        "widget": {
          "title": "udf_execution.duration_ms [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/udf_execution.duration_ms\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 285
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.initiated_request.to_bfe.errors_count_by_status [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.initiated_request.to_bfe.errors_count_by_status\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"status_code\"",
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 171
      },
      {
        "height": 19,
        "widget": {
          "title": "udf_execution.errors_count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/udf_execution.errors_count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos":24,
        "yPos": 285
      },
      {
        "height": 16,
        "widget": {
          "title": "auction.business_logic.bids_count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/auction.business_logic.bids_count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 304
      },
      {
        "height": 16,
        "widget": {
          "title": "auction.business_logic.bid_rejected_count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/auction.business_logic.bid_rejected_count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"seller_rejection_reason\"",
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 304
      },
      {
        "height": 19,
        "widget": {
          "title": "initiated_request.to_kv.duration_ms [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/initiated_request.to_kv.duration_ms\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 190
      },
      {
        "height": 19,
        "widget": {
          "title": "initiated_request.count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/initiated_request.count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"server_name\"",
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 95
      },
      {
        "height": 19,
        "widget": {
          "title": "auction.business_logic.bid_rejected_percent [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/auction.business_logic.bid_rejected_percent\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 323
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.initiated_request.to_auction.size_bytes [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.initiated_request.to_auction.size_bytes\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 171
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.initiated_request.to_auction.duration_ms [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.initiated_request.to_auction.duration_ms\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 152
      },
      {
        "height": 19,
        "widget": {
          "title": "initiated_request.to_kv.size_bytes [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/initiated_request.to_kv.size_bytes\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")  metric.label.\"service_name\"=\"sfe\""
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 209
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.initiated_response.to_kv.size_bytes [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.initiated_response.to_kv.size_bytes\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")  metric.label.\"service_name\"=\"sfe\""
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 228
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.errors_count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.errors_count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"error_code\"",
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 133
      },
      {
        "height": 19,
        "widget": {
          "title": "auction.errors_count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/auction.errors_count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"error_code\"",
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 133
      },
  {
        "height": 19,
        "widget": {
          "title": "initiated_request.to_kv.errors_count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/initiated_request.to_kv.errors_count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"status_code\"",
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 190
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.initiated_request.to_auction.errors_count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.initiated_request.to_auction.errors_count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"status_code\"",
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 152
      },
      {
        "height": 19,
        "widget": {
          "title": "request.failed_count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/request.failed_count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"status_code\"",
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 19
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.initiated_response.to_auction.size_bytes [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.initiated_response.to_auction.size_bytes\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 209
      },
      {
        "height": 19,
        "widget": {
          "title": "system.thread.count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"label\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/system.thread.count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 76
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.initiated_request.to_bfe.errors_count_by_buyer [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.initiated_request.to_bfe.errors_count_by_buyer\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"buyer\"",
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 247
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.initiated_request.to_bfe.count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.initiated_request.to_bfe.count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"buyer\"",
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 228
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.initiated_request.to_bfe.duration_ms [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesQueryLanguage": "fetch generic_task\n| { metric 'workload.googleapis.com/sfe.initiated_request.to_bfe.duration_ms'\n  ; metric 'workload.googleapis.com/sfe.initiated_request.to_bfe.count' }\n| filter (metric.deployment_environment =~ '${var.environment}')\n| group_by\n    [metric.buyer, metric.service_name, metric.deployment_environment,\n     metric.operator, metric.Noise, resource.task_id, metric.service_version]\n| align rate(1m)\n| outer_join 0\n| div"
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 247
      },
      {
        "height": 19,
        "widget": {
            "title": "sfe.initiated_request.to_bfe.size_bytes [MEAN]",
            "xyChart": {
              "chartOptions": {},
              "dataSets": [
                {
                  "minAlignmentPeriod": "60s",
                  "plotType": "LINE",
                  "targetAxis": "Y1",
                  "timeSeriesQuery": {
                    "timeSeriesFilter": {
                      "aggregation": {
                        "alignmentPeriod": "300s",
                        "perSeriesAligner": "ALIGN_RATE"
                      },
                      "filter": "metric.type=\"workload.googleapis.com/sfe.initiated_request.to_bfe.size_bytes\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                      "secondaryAggregation": {
                        "alignmentPeriod": "300s",
                        "crossSeriesReducer": "REDUCE_MEAN",
                        "groupByFields": [
                          "metric.label.\"buyer\"",
                          "metric.label.\"service_name\"",
                          "metric.label.\"deployment_environment\"",
                          "metric.label.\"operator\"",
                          "metric.label.\"Noise\"",
                          "resource.label.\"task_id\"",
                          "metric.label.\"service_version\""
                        ],
                        "perSeriesAligner": "ALIGN_MEAN"
                      }
                    }
                  }
                }
              ],
              "yAxis": {
                "scale": "LINEAR"
              }
            }
          },
          "width": 24,
          "yPos": 266
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.initiated_response.to_bfe.size_bytes [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.initiated_response.to_bfe.size_bytes\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"buyer\"",
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 266
      },
      {
        "height": 19,
        "widget": {
          "title": "system.key_fetch.failure_count [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"label\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/system.key_fetch.failure_count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 95
      },
      {
        "height": 19,
        "widget": {
          "title": "system.key_fetch.num_keys_parsed_on_recent_fetch [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"label\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/system.key_fetch.num_keys_parsed_on_recent_fetch\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 114
      },
      {
        "height": 19,
        "widget": {
          "title": "system.key_fetch.num_keys_cached_after_recent_fetch [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"label\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/system.key_fetch.num_keys_cached_after_recent_fetch\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 114
      },
      {
        "height": 19,
        "widget": {
          "title": "system.cpu.total_cores [MEAN]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/system.cpu.percent\" resource.type=\"generic_task\" metric.label.\"label\"=\"total cpu cores\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 57
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.business_logic.request_with_winner_count",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "perSeriesAligner": "ALIGN_RATE"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.business_logic.request_with_winner_count\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")",
                    "secondaryAggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_MEAN",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_MEAN"
                    }
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 320
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.business_logic.request_with_winner.duration_ms [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "300s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.business_logic.request_with_winner.duration_ms\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 339
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.protected_ciphertext.size_bytes [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.protected_ciphertext.size_bytes\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "xPos": 24,
        "yPos": 358
      },
      {
        "height": 19,
        "widget": {
          "title": "sfe.auction_config.size_bytes [95TH PERCENTILE]",
          "xyChart": {
            "chartOptions": {},
            "dataSets": [
              {
                "minAlignmentPeriod": "60s",
                "plotType": "LINE",
                "targetAxis": "Y1",
                "timeSeriesQuery": {
                  "timeSeriesFilter": {
                    "aggregation": {
                      "alignmentPeriod": "60s",
                      "crossSeriesReducer": "REDUCE_PERCENTILE_95",
                      "groupByFields": [
                        "metric.label.\"service_name\"",
                        "metric.label.\"deployment_environment\"",
                        "metric.label.\"operator\"",
                        "metric.label.\"Noise\"",
                        "resource.label.\"task_id\"",
                        "metric.label.\"service_version\""
                      ],
                      "perSeriesAligner": "ALIGN_DELTA"
                    },
                    "filter": "metric.type=\"workload.googleapis.com/sfe.auction_config.size_bytes\" resource.type=\"generic_task\" metric.label.\"deployment_environment\"=monitoring.regex.full_match(\"${var.environment}\")"
                  }
                }
              }
            ],
            "yAxis": {
              "scale": "LINEAR"
            }
          }
        },
        "width": 24,
        "yPos": 358
      }
    ]
  }
}
EOF
}
