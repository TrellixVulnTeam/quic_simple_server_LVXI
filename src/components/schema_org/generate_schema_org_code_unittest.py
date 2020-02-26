# Copyright 2020 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Tests for generate_schema_org_code."""

import sys
import unittest
import generate_schema_org_code
import os

SRC = os.path.join(os.path.dirname(__file__), os.path.pardir, os.path.pardir)
sys.path.append(os.path.join(SRC, 'third_party', 'pymock'))

import mock

_current_dir = os.path.dirname(os.path.realpath(__file__))
# jinja2 is in chromium's third_party directory
# Insert at front to override system libraries, and after path[0] == script dir
sys.path.insert(
    1, os.path.join(_current_dir, *([os.pardir] * 2 + ['third_party'])))
import jinja2


class GenerateSchemaOrgCodeTest(unittest.TestCase):
    def test_get_template_vars(self):
        file_content = """
    {
      "@graph": [
        {
          "@id": "http://schema.org/MediaObject",
          "@type": "rdfs:Class"
        },
        {
          "@id": "http://schema.org/propertyName",
          "@type": "rdf:Property"
        }
      ]
    }
    """
        with mock.patch('__builtin__.open',
                        mock.mock_open(read_data=file_content)) as m_open:
            self.assertEqual(
                generate_schema_org_code.get_template_vars(m_open), {
                    'entities': ['MediaObject'],
                    'properties': ['propertyName']
                })


if __name__ == '__main__':
    unittest.main()
