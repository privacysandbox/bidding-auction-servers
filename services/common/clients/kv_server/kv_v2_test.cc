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
#include "services/common/test/utils/proto_utils.h"
#include "services/common/test/utils/test_utils.h"
#include "services/common/util/json_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using google::protobuf::TextFormat;

class ConvertKvV2ResponseToV1StringTest : public ::testing::Test {
 protected:
  KVV2AdapterStats v2_adapter_stats_;
};

TEST_F(ConvertKvV2ResponseToV1StringTest, ConvertKeepsOnlyGivenTags) {
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
  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(absl::StrFormat(
          R"pb(
            compression_groups { compression_group_id: 33 content: "%s" })pb",
          absl::CEscape(kCompressionGroup)));
  auto result =
      ConvertKvV2ResponseToV1String({"keys"}, response, v2_adapter_stats_);
  ASSERT_TRUE(result.ok()) << result.status();
  std::string expected_parsed_signals =
      R"json(
      {
        "keys": {
          "hello": "world",
          "hello2":"world2"
      }
    })json";
  auto actual_signals_json = ParseJsonString(*result);
  auto expected_signals_json = ParseJsonString(expected_parsed_signals);
  ASSERT_TRUE(actual_signals_json.ok()) << actual_signals_json.status();
  ASSERT_TRUE(expected_signals_json.ok()) << expected_signals_json.status();
  ASSERT_EQ(*actual_signals_json, *expected_signals_json);
}

TEST_F(ConvertKvV2ResponseToV1StringTest, MultipleCompressionGroups) {
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

  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(
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
                          absl::CEscape(kCompressionGroup2)));
  auto result =
      ConvertKvV2ResponseToV1String({"keys"}, response, v2_adapter_stats_);
  ASSERT_TRUE(result.ok()) << result.status();
  std::string expected_parsed_signals =
      R"json(
      {
        "keys": {
          "hello": "world",
          "hello2": "world2",
          "hello44": "world44",
          "hello24": "world24"
      }
    })json";
  auto actual_signals_json = ParseJsonString(*result);
  auto expected_signals_json = ParseJsonString(expected_parsed_signals);
  ASSERT_TRUE(actual_signals_json.ok()) << actual_signals_json.status();
  ASSERT_TRUE(expected_signals_json.ok()) << expected_signals_json.status();
  ASSERT_EQ(*actual_signals_json, *expected_signals_json);
}

TEST_F(ConvertKvV2ResponseToV1StringTest, ConvertMultipleKeyTags) {
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
  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(
          absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        })",
                          absl::CEscape(kCompressionGroup)));
  auto result = ConvertKvV2ResponseToV1String(
      {"renderUrls", "adComponentRenderUrls"}, response, v2_adapter_stats_);
  ASSERT_TRUE(result.ok()) << result.status();
  std::string expected_parsed_signals =
      R"json(
      {
        "renderUrls": {
          "hello": "world"
        },
        "adComponentRenderUrls": {
          "hello2": "world2"
        }
      })json";
  auto actual_signals_json = ParseJsonString(*result);
  auto expected_signals_json = ParseJsonString(expected_parsed_signals);
  ASSERT_TRUE(actual_signals_json.ok()) << actual_signals_json.status();
  ASSERT_TRUE(expected_signals_json.ok()) << expected_signals_json.status();
  ASSERT_EQ(*actual_signals_json, *expected_signals_json);
}

TEST_F(ConvertKvV2ResponseToV1StringTest,
       MultipleCompressionGroupsMultipleKeyTags) {
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

  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(
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
                          absl::CEscape(kCompressionGroup2)));
  auto result = ConvertKvV2ResponseToV1String(
      {"renderUrls", "adComponentRenderUrls"}, response, v2_adapter_stats_);
  ASSERT_TRUE(result.ok()) << result.status();
  std::string expected_parsed_signals =
      R"JSON(
      {
        "renderUrls": {
          "hello": "world",
          "hello2": "world2",
          "hello44": "world44"
        },
        "adComponentRenderUrls": {
          "hello3": "world3",
          "hello24": "world24"
        }
      })JSON";
  auto actual_signals_json = ParseJsonString(*result);
  auto expected_signals_json = ParseJsonString(expected_parsed_signals);
  ASSERT_TRUE(actual_signals_json.ok()) << actual_signals_json.status();
  ASSERT_TRUE(expected_signals_json.ok()) << expected_signals_json.status();
  ASSERT_EQ(*actual_signals_json, *expected_signals_json);
}

TEST_F(ConvertKvV2ResponseToV1StringTest, MultipleValueTypes) {
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
            "keys"
          ],
          "keyValues": {
            "hello2": {
              "value": "[\"world2\"]"
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
            "hello3": {
              "value": "{\"world3\": {\"world3_val\" : 1}}"
            }
          }
        }
      ]
    }
  ])JSON";

  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(
          absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        }
        )",
                          absl::CEscape(kCompressionGroup1)));
  auto result =
      ConvertKvV2ResponseToV1String({"keys"}, response, v2_adapter_stats_);
  ASSERT_TRUE(result.ok()) << result.status();
  std::string expected_parsed_signals =
      R"JSON(
      {
        "keys": {
          "hello": "world",
          "hello2": ["world2"],
          "hello3": {
            "world3": {
              "world3_val":1
            }
          }
        }
      })JSON";
  auto actual_signals_json = ParseJsonString(*result);
  auto expected_signals_json = ParseJsonString(expected_parsed_signals);
  ASSERT_TRUE(actual_signals_json.ok()) << actual_signals_json.status();
  ASSERT_TRUE(expected_signals_json.ok()) << expected_signals_json.status();
  ASSERT_EQ(*actual_signals_json, *expected_signals_json);
  EXPECT_EQ(v2_adapter_stats_.values_with_json_string_parsing, 2);
  EXPECT_EQ(v2_adapter_stats_.values_without_json_string_parsing, 1);
}

TEST_F(ConvertKvV2ResponseToV1StringTest, MalformedJson) {
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
  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(
          absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        })",
                          absl::CEscape(kCompressionGroup)));
  auto result =
      ConvertKvV2ResponseToV1String({"keys"}, response, v2_adapter_stats_);
  ASSERT_FALSE(result.ok());
}

TEST_F(ConvertKvV2ResponseToV1StringTest, IgnoresKeyValueWithNonValueObject) {
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
            "hello": "not an object"
          }
        }
      ]
    }
  ])JSON";
  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(absl::StrFormat(
          R"pb(
            compression_groups { compression_group_id: 0 content: "%s" })pb",
          absl::CEscape(kCompressionGroup)));
  auto result =
      ConvertKvV2ResponseToV1String({"keys"}, response, v2_adapter_stats_);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "");
}

TEST_F(ConvertKvV2ResponseToV1StringTest, EmptyJson) {
  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(
          R"pb(
            compression_groups { compression_group_id: 33 content: "" })pb");
  auto result =
      ConvertKvV2ResponseToV1String({"keys"}, response, v2_adapter_stats_);
  ASSERT_FALSE(result.ok());
}

TEST_F(ConvertKvV2ResponseToV1StringTest, KeyTagsNotFoundReturnEmtpyString) {
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
  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(
          absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        })",
                          absl::CEscape(kCompressionGroup)));
  auto result =
      ConvertKvV2ResponseToV1String({"keys"}, response, v2_adapter_stats_);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ("", *result);
}

TEST(UseKvV2, BrowserNoTkvEnabledReturnsFalse) {
  EXPECT_FALSE(UseKvV2(ClientType::CLIENT_TYPE_BROWSER,
                       /*is_tkv_v2_browser_enabled=*/false,
                       /*is_test_mode_enabled=*/false,
                       /*is_tkv_v2_empty=*/false));
}

TEST(UseKvV2, BrowserTkvEnabledReturnsTrue) {
  EXPECT_TRUE(UseKvV2(ClientType::CLIENT_TYPE_BROWSER,
                      /*is_tkv_v2_browser_enabled=*/true,
                      /*is_test_mode_enabled=*/false,
                      /*is_tkv_v2_empty=*/false));
}

TEST(UseKvV2, AndroidTestModeFalseReturnsTrue) {
  EXPECT_TRUE(UseKvV2(ClientType::CLIENT_TYPE_ANDROID,
                      /*is_tkv_v2_browser_enabled=*/false,
                      /*is_test_mode_enabled=*/false,
                      /*is_tkv_v2_empty=*/false));
}

TEST(UseKvV2, AndroidTestModeTrueAddressEmptyReturnsFalse) {
  EXPECT_FALSE(UseKvV2(ClientType::CLIENT_TYPE_ANDROID,
                       /*is_tkv_v2_browser_enabled=*/false,
                       /*is_test_mode_enabled=*/true,
                       /*is_tkv_v2_empty=*/true));
}

TEST(UseKvV2, AndroidTestModeTrueAddressNotEmptyReturnsTrue) {
  EXPECT_TRUE(UseKvV2(ClientType::CLIENT_TYPE_ANDROID,
                      /*is_tkv_v2_browser_enabled=*/false,
                      /*is_test_mode_enabled=*/true,
                      /*is_tkv_v2_empty=*/false));
}

TEST_F(ConvertKvV2ResponseToV1StringTest, ParsesIgNamesOutputsCorrectly) {
  constexpr char kCompressionGroup[] = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "interestGroupNames"
          ],
          "keyValues": {
            "ig_name_0": {
              "value": "{\"priorityVector\":{\"signal1\":1}, \"updateIfOlderThanMs\": 10000}"
            }
          }
        }
      ]
    }
  ])JSON";
  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(absl::StrFormat(
          R"pb(
            compression_groups { compression_group_id: 0 content: "%s" })pb",
          absl::CEscape(kCompressionGroup)));
  auto result = ConvertKvV2ResponseToV1String({"interestGroupNames"}, response,
                                              v2_adapter_stats_);
  ASSERT_TRUE(result.ok()) << result.status();
  std::string expected_parsed_signals =
      R"json(
      {
        "perInterestGroupData": {
          "ig_name_0": {
            "priorityVector": { "signal1": 1 },
            "updateIfOlderThanMs": 10000
          }
        }
      })json";
  auto actual_signals_json = ParseJsonString(*result);
  auto expected_signals_json = ParseJsonString(expected_parsed_signals);
  ASSERT_TRUE(actual_signals_json.ok()) << actual_signals_json.status();
  ASSERT_TRUE(expected_signals_json.ok()) << expected_signals_json.status();
  ASSERT_EQ(*actual_signals_json, *expected_signals_json)
      << SerializeJsonDoc(*actual_signals_json)
      << SerializeJsonDoc(*expected_signals_json);
}

TEST_F(ConvertKvV2ResponseToV1StringTest, IgnoresIgNamesWithNonObject) {
  constexpr char kCompressionGroup[] = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "interestGroupNames"
          ],
          "keyValues": {
            "ig_name_0": "\"not an object - will ignore\""
          }
        }
      ]
    }
  ])JSON";
  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(absl::StrFormat(
          R"pb(
            compression_groups { compression_group_id: 0 content: "%s" })pb",
          absl::CEscape(kCompressionGroup)));
  auto result = ConvertKvV2ResponseToV1String({"interestGroupNames"}, response,
                                              v2_adapter_stats_);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "");
}

TEST_F(ConvertKvV2ResponseToV1StringTest, IgnoresIgNamesWithoutValueField) {
  constexpr char kCompressionGroup[] = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "interestGroupNames"
          ],
          "keyValues": {
            "ig_name_0": {
               "a": "doesn't have value"
            }
          }
        }
      ]
    }
  ])JSON";
  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(absl::StrFormat(
          R"pb(
            compression_groups { compression_group_id: 0 content: "%s" })pb",
          absl::CEscape(kCompressionGroup)));
  auto result = ConvertKvV2ResponseToV1String({"interestGroupNames"}, response,
                                              v2_adapter_stats_);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "");
}

TEST_F(ConvertKvV2ResponseToV1StringTest, IgnoresIgNamesWithInvalidJsonValue) {
  constexpr char kCompressionGroup[] = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "interestGroupNames"
          ],
          "keyValues": {
            "ig_name_0": {
               "value": "invalid, unescaped json"
            }
          }
        }
      ]
    }
  ])JSON";
  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(absl::StrFormat(
          R"pb(
            compression_groups { compression_group_id: 0 content: "%s" })pb",
          absl::CEscape(kCompressionGroup)));
  auto result = ConvertKvV2ResponseToV1String({"interestGroupNames"}, response,
                                              v2_adapter_stats_);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
