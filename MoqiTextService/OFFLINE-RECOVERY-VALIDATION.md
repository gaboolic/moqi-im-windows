# Offline TSF recovery validation

This change does not raise WINVER / _WIN32_WINNT, modify libIME2, or require installation/registration as part of source validation.

## Recovery contract

- Retiring an RPC stream clears queued replies, invalidates its scheduled response edit sessions, and hides/clears candidate and message UI.
- The context that owns the composition is retained independently of current focus. `Ime::TextService::endComposition` requests a synchronous read/write edit session. It retains ordinary inline text and clears its attributes; only libIME2's synthetic dummy anchor is removed. Candidate-only preedit is UI state, not document text.
- If the host refuses that synchronous write lock, a lifetime-guarded asynchronous write session retries termination. Until termination succeeds, no RPC reply is applied and no new backend connection is used. Key-down is temporarily consumed rather than passed into the abandoned composition. Once clean, communication failures pass through normally.
- Offline composition termination never reconnects from its termination notification. Old queued callbacks cannot flush replies from a replacement connection.

## Checks performed here

- Read libIME2's edit-session, composition termination, key-routing, and COM smart-pointer APIs.
- Source/diff checks only; no CMake build, installation, DLL registration, or live TSF test was run by this agent. Win32/x64 compilation belongs to the lead.

## Required runtime checks (not yet performed)

Using an already provisioned test system / TSF host (do not install or register as part of this checklist):

1. Start inline preedit with visible candidates; interrupt/hang the backend, then type a printable key. Existing document preedit must remain, underlining/composition must end, candidates/messages must disappear, and the new key must appear outside the old range.
2. Repeat in candidate-only preedit mode. No dummy anchor may remain in the document; no stale candidate recovery may reopen the old list.
3. Refuse synchronous write sessions once, then allow asynchronous write sessions. Verify no key changes the old composition while cleanup is pending; allow cleanup, then confirm normal fail-open and fresh reconnection.
4. Hold a queued async response edit session, disconnect, reconnect, then release the old session. It must not apply old commits or flush new replies using the old context. Deliver a late reply from the retired pipe and confirm no duplicate commit.
5. Switch document focus during disconnect and switch/deactivate the language profile while cleanup is queued. The old owning context must be cleaned; callbacks must not touch a destroyed/replaced Client.
6. After recovery, verify candidate paging, consecutive commit-plus-new-preedit responses, mode icon registration, and ordinary composition termination remain functional.

## Remaining risks

- Hosts that indefinitely deny all write sessions can leave cleanup pending and key-down temporarily consumed. This intentionally prioritizes not corrupting document text over fail-open in a still-active abandoned range.
- TSF host behavior for nested synchronous edit-session requests (the existing libIME2 API) and asynchronous retry must be verified on real hosts; no host integration tests are available in this scoped change.
- EndComposition preserves document text but uses libIME2's existing caret-collapse behavior. Candidate-only UI preedit is discarded on disconnect because it was never inserted into the document.
