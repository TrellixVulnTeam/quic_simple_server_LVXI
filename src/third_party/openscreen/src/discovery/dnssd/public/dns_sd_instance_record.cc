// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "discovery/dnssd/public/dns_sd_instance_record.h"

#include <cctype>

#include "util/logging.h"

namespace openscreen {
namespace discovery {
namespace {

bool IsValidUtf8(const std::string& string) {
  for (size_t i = 0; i < string.size(); i++) {
    if (string[i] >> 5 == 0x06) {  // 110xxxxx 10xxxxxx
      if (i + 1 >= string.size() || string[++i] >> 6 != 0x02) {
        return false;
      }
    } else if (string[i] >> 4 == 0x0E) {  // 1110xxxx 10xxxxxx 10xxxxxx
      if (i + 2 >= string.size() || string[++i] >> 6 != 0x02 ||
          string[++i] >> 6 != 0x02) {
        return false;
      }
    } else if (string[i] >> 3 == 0x1E) {  // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
      if (i + 3 >= string.size() || string[++i] >> 6 != 0x02 ||
          string[++i] >> 6 != 0x02 || string[++i] >> 6 != 0x02) {
        return false;
      }
    } else if ((string[i] & 0x80) != 0x0) {  // 0xxxxxxx
      return false;
    }
  }
  return true;
}

bool HasControlCharacters(const std::string& string) {
  for (auto ch : string) {
    if ((ch >= 0x0 && ch <= 0x1F /* Ascii control characters */) ||
        ch == 0x7F /* DEL character */) {
      return true;
    }
  }
  return false;
}

}  // namespace

DnsSdInstanceRecord::DnsSdInstanceRecord(std::string instance_id,
                                         std::string service_id,
                                         std::string domain_id,
                                         IPEndpoint endpoint,
                                         DnsSdTxtRecord txt)
    : DnsSdInstanceRecord(std::move(instance_id),
                          std::move(service_id),
                          std::move(domain_id),
                          std::move(txt)) {
  OSP_DCHECK(endpoint);
  if (endpoint.address.IsV4()) {
    address_v4_ = std::move(endpoint);
  } else if (endpoint.address.IsV6()) {
    address_v6_ = std::move(endpoint);
  } else {
    OSP_NOTREACHED();
  }
}

DnsSdInstanceRecord::DnsSdInstanceRecord(std::string instance_id,
                                         std::string service_id,
                                         std::string domain_id,
                                         IPEndpoint ipv4_endpoint,
                                         IPEndpoint ipv6_endpoint,
                                         DnsSdTxtRecord txt)
    : DnsSdInstanceRecord(std::move(instance_id),
                          std::move(service_id),
                          std::move(domain_id),
                          std::move(txt)) {
  OSP_CHECK(ipv4_endpoint);
  OSP_CHECK(ipv6_endpoint);
  OSP_CHECK(ipv4_endpoint.address.IsV4());
  OSP_CHECK(ipv6_endpoint.address.IsV6());

  address_v4_ = std::move(ipv4_endpoint);
  address_v6_ = std::move(ipv6_endpoint);
}

DnsSdInstanceRecord::DnsSdInstanceRecord(std::string instance_id,
                                         std::string service_id,
                                         std::string domain_id,
                                         DnsSdTxtRecord txt)
    : instance_id_(std::move(instance_id)),
      service_id_(std::move(service_id)),
      domain_id_(std::move(domain_id)),
      txt_(std::move(txt)) {
  OSP_DCHECK(IsInstanceValid(instance_id_));
  OSP_DCHECK(IsServiceValid(service_id_));
  OSP_DCHECK(IsDomainValid(domain_id_));
}

uint16_t DnsSdInstanceRecord::port() const {
  if (address_v4_) {
    return address_v4_.port;
  } else if (address_v6_) {
    return address_v6_.port;
  } else {
    OSP_NOTREACHED();
    return 0;
  }
}

// static
bool IsInstanceValid(const std::string& instance) {
  // According to RFC6763, Instance names must:
  // - Be encoded in Net-Unicode (which required UTF-8 formatting).
  // - NOT contain ASCII control characters
  // - Be no longer than 63 octets.

  return instance.size() <= 63 && !HasControlCharacters(instance) &&
         IsValidUtf8(instance);
}

// static
bool IsServiceValid(const std::string& service) {
  // According to RFC6763, the service name "consists of a pair of DNS labels".
  // "The first label of the pair is an underscore character followed by the
  // Service Name" and "The second label is either '_tcp' [...] or '_udp'".
  // According to RFC6335 Section 5.1, the Service Name section must:
  //   Contain from 1 to 15 characters.
  // - Only contain A-Z, a-Z, 0-9, and the hyphen character.
  // - Contain at least one letter.
  // - NOT begin or end with a hyphen.
  // - NOT contain two adjacent hyphens.
  if (service.size() > 21 || service.size() < 7) {  // Service name size + 6.
    return false;
  }

  const std::string protocol = service.substr(service.size() - 5);
  if (protocol != "._udp" && protocol != "._tcp") {
    return false;
  }

  if (service[0] != '_' || service[1] == '-' ||
      service[service.size() - 6] == '-') {
    return false;
  }

  bool last_char_hyphen = false;
  bool seen_letter = false;
  for (size_t i = 1; i < service.size() - 5; i++) {
    if (service[i] == '-') {
      if (last_char_hyphen) {
        return false;
      }
      last_char_hyphen = true;
    } else if (std::isalpha(service[i])) {
      seen_letter = true;
    } else if (!std::isdigit(service[i])) {
      return false;
    }
  }

  return seen_letter;
}

// static
bool IsDomainValid(const std::string& domain) {
  // As RFC6763 Section 4.1.3 provides no validation requirements for the domain
  // section, the following validations are used:
  // - All labels must be no longer than 63 characters
  // - Total length must be no more than 256 characters
  // - Must be encoded using valid UTF8
  // - Must not include any ASCII control characters

  if (domain.size() > 255) {
    return false;
  }

  size_t label_start = 0;
  for (size_t next_dot = domain.find('.'); next_dot != std::string::npos;
       next_dot = domain.find('.', label_start)) {
    if (next_dot - label_start > 63) {
      return false;
    }
    label_start = next_dot + 1;
  }

  return !HasControlCharacters(domain) && IsValidUtf8(domain);
}

bool operator<(const DnsSdInstanceRecord& lhs, const DnsSdInstanceRecord& rhs) {
  int comp = lhs.instance_id_.compare(rhs.instance_id_);
  if (comp != 0) {
    return comp < 0;
  }

  comp = lhs.service_id_.compare(rhs.service_id_);
  if (comp != 0) {
    return comp < 0;
  }

  comp = lhs.domain_id_.compare(rhs.domain_id_);
  if (comp != 0) {
    return comp < 0;
  }

  if (lhs.address_v4_ != rhs.address_v4_) {
    return IPEndpointComparator()(lhs.address_v4_, rhs.address_v4_);
  }

  if (lhs.address_v6_ != rhs.address_v6_) {
    return IPEndpointComparator()(lhs.address_v6_, rhs.address_v6_);
  }

  return lhs.txt_ < rhs.txt_;
}

}  // namespace discovery
}  // namespace openscreen
