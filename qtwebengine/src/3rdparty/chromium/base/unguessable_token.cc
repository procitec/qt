// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/unguessable_token.h"

#include <ostream>

#include "base/check.h"
#include "base/format_macros.h"
#include "base/rand_util.h"
#include "build/build_config.h"

#if !BUILDFLAG(IS_NACL)
#include "third_party/boringssl/src/include/openssl/mem.h"
#endif

namespace base {

UnguessableToken::UnguessableToken(const base::Token& token) : token_(token) {}

// static
UnguessableToken UnguessableToken::Create() {
  Token token = Token::CreateRandom();
  DCHECK(!token.is_zero());
  return UnguessableToken(token);
}

// static
const UnguessableToken& UnguessableToken::Null() {
  static const UnguessableToken null_token{};
  return null_token;
}

// static
absl::optional<UnguessableToken> UnguessableToken::Deserialize(uint64_t high,
                                                               uint64_t low) {
  // Receiving a zeroed out UnguessableToken from another process means that it
  // was never initialized via Create(). Since this method might also be used to
  // create an UnguessableToken from data on disk, we will handle this case more
  // gracefully since data could have been corrupted.
  if (high == 0 && low == 0) {
    return absl::nullopt;
  }
  return UnguessableToken(Token{high, low});
}

// static
absl::optional<UnguessableToken> UnguessableToken::DeserializeFromString(
    StringPiece string_representation) {
  auto token = Token::FromString(string_representation);
  // A zeroed out token means that it's not initialized via Create().
  if (!token.has_value() || token.value().is_zero()) {
    return absl::nullopt;
  }
  return UnguessableToken(token.value());
}

bool UnguessableToken::operator==(const UnguessableToken& other) const {
#if BUILDFLAG(IS_NACL)
  // BoringSSL is unavailable for NaCl builds so it remains timing dependent.
  return token_ == other.token_;
#else
  auto bytes = token_.AsBytes();
  auto other_bytes = other.token_.AsBytes();

  return CRYPTO_memcmp(bytes.data(), other_bytes.data(), bytes.size()) == 0;
#endif
}

std::ostream& operator<<(std::ostream& out, const UnguessableToken& token) {
  return out << "(" << token.ToString() << ")";
}

}  // namespace base
