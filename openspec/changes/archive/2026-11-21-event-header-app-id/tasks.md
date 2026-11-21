# Tasks

## 1. Header and producer

- [x] 1.1 Add `char app_id[4]` and `app_id_text()` to `EventHeader`; update the 72-byte layout
      assertions, JSON, and `to_string`.
- [x] 1.2 Add the application-code producer overloads and copy the code into every published
      header.
- [x] 1.3 Extend the event-system tests for the new layout, JSON round-trip, and producer stamp.

## 2. Recorder

- [x] 2.1 Name event channels by the header application code and fall back to the service name.
- [x] 2.2 Move the recorder event-envelope payload offset to 72 and declare `app_id`.
- [x] 2.3 Update the recorder smoke to publish a stamped event and assert the channel name.

## 3. Validation

- [x] 3.1 Run Debug, COM, Release, TSan, ASan, complexity, and format checks.
- [x] 3.2 Validate and archive the change.
