<!-- meristem-template:v1 -->
# Learnings

Project-local reusable lessons. A learning should prevent future work or error, not merely narrate what happened.

Use entries like:

## YYYY-MM-DD — Learning

- **Signal / failure:**
- **Mechanism:**
- **Scope:**
- **Evidence:**
- **Recommended behavior:**
**Validation status:** observed | tested | unvalidated

## 2026-11-27 — Real-time engine ownership

Clock Sync returned engine-owned output buffers after unlocking its processing mutex. A downstream libobs probe that changed quality twice on a control thread while consuming the packet reproduced a heap-use-after-free under AddressSanitizer. The bounded latest-wins handoff now exchanges ownership only at the next serialized callback boundary, and the control writer reclaims superseded or retired state. The actual probe passes ASan after the repair; a 50,000-publication ownership stress test passes ASan/UBSan with all destruction on control. Explicit converter replacement still re-primes; host-level libobs locks and third-party internals are outside this guarantee. Preserve audio history on ordinary parameter edits rather than rebuilding state.
