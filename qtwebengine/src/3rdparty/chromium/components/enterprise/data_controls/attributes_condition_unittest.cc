// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/enterprise/data_controls/attributes_condition.h"

#include <vector>

#include "base/json/json_reader.h"
#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace data_controls {

namespace {

constexpr char kGoogleUrl[] = "https://google.com";
constexpr char kChromiumUrl[] = "https://chromium.org";

base::Value CreateDict(const std::string& value) {
  auto dict = base::JSONReader::Read(value, base::JSON_ALLOW_TRAILING_COMMAS);
  EXPECT_TRUE(dict.has_value());
  return std::move(dict.value());
}

}  // namespace

TEST(AttributesConditionTest, InvalidSourceInputs) {
  // Invalid JSON types are rejected.
  ASSERT_FALSE(SourceAttributesCondition::Create(base::Value("some string")));
  ASSERT_FALSE(SourceAttributesCondition::Create(base::Value(12345)));
  ASSERT_FALSE(SourceAttributesCondition::Create(base::Value(99.999)));
  ASSERT_FALSE(SourceAttributesCondition::Create(
      base::Value(std::vector<char>({1, 2, 3, 4, 5}))));

  // Invalid dictionaries are rejected.
  ASSERT_FALSE(SourceAttributesCondition::Create(base::Value::Dict()));
  ASSERT_FALSE(SourceAttributesCondition::Create(CreateDict(R"({"foo": 1})")));

  // Dictionaries with correct keys but wrong schema for values are rejected
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"urls": "https://foo.com"})")));
  ASSERT_FALSE(SourceAttributesCondition::Create(CreateDict(R"({"urls": 1})")));
  ASSERT_FALSE(
      SourceAttributesCondition::Create(CreateDict(R"({"urls": 99.999})")));
  ASSERT_FALSE(
      SourceAttributesCondition::Create(CreateDict(R"({"incognito": "str"})")));
  ASSERT_FALSE(
      SourceAttributesCondition::Create(CreateDict(R"({"incognito": 1234})")));
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"os_clipboard": "str"})")));
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"os_clipboard": 1234})")));
#if BUILDFLAG(IS_CHROMEOS)
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"urls": "https://foo.com", "components": "ARC"})")));
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"urls": 1, "components": "ARC"})")));
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"urls": 99.999, "components": "ARC"})")));
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"components": "ARC"})")));
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"components": 12345})")));
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"components": 99.999})")));
#endif  // BUILDFLAG(IS_CHROMEOS)

  // Dictionaries with valid schemas but invalid URL patterns or components are
  // rejected.
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"urls": ["http://:port"]})")));
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"urls": ["http://?query"]})")));
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"urls": ["https://"]})")));
  ASSERT_FALSE(
      SourceAttributesCondition::Create(CreateDict(R"({"urls": ["//"]})")));
  ASSERT_FALSE(
      SourceAttributesCondition::Create(CreateDict(R"({"urls": ["a", 1]})")));
#if BUILDFLAG(IS_CHROMEOS)
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"urls": ["a", 1], "components": ["ARC"]})")));
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"components": ["1", "a"]})")));
  ASSERT_FALSE(SourceAttributesCondition::Create(
      CreateDict(R"({"components": ["5.5"]})")));
#endif  // BUILDFLAG(IS_CHROMEOS)
}

TEST(AttributesConditionTest, InvalidDestinationInputs) {
  // Invalid JSON types are rejected.
  ASSERT_FALSE(
      DestinationAttributesCondition::Create(base::Value("some string")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(base::Value(12345)));
  ASSERT_FALSE(DestinationAttributesCondition::Create(base::Value(99.999)));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      base::Value(std::vector<char>({1, 2, 3, 4, 5}))));

  // Invalid dictionaries are rejected.
  ASSERT_FALSE(DestinationAttributesCondition::Create(base::Value::Dict()));
  ASSERT_FALSE(
      DestinationAttributesCondition::Create(CreateDict(R"({"foo": 1})")));

  // Dictionaries with correct keys but wrong schema for values are rejected
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": "https://foo.com"})")));
  ASSERT_FALSE(
      DestinationAttributesCondition::Create(CreateDict(R"({"urls": 1})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": 99.999})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"incognito": "str"})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"incognito": 1234})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"os_clipboard": "str"})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"os_clipboard": 1234})")));
#if BUILDFLAG(IS_CHROMEOS)
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": "https://foo.com", "components": "ARC"})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": 1, "components": "ARC"})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": 99.999, "components": "ARC"})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"components": "ARC"})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"components": 12345})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"components": 99.999})")));
#endif  // BUILDFLAG(IS_CHROMEOS)

  // Dictionaries with valid schemas but invalid URL patterns or components are
  // rejected.
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": ["http://:port"]})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": ["http://?query"]})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": ["https://"]})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": ["//"]})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": ["a", 1]})")));
#if BUILDFLAG(IS_CHROMEOS)
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": ["a", 1], "components": ["ARC"]})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"components": ["1", "a"]})")));
  ASSERT_FALSE(DestinationAttributesCondition::Create(
      CreateDict(R"({"components": ["5.5"]})")));
#endif  // BUILDFLAG(IS_CHROMEOS)
}

TEST(AttributesConditionTest, AnyURL) {
  auto any_source_url =
      SourceAttributesCondition::Create(CreateDict(R"({"urls": ["*"]})"));
  auto any_destination_url =
      DestinationAttributesCondition::Create(CreateDict(R"({"urls": ["*"]})"));
  ASSERT_TRUE(any_source_url);
  ASSERT_TRUE(any_destination_url);
  for (const char* url : {kGoogleUrl, kChromiumUrl}) {
    ActionContext context = {
        .source = {.url = GURL(url)},
        .destination = {.url = GURL(url)},
    };
    ASSERT_TRUE(any_source_url->IsTriggered(context));
    ASSERT_TRUE(any_destination_url->IsTriggered(context));
  }
}

TEST(AttributesConditionTest, SpecificSourceURL) {
  auto google_url_source = SourceAttributesCondition::Create(
      CreateDict(R"({"urls": ["google.com"]})"));
  auto chromium_url_source = SourceAttributesCondition::Create(
      CreateDict(R"({"urls": ["chromium.org"]})"));

  ASSERT_TRUE(google_url_source);
  ASSERT_TRUE(chromium_url_source);

  ASSERT_TRUE(
      google_url_source->IsTriggered({.source = {.url = GURL(kGoogleUrl)}}));
  ASSERT_TRUE(chromium_url_source->IsTriggered(
      {.source = {.url = GURL(kChromiumUrl)}}));

  ASSERT_FALSE(
      google_url_source->IsTriggered({.source = {.url = GURL(kChromiumUrl)}}));
  ASSERT_FALSE(
      chromium_url_source->IsTriggered({.source = {.url = GURL(kGoogleUrl)}}));
}

TEST(AttributesConditionTest, SpecificDestinationURL) {
  auto google_url_destination = DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": ["google.com"]})"));
  auto chromium_url_destination = DestinationAttributesCondition::Create(
      CreateDict(R"({"urls": ["chromium.org"]})"));

  ASSERT_TRUE(google_url_destination);
  ASSERT_TRUE(chromium_url_destination);

  ASSERT_TRUE(google_url_destination->IsTriggered(
      {.destination = {.url = GURL(kGoogleUrl)}}));
  ASSERT_TRUE(chromium_url_destination->IsTriggered(
      {.destination = {.url = GURL(kChromiumUrl)}}));

  ASSERT_FALSE(google_url_destination->IsTriggered(
      {.destination = {.url = GURL(kChromiumUrl)}}));
  ASSERT_FALSE(chromium_url_destination->IsTriggered(
      {.destination = {.url = GURL(kGoogleUrl)}}));
}

#if BUILDFLAG(IS_CHROMEOS)
TEST(AttributesConditionTest, AllComponents) {
  auto any_component = DestinationAttributesCondition::Create(CreateDict(R"(
    {
      "components": ["ARC", "CROSTINI", "PLUGIN_VM", "USB", "DRIVE", "ONEDRIVE"]
    })"));
  ASSERT_TRUE(any_component);
  for (Component component : kAllComponents) {
    ActionContext context = {.destination = {.component = component}};
    ASSERT_TRUE(any_component->IsTriggered(context));
  }
}

TEST(AttributesConditionTest, OneComponent) {
  for (Component condition_component : kAllComponents) {
    constexpr char kTemplate[] = R"({"components": ["%s"]})";
    auto one_component =
        DestinationAttributesCondition::Create(CreateDict(base::StringPrintf(
            kTemplate, GetComponentMapping(condition_component).c_str())));

    for (Component context_component : kAllComponents) {
      ActionContext context = {.destination = {.component = context_component}};
      if (context_component == condition_component) {
        ASSERT_TRUE(one_component->IsTriggered(context));
      } else {
        ASSERT_FALSE(one_component->IsTriggered(context));
      }
    }
  }
}

TEST(AttributesConditionTest, URLAndAllComponents) {
  auto any_component_or_url =
      DestinationAttributesCondition::Create(CreateDict(R"(
      {
        "urls": ["*"],
        "components": ["ARC", "CROSTINI", "PLUGIN_VM", "USB", "DRIVE",
                       "ONEDRIVE"]
      })"));
  ASSERT_TRUE(any_component_or_url);
  for (Component component : kAllComponents) {
    for (const char* url : {kGoogleUrl, kChromiumUrl}) {
      ActionContext context = {
          .destination = {.url = GURL(url), .component = component}};
      ASSERT_TRUE(any_component_or_url->IsTriggered(context));
    }
  }
}

TEST(AttributesConditionTest, URLAndOneComponent) {
  for (Component condition_component : kAllComponents) {
    constexpr char kTemplate[] =
        R"({"urls": ["google.com"], "components": ["%s"]})";
    auto google_and_one_component =
        DestinationAttributesCondition::Create(CreateDict(base::StringPrintf(
            kTemplate, GetComponentMapping(condition_component).c_str())));

    ASSERT_TRUE(google_and_one_component);
    for (Component context_component : kAllComponents) {
      for (const char* url : {kGoogleUrl, kChromiumUrl}) {
        ActionContext context = {
            .destination = {.url = GURL(url), .component = context_component}};
        if (context_component == condition_component && url == kGoogleUrl) {
          ASSERT_TRUE(google_and_one_component->IsTriggered(context))
              << "Expected " << GetComponentMapping(context_component)
              << " to trigger for " << url;
        } else {
          ASSERT_FALSE(google_and_one_component->IsTriggered(context))
              << "Expected " << GetComponentMapping(context_component)
              << " to not trigger for " << url;
        }
      }
    }
  }
}
#endif  // BUILDFLAG(IS_CHROMEOS)

TEST(AttributesConditionTest, IncognitoDestination) {
  auto incognito_dst = DestinationAttributesCondition::Create(CreateDict(R"(
      {
        "incognito": true,
      })"));
  ASSERT_TRUE(incognito_dst);
  ASSERT_TRUE(incognito_dst->IsTriggered({.destination = {.incognito = true}}));
  ASSERT_FALSE(
      incognito_dst->IsTriggered({.destination = {.incognito = false}}));
  ASSERT_FALSE(incognito_dst->IsTriggered({.source = {.incognito = true}}));
  ASSERT_FALSE(incognito_dst->IsTriggered({.source = {.incognito = false}}));

  auto non_incognito_dst = DestinationAttributesCondition::Create(CreateDict(R"(
      {
        "incognito": false,
      })"));
  ASSERT_TRUE(non_incognito_dst);
  ASSERT_FALSE(
      non_incognito_dst->IsTriggered({.destination = {.incognito = true}}));
  ASSERT_TRUE(
      non_incognito_dst->IsTriggered({.destination = {.incognito = false}}));
  ASSERT_FALSE(non_incognito_dst->IsTriggered({.source = {.incognito = true}}));
  ASSERT_FALSE(
      non_incognito_dst->IsTriggered({.source = {.incognito = false}}));
}

TEST(AttributesConditionTest, IncognitoSource) {
  auto incognito_src = SourceAttributesCondition::Create(CreateDict(R"(
      {
        "incognito": true,
      })"));
  ASSERT_TRUE(incognito_src);
  ASSERT_FALSE(
      incognito_src->IsTriggered({.destination = {.incognito = true}}));
  ASSERT_FALSE(
      incognito_src->IsTriggered({.destination = {.incognito = false}}));
  ASSERT_TRUE(incognito_src->IsTriggered({.source = {.incognito = true}}));
  ASSERT_FALSE(incognito_src->IsTriggered({.source = {.incognito = false}}));

  auto non_incognito_src = SourceAttributesCondition::Create(CreateDict(R"(
      {
        "incognito": false,
      })"));
  ASSERT_TRUE(non_incognito_src);
  ASSERT_FALSE(
      non_incognito_src->IsTriggered({.destination = {.incognito = true}}));
  ASSERT_FALSE(
      non_incognito_src->IsTriggered({.destination = {.incognito = false}}));
  ASSERT_FALSE(non_incognito_src->IsTriggered({.source = {.incognito = true}}));
  ASSERT_TRUE(non_incognito_src->IsTriggered({.source = {.incognito = false}}));
}

TEST(AttributesConditionTest, URLAndIncognitoDestination) {
  auto url_and_incognito = DestinationAttributesCondition::Create(CreateDict(R"(
      {
        "urls": ["google.com"],
        "incognito": true,
      })"));
  ASSERT_TRUE(url_and_incognito);
  ASSERT_TRUE(url_and_incognito->IsTriggered(
      {.destination = {.url = GURL(kGoogleUrl), .incognito = true}}));
  ASSERT_FALSE(url_and_incognito->IsTriggered(
      {.destination = {.url = GURL(kGoogleUrl), .incognito = false}}));
  ASSERT_FALSE(url_and_incognito->IsTriggered(
      {.destination = {.url = GURL(kGoogleUrl)}}));
  ASSERT_FALSE(url_and_incognito->IsTriggered(
      {.destination = {.url = GURL(kChromiumUrl), .incognito = true}}));
  ASSERT_FALSE(url_and_incognito->IsTriggered(
      {.destination = {.url = GURL(kChromiumUrl), .incognito = false}}));
  ASSERT_FALSE(url_and_incognito->IsTriggered(
      {.destination = {.url = GURL(kChromiumUrl)}}));
  ASSERT_FALSE(
      url_and_incognito->IsTriggered({.destination = {.incognito = true}}));
  ASSERT_FALSE(
      url_and_incognito->IsTriggered({.destination = {.incognito = false}}));

  auto url_and_not_incognito =
      DestinationAttributesCondition::Create(CreateDict(R"(
      {
        "urls": ["google.com"],
        "incognito": false,
      })"));
  ASSERT_TRUE(url_and_not_incognito);
  ASSERT_FALSE(url_and_not_incognito->IsTriggered(
      {.destination = {.url = GURL(kGoogleUrl), .incognito = true}}));
  ASSERT_TRUE(url_and_not_incognito->IsTriggered(
      {.destination = {.url = GURL(kGoogleUrl), .incognito = false}}));
  ASSERT_FALSE(url_and_not_incognito->IsTriggered(
      {.destination = {.url = GURL(kGoogleUrl)}}));
  ASSERT_FALSE(url_and_not_incognito->IsTriggered(
      {.destination = {.url = GURL(kChromiumUrl), .incognito = true}}));
  ASSERT_FALSE(url_and_not_incognito->IsTriggered(
      {.destination = {.url = GURL(kChromiumUrl), .incognito = false}}));
  ASSERT_FALSE(url_and_not_incognito->IsTriggered(
      {.destination = {.url = GURL(kChromiumUrl)}}));
  ASSERT_FALSE(
      url_and_not_incognito->IsTriggered({.destination = {.incognito = true}}));
  ASSERT_FALSE(url_and_not_incognito->IsTriggered(
      {.destination = {.incognito = false}}));
}

TEST(AttributesConditionTest, URLAndIncognitoSource) {
  auto url_and_incognito = SourceAttributesCondition::Create(CreateDict(R"(
      {
        "urls": ["google.com"],
        "incognito": true,
      })"));
  ASSERT_TRUE(url_and_incognito);
  ASSERT_TRUE(url_and_incognito->IsTriggered(
      {.source = {.url = GURL(kGoogleUrl), .incognito = true}}));
  ASSERT_FALSE(url_and_incognito->IsTriggered(
      {.source = {.url = GURL(kGoogleUrl), .incognito = false}}));
  ASSERT_FALSE(
      url_and_incognito->IsTriggered({.source = {.url = GURL(kGoogleUrl)}}));
  ASSERT_FALSE(url_and_incognito->IsTriggered(
      {.source = {.url = GURL(kChromiumUrl), .incognito = true}}));
  ASSERT_FALSE(url_and_incognito->IsTriggered(
      {.source = {.url = GURL(kChromiumUrl), .incognito = false}}));
  ASSERT_FALSE(
      url_and_incognito->IsTriggered({.source = {.url = GURL(kChromiumUrl)}}));
  ASSERT_FALSE(url_and_incognito->IsTriggered({.source = {.incognito = true}}));
  ASSERT_FALSE(
      url_and_incognito->IsTriggered({.source = {.incognito = false}}));

  auto url_and_not_incognito = SourceAttributesCondition::Create(CreateDict(R"(
      {
        "urls": ["google.com"],
        "incognito": false,
      })"));
  ASSERT_TRUE(url_and_not_incognito);
  ASSERT_FALSE(url_and_not_incognito->IsTriggered(
      {.source = {.url = GURL(kGoogleUrl), .incognito = true}}));
  ASSERT_TRUE(url_and_not_incognito->IsTriggered(
      {.source = {.url = GURL(kGoogleUrl), .incognito = false}}));
  ASSERT_FALSE(url_and_not_incognito->IsTriggered(
      {.source = {.url = GURL(kGoogleUrl)}}));
  ASSERT_FALSE(url_and_not_incognito->IsTriggered(
      {.source = {.url = GURL(kChromiumUrl), .incognito = true}}));
  ASSERT_FALSE(url_and_not_incognito->IsTriggered(
      {.source = {.url = GURL(kChromiumUrl), .incognito = false}}));
  ASSERT_FALSE(url_and_not_incognito->IsTriggered(
      {.source = {.url = GURL(kChromiumUrl)}}));
  ASSERT_FALSE(
      url_and_not_incognito->IsTriggered({.source = {.incognito = true}}));
  ASSERT_FALSE(
      url_and_not_incognito->IsTriggered({.source = {.incognito = false}}));
}

TEST(AttributesConditionTest, URLAndNoIncognitoDestination) {
  // When "incognito" is not in the condition, its value in the context
  // shouldn't affect whether the condition is triggered or not.
  auto any_url = DestinationAttributesCondition::Create(CreateDict(R"(
      {
        "urls": ["*"],
      })"));
  ASSERT_TRUE(any_url);
  ASSERT_TRUE(any_url->IsTriggered(
      {.destination = {.url = GURL(kGoogleUrl), .incognito = true}}));
  ASSERT_TRUE(any_url->IsTriggered(
      {.destination = {.url = GURL(kGoogleUrl), .incognito = false}}));
  ASSERT_TRUE(any_url->IsTriggered({.destination = {.url = GURL(kGoogleUrl)}}));
  ASSERT_FALSE(any_url->IsTriggered({.destination = {.incognito = true}}));
  ASSERT_FALSE(any_url->IsTriggered({.destination = {.incognito = false}}));
  ASSERT_FALSE(any_url->IsTriggered({.destination = {}}));
}

TEST(AttributesConditionTest, URLAndNoIncognitoSource) {
  // When "incognito" is not in the condition, its value in the context
  // shouldn't affect whether the condition is triggered or not.
  auto any_url = SourceAttributesCondition::Create(CreateDict(R"(
      {
        "urls": ["*"],
      })"));
  ASSERT_TRUE(any_url);
  ASSERT_TRUE(any_url->IsTriggered(
      {.source = {.url = GURL(kGoogleUrl), .incognito = true}}));
  ASSERT_TRUE(any_url->IsTriggered(
      {.source = {.url = GURL(kGoogleUrl), .incognito = false}}));
  ASSERT_TRUE(any_url->IsTriggered({.source = {.url = GURL(kGoogleUrl)}}));
  ASSERT_FALSE(any_url->IsTriggered({.source = {.incognito = true}}));
  ASSERT_FALSE(any_url->IsTriggered({.source = {.incognito = false}}));
  ASSERT_FALSE(any_url->IsTriggered({.source = {}}));
}

TEST(AttributesConditionTest, OsClipboardDestination) {
  auto os_clipboard_dst = DestinationAttributesCondition::Create(CreateDict(R"(
      {
        "os_clipboard": true,
      })"));
  ASSERT_TRUE(os_clipboard_dst);
  ASSERT_TRUE(
      os_clipboard_dst->IsTriggered({.destination = {.os_clipboard = true}}));
  ASSERT_FALSE(
      os_clipboard_dst->IsTriggered({.destination = {.os_clipboard = false}}));
  ASSERT_FALSE(
      os_clipboard_dst->IsTriggered({.source = {.os_clipboard = true}}));
  ASSERT_FALSE(
      os_clipboard_dst->IsTriggered({.source = {.os_clipboard = false}}));

  auto non_os_clipboard_dst =
      DestinationAttributesCondition::Create(CreateDict(R"(
      {
        "os_clipboard": false,
      })"));
  ASSERT_TRUE(non_os_clipboard_dst);
  ASSERT_FALSE(non_os_clipboard_dst->IsTriggered(
      {.destination = {.os_clipboard = true}}));
  ASSERT_TRUE(non_os_clipboard_dst->IsTriggered(
      {.destination = {.os_clipboard = false}}));

  // Contexts without a specific `destination` are defaulted to a "false" value
  // for `os_clipboard`, and as such pass the condition of
  // `non_os_clipboard_dst`.
  ASSERT_TRUE(
      non_os_clipboard_dst->IsTriggered({.source = {.os_clipboard = true}}));
  ASSERT_TRUE(
      non_os_clipboard_dst->IsTriggered({.source = {.os_clipboard = false}}));
}

TEST(AttributesConditionTest, OsClipboardSource) {
  auto os_clipboard_src = SourceAttributesCondition::Create(CreateDict(R"(
      {
        "os_clipboard": true,
      })"));
  ASSERT_TRUE(os_clipboard_src);
  ASSERT_FALSE(
      os_clipboard_src->IsTriggered({.destination = {.os_clipboard = true}}));
  ASSERT_FALSE(
      os_clipboard_src->IsTriggered({.destination = {.os_clipboard = false}}));
  ASSERT_TRUE(
      os_clipboard_src->IsTriggered({.source = {.os_clipboard = true}}));
  ASSERT_FALSE(
      os_clipboard_src->IsTriggered({.source = {.os_clipboard = false}}));

  auto non_os_clipboard_src = SourceAttributesCondition::Create(CreateDict(R"(
      {
        "os_clipboard": false,
      })"));
  ASSERT_TRUE(non_os_clipboard_src);
  ASSERT_FALSE(
      non_os_clipboard_src->IsTriggered({.source = {.os_clipboard = true}}));
  ASSERT_TRUE(
      non_os_clipboard_src->IsTriggered({.source = {.os_clipboard = false}}));

  // Contexts without a specific `source` are defaulted to a "false" value
  // for `os_clipboard`, and as such pass the condition of
  // `non_os_clipboard_src`.
  ASSERT_TRUE(non_os_clipboard_src->IsTriggered(
      {.destination = {.os_clipboard = true}}));
  ASSERT_TRUE(non_os_clipboard_src->IsTriggered(
      {.destination = {.os_clipboard = false}}));
}

}  // namespace data_controls
