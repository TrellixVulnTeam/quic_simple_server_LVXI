# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

def _RunArgs(args, input_api):
  p = input_api.subprocess.Popen(args, stdout=input_api.subprocess.PIPE,
                                 stderr=input_api.subprocess.STDOUT)
  out, _ = p.communicate()
  return (out, p.returncode)


def _CheckRegisteredMetrics(input_api, output_api):
  """ Check that all tracing metrics are imported in all_metrics.html """
  results = []
  tracing_dir = input_api.PresubmitLocalPath()
  out, return_code = _RunArgs(
      [input_api.python_executable,
       input_api.os_path.join(tracing_dir, 'bin', 'validate_all_metrics')],
      input_api)
  if return_code:
    results.append(output_api.PresubmitError(
        'Failed validate_all_metrics: ', long_text=out))
  return results


def _CheckRegisteredDiagnostics(input_api, output_api):
  """Check that all Diagnostic subclasses are registered."""
  results = []
  tracing_dir = input_api.PresubmitLocalPath()
  out, return_code = _RunArgs(
      [input_api.python_executable,
       input_api.os_path.join(tracing_dir, 'bin', 'validate_all_diagnostics')],
      input_api)
  if return_code:
    results.append(output_api.PresubmitError(
        'Failed validate_all_diagnostics: ', long_text=out))
  return results


def _WarnOnReservedInfosChanges(input_api, output_api):
  source_file_filter = lambda x: input_api.FilterSourceFile(
      x, white_list=[r'.*reserved_infos\.(py|cc)$'])

  count = len(input_api.AffectedSourceFiles(source_file_filter))
  results = []
  if count == 1:
    results.append(output_api.PresubmitPromptWarning(
        'Looks like you are modifying one of reserved_infos.py and\n'
        'reserved_infos.cc. Please make sure the values in both files\n'
        'are in sync.'))
  return results


def _CheckProtoNamespace(input_api, output_api):
  source_file_filter = lambda x: input_api.FilterSourceFile(
      x, white_list=[r'.*\.(cc|h)$'])

  files = []
  for f in input_api.AffectedSourceFiles(source_file_filter):
    contents = input_api.ReadFile(f)
    print f
    if 'protobuf::' in contents:
      files.append(f)

  if files:
    return [output_api.PresubmitError(
        'Do not use the google::protobuf namespace. Use auto for '
        'google::protobuf::Map or other proto types you need.\n'
        'google::protobuf is currently not compatible with downstream '
        'proto libraries.',
        files)]

  return []

def CheckChangeOnUpload(input_api, output_api):
  return _CheckChange(input_api, output_api)


def CheckChangeOnCommit(input_api, output_api):
  return _CheckChange(input_api, output_api)


def _CheckChange(input_api, output_api):
  results = []

  original_sys_path = sys.path
  try:
    sys.path += [input_api.PresubmitLocalPath()]
    from tracing_build import check_gni
    error = check_gni.GniCheck()
    if error:
      results.append(output_api.PresubmitError(error))
  finally:
    sys.path = original_sys_path

  black_list = input_api.DEFAULT_BLACK_LIST + (".*_pb2.py$",)
  results += input_api.RunTests(input_api.canned_checks.GetPylint(
      input_api, output_api, extra_paths_list=_GetPathsToPrepend(input_api),
      pylintrc='../pylintrc', black_list=black_list))

  results += _CheckRegisteredMetrics(input_api, output_api)
  results += _CheckRegisteredDiagnostics(input_api, output_api)
  results += _WarnOnReservedInfosChanges(input_api, output_api)
  results += _CheckProtoNamespace(input_api, output_api)

  return results


def _GetPathsToPrepend(input_api):
  import tracing_project
  project_dir = input_api.PresubmitLocalPath()
  catapult_dir = input_api.os_path.join(project_dir, '..')
  return [
      project_dir,
      input_api.os_path.join(catapult_dir, 'third_party', 'mock'),
  ] + tracing_project.GetDependencyPaths()
