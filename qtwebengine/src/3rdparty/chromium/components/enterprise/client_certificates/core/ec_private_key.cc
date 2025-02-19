// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/enterprise/client_certificates/core/ec_private_key.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "components/enterprise/client_certificates/core/private_key_types.h"
#include "crypto/ec_private_key.h"
#include "crypto/ec_signature_creator.h"

namespace client_certificates {

ECPrivateKey::ECPrivateKey(std::unique_ptr<crypto::ECPrivateKey> key)
    : PrivateKey(PrivateKeySource::kSoftwareKey), key_(std::move(key)) {
  CHECK(key_);
}

ECPrivateKey::~ECPrivateKey() = default;

std::optional<std::vector<uint8_t>> ECPrivateKey::SignSlowly(
    base::span<const uint8_t> data) const {
  auto signer = crypto::ECSignatureCreator::Create(key_.get());
  if (!signer) {
    return std::nullopt;
  }

  std::vector<uint8_t> signature;
  if (!signer->Sign(data, &signature)) {
    return std::nullopt;
  }
  return signature;
}

std::vector<uint8_t> ECPrivateKey::GetSubjectPublicKeyInfo() const {
  std::vector<uint8_t> pubkey;
  if (!key_->ExportPublicKey(&pubkey)) {
    return std::vector<uint8_t>();
  }
  return pubkey;
}

crypto::SignatureVerifier::SignatureAlgorithm ECPrivateKey::GetAlgorithm()
    const {
  return crypto::SignatureVerifier::ECDSA_SHA256;
}

}  // namespace client_certificates
