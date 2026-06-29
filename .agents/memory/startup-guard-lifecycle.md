---
name: One-shot startup guard lifecycle
description: How to correctly scope temporary guards that protect recording startup.
---

When a temporary guard flag is introduced to protect the first frame(s) of a recording session (e.g., to ignore the transient dead state caused by `togglePracticeMode`/`resetLevel`), it must be cleared on the first stable frame, not only inside the branch it protects.

**Why:** In this mod, `m_justStartedRecording` was initially cleared only inside the respawn-after-death branch. If the first post-start frame was already alive, the guard never fired and stayed `true` until the *first real death*, incorrectly suppressing checkpoint rewind and buffer trimming for that real respawn.

**How to apply:** After the protected branch, clear the guard unconditionally on the same frame so it is always a one-shot startup-only flag. For example:

```cpp
if (m_fields->m_justStartedRecording) {
    m_fields->m_justStartedRecording = false;
    // skip startup-specific cleanup
} else if (...) {
    // normal respawn handling
}
// Ensure the flag is gone after this frame even if the guard didn't fire.
m_fields->m_justStartedRecording = false;
```
