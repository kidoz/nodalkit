#!/usr/bin/env python3
"""Drive a running NodalKit application through libatspi, the client library
screen readers such as Orca use, and fail unless it behaves like a conforming
AT-SPI application: discoverable on the desktop, walkable with correct roles,
names, states, extents, text and actions, and announcing focus and name changes.

Usage: atspi_live_check.py "Application Name" [timeout-seconds]

Run it inside a session that has an accessibility bus; atspi_live_check.sh sets
one up together with a headless compositor.
"""

import sys
import time

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib  # noqa: E402

failures = []


def check(ok, message):
    print(("PASS " if ok else "FAIL ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


def pump(seconds):
    context = GLib.MainContext.default()
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        while context.iteration(False):
            pass
        time.sleep(0.02)


def find_application(name, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        desktop = Atspi.get_desktop(0)
        for index in range(desktop.get_child_count()):
            app = desktop.get_child_at_index(index)
            if app is not None and app.get_name() == name:
                return app
        pump(0.25)
    return None


def walk(node):
    found = [node]
    for index in range(node.get_child_count()):
        child = node.get_child_at_index(index)
        if child is not None:
            found.extend(walk(child))
    return found


def first(nodes, role, name=None):
    for node in nodes:
        if node.get_role() == role and (name is None or node.get_name() == name):
            return node
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    name = sys.argv[1]
    timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

    app = find_application(name, timeout)
    if not check(app is not None, f"'{name}' is listed on the accessibility desktop"):
        return 1
    check(app.get_toolkit_name() == "NodalKit", "the application reports the NodalKit toolkit")
    check(app.get_role() == Atspi.Role.APPLICATION, "the root has the application role")

    window = app.get_child_at_index(0) if app.get_child_count() > 0 else None
    if not check(window is not None and window.get_role() == Atspi.Role.FRAME,
                 "the first child is a frame"):
        return 1

    # The application registers before its widgets are first laid out; wait for layout, as a
    # screen reader effectively does by reacting to window events. The frame's own extents
    # come from the surface size, so settle on its first child instead.
    def laid_out():
        if window.get_child_count() == 0:
            return False
        content = window.get_child_at_index(0)
        return (content is not None and
                Atspi.Component.get_extents(content, Atspi.CoordType.WINDOW).width > 0)

    deadline = time.monotonic() + timeout
    while not laid_out() and time.monotonic() < deadline:
        pump(0.1)
    pump(0.5)

    nodes = walk(app)
    check(len(nodes) > 10, f"the tree is walkable ({len(nodes)} objects)")
    button = first(nodes, Atspi.Role.PUSH_BUTTON, "Open Search")
    entry = first(nodes, Atspi.Role.ENTRY)
    if not check(button is not None, "the 'Open Search' push button is exposed"):
        return 1
    if not check(entry is not None, "a text entry is exposed"):
        return 1

    parent = button.get_parent()
    index = button.get_index_in_parent()
    check(parent is not None and index >= 0 and parent.get_child_at_index(index) == button,
          "parent and index-in-parent round-trip")
    states = button.get_state_set()
    check(states.contains(Atspi.StateType.ENABLED) and
          states.contains(Atspi.StateType.FOCUSABLE) and
          states.contains(Atspi.StateType.SHOWING),
          "the button is enabled, focusable, and showing")
    extents = Atspi.Component.get_extents(button, Atspi.CoordType.WINDOW)
    check(extents.width > 0 and extents.height > 0,
          f"the button has extents ({extents.x}, {extents.y}, {extents.width}x{extents.height})")
    check(Atspi.Action.get_n_actions(button) >= 1 and
          Atspi.Action.get_action_name(button, 0) == "activate",
          "the button's default action is 'activate'")
    check(Atspi.Text.get_text(entry, 0, -1) == "Battle City", "the entry text is readable")
    check(Atspi.Text.get_character_count(entry) == len("Battle City"),
          "the entry reports its character count")
    check(entry.get_state_set().contains(Atspi.StateType.EDITABLE), "the entry is editable")

    events = []
    listener = Atspi.EventListener.new(lambda event: events.append(event))
    for event_type in ("object:state-changed:focused", "object:property-change:accessible-name"):
        listener.register(event_type)
    pump(0.5)

    Atspi.Component.grab_focus(entry)
    pump(1.5)
    check(any(e.type == "object:state-changed:focused" and e.detail1 == 1 and
              e.source.get_role() == Atspi.Role.ENTRY for e in events),
          "focusing the entry announces focused=1 on it")
    check(entry.get_state_set().contains(Atspi.StateType.FOCUSED), "the entry reports focus")

    events.clear()
    check(Atspi.Action.do_action(button, 0), "the button's default action is accepted")
    pump(1.5)
    check(any(e.type == "object:property-change:accessible-name" and
              e.source.get_name() == "Open Search" and e.source != button for e in events),
          "activating the button announces the status label's new name")

    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
