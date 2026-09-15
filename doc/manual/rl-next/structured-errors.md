---
synopsis: Errors can carry structured data, which the JSON logger and the daemon protocol pass on
issues: [7865]
prs: []
---

An error may now expose its content as data as well as a message. When
it does, `--log-format internal-json` emits that data as a `structured`
field on the error's `msg` record, and on any trace frame that has data
of its own, next to the rendered `raw_msg`. Errors without structured
data produce the same records as before.

The daemon protocol gained the `structured-errors` feature: when both
ends support it, an error's structured data crosses the wire together
with its message instead of being lost.

The first errors to expose their content are from the evaluator:
`builtins.throw` and `builtins.abort` (`type` `throw` and `abort`, with the
`message`), undefined variables and missing function arguments (`name`),
values of the wrong type (`expected`, `found`, `value`), failed `assert`
expressions (`expression`), infinite recursion, stack overflow, and
invalid store paths (`path`). Build failures carry their `status` and
whether the build was non-deterministic. System errors carry the error
`code`, its `category`, and the system's `message` for it; file transfer
failures their `kind` and, when small, the `response` body. The evaluator's own trace
frames carry data too: which builtin or function was being called, which
attribute or file was being evaluated. More will follow.
