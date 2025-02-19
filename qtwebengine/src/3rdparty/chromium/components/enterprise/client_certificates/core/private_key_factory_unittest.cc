// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/enterprise/client_certificates/core/private_key_factory.h"

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/test_future.h"
#include "components/enterprise/client_certificates/core/mock_private_key.h"
#include "components/enterprise/client_certificates/core/mock_private_key_factory.h"
#include "components/enterprise/client_certificates/core/private_key.h"
#include "components/enterprise/client_certificates/core/private_key_types.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::StrictMock;

namespace client_certificates {

std::unique_ptr<MockPrivateKeyFactory> CreateMockedFactory() {
  auto mock_factory = std::make_unique<StrictMock<MockPrivateKeyFactory>>();

  ON_CALL(*mock_factory, CreatePrivateKey(_))
      .WillByDefault(Invoke([](PrivateKeyFactory::PrivateKeyCallback callback) {
        std::move(callback).Run(base::MakeRefCounted<MockPrivateKey>());
      }));
  return mock_factory;
}

TEST(PrivateKeyFactoryTest, CreatePrivateKey_NoSupportedSource) {
  auto factory = PrivateKeyFactory::Create({});

  base::test::TestFuture<scoped_refptr<PrivateKey>> test_future;
  factory->CreatePrivateKey(test_future.GetCallback());

  EXPECT_FALSE(test_future.Get());
}

TEST(PrivateKeyFactoryTest, CreatePrivateKey_OnlySoftwareSource) {
  auto software_factory = CreateMockedFactory();
  EXPECT_CALL(*software_factory, CreatePrivateKey(_));

  PrivateKeyFactory::PrivateKeyFactoriesMap map;
  map.insert_or_assign(PrivateKeySource::kSoftwareKey,
                       std::move(software_factory));
  auto factory = PrivateKeyFactory::Create(std::move(map));

  base::test::TestFuture<scoped_refptr<PrivateKey>> test_future;
  factory->CreatePrivateKey(test_future.GetCallback());

  EXPECT_TRUE(test_future.Get());
}

TEST(PrivateKeyFactoryTest, CreatePrivateKey_AllSources) {
  auto unexportable_factory = CreateMockedFactory();
  auto software_factory = CreateMockedFactory();

  EXPECT_CALL(*unexportable_factory, CreatePrivateKey(_));

  PrivateKeyFactory::PrivateKeyFactoriesMap map;
  map.insert_or_assign(PrivateKeySource::kSoftwareKey,
                       std::move(software_factory));
  map.insert_or_assign(PrivateKeySource::kUnexportableKey,
                       std::move(unexportable_factory));
  auto factory = PrivateKeyFactory::Create(std::move(map));

  base::test::TestFuture<scoped_refptr<PrivateKey>> test_future;
  factory->CreatePrivateKey(test_future.GetCallback());

  EXPECT_TRUE(test_future.Get());
}

}  // namespace client_certificates
