// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef NEARBY_PRESENCE_NP_CPP_FFI_INCLUDE_NP_PROTOCOL_H_
#define NEARBY_PRESENCE_NP_CPP_FFI_INCLUDE_NP_PROTOCOL_H_

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "np_cpp_ffi_types.h"

#include <span>

// This namespace provides a C++ API surface to the Rust nearby protocol
// implementation. This is a wrapper over the np_ffi::internal namespace defined
// in the headers np_cpp_ffi_functions.h and np_cpp_ffi_types.h which are
// autogenerated by cbindgen based on the rust crate np_c_ffi.
//
// Classes in this namespace which have explicitly deleted copy-assignment and
// copy-constructor functions are backed by a C handle allocated in the
// underlying rust library. These handles are managed by the C++ classes which
// hold them and will be freed automatically when the corresponding  type which
// currently owns the handle goes out of scope.
//
// A note on conventions in this API surface:
//
// Function prefixed with `Into` will transfer ownership of the current instance
// into a the newly returned object type, meaning that the previous instance is
// no longer valid. Attempting to use a type which has already been moved out of
// will result in the panic handler being invoked, which by default triggers
// std::abort();
//
// Functions prefixed with `As` will not transfer ownership and instead return a
// casted version of the previous object.
//
// Functions prefixed with `Try` are fallible and will return an
// absl::StatusOr<T> result that needs to be checked before its used
//
// DO NOT DIRECTLY access the types defined in np_ffi::internal::*, these are
// auto-generated and are easy to mis-use. Instead use the public types exposed
// via the `nearby_protocol` namespace.
namespace nearby_protocol {

// Re-exporting cbindgen generated types which are used in the public API
using np_ffi::internal::AddCredentialToSlabResult;
using np_ffi::internal::BooleanActionType;
using np_ffi::internal::CreateCredentialBookResultKind;
using np_ffi::internal::CreateCredentialSlabResultKind;
using np_ffi::internal::DeserializeAdvertisementResultKind;
using np_ffi::internal::DeserializedV0AdvertisementKind;
using np_ffi::internal::DeserializedV0IdentityKind;
using np_ffi::internal::DeserializedV1IdentityKind;
using np_ffi::internal::GetV0DEResultKind;
using np_ffi::internal::PanicReason;
using np_ffi::internal::TxPower;
using np_ffi::internal::V0DataElementKind;

template <uintptr_t N> using FfiByteBuffer = np_ffi::internal::ByteBuffer<N>;

// All of the types defined in this header
class RawAdvertisementPayload;
class CredentialBook;
class CredentialSlab;
class Deserializer;
class DeserializeAdvertisementResult;
class MatchedCredentialData;
class V0MatchableCredential;
class V1MatchableCredential;

// V0 Classes
class DeserializedV0Advertisement;
class LegibleDeserializedV0Advertisement;
class V0DataElement;
class V0Payload;
class V0Actions;

// V1 Classes
class DeserializedV1Advertisement;
class DeserializedV1Section;
class V1DataElement;

// Global static singleton class used to customize the deserialization library.
// If no values are set, then the default values will be used. In most cases the
// default max instance values won't need to be changed unless you are running
// in a constrained resources environment, and need to specify an upper limit
// on memory consumption. See np_ffi_global_config_* functions in
// np_cpp_ffi_functions.h for more info
class GlobalConfig {
public:
  // This class provides the static methods needed for global configuration,
  // so it does not need to be constructable, cloneable, or assignable.
  GlobalConfig(GlobalConfig const &) = delete;
  void operator=(GlobalConfig const &) = delete;
  GlobalConfig() = delete;

  // Provides a user specified panic handler. This method will only have an
  // effect on the global panic-handler the first time it's called, and this
  // method will return `true` to indicate that the panic handler was
  // successfully set. All subsequent calls to this method will simply ignore
  // the argument and return `false`. see np_ffi_global_config_panic_handler in
  // np_cpp_ffi_functions.h for more info
  [[nodiscard]] static bool SetPanicHandler(void (*handler)(PanicReason));

  // Sets an override to the number of shards employed in the libraries internal
  // handle maps, used to set an upper limit on memory consumption, see
  // np_ffi_global_config_set_num_shards in np_cpp_ffi_functions.h for more info
  static void SetNumShards(uint8_t num_shards);

  // Sets the maximum number of active handles to credential slabs which may be
  // active at any one time. See
  // np_ffi_global_config_set_max_num_credential_slabs in np_cpp_ffi_functions.h
  // for more info
  static void SetMaxNumCredentialSlabs(uint32_t max_num_credential_slabs);

  // Sets the maximum number of active handles to credential books which may be
  // active at any one time. See
  // np_ffi_global_config_set_max_num_credential_books in np_cpp_ffi_functions.h
  // for more info
  static void SetMaxNumCredentialBooks(uint32_t max_num_credential_books);

  // Sets the maximum number of active handles to deserialized v0 advertisements
  // which may be active at any one time. See
  // np_ffi_global_config_set_max_num_deserialized_v0_advertisements for more
  // info.
  static void SetMaxNumDeserializedV0Advertisements(
      uint32_t max_num_deserialized_v0_advertisements);

  // Sets the maximum number of active handles to deserialized v1 advertisements
  // which may be active at any one time
  static void SetMaxNumDeserializedV1Advertisements(
      uint32_t max_num_deserialized_v1_advertisements);
};

// Holds the credentials used in the construction of a credential book
// using CredentialBook::TryCreateFromSlab()
class CredentialSlab {
public:
  // Don't allow copy constructor or copy assignment, since that would result in
  // the underlying handle being freed multiple times
  CredentialSlab(const CredentialSlab &other) = delete;
  CredentialSlab &operator=(const CredentialSlab &other) = delete;

  // Move constructor and move assignment are needed in order to wrap this class
  // in absl::StatusOr
  CredentialSlab(CredentialSlab &&other) noexcept;
  CredentialSlab &operator=(CredentialSlab &&other) noexcept;

  // The destructor for a CredentialSlab, this will be called when a
  // CredentialSlab instance goes out of scope and will free the underlying
  // resources
  ~CredentialSlab();

  // Creates a new instance of a CredentialSlab, returns the CredentialSlab on
  // success or a Status code on failure
  [[nodiscard]] static absl::StatusOr<CredentialSlab> TryCreate();

  // Adds a V0 credential to the slab
  [[nodiscard]] absl::Status AddV0Credential(V0MatchableCredential v0_cred);

  // Adds a V1 credential to the slab
  [[nodiscard]] absl::Status AddV1Credential(V1MatchableCredential v1_cred);

private:
  friend class CredentialBook;
  explicit CredentialSlab(np_ffi::internal::CredentialSlab credential_slab)
      : credential_slab_(credential_slab), moved_(false) {}

  np_ffi::internal::CredentialSlab credential_slab_;
  bool moved_;
};

// Holds the credentials used when decrypting data of an advertisement.
// This needs to be passed to Deserializer::DeserializeAdvertisement() when
// attempting to deserialize a payload
class CredentialBook {
public:
  // Don't allow copy constructor or copy assignment, since that would result in
  // the underlying handle being freed multiple times
  CredentialBook(const CredentialBook &other) = delete;
  CredentialBook &operator=(const CredentialBook &other) = delete;

  // Move constructor and move assignment are needed in order to wrap this class
  // in absl::StatusOr
  CredentialBook(CredentialBook &&other) noexcept;
  CredentialBook &operator=(CredentialBook &&other) noexcept;

  // The destructor for a CredentialBook, this will be called when a
  // CredentialBook instance goes out of scope and will free the underlying
  // resources
  ~CredentialBook();

  // Creates a new instance of a CredentialBook from a CredentialSlab,
  // returning the CredentialBook on success or a Status code on failure.
  // The passed credential-slab will be deallocated if this operation
  // is successful.
  [[nodiscard]] static absl::StatusOr<CredentialBook>
  TryCreateFromSlab(CredentialSlab &slab);

private:
  friend class Deserializer;
  explicit CredentialBook(np_ffi::internal::CredentialBook credential_book)
      : credential_book_(credential_book), moved_(false) {}

  np_ffi::internal::CredentialBook credential_book_;
  bool moved_;
};

// Holds data associated with a specific credential which will be returned to
// the caller when it is successfully matched with an advertisement.
class MatchedCredentialData {
public:
  // Creates matched credential data from a provided credential_id used to
  // correlate the data back to its full credential data, and the metadata byte
  // buffer as copied from the given span over bytes. After calling
  // this the bytes are copied into the rust code, so the
  // encrypted_metadata_bytes_buffer can be freed.
  //
  // Safety: this is safe if the span is over a valid buffer of bytes. The copy
  // from the memory address isn't atomic, so concurrent modification of the
  // array from another thread would cause undefined behavior.
  [[nodiscard]] MatchedCredentialData(uint32_t cred_id,
                                      std::span<uint8_t> metadata_bytes);

private:
  np_ffi::internal::FfiMatchedCredential data_;
  friend class V0MatchableCredential;
  friend class V1MatchableCredential;
};

// Holds the v0 credential data needed by the deserializer to decrypt
// advertisements, along with some provided matched data that will be returned
// back to the caller upon a successful credential match.
class V0MatchableCredential {
public:
  // Creates a new V0MatchableCredential from a key seed, its calculated hmac
  // value and some match data.
  [[nodiscard]] V0MatchableCredential(
      std::array<uint8_t, 32> key_seed,
      std::array<uint8_t, 32> legacy_metadata_key_hmac,
      MatchedCredentialData matched_credential_data);

private:
  friend class CredentialSlab;
  np_ffi::internal::V0MatchableCredential internal_{};
};

// Holds the v1 credential data needed by the deserializer to decrypt
// advertisements, along with some provided matched data that will be returned
// back to the caller upon a successful credential match.
class V1MatchableCredential {
public:
  // Creates a new V1MatchableCredential from key material, its calculated hmac
  // value and some match data.
  [[nodiscard]] V1MatchableCredential(
      std::array<uint8_t, 32> key_seed,
      std::array<uint8_t, 32> expected_unsigned_metadata_key_hmac,
      std::array<uint8_t, 32> expected_signed_metadata_key_hmac,
      std::array<uint8_t, 32> pub_key,
      MatchedCredentialData matched_credential_data);

private:
  friend class CredentialSlab;
  np_ffi::internal::V1MatchableCredential internal_;
};

// Representation of a buffer of bytes returned from deserialization APIs
template <size_t N> class ByteBuffer {
public:
  // Constructor for a fixed length buffer of bytes from its internal struct
  // data representation consisting of a length and array of unint8_t bytes.
  [[nodiscard]] explicit ByteBuffer(FfiByteBuffer<N> internal)
      : internal_(internal) {}

  // Creates a ByteBuffer from a absl::string_view of bytes, returning an
  // absl::OutOfRangeError in the case where bytes is too large to fit into the
  // buffer. On success the returned type contains a copy of the provided bytes.
  [[nodiscard]] static absl::StatusOr<ByteBuffer<N>>
  CopyFrom(absl::string_view bytes) {
    if (bytes.length() > N) {
      return absl::OutOfRangeError(
          absl::StrFormat("Provided bytes of length %d will not fit into a "
                          "ByteBuffer<N> of size N=%d",
                          bytes.length(), N));
    }
    np_ffi::internal::ByteBuffer<N> internal =
        np_ffi::internal::ByteBuffer<N>();
    internal.len = bytes.length();
    std::copy(std::begin(bytes), std::end(bytes), internal.bytes);
    return ByteBuffer(internal);
  }

  // Helper method to convert the ByteBuffer into a vector. The vector will
  // contain a copy of the bytes and won't share the underlying buffer.
  [[nodiscard]] std::vector<uint8_t> ToVector() {
    std::vector<uint8_t> result(internal_.bytes,
                                internal_.bytes + internal_.len);
    return result;
  }

  // Helper method to convert the ByteBuffer into a std::string. The returned
  // string will contain a copy of the bytes and won't share the underlying
  // buffer.
  [[nodiscard]] std::string ToString() {
    std::string result;
    result.assign(internal_.bytes, internal_.bytes + internal_.len);
    return result;
  }

private:
  friend class V1DataElement;
  friend class Deserializer;
  np_ffi::internal::ByteBuffer<N> internal_;
};

class RawAdvertisementPayload {
public:
  // Creates a RawAdvertisementPayload from a ByteBuffer.
  explicit RawAdvertisementPayload(ByteBuffer<255> bytes) : buffer_(bytes) {}

  ByteBuffer<255> buffer_;

private:
  friend class Deserializer;
};

// A global static Deserializer, configured by GlobalConfig and used to
// deserialize advertisement payloads
class Deserializer {
public:
  // Attempts to deserialize an advertisement with the given service-data
  // payload (presumed to be under the NP service UUID) using credentials pulled
  // from the given credential-book. See np_ffi_deserialize_advertisement in
  // np_cpp_ffi_functions.h for more info
  [[nodiscard]] static DeserializeAdvertisementResult
  DeserializeAdvertisement(RawAdvertisementPayload &payload,
                           const CredentialBook &credential_book);
};

// The result type returned from Deserializer::DeserializeAdvertisement(). Can
// be used to further process the advertisement and inspect its contents
class DeserializeAdvertisementResult {
public:
  // Don't allow copy constructor or copy assignment, since that would result in
  // the underlying handle being freed multiple times
  DeserializeAdvertisementResult(const DeserializeAdvertisementResult &other) =
      delete;
  DeserializeAdvertisementResult &
  operator=(const DeserializeAdvertisementResult &other) = delete;

  // Move constructor and move assignment operators
  DeserializeAdvertisementResult(
      DeserializeAdvertisementResult &&other) noexcept;
  DeserializeAdvertisementResult &
  operator=(DeserializeAdvertisementResult &&other) noexcept;

  // Frees the underlying resources of the result.
  ~DeserializeAdvertisementResult();

  // Returns the DeserializeAdvertisementResultKind of the Result
  [[nodiscard]] DeserializeAdvertisementResultKind GetKind();

  // Casts a `DeserializeAdvertisementResult` to the `V0` variant, panicking in
  // the case where the passed value is of a different enum variant. This can
  // only be called once. When called, this object is moved into the
  // returned 'DeserializedV0Advertisement' and this object is no longer valid.
  [[nodiscard]] DeserializedV0Advertisement IntoV0();

  // Casts a `DeserializeAdvertisementResult` to the `V1` variant, panicking in
  // the case where the passed value is of a different enum variant. This can
  // only be called once. After this is cast into a `V1` variant this result
  // is no longer valid.
  [[nodiscard]] DeserializedV1Advertisement IntoV1();

private:
  friend class Deserializer;
  explicit DeserializeAdvertisementResult(
      np_ffi::internal::DeserializeAdvertisementResult result)
      : result_(result), moved_(false) {}

  np_ffi::internal::DeserializeAdvertisementResult result_;
  bool moved_;
};

// A deserialized V0 advertisement payload
class DeserializedV0Advertisement {
public:
  // Don't allow copy constructor or copy assignment, since that would result in
  // the underlying handle being freed multiple times
  DeserializedV0Advertisement(const DeserializedV0Advertisement &other) =
      delete;
  DeserializedV0Advertisement &
  operator=(const DeserializedV0Advertisement &other) = delete;

  // Move constructor and move assignment operators
  DeserializedV0Advertisement(DeserializedV0Advertisement &&other) noexcept;
  DeserializedV0Advertisement &
  operator=(DeserializedV0Advertisement &&other) noexcept;

  // The destructor which will be called when a DeserializedV0Advertisement
  // instance goes out of scope, and will free the underlying resources
  ~DeserializedV0Advertisement();

  // Returns the DeserializedV0AdvertisementKind of the advertisement
  [[nodiscard]] DeserializedV0AdvertisementKind GetKind();

  // Casts a `DeserializedV0Advertisement` to the `Legible` variant, panicking
  // in the case where the passed value is of a different enum variant.
  // After calling this the object is moved into the returned
  // `LegibleDeserializedV0Advertisement`, and this object is no longer valid.
  [[nodiscard]] LegibleDeserializedV0Advertisement IntoLegible();

private:
  friend class DeserializeAdvertisementResult;
  explicit DeserializedV0Advertisement(
      np_ffi::internal::DeserializedV0Advertisement v0_advertisement)
      : v0_advertisement_(v0_advertisement), moved_(false) {}

  np_ffi::internal::DeserializedV0Advertisement v0_advertisement_;
  bool moved_;
};

// A Legible deserialized V0 advertisement, which means the contents of it are
// either plaintext OR have already been decrypted successfully by a matching
// credential in the provided CredentialBook
class LegibleDeserializedV0Advertisement {
public:
  // Don't allow copy constructor or copy assignment, since that would result in
  // the underlying handle being freed multiple times
  LegibleDeserializedV0Advertisement(
      const LegibleDeserializedV0Advertisement &other) = delete;
  LegibleDeserializedV0Advertisement &
  operator=(const LegibleDeserializedV0Advertisement &other) = delete;

  // Move constructor and move assignment operators
  LegibleDeserializedV0Advertisement(
      LegibleDeserializedV0Advertisement &&other) noexcept;
  LegibleDeserializedV0Advertisement &
  operator=(LegibleDeserializedV0Advertisement &&other) noexcept;

  // The destructor which will be called when a this instance goes out of scope,
  // and will free the underlying parent handle.
  ~LegibleDeserializedV0Advertisement();

  // Returns just the kind of identity (public/encrypted)
  // associated with the advertisement
  [[nodiscard]] DeserializedV0IdentityKind GetIdentityKind();
  // Returns the number of data elements in the advertisement
  [[nodiscard]] uint8_t GetNumberOfDataElements();
  // Returns just the data-element payload of the advertisement
  [[nodiscard]] V0Payload IntoPayload();

private:
  friend class DeserializedV0Advertisement;
  explicit LegibleDeserializedV0Advertisement(
      np_ffi::internal::LegibleDeserializedV0Advertisement
          legible_v0_advertisement)
      : legible_v0_advertisement_(legible_v0_advertisement), moved_(false) {}

  np_ffi::internal::LegibleDeserializedV0Advertisement
      legible_v0_advertisement_;
  bool moved_;
};

// A data element payload of a Deserialized V0 Advertisement.
class V0Payload {
public:
  // Don't allow copy constructor or copy assignment, since that would result in
  // the underlying parent handle being freed multiple times.
  V0Payload(const V0Payload &other) = delete;
  V0Payload &operator=(const V0Payload &other) = delete;

  // Move constructor and move assignment operators
  V0Payload(V0Payload &&other) noexcept;
  V0Payload &operator=(V0Payload &&other) noexcept;

  // Frees the underlying handle when this goes out of scope
  ~V0Payload();

  // Tries to retrieve the data element at the given index, returns the data
  // element if it exists otherwise returns an Error status code
  [[nodiscard]] absl::StatusOr<V0DataElement> TryGetDataElement(uint8_t index);

private:
  friend class LegibleDeserializedV0Advertisement;
  explicit V0Payload(np_ffi::internal::V0Payload v0_payload)
      : v0_payload_(v0_payload), moved_(false) {}

  np_ffi::internal::V0Payload v0_payload_;
  bool moved_;
};

// A single deserialized V0 data element
class V0DataElement {
public:
  // Yields the V0DataElementKind of the data element
  [[nodiscard]] V0DataElementKind GetKind();
  // Casts the V0DataElement into the TxPower variant, panicking in the case
  // where the data element is of a different enum variant
  [[nodiscard]] TxPower AsTxPower();
  // Casts the V0DataElement into the Actions variant, panicking in the case
  // where the data element is of a different enum variant
  [[nodiscard]] V0Actions AsActions();

private:
  friend class V0Payload;
  explicit V0DataElement(np_ffi::internal::V0DataElement v0_data_element)
      : v0_data_element_(v0_data_element) {}
  np_ffi::internal::V0DataElement v0_data_element_;
};

// A V0 Actions Data Element
class V0Actions {
public:
  // Gets the V0 Action bits as represented by a u32 where the last 8 bits are
  // always 0 since V0 actions can only hold up to 24 bits.
  [[nodiscard]] uint32_t GetAsU32();

  /// Return whether a boolean action type is present in this data element
  [[nodiscard]] bool HasAction(BooleanActionType action);

  /// Gets the 4 bit context sync sequence number as a uint8_t from this data
  /// element
  [[nodiscard]] uint8_t GetContextSyncSequenceNumber();

private:
  friend class V0DataElement;
  explicit V0Actions(np_ffi::internal::V0Actions actions) : actions_(actions) {}
  np_ffi::internal::V0Actions actions_;
};

// A deserialized V1 Advertisement payload
class DeserializedV1Advertisement {
public:
  // Don't allow copy constructor or copy assignment, since that would result in
  // the underlying handle being freed multiple times
  DeserializedV1Advertisement(const DeserializedV1Advertisement &other) =
      delete;
  DeserializedV1Advertisement &
  operator=(const DeserializedV1Advertisement &other) = delete;

  // Move constructor and move assignment operators
  DeserializedV1Advertisement(DeserializedV1Advertisement &&other) noexcept;
  DeserializedV1Advertisement &
  operator=(DeserializedV1Advertisement &&other) noexcept;

  // Gets the number of legible sections on a deserialized V1 advertisement.
  // This is usable as an iteration bound for the section_index of TryGetSection
  [[nodiscard]] uint8_t GetNumLegibleSections();
  // Gets the number of sections on a deserialized V1 advertisement which
  // were unable to be decrypted with the credentials that the receiver
  // possesses
  [[nodiscard]] uint8_t GetNumUndecryptableSections();
  // Tries to get the section with the given index in a deserialized V1
  // advertisement. Returns a error code in the result of an invalid index
  [[nodiscard]] absl::StatusOr<DeserializedV1Section>
  TryGetSection(uint8_t section_index);

private:
  friend class DeserializeAdvertisementResult;
  explicit DeserializedV1Advertisement(
      np_ffi::internal::DeserializedV1Advertisement v1_advertisement);

  std::shared_ptr<np_ffi::internal::DeserializedV1Advertisement>
      v1_advertisement_;
};

// A Deserialized V1 Section of an advertisement
class DeserializedV1Section {
public:
  // Returns the number of data elements present in the section
  [[nodiscard]] uint8_t NumberOfDataElements();
  // Returns the DeserializedV1IdentityKind of the identity
  [[nodiscard]] DeserializedV1IdentityKind GetIdentityKind();
  // Tries to get the data element in the section at the given index
  [[nodiscard]] absl::StatusOr<V1DataElement> TryGetDataElement(uint8_t index);

private:
  friend class DeserializedV1Advertisement;
  explicit DeserializedV1Section(
      np_ffi::internal::DeserializedV1Section section,
      std::shared_ptr<np_ffi::internal::DeserializedV1Advertisement>
          owning_v1_advertisement)
      : section_(section),
        owning_v1_advertisement_(std::move(owning_v1_advertisement)) {}
  np_ffi::internal::DeserializedV1Section section_;
  std::shared_ptr<np_ffi::internal::DeserializedV1Advertisement>
      owning_v1_advertisement_;
};

// A V1 Data Element
class V1DataElement {
public:
  // Yields the unsigned 32-bit integer V1 DE type code
  [[nodiscard]] uint32_t GetDataElementTypeCode() const;
  // Yields the payload bytes of the data element
  [[nodiscard]] ByteBuffer<127> GetPayload() const;

private:
  friend class DeserializedV1Section;
  explicit V1DataElement(np_ffi::internal::V1DataElement v1_data_element)
      : v1_data_element_(v1_data_element) {}
  np_ffi::internal::V1DataElement v1_data_element_;
};

} //  namespace nearby_protocol

#endif // NEARBY_PRESENCE_NP_CPP_FFI_INCLUDE_NP_PROTOCOL_H_
