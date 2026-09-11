---
id: verify-editorconfig-source-parsing
date: 2026-09-08
scope: module
tags: [editorconfig, rider, parsing, testing]
source: bug-fix
confidence: 0.7
related: []
---

# Verify EditorConfig properties from source text

## Context
Rider processes EditorConfig source through its lexer and parser before
matching plugin property descriptors.

## Mistake
Constructed PSI proxies can satisfy a descriptor while representing a value
that the real parser cannot produce. Dots in helper list entries expose this
gap and can also produce misleading redundancy warnings after parser recovery.

## Lesson
Verify custom property examples through the installed SDK parser, including
the complete resulting values and error nodes. Keep descriptor validation as
a separate check. Treat an empty MCP problem list as the connector's result,
not proof that editor highlighting is clean.

Exercise the production descriptor provider as well as the parser. Read bundled
plugin schemas through the plugin classloader. The [IntelliJ VFS snapshot](https://plugins.jetbrains.com/docs/intellij/virtual-file-system.html)
persists across sessions and uses timestamps to detect changes, while reproducible
archives retain fixed entry timestamps.

## When to Apply
When adding or changing EditorConfig properties, schemas, or serialized values.
