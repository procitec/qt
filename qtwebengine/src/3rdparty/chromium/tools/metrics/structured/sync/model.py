# Copyright 2021 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Model of a structured metrics description xml file.

This marshals an XML string into a Model, and validates that the XML is
semantically correct. The model can also be used to create a canonically
formatted version XML.
"""

import textwrap as tw
import xml.etree.ElementTree as ET

import sync.model_util as util


# Default key rotation period if not explicitly specified in the XML.
DEFAULT_KEY_ROTATION_PERIOD = 90

# Default scope if not explicitly specified in the XML.
DEFAULT_PROJECT_SCOPE = "device"

# Project name for event sequencing.
#
# This project name should be consistent with the name in structured.xml as well
# as the server.
EVENT_SEQUENCE_PROJECT_NAME = "CrOSEvents"


def wrap(text: str, indent: str) -> str:
  wrapper = tw.TextWrapper(width=80,
                           initial_indent=indent,
                           subsequent_indent=indent)
  return wrapper.fill(tw.dedent(text))


class Model:
  """Represents all projects in the structured.xml file.

    A Model is initialized with an XML string representing the top-level of
    the structured.xml file. This file is built from three building blocks:
    projects, events, and metrics. These have the following attributes.

      PROJECT
      - summary
      - id specifier
      - one or more owners
      - one or more events

      EVENT
      - summary
      - one or more metrics

      METRIC
      - summary
      - data type

    The following is an example input XML.

      <structured-metrics>
      <project name="MyProject">
        <owner>owner@chromium.org</owner>
        <id>none</id>
        <scope>profile</scope>
        <summary> My project. </summary>

        <event name="MyEvent">
          <summary> My event. </summary>
          <metric name="MyMetric" type="int">
            <summary> My metric. </summary>
          </metric>
        </event>
      </project>
      </structured-metrics>

    Calling str(model) will return a canonically formatted XML string.
    """

  OWNER_REGEX = r"^.+@(chromium\.org|google\.com)$"
  NAME_REGEX = r"^[A-Za-z0-9_.]+$"
  TYPE_REGEX = r"^(hmac-string|raw-string|int|double|int-array)$"
  ID_REGEX = r"^(none|per-project|uma)$"
  SCOPE_REGEX = r"^(profile|device)$"
  KEY_REGEX = r"^[0-9]+$"
  MAX_REGEX = r"^[0-9]+$"

  def __init__(self, xml_string: str):
    elem = ET.fromstring(xml_string)
    util.check_attributes(elem, set())
    util.check_children(elem, {"project"})
    util.check_child_names_unique(elem, "project")
    projects = util.get_compound_children(elem, "project")
    self.projects = [Project(p) for p in projects]

  def __repr__(self):
    projects = "\n\n".join(str(p) for p in self.projects)

    return tw.dedent(f"""\
               <structured-metrics>

               {projects}

               </structured-metrics>""")


class Project:
  """Represents a single structured metrics project.

    A Project is initialized with an XML node representing one project, eg:

      <project name="MyProject" cros_events="true">
        <owner>owner@chromium.org</owner>
        <id>none</id>
        <scope>project</scope>
        <key-rotation>60</key-rotation>
        <summary> My project. </summary>

        <event name="MyEvent">
          <summary> My event. </summary>
          <metric name="MyMetric" type="int">
            <summary> My metric. </summary>
          </metric>
        </event>
      </project>

    Calling str(project) will return a canonically formatted XML string.
    """

  def __init__(self, elem: ET.Element):
    util.check_attributes(elem, {"name"}, {"cros_events"})
    util.check_children(elem, {"id", "summary", "owner", "event"})
    util.check_child_names_unique(elem, "event")

    self.name = util.get_attr(elem, "name", Model.NAME_REGEX)
    self.id = util.get_text_child(elem, "id", Model.ID_REGEX)
    self.summary = util.get_text_child(elem, "summary")
    self.owners = util.get_text_children(elem, "owner", Model.OWNER_REGEX)

    self.key_rotation_period = DEFAULT_KEY_ROTATION_PERIOD
    self.scope = DEFAULT_PROJECT_SCOPE
    self.is_event_sequence_project = util.get_boolean_attr(elem, "cros_events")

    # Check if key-rotation is specified. If so, then change the
    # key_rotation_period.
    if elem.find("key-rotation") is not None:
      self.key_rotation_period = util.get_text_child(elem, "key-rotation",
                                                     Model.KEY_REGEX)

    # Check if scope is specified. If so, then change the scope.
    if elem.find("scope") is not None:
      self.scope = util.get_text_child(elem, "scope", Model.SCOPE_REGEX)

    self.events = [
        Event(e, self) for e in util.get_compound_children(elem, "event")
    ]

  def __repr__(self):
    events = "\n\n".join(str(e) for e in self.events)
    events = tw.indent(events, "  ")
    summary = wrap(self.summary, indent="    ")
    owners = "\n".join(f"  <owner>{o}</owner>" for o in self.owners)
    if self.is_event_sequence_project:
      cros_events_attr = ' cros_events="true"'
    else:
      cros_events_attr = ""

    return tw.dedent(f"""\
               <project name="{self.name}"{cros_events_attr}>
               {owners}
                 <id>{self.id}</id>
                 <scope>{self.scope}</scope>
                 <key-rotation>{self.key_rotation_period}</key-rotation>
                 <summary>
               {summary}
                 </summary>

               {events}
               </project>""")


class Event:
  """Represents a single structured metrics event.

    An Event is initialized with an XML node representing one event, eg:

      <event name="MyEvent">
        <summary> My event. </summary>
        <metric name="MyMetric" type="int">
          <summary> My metric. </summary>
        </metric>
      </event>

    Calling str(event) will return a canonically formatted XML string.
    """

  def __init__(self, elem: ET.Element, project: Project):
    util.check_attributes(elem, {"name"}, {"force_record"})

    if project.is_event_sequence_project:
      expected_children = {"summary"}
    else:
      expected_children = {"summary", "metric"}

    util.check_children(elem, expected_children)

    util.check_child_names_unique(elem, "metric")

    self.name = util.get_attr(elem, "name", Model.NAME_REGEX)
    self.force_record = util.get_boolean_attr(elem, "force_record")
    self.summary = util.get_text_child(elem, "summary")
    self.metrics = [
        Metric(m, project) for m in util.get_compound_children(
            elem, "metric", project.is_event_sequence_project)
    ]

  def __repr__(self):
    metrics = "\n".join(str(m) for m in self.metrics)
    metrics = tw.indent(metrics, "  ")
    summary = wrap(self.summary, indent="    ")
    if self.force_record:
      force_record = ' force_record="true"'
    else:
      force_record = ""

    return tw.dedent(f"""\
               <event name="{self.name}"{force_record}>
                 <summary>
               {summary}
                 </summary>
               {metrics}
               </event>""")


class Metric:
  """Represents a single metric.

    A Metric is initialized with an XML node representing one metric, eg:

      <metric name="MyMetric" type="int">
        <summary> My metric. </summary>
      </metric>

    Calling str(metric) will return a canonically formatted XML string.
    """

  def __init__(self, elem: ET.Element, project: Project):
    util.check_attributes(elem, {"name", "type"}, {"max"})
    util.check_children(elem, {"summary"})

    self.name = util.get_attr(elem, "name", Model.NAME_REGEX)
    self.type = util.get_attr(elem, "type", Model.TYPE_REGEX)
    self.summary = util.get_text_child(elem, "summary")

    if self.type == "int-array":
      self.max_size = int(util.get_attr(elem, "max", Model.MAX_REGEX))

    if self.type == "raw-string" and (project.id != "none" and project.name
                                      != EVENT_SEQUENCE_PROJECT_NAME):
      util.error(
          elem,
          "raw-string metrics must be in a project with id type "
          f"'none' or project name '{EVENT_SEQUENCE_PROJECT_NAME}',"
          f" but {project.name} has id type '{project.id}'",
      )

  def is_array(self) -> bool:
    return "array" in self.type

  def __repr__(self):
    summary = wrap(self.summary, indent="    ")
    return tw.dedent(f"""\
               <metric name="{self.name}" type="{self.type}">
                 <summary>
               {summary}
                 </summary>
               </metric>""")
