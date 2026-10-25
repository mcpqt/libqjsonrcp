# JSON-RPC 2.0 Compliance Findings — libqjsonrpc

Date: 2026-10-25
Spec source: `JSON-RPC 2.0 Specification.html` (saved in this repo)
Code reviewed: `src/json.h`, `src/jsonrpc.h`, `src/jsonrpc.cpp`, `src/error.h`,
`src/session.h`, `src/session.cpp`, `src/transport.h`,
`src/stdio_transport.cpp`, `src/qt_tcp_transport.cpp`, `tests/tst_jsonrpc.cpp`.

## Verdict

**Partially compliant at the time of review.** The single-request happy path
works and the JSON layer is already Qt-native (`QJsonValue`/`QJsonObject`/
`QJsonArray`/`QJsonDocument`; no third-party JSON library is involved). At the
time of the review the implementation was missing several **MUST**
requirements, most importantly batch support and error responses for malformed/
invalid frames. It was lenient in a few places where the spec uses
**SHOULD NOT**.

> **Update 2026-10-25:** F1–F10 and F12 have been implemented; F11 remains a
> documented leniency. See `JSONRPC_COMPLIANCE_UPDATES.md` for the resolution
> status and verification (`37 passed, 0 failed`).

## Findings

| ID | Severity | Area | Location |
|----|----------|------|----------|
| F1 | High | Missing Parse/Invalid Request error responses | `src/session.cpp:352-385` |
| F2 | High | Batch requests unsupported | `src/jsonrpc.cpp:169-186`, `src/session.cpp:352-385` |
| F3 | Medium | `params` not validated as Structured | `src/jsonrpc.cpp:15-29`, `93-125` |
| F4 | Medium | Reserved `rpc.` method prefix not enforced | `src/jsonrpc.cpp:15-20` (unused) |
| F5 | Medium | Response `id` not required on success | `src/jsonrpc.cpp:141-167` |
| F6 | Medium | Error object decoding does not validate | `src/error.h:108-115` |
| F7 | Medium | Mixed request/response shapes accepted | `src/jsonrpc.cpp:169-186` |
| F8 | Medium | Top-level primitive/empty-array inputs dropped | `src/json.h:502-516`, `src/session.cpp:352-385` |
| F9 | Low | Explicit `params: null` treated as absent | `src/json.h:493-495`, `src/jsonrpc.cpp:93-125` |
| F10 | Low | Outbound request `params` not validated | `src/session.cpp:218-233` |
| F11 | Low | Reserved error-code range not enforced | `src/error.h:20-45` |
| F12 | Test discrepancy | Request id null/fractional accepted | `src/jsonrpc.cpp:58-77`, `tests/tst_jsonrpc.cpp:218-229` |

## Details

### F1 — Missing Parse/Invalid Request error responses (High)
`Session::handle_frame` logs and drops any frame that fails `parse_json` or
`parse_message`. The spec says the Server MUST reply to a request; its examples
show invalid JSON and invalid Request objects returning a single error response
with `id: null` (Parse error -32700 / Invalid Request -32600). See
`src/session.cpp:352-385` (comments explicitly acknowledge the drop).

Impact: clients waiting for a response can hang; protocol error reporting is
lost. Recommendation: send a single error response with `id: null` for parse
errors and invalid request envelopes. Notifications cannot be identified
reliably for invalid JSON, but the spec examples still require the error
response.

### F2 — Batch requests unsupported (High)
The spec section 6 requires servers to accept an Array of Request objects and
return an Array of Responses (omitting notifications; no empty array; single
error object for an empty/invalid batch). `parse_json` accepts top-level arrays,
but `parse_message` immediately calls `as_object(j)` and throws; `handle_frame`
then drops the frame. See `src/jsonrpc.cpp:169-186` and `src/session.cpp:352-385`.

Impact: any batch request from a conforming client is silently discarded.
Recommendation: model a batch as a distinct message type or handle arrays in
`handle_frame`; dispatch each element, collect responses for non-notifications,
and send an array only when non-empty.

### F3 — `params` not validated as Structured (Medium)
Spec 4.2: if present, `params` MUST be an Object or Array. `validate_params`
exists (`src/jsonrpc.cpp:22-29`) but is never called. `JsonRpcRequest::operator=`
and `JsonRpcNotification::operator=` simply copy whatever `params` value is
present (`src/jsonrpc.cpp:93-125`). `Session::handle_request` forwards it to the
handler without validation (`src/session.cpp:453-457`).

Impact: malformed params reach handlers; clients may get a handler-specific
failure instead of the required Invalid params (-32602) / Invalid Request
(-32600). Recommendation: validate at decode time when `params` is present and
reject non-object/non-array values; use -32602 when the request id is known.

### F4 — Reserved `rpc.` method prefix not enforced (Medium)
Spec 4 and 8: method names beginning with `rpc.` are reserved. `validate_method`
exists (`src/jsonrpc.cpp:15-20`) but is never called. Requests and notifications
with `rpc.*` methods are accepted and routed normally.

Impact: application handlers can collide with protocol extension methods.
Recommendation: call `validate_method` during request/notification decode (or at
least before dispatch) and return Invalid Request.

### F5 — Response `id` not required on success (Medium)
Spec 5: `id` is REQUIRED on a Response. It may be Null only when the Server
could not detect the request id (e.g. Parse error/Invalid Request).
`JsonRpcResponse::operator=` accepts a missing `id` for **any** response and
stores a null id (`src/jsonrpc.cpp:146-151`). Serialization always emits an
`id` member, but decoding loses the distinction between absent and null and
accepts invalid success responses.

Impact: a success response without an id cannot be correlated and should be
rejected as Invalid Request. Recommendation: require `id` for responses; allow
absent/null only for error responses where the request id could not be
determined.

### F6 — Error object decoding does not validate (Medium)
Spec 5.1: `error` MUST be an Object with `code` an integer and `message` a
string; `data` is optional. `ErrorObject::operator=` does:
`value.toObject()`, `obj.value("code").toInt()`, `obj.value("message").toString()`
(`src/error.h:108-115`). Non-object input becomes an empty object; non-integer
code truncates or becomes 0; non-string message becomes an empty string; absent
fields are silently defaulted.

Impact: malformed error responses are accepted and may resolve futures with
bogus errors. Recommendation: require an object, require `code` to be an exact
integer, require `message` to be a string, and reject malformed error objects
with a parse/invalid-response error.

### F7 — Mixed request/response shapes accepted (Medium)
`parse_message` classifies solely by presence of `method`/`id`/`result`/`error`
(`src/jsonrpc.cpp:169-186`). An object containing `method` and `result` (or
`error`) is treated as a Request or Notification and the response members are
ignored. The spec requires an object to be either a Request/Notification or a
Response; mixed shapes are Invalid Request.

Impact: malformed frames are silently misinterpreted. Recommendation: reject
frames that combine `method` with `result`/`error`, or combine `id` with
`method` and `result`/`error`, etc.

### F8 — Top-level primitive / empty-array inputs dropped (Medium)
`parse_json` throws for top-level primitives (`src/json.h:502-516`) and accepts
empty arrays. `handle_frame` catches the throw and drops the frame; empty arrays
then fail `as_object` and are dropped. Spec examples require a single Invalid
Request / Parse error response with `id: null` for these cases (and for an empty
batch specifically, a single Invalid Request response).

Impact: invalid frames do not produce the mandated error response. Recommendation:
route all parse/classification failures through a response path (see F1/F2).

### F9 — Explicit `params: null` treated as absent (Low)
`take_optional_json` stores an explicit JSON null for `params`, but both the
serializer and `validate_params` treat null as “absent”
(`src/json.h:493-495`, `src/jsonrpc.cpp:22-29`, `88-89`, `110-111`). Spec 4.2
says params, if present, MUST be Structured; an explicit null is not structured.

Impact: a request with `"params": null` is accepted and round-tripped as if
`params` were omitted. Recommendation: track key presence; reject explicit null
params (or normalize only when the key is absent).

### F10 — Outbound request params not validated (Low)
`Session::register_pending` copies any non-null `params` into the request and
sends it without checking object/array (`src/session.cpp:218-233`). The spec
requires params to be Structured. Recommendation: validate in
`send_request`/`register_pending` or document that callers must supply
Object/Array.

### F11 — Reserved error-code range not enforced (Low)
Spec 5.1 reserves -32768..-32000. The enum includes application/MCP codes and
`no_error = 0`; nothing prevents constructing an `ErrorObject` with a reserved
code that is not defined by the spec. This is a “SHOULD”/definition issue rather
than a strict MUST for every implementation. Recommendation: validate custom
codes or document the allowed ranges.

### F12 — Request id null/fractional accepted (Test discrepancy)
Spec 4: `id` MUST be String, Number, or NULL if included; Null is discouraged;
Numbers SHOULD NOT contain fractional parts. `RequestId::operator=` accepts
strings, any double (including fractional), and null
(`src/jsonrpc.cpp:58-77`). This is **spec-lenient, not a MUST violation**, but
`testRequestIdInvalid` expects boolean/fractional/null to throw
(`tests/tst_jsonrpc.cpp:218-229`), so the suite fails for null/fractional.

Impact: no protocol violation; only a test/implementation policy mismatch.
Recommendation: decide policy. For strict spec alignment, accept null and
fractional (as implemented) and update the test; if the project wants stricter
ids, reject null/fractional and document that deviation.

## Verified compliant behavior

- JSON layer is Qt-native; no non-Qt JSON library is used.
- `jsonrpc` MUST be exactly `"2.0"` on request/notification/response; enforced.
- Request `method` MUST be a String; enforced by `require<QString>`.
- Request/notification/response are mutually classified for the normal single-
  message cases.
- Response carries exactly one of `result`/`error`; enforced
  (`src/jsonrpc.cpp:153-159`).
- Response serialization emits `jsonrpc`, `id`, and either `result` or `error`.
- Session echoes the request id in the response (`src/session.cpp:440-441`).
- Valid notifications produce no response (`src/session.cpp:505-527`).
- Missing handlers produce Method not found (-32601); handler exceptions map to
  Invalid params (-32602) or Internal error (-32603) as appropriate
  (`src/session.cpp:443-492`).
- Member names are case-sensitive via Qt JSON objects.

## Suggested fix order

1. F1/F8: add a response path for parse/invalid-request failures.
2. F2: add batch support (including empty/invalid batch semantics).
3. F3/F4/F9: enforce params and reserved method rules at decode time.
4. F5/F7: tighten response id and frame-shape validation.
5. F6/F11: validate error objects and code ranges.
6. F12: align the test with the chosen id policy.

## Changes already made in this session (for context)

- Added explicit `from_json` overloads for wire structs to stop generic ADL
  recursion that caused a stack-overflow SIGSEGV in tests.
- Restored `JsonRpcResponse::outcome` to a `std::variant<QJsonValue, ErrorObject>`
  so assigning an `ErrorObject` automatically marks the response as an error and
  removes the dangling-reference `error()` implementation.
- Removed duplicate `v.isNull()` checks in `src/json.h`.

These changes are build-clean; the only failing test is F12.
