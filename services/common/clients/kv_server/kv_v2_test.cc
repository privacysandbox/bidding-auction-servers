/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "kv_v2.h"

#include <utility>
#include <vector>

#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "services/common/test/utils/test_utils.h"
#include "services/common/util/json_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using google::protobuf::TextFormat;

TEST(ConvertKvV2ResponseToV1String, ConvertKeepsOnlyGivenTags) {
  kv_server::v2::GetValuesResponse response;
  constexpr char kCompressionGroup[] = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "hello": {
              "value": "world"
            }
          }
        },
        {
          "tags": [
            "structured",
            "groupNames"
          ],
          "keyValues": {
            "nohello": {
              "value": "world"
            }
          }
        }
      ]
    },
    {
      "id": 1,
      "keyGroupOutputs": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "hello2": {
              "value": "world2"
            }
          }
        }
      ]
    }
  ])JSON";
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(
          R"pb(
            compression_groups { compression_group_id: 33 content: "%s" })pb",
          absl::CEscape(kCompressionGroup)),
      &response));
  auto result = ConvertKvV2ResponseToV1String({"keys"}, response);
  ASSERT_TRUE(result.ok()) << result.status();
  std::string expected_parsed_signals =
      R"json(
      {
        "keys": {
          "hello": {
            "value": "world"
          },
          "hello2": {
            "value": "world2"
          }
      }
    })json";
  auto actual_signals_json = ParseJsonString(*result);
  auto expected_signals_json = ParseJsonString(expected_parsed_signals);
  ASSERT_TRUE(actual_signals_json.ok()) << actual_signals_json.status();
  ASSERT_TRUE(expected_signals_json.ok()) << expected_signals_json.status();
  ASSERT_EQ(actual_signals_json, expected_signals_json);
}

TEST(ConvertKvV2ResponseToV1String, MultipleCompressionGroups) {
  kv_server::v2::GetValuesResponse response;
  constexpr char kCompressionGroup1[] = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "hello": {
              "value": "world"
            }
          }
        },
        {
          "tags": [
            "structured",
            "groupNames"
          ],
          "keyValues": {
            "nohello": {
              "value": "world"
            }
          }
        }
      ]
    },
    {
      "id": 1,
      "keyGroupOutputs": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "hello2": {
              "value": "world2"
            }
          }
        }
      ]
    }
  ])JSON";

  constexpr char kCompressionGroup2[] = R"JSON(
  [
    {
      "id": 3,
      "keyGroupOutputs": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "hello44": {
              "value": "world44"
            }
          }
        },
        {
          "tags": [
            "structured",
            "groupNames"
          ],
          "keyValues": {
            "blah": {
              "value": "blah"
            }
          }
        }
      ]
    },
    {
      "id": 4,
      "keyGroupOutputs": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "hello24": {
              "value": "world24"
            }
          }
        }
      ]
    }
  ])JSON";

  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        }
        compression_groups {
          compression_group_id : 34
          content : "%s"
        }
        )",
                      absl::CEscape(kCompressionGroup1),
                      absl::CEscape(kCompressionGroup2)),
      &response));
  auto result = ConvertKvV2ResponseToV1String({"keys"}, response);
  ASSERT_TRUE(result.ok()) << result.status();
  std::string expected_parsed_signals =
      R"json(
      {
        "keys": {
          "hello": {
            "value": "world"
          },
          "hello2": {
            "value": "world2"
          },
          "hello44": {
            "value": "world44"
          },
          "hello24": {
            "value": "world24"
          }
      }
    })json";
  auto actual_signals_json = ParseJsonString(*result);
  auto expected_signals_json = ParseJsonString(expected_parsed_signals);
  ASSERT_TRUE(actual_signals_json.ok()) << actual_signals_json.status();
  ASSERT_TRUE(expected_signals_json.ok()) << expected_signals_json.status();
  ASSERT_EQ(actual_signals_json, expected_signals_json);
}

TEST(ConvertKvV2ResponseToV1String, ConvertMultipleKeyTags) {
  kv_server::v2::GetValuesResponse response;
  constexpr char kCompressionGroup[] = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "renderUrls"
          ],
          "keyValues": {
            "hello": {
              "value": "world"
            }
          }
        },
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "nohello": {
              "value": "world"
            }
          }
        }
      ]
    },
    {
      "id": 1,
      "keyGroupOutputs": [
        {
          "tags": [
            "adComponentRenderUrls"
          ],
          "keyValues": {
            "hello2": {
              "value": "world2"
            }
          }
        }
      ]
    }
  ])JSON";
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        })",
                      absl::CEscape(kCompressionGroup)),
      &response));
  auto result = ConvertKvV2ResponseToV1String(
      {"renderUrls", "adComponentRenderUrls"}, response);
  ASSERT_TRUE(result.ok()) << result.status();
  std::string expected_parsed_signals =
      R"json(
      {
        "renderUrls": {
          "hello": {
            "value": "world"
          }
        },
        "adComponentRenderUrls": {
          "hello2": {
            "value": "world2"
          }
        }
      })json";
  auto actual_signals_json = ParseJsonString(*result);
  auto expected_signals_json = ParseJsonString(expected_parsed_signals);
  ASSERT_TRUE(actual_signals_json.ok()) << actual_signals_json.status();
  ASSERT_TRUE(expected_signals_json.ok()) << expected_signals_json.status();
  ASSERT_EQ(actual_signals_json, expected_signals_json);
}

TEST(ConvertKvV2ResponseToV1String, MultipleCompressionGroupsMultipleKeyTags) {
  kv_server::v2::GetValuesResponse response;
  constexpr char kCompressionGroup1[] = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "renderUrls"
          ],
          "keyValues": {
            "hello": {
              "value": "world"
            }
          }
        },
        {
          "tags": [
            "adComponentRenderUrls"
          ],
          "keyValues": {
            "hello3": {
              "value": "world3"
            }
          }
        }
      ]
    },
    {
      "id": 1,
      "keyGroupOutputs": [
        {
          "tags": [
            "renderUrls"
          ],
          "keyValues": {
            "hello2": {
              "value": "world2"
            }
          }
        }
      ]
    }
  ])JSON";

  constexpr char kCompressionGroup2[] = R"JSON(
  [
    {
      "id": 3,
      "keyGroupOutputs": [
        {
          "tags": [
            "renderUrls"
          ],
          "keyValues": {
            "hello44": {
              "value": "world44"
            }
          }
        },
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "blah": {
              "value": "blah"
            }
          }
        }
      ]
    },
    {
      "id": 4,
      "keyGroupOutputs": [
        {
          "tags": [
            "adComponentRenderUrls"
          ],
          "keyValues": {
            "hello24": {
              "value": "world24"
            }
          }
        }
      ]
    }
  ])JSON";

  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        }
        compression_groups {
          compression_group_id : 34
          content : "%s"
        }
        )",
                      absl::CEscape(kCompressionGroup1),
                      absl::CEscape(kCompressionGroup2)),
      &response));
  auto result = ConvertKvV2ResponseToV1String(
      {"renderUrls", "adComponentRenderUrls"}, response);
  ASSERT_TRUE(result.ok()) << result.status();
  std::string expected_parsed_signals =
      R"JSON(
      {
        "renderUrls": {
          "hello": {
            "value": "world"
          },
          "hello2": {
            "value": "world2"
          },
          "hello44": {
            "value": "world44"
          }
        },
        "adComponentRenderUrls": {
          "hello3": {
            "value": "world3"
          },
          "hello24": {
            "value": "world24"
          }
        }
      })JSON";
  auto actual_signals_json = ParseJsonString(*result);
  auto expected_signals_json = ParseJsonString(expected_parsed_signals);
  ASSERT_TRUE(actual_signals_json.ok()) << actual_signals_json.status();
  ASSERT_TRUE(expected_signals_json.ok()) << expected_signals_json.status();
  ASSERT_EQ(actual_signals_json, expected_signals_json);
}

TEST(ConvertKvV2ResponseToV1String, MalformedJson) {
  kv_server::v2::GetValuesResponse response;
  constexpr char kCompressionGroup[] = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputsFAIL": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "hello": {
              "value": "world"
            }
          }
        },
        {
          "tags": [
            "structured",
            "groupNames"
          ],
          "keyValues": {
            "nohello": {
              "value": "world"
            }
          }
        }
      ]
    },
    {
      "id": 1,
      "keyGroupOutputs": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "hello2": {
              "value": "world2"
            }
          }
        }
      ]
    }
  ])JSON";
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        })",
                      absl::CEscape(kCompressionGroup)),
      &response));
  auto result = ConvertKvV2ResponseToV1String({"keys"}, response);
  ASSERT_FALSE(result.ok());
}

TEST(ConvertKvV2ResponseToV1String, EmptyJson) {
  kv_server::v2::GetValuesResponse response;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"(
        compression_groups {
          compression_group_id : 33
          content : ""
        })",
      &response));
  auto result = ConvertKvV2ResponseToV1String({"keys"}, response);
  ASSERT_FALSE(result.ok());
}

TEST(ConvertKvV2ResponseToV1String, KeyTagsNotFoundReturnEmtpyString) {
  kv_server::v2::GetValuesResponse response;
  constexpr char kCompressionGroup[] = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "someTag"
          ],
          "keyValues": {
            "hello": {
              "value": "world"
            }
          }
        }
      ]
    }
  ])JSON";
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        })",
                      absl::CEscape(kCompressionGroup)),
      &response));
  auto result = ConvertKvV2ResponseToV1String({"keys"}, response);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ("", *result);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
