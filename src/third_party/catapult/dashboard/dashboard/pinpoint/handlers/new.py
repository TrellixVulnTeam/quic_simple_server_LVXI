# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import print_function
from __future__ import division
from __future__ import absolute_import

import json
import logging

from dashboard.api import api_request_handler
from dashboard.common import bot_configurations
from dashboard.common import utils
from dashboard.pinpoint.models import change
from dashboard.pinpoint.models import job as job_module
from dashboard.pinpoint.models import job_state
from dashboard.pinpoint.models import quest as quest_module
from dashboard.pinpoint.models import scheduler
from dashboard.pinpoint.models import task as task_module
from dashboard.pinpoint.models.tasks import performance_bisection
from dashboard.pinpoint.models.tasks import read_value

_ERROR_BUG_ID = 'Bug ID must be an integer.'
_ERROR_TAGS_DICT = 'Tags must be a dict of key/value string pairs.'
_ERROR_UNSUPPORTED = 'This benchmark (%s) is unsupported.'
_UNSUPPORTED_BENCHMARKS = []


class New(api_request_handler.ApiRequestHandler):
  """Handler that cooks up a fresh Pinpoint job."""

  def _CheckUser(self):
    self._CheckIsLoggedIn()
    if not utils.IsTryjobUser():
      raise api_request_handler.ForbiddenError()

  def Post(self):
    # TODO(dberris): Validate the inputs based on the type of job requested.
    job = _CreateJob(self.request)

    scheduler.Schedule(job)

    job.PostCreationUpdate()

    return {
        'jobId': job.job_id,
        'jobUrl': job.url,
    }


def _CreateJob(request):
  """Creates a new Pinpoint job from WebOb request arguments."""
  original_arguments = request.params.mixed()
  logging.debug('Received Params: %s', original_arguments)

  arguments = _ArgumentsWithConfiguration(original_arguments)
  logging.debug('Updated Params: %s', arguments)

  # Validate arguments and convert them to canonical internal representation.
  quests = _GenerateQuests(arguments)

  bug_id = _ValidateBugId(arguments.get('bug_id'))
  comparison_mode = _ValidateComparisonMode(arguments.get('comparison_mode'))
  comparison_magnitude = _ValidateComparisonMagnitude(
      arguments.get('comparison_magnitude'))
  gerrit_server, gerrit_change_id = _ValidatePatch(arguments.get('patch'))
  name = arguments.get('name')
  pin = _ValidatePin(arguments.get('pin'))
  tags = _ValidateTags(arguments.get('tags'))
  user = _ValidateUser(arguments.get('user'))
  changes = _ValidateChanges(comparison_mode, arguments)

  # TODO(dberris): Make this the default when we've graduated the beta.
  use_execution_engine = (
      arguments.get('experimental_execution_engine') and
      arguments.get('comparison_mode') == job_state.PERFORMANCE)

  # Create job.
  job = job_module.Job.New(
      quests if not use_execution_engine else (),
      changes,
      arguments=original_arguments,
      bug_id=bug_id,
      comparison_mode=comparison_mode,
      comparison_magnitude=comparison_magnitude,
      gerrit_server=gerrit_server,
      gerrit_change_id=gerrit_change_id,
      name=name,
      pin=pin,
      tags=tags,
      user=user,
      use_execution_engine=use_execution_engine)

  if use_execution_engine:
    # TODO(dberris): We need to figure out a way to get the arguments to be more
    # structured when it comes in from the UI, so that we don't need to do the
    # manual translation of options here.
    # TODO(dberris): Decide whether we can make some of these hard-coded options
    # be part of a template that's available in the UI (or by configuration
    # somewhere else, maybe luci-config?)
    start_change, end_change = changes
    target = arguments.get('target')
    task_options = performance_bisection.TaskOptions(
        build_option_template=performance_bisection.BuildOptionTemplate(
            builder=arguments.get('builder'),
            target=target,
            bucket=arguments.get('bucket', 'master.tryserver.chromium.perf'),
        ),
        test_option_template=performance_bisection.TestOptionTemplate(
            swarming_server=arguments.get('swarming_server'),
            dimensions=arguments.get('dimensions'),
            extra_args=arguments.get('extra_test_args'),
        ),
        read_option_template=performance_bisection.ReadOptionTemplate(
            benchmark=arguments.get('benchmark'),
            histogram_options=read_value.HistogramOptions(
                grouping_label=arguments.get('grouping_label'),
                story=arguments.get('story'),
                statistic=arguments.get('statistic'),
                histogram_name=arguments.get('chart'),
            ),
            graph_json_options=read_value.GraphJsonOptions(
                chart=arguments.get('chart'), trace=arguments.get('trace')),
            mode=('histogram_sets'
                  if target in performance_bisection.EXPERIMENTAL_TARGET_SUPPORT
                  else 'graph_json')),
        analysis_options=performance_bisection.AnalysisOptions(
            comparison_magnitude=arguments.get('comparison_magnitude'),
            min_attempts=10,
            max_attempts=60,
        ),
        start_change=start_change,
        end_change=end_change,
        pinned_change=arguments.get('patch'),
    )
    task_module.PopulateTaskGraph(
        job, performance_bisection.CreateGraph(task_options, arguments))
  return job


def _ArgumentsWithConfiguration(original_arguments):
  # "configuration" is a special argument that maps to a list of preset
  # arguments. Pull any arguments from the specified "configuration", if any.
  new_arguments = original_arguments.copy()

  configuration = original_arguments.get('configuration')
  if configuration:
    try:
      default_arguments = bot_configurations.Get(configuration)
    except KeyError:
      # Reraise with a clearer message.
      raise ValueError("Bot Config: %s doesn't exist." % configuration)
    logging.info('Bot Config: %s', default_arguments)

    if default_arguments:
      for k, v in list(default_arguments.items()):
        new_arguments.setdefault(k, v)

  if new_arguments.get('benchmark') in _UNSUPPORTED_BENCHMARKS:
    raise ValueError(_ERROR_UNSUPPORTED % new_arguments.get('benchmark'))

  return new_arguments


def _ValidateBugId(bug_id):
  if not bug_id:
    return None

  try:
    return int(bug_id)
  except ValueError:
    raise ValueError(_ERROR_BUG_ID)


def _ValidateChanges(comparison_mode, arguments):
  changes = arguments.get('changes')
  if changes:
    # FromData() performs input validation.
    return [change.Change.FromData(c) for c in json.loads(changes)]

  # Currently we only want try job support compare BASE and BASE+PATH
  # So override start and end to ensure they are same
  if comparison_mode == job_state.TRY:
    arguments['start_git_hash'] = arguments['base_git_hash']
    arguments['end_git_hash'] = arguments['base_git_hash']

  commit_1 = change.Commit.FromDict({
      'repository': arguments.get('repository'),
      'git_hash': arguments.get('start_git_hash'),
  })

  commit_2 = change.Commit.FromDict({
      'repository': arguments.get('repository'),
      'git_hash': arguments.get('end_git_hash'),
  })

  if 'patch' in arguments:
    patch = change.GerritPatch.FromUrl(arguments['patch'])
  else:
    patch = None

  change_1 = change.Change(commits=(commit_1,))
  change_2 = change.Change(commits=(commit_2,), patch=patch)

  return change_1, change_2


def _ValidatePatch(patch_data):
  if patch_data:
    patch_details = change.GerritPatch.FromData(patch_data)
    return patch_details.server, patch_details.change
  return None, None


def _ValidateComparisonMode(comparison_mode):
  if not comparison_mode:
    comparison_mode = job_state.TRY
  if comparison_mode and comparison_mode not in job_module.COMPARISON_MODES:
    raise ValueError('`comparison_mode` should be one of %s. Got "%s".' %
                     (job_module.COMPARISON_MODES + (None,), comparison_mode))
  return comparison_mode


def _ValidateComparisonMagnitude(comparison_magnitude):
  if not comparison_magnitude:
    return 1.0
  return float(comparison_magnitude)


def _GenerateQuests(arguments):
  """Generate a list of Quests from a dict of arguments.

  GenerateQuests uses the arguments to infer what types of Quests the user wants
  to run, and creates a list of Quests with the given configuration.

  Arguments:
    arguments: A dict or MultiDict containing arguments.

  Returns:
    A tuple of (arguments, quests), where arguments is a dict containing the
    request arguments that were used, and quests is a list of Quests.
  """
  quests = arguments.get('quests')
  if quests:
    if isinstance(quests, basestring):
      quests = quests.split(',')
    quest_classes = []
    for quest in quests:
      if not hasattr(quest_module, quest):
        raise ValueError('Unknown quest: "%s"' % quest)
      quest_classes.append(getattr(quest_module, quest))
  else:
    target = arguments.get('target')
    logging.debug('Target: %s', target)

    if target in ('performance_test_suite', 'performance_webview_test_suite',
                  'telemetry_perf_tests', 'telemetry_perf_webview_tests'):
      quest_classes = (quest_module.FindIsolate, quest_module.RunTelemetryTest,
                       quest_module.ReadHistogramsJsonValue)
    elif target == 'vr_perf_tests':
      quest_classes = (quest_module.FindIsolate,
                       quest_module.RunVrTelemetryTest,
                       quest_module.ReadHistogramsJsonValue)
    else:
      quest_classes = (quest_module.FindIsolate, quest_module.RunGTest,
                       quest_module.ReadGraphJsonValue)

  quest_instances = []
  for quest_class in quest_classes:
    # FromDict() performs input validation.
    quest_instances.append(quest_class.FromDict(arguments))

  return quest_instances


def _ValidatePin(pin):
  if not pin:
    return None
  return change.Change.FromData(pin)


def _ValidateTags(tags):
  if not tags:
    return {}

  tags_dict = json.loads(tags)

  if not isinstance(tags_dict, dict):
    raise ValueError(_ERROR_TAGS_DICT)

  for k, v in tags_dict.items():
    if not isinstance(k, basestring) or not isinstance(v, basestring):
      raise ValueError(_ERROR_TAGS_DICT)

  return tags_dict


def _ValidateUser(user):
  return user or utils.GetEmail()
