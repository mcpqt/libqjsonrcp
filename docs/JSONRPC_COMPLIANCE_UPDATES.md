# JSON-RPC 2.0 Compliance Updates — libqjsonrpc

Date: 2026-10-25
Related document: `JSONRPC_COMPLIANCE_FINDINGS.md`
Spec source: `JSON-RPC 2.0 Specification.html`

## Summary

The library now uses Qt's JSON types exclusively for wire messages. The codec
layer (`src/json.h`) is built on `QJsonValue`, `QJsonObject`, `QJsonArray`, and
`QJsonDocument`; there is no third-party JSON library in the build. This update
closes the protocol gaps identified in the findings document and adds regression
coverage in `tests/tst_jsonrpc.cpp`.

Verification: `ctest --test-dir build --output-on-failure` reports
`37 passed, 0 failed`.

## Finding resolution

| ID | Status | Resolution |
|----|--------|------------|
| F1 | Fixed | Parse/invalid-request failures now send a JSON-RPC error response with `id: null` (parse error -32700 or invalid request -32600) instead of silently dropping the frame. See `Session::handle_frame` and `Session::send_error`. |
| F2 | Fixed | `JsonRpcMessage` now includes `std::shared_ptr<JsonRpcBatch>`. `parse_message` accepts non-empty top-level arrays; `Session::handle_batch` dispatches each element and sends one response array when at least one response is required. Notification-only batches produce no response. |
| F3 | Fixed | Requests/notifications validate `params` when present: it must be a `QJsonObject` or `QJsonArray`. Non-structured params are rejected during decode. |
| F4 | Fixed | Method names beginning with the reserved `rpc.` prefix are rejected for inbound requests/notifications and outbound calls. |
| F5 | Fixed | Inbound responses must contain an `id`. A success response may not use a null id; error responses may use a null id only when the request id could not be identified. |
| F6 | Fixed | `ErrorObject` decoding validates the wire shape: the value must be an object, `code` must be an exact integer, and `message` must be a string. Unknown/application-defined integer codes are preserved. |
| F7 | Fixed | Mixed envelope shapes are rejected: an object carrying both `method` and `result`/`error` is invalid. |
| F8 | Fixed | Top-level primitive/empty-array inputs produce the required error response. Empty batches produce a single invalid-request error object. |
| F9 | Fixed | Explicit `"params": null` is rejected when the key is present; absent `params` remains valid and is not serialized. |
| F10 | Fixed | Outbound request/notification params are validated. Invalid requests reject the returned future; invalid notifications return `std::errc::invalid_argument`. |
| F11 | Documented | Reserved error-code ranges are not enforced; unknown application-defined integer codes are intentionally accepted. This remains a documented leniency rather than a MUST violation. |
| F12 | Aligned | Tests now follow the implemented spec-lenient id policy: null and fractional numeric ids are accepted and preserved; boolean ids are rejected. |

## New and updated tests

- `testParamsValidation` — object/array params accepted; string and explicit null rejected.
- `testReservedMethod` — `rpc.*` methods rejected for requests and notifications.
- `testBatchParse` — batch parsing, invalid-element flagging, empty-array rejection, and round-trip serialization.
- `testBatchDispatch` — mixed valid request + invalid element produces a two-element response array; notification-only batches produce no response.
- `testParseErrorSendsResponse` — malformed JSON and empty arrays return the mandated error responses.
- Existing notification/request/session tests now use `QJsonObject` params rather than string literals, matching the Qt-native wire model.
- `testRequestIdInvalid` now checks acceptance of null/fractional ids and rejection of booleans.

## Notes and remaining leniencies

- Batch response ordering is unspecified by JSON-RPC 2.0; responses are emitted in
  completion order.
- Fractional request ids are preserved even though the spec discourages them.
- Error codes outside the standard set are accepted and round-tripped.
- Invalid `params` encountered during envelope decoding are reported as Invalid
  Request (-32600) with a null id, because the frame is not a valid Request
  object. A handler that detects a bad parameter shape may still return Invalid
  Params (-32602) with the original id.
