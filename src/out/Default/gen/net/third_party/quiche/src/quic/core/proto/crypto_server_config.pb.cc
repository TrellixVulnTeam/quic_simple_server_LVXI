// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: crypto_server_config.proto

#include "crypto_server_config.pb.h"

#include <algorithm>

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/wire_format_lite.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
// @@protoc_insertion_point(includes)
#include <google/protobuf/port_def.inc>
extern PROTOBUF_INTERNAL_EXPORT_crypto_5fserver_5fconfig_2eproto ::PROTOBUF_NAMESPACE_ID::internal::SCCInfo<0> scc_info_QuicServerConfigProtobuf_PrivateKey_crypto_5fserver_5fconfig_2eproto;
namespace quic {
class QuicServerConfigProtobuf_PrivateKeyDefaultTypeInternal {
 public:
  ::PROTOBUF_NAMESPACE_ID::internal::ExplicitlyConstructed<QuicServerConfigProtobuf_PrivateKey> _instance;
} _QuicServerConfigProtobuf_PrivateKey_default_instance_;
class QuicServerConfigProtobufDefaultTypeInternal {
 public:
  ::PROTOBUF_NAMESPACE_ID::internal::ExplicitlyConstructed<QuicServerConfigProtobuf> _instance;
} _QuicServerConfigProtobuf_default_instance_;
}  // namespace quic
static void InitDefaultsscc_info_QuicServerConfigProtobuf_crypto_5fserver_5fconfig_2eproto() {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  {
    void* ptr = &::quic::_QuicServerConfigProtobuf_default_instance_;
    new (ptr) ::quic::QuicServerConfigProtobuf();
    ::PROTOBUF_NAMESPACE_ID::internal::OnShutdownDestroyMessage(ptr);
  }
  ::quic::QuicServerConfigProtobuf::InitAsDefaultInstance();
}

NET_EXPORT_PRIVATE ::PROTOBUF_NAMESPACE_ID::internal::SCCInfo<1> scc_info_QuicServerConfigProtobuf_crypto_5fserver_5fconfig_2eproto =
    {{ATOMIC_VAR_INIT(::PROTOBUF_NAMESPACE_ID::internal::SCCInfoBase::kUninitialized), 1, InitDefaultsscc_info_QuicServerConfigProtobuf_crypto_5fserver_5fconfig_2eproto}, {
      &scc_info_QuicServerConfigProtobuf_PrivateKey_crypto_5fserver_5fconfig_2eproto.base,}};

static void InitDefaultsscc_info_QuicServerConfigProtobuf_PrivateKey_crypto_5fserver_5fconfig_2eproto() {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  {
    void* ptr = &::quic::_QuicServerConfigProtobuf_PrivateKey_default_instance_;
    new (ptr) ::quic::QuicServerConfigProtobuf_PrivateKey();
    ::PROTOBUF_NAMESPACE_ID::internal::OnShutdownDestroyMessage(ptr);
  }
  ::quic::QuicServerConfigProtobuf_PrivateKey::InitAsDefaultInstance();
}

NET_EXPORT_PRIVATE ::PROTOBUF_NAMESPACE_ID::internal::SCCInfo<0> scc_info_QuicServerConfigProtobuf_PrivateKey_crypto_5fserver_5fconfig_2eproto =
    {{ATOMIC_VAR_INIT(::PROTOBUF_NAMESPACE_ID::internal::SCCInfoBase::kUninitialized), 0, InitDefaultsscc_info_QuicServerConfigProtobuf_PrivateKey_crypto_5fserver_5fconfig_2eproto}, {}};

namespace quic {

// ===================================================================

void QuicServerConfigProtobuf_PrivateKey::InitAsDefaultInstance() {
}
class QuicServerConfigProtobuf_PrivateKey::_Internal {
 public:
  using HasBits = decltype(std::declval<QuicServerConfigProtobuf_PrivateKey>()._has_bits_);
  static void set_has_tag(HasBits* has_bits) {
    (*has_bits)[0] |= 2u;
  }
  static void set_has_private_key(HasBits* has_bits) {
    (*has_bits)[0] |= 1u;
  }
};

QuicServerConfigProtobuf_PrivateKey::QuicServerConfigProtobuf_PrivateKey()
  : ::PROTOBUF_NAMESPACE_ID::MessageLite(), _internal_metadata_(nullptr) {
  SharedCtor();
  // @@protoc_insertion_point(constructor:quic.QuicServerConfigProtobuf.PrivateKey)
}
QuicServerConfigProtobuf_PrivateKey::QuicServerConfigProtobuf_PrivateKey(const QuicServerConfigProtobuf_PrivateKey& from)
  : ::PROTOBUF_NAMESPACE_ID::MessageLite(),
      _internal_metadata_(nullptr),
      _has_bits_(from._has_bits_) {
  _internal_metadata_.MergeFrom(from._internal_metadata_);
  private_key_.UnsafeSetDefault(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited());
  if (from.has_private_key()) {
    private_key_.AssignWithDefault(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), from.private_key_);
  }
  tag_ = from.tag_;
  // @@protoc_insertion_point(copy_constructor:quic.QuicServerConfigProtobuf.PrivateKey)
}

void QuicServerConfigProtobuf_PrivateKey::SharedCtor() {
  ::PROTOBUF_NAMESPACE_ID::internal::InitSCC(&scc_info_QuicServerConfigProtobuf_PrivateKey_crypto_5fserver_5fconfig_2eproto.base);
  private_key_.UnsafeSetDefault(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited());
  tag_ = 0u;
}

QuicServerConfigProtobuf_PrivateKey::~QuicServerConfigProtobuf_PrivateKey() {
  // @@protoc_insertion_point(destructor:quic.QuicServerConfigProtobuf.PrivateKey)
  SharedDtor();
}

void QuicServerConfigProtobuf_PrivateKey::SharedDtor() {
  private_key_.DestroyNoArena(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited());
}

void QuicServerConfigProtobuf_PrivateKey::SetCachedSize(int size) const {
  _cached_size_.Set(size);
}
const QuicServerConfigProtobuf_PrivateKey& QuicServerConfigProtobuf_PrivateKey::default_instance() {
  ::PROTOBUF_NAMESPACE_ID::internal::InitSCC(&::scc_info_QuicServerConfigProtobuf_PrivateKey_crypto_5fserver_5fconfig_2eproto.base);
  return *internal_default_instance();
}


void QuicServerConfigProtobuf_PrivateKey::Clear() {
// @@protoc_insertion_point(message_clear_start:quic.QuicServerConfigProtobuf.PrivateKey)
  ::PROTOBUF_NAMESPACE_ID::uint32 cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  cached_has_bits = _has_bits_[0];
  if (cached_has_bits & 0x00000001u) {
    private_key_.ClearNonDefaultToEmptyNoArena();
  }
  tag_ = 0u;
  _has_bits_.Clear();
  _internal_metadata_.Clear();
}

#if GOOGLE_PROTOBUF_ENABLE_EXPERIMENTAL_PARSER
const char* QuicServerConfigProtobuf_PrivateKey::_InternalParse(const char* ptr, ::PROTOBUF_NAMESPACE_ID::internal::ParseContext* ctx) {
#define CHK_(x) if (PROTOBUF_PREDICT_FALSE(!(x))) goto failure
  _Internal::HasBits has_bits{};
  while (!ctx->Done(&ptr)) {
    ::PROTOBUF_NAMESPACE_ID::uint32 tag;
    ptr = ::PROTOBUF_NAMESPACE_ID::internal::ReadTag(ptr, &tag);
    CHK_(ptr);
    switch (tag >> 3) {
      // required uint32 tag = 1;
      case 1:
        if (PROTOBUF_PREDICT_TRUE(static_cast<::PROTOBUF_NAMESPACE_ID::uint8>(tag) == 8)) {
          _Internal::set_has_tag(&has_bits);
          tag_ = ::PROTOBUF_NAMESPACE_ID::internal::ReadVarint(&ptr);
          CHK_(ptr);
        } else goto handle_unusual;
        continue;
      // required bytes private_key = 2;
      case 2:
        if (PROTOBUF_PREDICT_TRUE(static_cast<::PROTOBUF_NAMESPACE_ID::uint8>(tag) == 18)) {
          ptr = ::PROTOBUF_NAMESPACE_ID::internal::InlineGreedyStringParser(mutable_private_key(), ptr, ctx);
          CHK_(ptr);
        } else goto handle_unusual;
        continue;
      default: {
      handle_unusual:
        if ((tag & 7) == 4 || tag == 0) {
          ctx->SetLastTag(tag);
          goto success;
        }
        ptr = UnknownFieldParse(tag, &_internal_metadata_, ptr, ctx);
        CHK_(ptr != nullptr);
        continue;
      }
    }  // switch
  }  // while
success:
  _has_bits_.Or(has_bits);
  return ptr;
failure:
  ptr = nullptr;
  goto success;
#undef CHK_
}
#else  // GOOGLE_PROTOBUF_ENABLE_EXPERIMENTAL_PARSER
bool QuicServerConfigProtobuf_PrivateKey::MergePartialFromCodedStream(
    ::PROTOBUF_NAMESPACE_ID::io::CodedInputStream* input) {
#define DO_(EXPRESSION) if (!PROTOBUF_PREDICT_TRUE(EXPRESSION)) goto failure
  ::PROTOBUF_NAMESPACE_ID::uint32 tag;
  ::PROTOBUF_NAMESPACE_ID::internal::LiteUnknownFieldSetter unknown_fields_setter(
      &_internal_metadata_);
  ::PROTOBUF_NAMESPACE_ID::io::StringOutputStream unknown_fields_output(
      unknown_fields_setter.buffer());
  ::PROTOBUF_NAMESPACE_ID::io::CodedOutputStream unknown_fields_stream(
      &unknown_fields_output, false);
  // @@protoc_insertion_point(parse_start:quic.QuicServerConfigProtobuf.PrivateKey)
  for (;;) {
    ::std::pair<::PROTOBUF_NAMESPACE_ID::uint32, bool> p = input->ReadTagWithCutoffNoLastTag(127u);
    tag = p.first;
    if (!p.second) goto handle_unusual;
    switch (::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::GetTagFieldNumber(tag)) {
      // required uint32 tag = 1;
      case 1: {
        if (static_cast< ::PROTOBUF_NAMESPACE_ID::uint8>(tag) == (8 & 0xFF)) {
          _Internal::set_has_tag(&_has_bits_);
          DO_((::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::ReadPrimitive<
                   ::PROTOBUF_NAMESPACE_ID::uint32, ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::TYPE_UINT32>(
                 input, &tag_)));
        } else {
          goto handle_unusual;
        }
        break;
      }

      // required bytes private_key = 2;
      case 2: {
        if (static_cast< ::PROTOBUF_NAMESPACE_ID::uint8>(tag) == (18 & 0xFF)) {
          DO_(::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::ReadBytes(
                input, this->mutable_private_key()));
        } else {
          goto handle_unusual;
        }
        break;
      }

      default: {
      handle_unusual:
        if (tag == 0) {
          goto success;
        }
        DO_(::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::SkipField(
            input, tag, &unknown_fields_stream));
        break;
      }
    }
  }
success:
  // @@protoc_insertion_point(parse_success:quic.QuicServerConfigProtobuf.PrivateKey)
  return true;
failure:
  // @@protoc_insertion_point(parse_failure:quic.QuicServerConfigProtobuf.PrivateKey)
  return false;
#undef DO_
}
#endif  // GOOGLE_PROTOBUF_ENABLE_EXPERIMENTAL_PARSER

void QuicServerConfigProtobuf_PrivateKey::SerializeWithCachedSizes(
    ::PROTOBUF_NAMESPACE_ID::io::CodedOutputStream* output) const {
  // @@protoc_insertion_point(serialize_start:quic.QuicServerConfigProtobuf.PrivateKey)
  ::PROTOBUF_NAMESPACE_ID::uint32 cached_has_bits = 0;
  (void) cached_has_bits;

  cached_has_bits = _has_bits_[0];
  // required uint32 tag = 1;
  if (cached_has_bits & 0x00000002u) {
    ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::WriteUInt32(1, this->tag(), output);
  }

  // required bytes private_key = 2;
  if (cached_has_bits & 0x00000001u) {
    ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::WriteBytesMaybeAliased(
      2, this->private_key(), output);
  }

  output->WriteRaw(_internal_metadata_.unknown_fields().data(),
                   static_cast<int>(_internal_metadata_.unknown_fields().size()));
  // @@protoc_insertion_point(serialize_end:quic.QuicServerConfigProtobuf.PrivateKey)
}

size_t QuicServerConfigProtobuf_PrivateKey::RequiredFieldsByteSizeFallback() const {
// @@protoc_insertion_point(required_fields_byte_size_fallback_start:quic.QuicServerConfigProtobuf.PrivateKey)
  size_t total_size = 0;

  if (has_private_key()) {
    // required bytes private_key = 2;
    total_size += 1 +
      ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::BytesSize(
        this->private_key());
  }

  if (has_tag()) {
    // required uint32 tag = 1;
    total_size += 1 +
      ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::UInt32Size(
        this->tag());
  }

  return total_size;
}
size_t QuicServerConfigProtobuf_PrivateKey::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:quic.QuicServerConfigProtobuf.PrivateKey)
  size_t total_size = 0;

  total_size += _internal_metadata_.unknown_fields().size();

  if (((_has_bits_[0] & 0x00000003) ^ 0x00000003) == 0) {  // All required fields are present.
    // required bytes private_key = 2;
    total_size += 1 +
      ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::BytesSize(
        this->private_key());

    // required uint32 tag = 1;
    total_size += 1 +
      ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::UInt32Size(
        this->tag());

  } else {
    total_size += RequiredFieldsByteSizeFallback();
  }
  ::PROTOBUF_NAMESPACE_ID::uint32 cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  int cached_size = ::PROTOBUF_NAMESPACE_ID::internal::ToCachedSize(total_size);
  SetCachedSize(cached_size);
  return total_size;
}

void QuicServerConfigProtobuf_PrivateKey::CheckTypeAndMergeFrom(
    const ::PROTOBUF_NAMESPACE_ID::MessageLite& from) {
  MergeFrom(*::PROTOBUF_NAMESPACE_ID::internal::DownCast<const QuicServerConfigProtobuf_PrivateKey*>(
      &from));
}

void QuicServerConfigProtobuf_PrivateKey::MergeFrom(const QuicServerConfigProtobuf_PrivateKey& from) {
// @@protoc_insertion_point(class_specific_merge_from_start:quic.QuicServerConfigProtobuf.PrivateKey)
  GOOGLE_DCHECK_NE(&from, this);
  _internal_metadata_.MergeFrom(from._internal_metadata_);
  ::PROTOBUF_NAMESPACE_ID::uint32 cached_has_bits = 0;
  (void) cached_has_bits;

  cached_has_bits = from._has_bits_[0];
  if (cached_has_bits & 0x00000003u) {
    if (cached_has_bits & 0x00000001u) {
      _has_bits_[0] |= 0x00000001u;
      private_key_.AssignWithDefault(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), from.private_key_);
    }
    if (cached_has_bits & 0x00000002u) {
      tag_ = from.tag_;
    }
    _has_bits_[0] |= cached_has_bits;
  }
}

void QuicServerConfigProtobuf_PrivateKey::CopyFrom(const QuicServerConfigProtobuf_PrivateKey& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:quic.QuicServerConfigProtobuf.PrivateKey)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool QuicServerConfigProtobuf_PrivateKey::IsInitialized() const {
  if ((_has_bits_[0] & 0x00000003) != 0x00000003) return false;
  return true;
}

void QuicServerConfigProtobuf_PrivateKey::InternalSwap(QuicServerConfigProtobuf_PrivateKey* other) {
  using std::swap;
  _internal_metadata_.Swap(&other->_internal_metadata_);
  swap(_has_bits_[0], other->_has_bits_[0]);
  private_key_.Swap(&other->private_key_, &::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(),
    GetArenaNoVirtual());
  swap(tag_, other->tag_);
}

std::string QuicServerConfigProtobuf_PrivateKey::GetTypeName() const {
  return "quic.QuicServerConfigProtobuf.PrivateKey";
}


// ===================================================================

void QuicServerConfigProtobuf::InitAsDefaultInstance() {
}
class QuicServerConfigProtobuf::_Internal {
 public:
  using HasBits = decltype(std::declval<QuicServerConfigProtobuf>()._has_bits_);
  static void set_has_config(HasBits* has_bits) {
    (*has_bits)[0] |= 1u;
  }
  static void set_has_primary_time(HasBits* has_bits) {
    (*has_bits)[0] |= 2u;
  }
  static void set_has_priority(HasBits* has_bits) {
    (*has_bits)[0] |= 4u;
  }
};

QuicServerConfigProtobuf::QuicServerConfigProtobuf()
  : ::PROTOBUF_NAMESPACE_ID::MessageLite(), _internal_metadata_(nullptr) {
  SharedCtor();
  // @@protoc_insertion_point(constructor:quic.QuicServerConfigProtobuf)
}
QuicServerConfigProtobuf::QuicServerConfigProtobuf(const QuicServerConfigProtobuf& from)
  : ::PROTOBUF_NAMESPACE_ID::MessageLite(),
      _internal_metadata_(nullptr),
      _has_bits_(from._has_bits_),
      key_(from.key_) {
  _internal_metadata_.MergeFrom(from._internal_metadata_);
  config_.UnsafeSetDefault(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited());
  if (from.has_config()) {
    config_.AssignWithDefault(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), from.config_);
  }
  ::memcpy(&primary_time_, &from.primary_time_,
    static_cast<size_t>(reinterpret_cast<char*>(&priority_) -
    reinterpret_cast<char*>(&primary_time_)) + sizeof(priority_));
  // @@protoc_insertion_point(copy_constructor:quic.QuicServerConfigProtobuf)
}

void QuicServerConfigProtobuf::SharedCtor() {
  ::PROTOBUF_NAMESPACE_ID::internal::InitSCC(&scc_info_QuicServerConfigProtobuf_crypto_5fserver_5fconfig_2eproto.base);
  config_.UnsafeSetDefault(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited());
  ::memset(&primary_time_, 0, static_cast<size_t>(
      reinterpret_cast<char*>(&priority_) -
      reinterpret_cast<char*>(&primary_time_)) + sizeof(priority_));
}

QuicServerConfigProtobuf::~QuicServerConfigProtobuf() {
  // @@protoc_insertion_point(destructor:quic.QuicServerConfigProtobuf)
  SharedDtor();
}

void QuicServerConfigProtobuf::SharedDtor() {
  config_.DestroyNoArena(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited());
}

void QuicServerConfigProtobuf::SetCachedSize(int size) const {
  _cached_size_.Set(size);
}
const QuicServerConfigProtobuf& QuicServerConfigProtobuf::default_instance() {
  ::PROTOBUF_NAMESPACE_ID::internal::InitSCC(&::scc_info_QuicServerConfigProtobuf_crypto_5fserver_5fconfig_2eproto.base);
  return *internal_default_instance();
}


void QuicServerConfigProtobuf::Clear() {
// @@protoc_insertion_point(message_clear_start:quic.QuicServerConfigProtobuf)
  ::PROTOBUF_NAMESPACE_ID::uint32 cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  key_.Clear();
  cached_has_bits = _has_bits_[0];
  if (cached_has_bits & 0x00000001u) {
    config_.ClearNonDefaultToEmptyNoArena();
  }
  if (cached_has_bits & 0x00000006u) {
    ::memset(&primary_time_, 0, static_cast<size_t>(
        reinterpret_cast<char*>(&priority_) -
        reinterpret_cast<char*>(&primary_time_)) + sizeof(priority_));
  }
  _has_bits_.Clear();
  _internal_metadata_.Clear();
}

#if GOOGLE_PROTOBUF_ENABLE_EXPERIMENTAL_PARSER
const char* QuicServerConfigProtobuf::_InternalParse(const char* ptr, ::PROTOBUF_NAMESPACE_ID::internal::ParseContext* ctx) {
#define CHK_(x) if (PROTOBUF_PREDICT_FALSE(!(x))) goto failure
  _Internal::HasBits has_bits{};
  while (!ctx->Done(&ptr)) {
    ::PROTOBUF_NAMESPACE_ID::uint32 tag;
    ptr = ::PROTOBUF_NAMESPACE_ID::internal::ReadTag(ptr, &tag);
    CHK_(ptr);
    switch (tag >> 3) {
      // required bytes config = 1;
      case 1:
        if (PROTOBUF_PREDICT_TRUE(static_cast<::PROTOBUF_NAMESPACE_ID::uint8>(tag) == 10)) {
          ptr = ::PROTOBUF_NAMESPACE_ID::internal::InlineGreedyStringParser(mutable_config(), ptr, ctx);
          CHK_(ptr);
        } else goto handle_unusual;
        continue;
      // repeated .quic.QuicServerConfigProtobuf.PrivateKey key = 2;
      case 2:
        if (PROTOBUF_PREDICT_TRUE(static_cast<::PROTOBUF_NAMESPACE_ID::uint8>(tag) == 18)) {
          ptr -= 1;
          do {
            ptr += 1;
            ptr = ctx->ParseMessage(add_key(), ptr);
            CHK_(ptr);
            if (!ctx->DataAvailable(ptr)) break;
          } while (::PROTOBUF_NAMESPACE_ID::internal::UnalignedLoad<::PROTOBUF_NAMESPACE_ID::uint8>(ptr) == 18);
        } else goto handle_unusual;
        continue;
      // optional int64 primary_time = 3;
      case 3:
        if (PROTOBUF_PREDICT_TRUE(static_cast<::PROTOBUF_NAMESPACE_ID::uint8>(tag) == 24)) {
          _Internal::set_has_primary_time(&has_bits);
          primary_time_ = ::PROTOBUF_NAMESPACE_ID::internal::ReadVarint(&ptr);
          CHK_(ptr);
        } else goto handle_unusual;
        continue;
      // optional uint64 priority = 4;
      case 4:
        if (PROTOBUF_PREDICT_TRUE(static_cast<::PROTOBUF_NAMESPACE_ID::uint8>(tag) == 32)) {
          _Internal::set_has_priority(&has_bits);
          priority_ = ::PROTOBUF_NAMESPACE_ID::internal::ReadVarint(&ptr);
          CHK_(ptr);
        } else goto handle_unusual;
        continue;
      default: {
      handle_unusual:
        if ((tag & 7) == 4 || tag == 0) {
          ctx->SetLastTag(tag);
          goto success;
        }
        ptr = UnknownFieldParse(tag, &_internal_metadata_, ptr, ctx);
        CHK_(ptr != nullptr);
        continue;
      }
    }  // switch
  }  // while
success:
  _has_bits_.Or(has_bits);
  return ptr;
failure:
  ptr = nullptr;
  goto success;
#undef CHK_
}
#else  // GOOGLE_PROTOBUF_ENABLE_EXPERIMENTAL_PARSER
bool QuicServerConfigProtobuf::MergePartialFromCodedStream(
    ::PROTOBUF_NAMESPACE_ID::io::CodedInputStream* input) {
#define DO_(EXPRESSION) if (!PROTOBUF_PREDICT_TRUE(EXPRESSION)) goto failure
  ::PROTOBUF_NAMESPACE_ID::uint32 tag;
  ::PROTOBUF_NAMESPACE_ID::internal::LiteUnknownFieldSetter unknown_fields_setter(
      &_internal_metadata_);
  ::PROTOBUF_NAMESPACE_ID::io::StringOutputStream unknown_fields_output(
      unknown_fields_setter.buffer());
  ::PROTOBUF_NAMESPACE_ID::io::CodedOutputStream unknown_fields_stream(
      &unknown_fields_output, false);
  // @@protoc_insertion_point(parse_start:quic.QuicServerConfigProtobuf)
  for (;;) {
    ::std::pair<::PROTOBUF_NAMESPACE_ID::uint32, bool> p = input->ReadTagWithCutoffNoLastTag(127u);
    tag = p.first;
    if (!p.second) goto handle_unusual;
    switch (::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::GetTagFieldNumber(tag)) {
      // required bytes config = 1;
      case 1: {
        if (static_cast< ::PROTOBUF_NAMESPACE_ID::uint8>(tag) == (10 & 0xFF)) {
          DO_(::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::ReadBytes(
                input, this->mutable_config()));
        } else {
          goto handle_unusual;
        }
        break;
      }

      // repeated .quic.QuicServerConfigProtobuf.PrivateKey key = 2;
      case 2: {
        if (static_cast< ::PROTOBUF_NAMESPACE_ID::uint8>(tag) == (18 & 0xFF)) {
          DO_(::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::ReadMessage(
                input, add_key()));
        } else {
          goto handle_unusual;
        }
        break;
      }

      // optional int64 primary_time = 3;
      case 3: {
        if (static_cast< ::PROTOBUF_NAMESPACE_ID::uint8>(tag) == (24 & 0xFF)) {
          _Internal::set_has_primary_time(&_has_bits_);
          DO_((::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::ReadPrimitive<
                   ::PROTOBUF_NAMESPACE_ID::int64, ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::TYPE_INT64>(
                 input, &primary_time_)));
        } else {
          goto handle_unusual;
        }
        break;
      }

      // optional uint64 priority = 4;
      case 4: {
        if (static_cast< ::PROTOBUF_NAMESPACE_ID::uint8>(tag) == (32 & 0xFF)) {
          _Internal::set_has_priority(&_has_bits_);
          DO_((::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::ReadPrimitive<
                   ::PROTOBUF_NAMESPACE_ID::uint64, ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::TYPE_UINT64>(
                 input, &priority_)));
        } else {
          goto handle_unusual;
        }
        break;
      }

      default: {
      handle_unusual:
        if (tag == 0) {
          goto success;
        }
        DO_(::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::SkipField(
            input, tag, &unknown_fields_stream));
        break;
      }
    }
  }
success:
  // @@protoc_insertion_point(parse_success:quic.QuicServerConfigProtobuf)
  return true;
failure:
  // @@protoc_insertion_point(parse_failure:quic.QuicServerConfigProtobuf)
  return false;
#undef DO_
}
#endif  // GOOGLE_PROTOBUF_ENABLE_EXPERIMENTAL_PARSER

void QuicServerConfigProtobuf::SerializeWithCachedSizes(
    ::PROTOBUF_NAMESPACE_ID::io::CodedOutputStream* output) const {
  // @@protoc_insertion_point(serialize_start:quic.QuicServerConfigProtobuf)
  ::PROTOBUF_NAMESPACE_ID::uint32 cached_has_bits = 0;
  (void) cached_has_bits;

  cached_has_bits = _has_bits_[0];
  // required bytes config = 1;
  if (cached_has_bits & 0x00000001u) {
    ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::WriteBytesMaybeAliased(
      1, this->config(), output);
  }

  // repeated .quic.QuicServerConfigProtobuf.PrivateKey key = 2;
  for (unsigned int i = 0,
      n = static_cast<unsigned int>(this->key_size()); i < n; i++) {
    ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::WriteMessage(
      2,
      this->key(static_cast<int>(i)),
      output);
  }

  // optional int64 primary_time = 3;
  if (cached_has_bits & 0x00000002u) {
    ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::WriteInt64(3, this->primary_time(), output);
  }

  // optional uint64 priority = 4;
  if (cached_has_bits & 0x00000004u) {
    ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::WriteUInt64(4, this->priority(), output);
  }

  output->WriteRaw(_internal_metadata_.unknown_fields().data(),
                   static_cast<int>(_internal_metadata_.unknown_fields().size()));
  // @@protoc_insertion_point(serialize_end:quic.QuicServerConfigProtobuf)
}

size_t QuicServerConfigProtobuf::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:quic.QuicServerConfigProtobuf)
  size_t total_size = 0;

  total_size += _internal_metadata_.unknown_fields().size();

  // required bytes config = 1;
  if (has_config()) {
    total_size += 1 +
      ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::BytesSize(
        this->config());
  }
  ::PROTOBUF_NAMESPACE_ID::uint32 cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  // repeated .quic.QuicServerConfigProtobuf.PrivateKey key = 2;
  {
    unsigned int count = static_cast<unsigned int>(this->key_size());
    total_size += 1UL * count;
    for (unsigned int i = 0; i < count; i++) {
      total_size +=
        ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::MessageSize(
          this->key(static_cast<int>(i)));
    }
  }

  cached_has_bits = _has_bits_[0];
  if (cached_has_bits & 0x00000006u) {
    // optional int64 primary_time = 3;
    if (cached_has_bits & 0x00000002u) {
      total_size += 1 +
        ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::Int64Size(
          this->primary_time());
    }

    // optional uint64 priority = 4;
    if (cached_has_bits & 0x00000004u) {
      total_size += 1 +
        ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::UInt64Size(
          this->priority());
    }

  }
  int cached_size = ::PROTOBUF_NAMESPACE_ID::internal::ToCachedSize(total_size);
  SetCachedSize(cached_size);
  return total_size;
}

void QuicServerConfigProtobuf::CheckTypeAndMergeFrom(
    const ::PROTOBUF_NAMESPACE_ID::MessageLite& from) {
  MergeFrom(*::PROTOBUF_NAMESPACE_ID::internal::DownCast<const QuicServerConfigProtobuf*>(
      &from));
}

void QuicServerConfigProtobuf::MergeFrom(const QuicServerConfigProtobuf& from) {
// @@protoc_insertion_point(class_specific_merge_from_start:quic.QuicServerConfigProtobuf)
  GOOGLE_DCHECK_NE(&from, this);
  _internal_metadata_.MergeFrom(from._internal_metadata_);
  ::PROTOBUF_NAMESPACE_ID::uint32 cached_has_bits = 0;
  (void) cached_has_bits;

  key_.MergeFrom(from.key_);
  cached_has_bits = from._has_bits_[0];
  if (cached_has_bits & 0x00000007u) {
    if (cached_has_bits & 0x00000001u) {
      _has_bits_[0] |= 0x00000001u;
      config_.AssignWithDefault(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), from.config_);
    }
    if (cached_has_bits & 0x00000002u) {
      primary_time_ = from.primary_time_;
    }
    if (cached_has_bits & 0x00000004u) {
      priority_ = from.priority_;
    }
    _has_bits_[0] |= cached_has_bits;
  }
}

void QuicServerConfigProtobuf::CopyFrom(const QuicServerConfigProtobuf& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:quic.QuicServerConfigProtobuf)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool QuicServerConfigProtobuf::IsInitialized() const {
  if ((_has_bits_[0] & 0x00000001) != 0x00000001) return false;
  if (!::PROTOBUF_NAMESPACE_ID::internal::AllAreInitialized(this->key())) return false;
  return true;
}

void QuicServerConfigProtobuf::InternalSwap(QuicServerConfigProtobuf* other) {
  using std::swap;
  _internal_metadata_.Swap(&other->_internal_metadata_);
  swap(_has_bits_[0], other->_has_bits_[0]);
  CastToBase(&key_)->InternalSwap(CastToBase(&other->key_));
  config_.Swap(&other->config_, &::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(),
    GetArenaNoVirtual());
  swap(primary_time_, other->primary_time_);
  swap(priority_, other->priority_);
}

std::string QuicServerConfigProtobuf::GetTypeName() const {
  return "quic.QuicServerConfigProtobuf";
}


// @@protoc_insertion_point(namespace_scope)
}  // namespace quic
PROTOBUF_NAMESPACE_OPEN
template<> PROTOBUF_NOINLINE ::quic::QuicServerConfigProtobuf_PrivateKey* Arena::CreateMaybeMessage< ::quic::QuicServerConfigProtobuf_PrivateKey >(Arena* arena) {
  return Arena::CreateInternal< ::quic::QuicServerConfigProtobuf_PrivateKey >(arena);
}
template<> PROTOBUF_NOINLINE ::quic::QuicServerConfigProtobuf* Arena::CreateMaybeMessage< ::quic::QuicServerConfigProtobuf >(Arena* arena) {
  return Arena::CreateInternal< ::quic::QuicServerConfigProtobuf >(arena);
}
PROTOBUF_NAMESPACE_CLOSE

// @@protoc_insertion_point(global_scope)
#include <google/protobuf/port_undef.inc>
