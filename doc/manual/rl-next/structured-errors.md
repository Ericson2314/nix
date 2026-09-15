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

So far only `InvalidPathError` from the evaluator exposes anything; more
errors will follow.
