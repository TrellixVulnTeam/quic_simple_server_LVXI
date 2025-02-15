# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: cached_network_parameters.proto

import sys
_b=sys.version_info[0]<3 and (lambda x:x) or (lambda x:x.encode('latin1'))
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from google.protobuf import reflection as _reflection
from google.protobuf import symbol_database as _symbol_database
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()




DESCRIPTOR = _descriptor.FileDescriptor(
  name='cached_network_parameters.proto',
  package='quic',
  syntax='proto2',
  serialized_options=_b('H\003'),
  serialized_pb=_b('\n\x1f\x63\x61\x63hed_network_parameters.proto\x12\x04quic\"\xc7\x02\n\x17\x43\x61\x63hedNetworkParameters\x12\x16\n\x0eserving_region\x18\x01 \x01(\t\x12+\n#bandwidth_estimate_bytes_per_second\x18\x02 \x01(\x05\x12/\n\'max_bandwidth_estimate_bytes_per_second\x18\x05 \x01(\x05\x12\'\n\x1fmax_bandwidth_timestamp_seconds\x18\x06 \x01(\x03\x12\x12\n\nmin_rtt_ms\x18\x03 \x01(\x05\x12!\n\x19previous_connection_state\x18\x04 \x01(\x05\x12\x11\n\ttimestamp\x18\x07 \x01(\x03\"C\n\x17PreviousConnectionState\x12\x0e\n\nSLOW_START\x10\x00\x12\x18\n\x14\x43ONGESTION_AVOIDANCE\x10\x01\x42\x02H\x03')
)



_CACHEDNETWORKPARAMETERS_PREVIOUSCONNECTIONSTATE = _descriptor.EnumDescriptor(
  name='PreviousConnectionState',
  full_name='quic.CachedNetworkParameters.PreviousConnectionState',
  filename=None,
  file=DESCRIPTOR,
  values=[
    _descriptor.EnumValueDescriptor(
      name='SLOW_START', index=0, number=0,
      serialized_options=None,
      type=None),
    _descriptor.EnumValueDescriptor(
      name='CONGESTION_AVOIDANCE', index=1, number=1,
      serialized_options=None,
      type=None),
  ],
  containing_type=None,
  serialized_options=None,
  serialized_start=302,
  serialized_end=369,
)
_sym_db.RegisterEnumDescriptor(_CACHEDNETWORKPARAMETERS_PREVIOUSCONNECTIONSTATE)


_CACHEDNETWORKPARAMETERS = _descriptor.Descriptor(
  name='CachedNetworkParameters',
  full_name='quic.CachedNetworkParameters',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    _descriptor.FieldDescriptor(
      name='serving_region', full_name='quic.CachedNetworkParameters.serving_region', index=0,
      number=1, type=9, cpp_type=9, label=1,
      has_default_value=False, default_value=_b("").decode('utf-8'),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='bandwidth_estimate_bytes_per_second', full_name='quic.CachedNetworkParameters.bandwidth_estimate_bytes_per_second', index=1,
      number=2, type=5, cpp_type=1, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='max_bandwidth_estimate_bytes_per_second', full_name='quic.CachedNetworkParameters.max_bandwidth_estimate_bytes_per_second', index=2,
      number=5, type=5, cpp_type=1, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='max_bandwidth_timestamp_seconds', full_name='quic.CachedNetworkParameters.max_bandwidth_timestamp_seconds', index=3,
      number=6, type=3, cpp_type=2, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='min_rtt_ms', full_name='quic.CachedNetworkParameters.min_rtt_ms', index=4,
      number=3, type=5, cpp_type=1, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='previous_connection_state', full_name='quic.CachedNetworkParameters.previous_connection_state', index=5,
      number=4, type=5, cpp_type=1, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='timestamp', full_name='quic.CachedNetworkParameters.timestamp', index=6,
      number=7, type=3, cpp_type=2, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      serialized_options=None, file=DESCRIPTOR),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
    _CACHEDNETWORKPARAMETERS_PREVIOUSCONNECTIONSTATE,
  ],
  serialized_options=None,
  is_extendable=False,
  syntax='proto2',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=42,
  serialized_end=369,
)

_CACHEDNETWORKPARAMETERS_PREVIOUSCONNECTIONSTATE.containing_type = _CACHEDNETWORKPARAMETERS
DESCRIPTOR.message_types_by_name['CachedNetworkParameters'] = _CACHEDNETWORKPARAMETERS
_sym_db.RegisterFileDescriptor(DESCRIPTOR)

CachedNetworkParameters = _reflection.GeneratedProtocolMessageType('CachedNetworkParameters', (_message.Message,), {
  'DESCRIPTOR' : _CACHEDNETWORKPARAMETERS,
  '__module__' : 'cached_network_parameters_pb2'
  # @@protoc_insertion_point(class_scope:quic.CachedNetworkParameters)
  })
_sym_db.RegisterMessage(CachedNetworkParameters)


DESCRIPTOR._options = None
# @@protoc_insertion_point(module_scope)
