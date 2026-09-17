# NodalKit documentation

This documentation follows the [Diátaxis](https://diataxis.fr/) framework.
Every page belongs to exactly one of four types, so you can go straight to
the kind of help you need.

|                 | Learning                                               | Working                                                 |
| --------------- | ------------------------------------------------------ | ------------------------------------------------------- |
| **Practical**   | [Tutorials](#tutorials): lessons that take you by the hand | [How-to guides](#how-to-guides): steps for a real task |
| **Theoretical** | [Explanation](#explanation): background and reasoning  | [Reference](#reference): facts about the toolkit        |

## Tutorials

Learning-oriented lessons. Start here if you are new to NodalKit.

- [Your first NodalKit application](tutorials/your-first-application.md):
  build and install the SDK, then write, build, and run a small windowed
  application against it.

## How-to guides

Task-oriented steps for developers who already know the basics.

- [Consume the installed SDK](how-to/consume-the-installed-sdk.md)
- [Integrate NodalKit into a Meson build](how-to/integrate-with-meson.md)
- [Launch and monitor external processes](how-to/launch-external-processes.md)
- [Use native window handles](how-to/use-native-window-handles.md)
- [Export a support bundle](how-to/export-a-support-bundle.md)
- [Validate accessibility and keyboard behavior](how-to/validate-accessibility.md)
- [Assemble an emulator frontend](how-to/assemble-an-emulator-frontend.md)

## Reference

Information-oriented descriptions of what the toolkit provides.

- [Platform support](reference/platform-support.md)
- [Windows support matrix](reference/windows-support-matrix.md)
- [Native handle contract](reference/native-handles.md)
- [Diagnostics facilities](reference/diagnostics.md)

The public API reference lives in the headers under
[`include/nk/`](../include/nk/) as Doxygen-style comments. Release notes are
in [CHANGELOG.md](../CHANGELOG.md).

## Explanation

Understanding-oriented discussion of design and trade-offs.

- [Application architecture](explanation/application-architecture.md)
- [Toolkit boundaries](explanation/toolkit-boundaries.md)
- [Mixing C++ standards in one process](explanation/mixing-cpp-standards.md)

## Contributing a page

Decide which of the four types the page is before writing it. A tutorial
teaches through a safe, repeatable lesson. A how-to guide solves one task for
someone who already knows the basics. Reference describes what exists,
without advice. Explanation discusses why things are the way they are. If a
draft does more than one of these, split it.
