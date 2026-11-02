# Localize an application

NodalKit provides `nk::MessageCatalog` for dependency-free translated strings.
Your application chooses which catalog to load and passes the resulting UTF-8
text to widgets. This guide covers basic message lookup and named placeholders;
it does not add automatic locale selection or locale-sensitive formatting.

## Create message catalogs

Store one UTF-8 `.nkstrings` file per language. For example,
`locales/app.en.nkstrings`:

```text
# English messages
app.title=Photo library
welcome.user=Welcome, {user}!
files.selected={count} files selected from {folder}
```

A Russian `locales/app.ru.nkstrings` can reorder those placeholders:

```text
# Russian messages
app.title=Фототека
welcome.user=Добро пожаловать, {user}!
files.selected=Из папки {folder} выбрано файлов: {count}
```

Each non-comment line has the form `message.id=translated text`. Message IDs
must be unique within a file. NodalKit trims whitespace around IDs and values.
Blank lines and lines whose first non-whitespace character is `#` are ignored.
An initial UTF-8 byte-order mark is accepted.

Use these escapes when needed:

| Escape | Result |
| ------ | ------ |
| `\\` | Backslash |
| `\=` | Literal `=` |
| `\#` | Literal `#` |
| `\n` | Newline |
| `\r` | Carriage return |
| `\t` | Tab |

Other or trailing backslash escapes are errors. Loading also rejects invalid
UTF-8, missing separators, empty IDs, and duplicate IDs.

## Load and query a catalog

Load the application-selected catalog before constructing localized widgets:

```cpp
#include <cstdio>
#include <nk/l10n/message_catalog.h>
#include <nk/widgets/label.h>

nk::MessageCatalog messages;
if (auto loaded = messages.load("locales/app.ru.nkstrings"); !loaded) {
    std::fprintf(stderr, "%s\n", loaded.error().c_str());
    // Choose an application-specific fallback or report the error.
}

auto welcome = nk::Label::create(
    messages.translate("welcome.user", {{"user", "Ада"}}));
auto selected = nk::Label::create(messages.translate(
    "files.selected", {{"count", "3"}, {"folder", "Документы"}}));
```

`load()` replaces the existing messages only after the entire file succeeds.
If loading fails, the previous catalog remains usable and the returned error
contains the path and, for syntax errors, the line number. A missing file or
read failure is also reported.

`translate()` has deliberately visible fallback behavior:

- A missing message ID returns that ID unchanged.
- A missing argument leaves its `{name}` placeholder in the result.
- Extra arguments are ignored.
- Arguments can be repeated and reordered by each translation.
- Argument values are inserted literally, never expanded as templates.
- `{{` and `}}` produce literal braces in a message template.

Use `contains()` to test for a message, `size()` for the entry count, and
`clear()` to remove all entries. Catalogs are not thread-safe; use one on its
owning thread, normally the UI thread.

## Package catalogs with the application

`.nkstrings` files are application resources, not NodalKit SDK resources. Install
them beside your application's other data and resolve their location using your
application's resource-path policy. With Meson, for example:

```meson
install_subdir('locales',
    install_dir : get_option('datadir') / 'my_application',
)
```

Do not rely on the process working directory in an installed application. The
example above uses a relative path only to keep the API example short.

## Current limitations

`MessageCatalog` intentionally handles basic translated strings only. It does
not provide:

- operating-system locale detection or application locale state;
- automatic language fallback or live language switching;
- plural, gender, or grammatical selection rules;
- locale-aware number, currency, date, or time formatting;
- right-to-left layout or other text-direction changes.

To change language, load another catalog and update application-owned widget
text explicitly. Use separate message IDs rather than plural rules in this basic
API when an application needs a small, fixed set of language-independent cases.
