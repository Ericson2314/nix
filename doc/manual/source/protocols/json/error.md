{{#include error-v1-fixed.md}}

## Examples

### A thrown error

```json
{{#include schema/error-v1/throw.json}}
```

### A value of the wrong type

```json
{{#include schema/error-v1/unexpected_type.json}}
```

### A failed build

```json
{{#include schema/error-v1-store/build_failure.json}}
```

### A system error

```json
{{#include schema/error-v1-util/system.json}}
```
