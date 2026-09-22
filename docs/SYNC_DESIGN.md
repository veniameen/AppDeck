# Shared projects and automation definitions

This design applies to Codex groups with a connected base and shared history enabled. It replaces the former base-to-copy project union with a durable group registry. The original Codex home and linked copies are participants; independent profiles and account-bound cloud projects are outside the group.

The implementation is split between `src/project_sync.hpp` and `src/automation_sync.hpp` (in-memory reconciliation), `src/group_sync.hpp` (filesystem, lifecycle and journal), and `src/project_server_sync.hpp` (Codex's own project API). It does not clone, open, edit or link a Codex database, inspect credentials, or replace Electron user data.

## Where the truth lives

`Shared/<app-id>/group-sync-v1.json`, under AppDeck's data root, contains the agreed project records, automation definitions, deletion tombstones, per-profile observations and application acknowledgements. The file belongs to AppDeck. It is written atomically with mode 0600; its containing directory is private. Serialization is bounded to the same 8 MiB limit used by the reader. An invalid or unsupported journal is an error, not permission to rebuild it from an arbitrary profile.

Each profile's files are an input and a projection of that group registry. They are not complete replicas of another account. File modification times do not choose a winner. A profile that has been offline contributes changes relative to its own previous observation, rather than presenting its entire stale snapshot as new truth.

Bootstrap cannot reconstruct a deletion made before AppDeck started keeping observations. The first valid project snapshots are therefore combined without inferring deletion from absence. Distinct project IDs remain distinct even if they refer to the same directory. Once a baseline exists, an explicit change from a present project to an absent project can produce a tombstone. Deleting or replacing the journal discards this history and must not be used as a routine refresh operation.

## Projects

Supported observations contain an explicit `local-projects` dictionary, including an empty dictionary, plus optional `project-order`. A local record is restricted to `id`, `name`, absolute local `rootPaths`, and recognized creation/update timestamps. Namespaced `cloud:` entries remain local to their account. An unsupported local record makes that profile's project observation unusable as a whole; the parser does not silently discard an unknown record and interpret it as a deletion.

Project records retain a revision, generation, writer, and the revision on which an edit was based. Each profile retains its observed snapshot and revisions it has seen. Adds, name changes, root changes and deletions can originate in any participating profile. An unchanged stale record cannot resurrect a tombstone. A later explicit re-add is accepted only after that profile has observed the deletion. Concurrent deletions take precedence over edits to the same observed generation; concurrent edits use their causal revision and then a stable lexical profile-ID order. Conflicts are reported in the journal. This ordering is deterministic, not a claim that one account's clock is more accurate.

The established group project order is retained and new IDs are appended deterministically. Reordering a sidebar without changing its project records is not currently a synchronized edit. The AppDeck Projects page projects the same group list; removing a synchronized entry there requests removal from the group, not deletion of its directory or task history.

### Native API and Desktop cache

The inspected ChatGPT/Codex Desktop build, 26.915.31945 (9922), has both native app-server projects and a Desktop cache in `.codex-global-state.json`. Its normal Desktop create, update and delete flows update the local cache as well as the native project backend. Synchronizing only one of these stores is insufficient.

For a stopped participant, AppDeck starts the registered bundle's own `codex app-server` with that participant's `CODEX_HOME` and the group's explicit `CODEX_SQLITE_HOME`. Requests use `initialize`, `project/list`, `project/import`, `project/update`, `project/delete`, and the task metadata operations needed to remove memberships. There is no direct SQL or HTTP client in this path. The helper has a bounded lifetime and is reaped when the request completes or fails.

Imports use the local project ID as an idempotency key. Local-to-native mappings learned in any participant are retained in the group journal; inconsistent mappings stop the operation. A missing native project that was already mapped is reported rather than recreated under a new identity. Deletion is restricted to explicit tombstones with a known native mapping. Task memberships are made projectless before a mapped native project is deleted; tasks and repository files are retained. An unmapped tombstone is returned as unresolved and prevents a successful completion claim instead of guessing which native project to delete.

After a successful API operation, AppDeck writes the agreed local registry, order and legacy root labels into the stopped participant's Desktop cache. It preserves unrelated settings and account state. It updates the current host's local-to-native ID mapping and removes references to deleted projects from the selected project, pinned project IDs and task project assignments. The previous file is kept beside it as `.appdeck-previous`.

The source observations are the supported Desktop caches. A project changed exclusively by another app-server client, without updating a Desktop cache, is not automatically imported as a new group edit. In particular, an externally deleted mapped native project can cause a compatibility error while the cache still says that it exists. The implementation preserves that disagreement rather than silently recreating the project.

## Automation definitions and execution ownership

The installed Desktop reads legacy definitions from `$CODEX_HOME/automations/<id>/automation.toml`. Its scheduler state and run history use `$CODEX_HOME/sqlite/codex.db`, independently of the core database selected by `CODEX_SQLITE_HOME`. Sharing core task history therefore does not transfer automation definitions or establish a shared scheduler.

AppDeck synchronizes supported version-1 TOML definitions only. It preserves the definition text, including multiline prompts, schedule, model settings, notification policy and supported project/thread destination fields. Unknown or duplicate fields, unsafe identifiers, unsupported destinations and malformed text produce a conflict without overwriting the original definition. Automation memory files, jitter salt, generated outputs, runtime state and run history are not copied. Removing a definition removes only `automation.toml`, after keeping its previous version; its containing directory and other files remain.

Each canonical definition has one owner profile. On first discovery this is the profile that supplied it; the base is considered first when identical definitions already exist in multiple profiles. New definitions created in a copy are owned by that copy. Equal IDs with differing untracked content are a conflict, not an instruction to overwrite one account's definition.

The owner receives the canonical ACTIVE or PAUSED state. Every other profile receives a PAUSED projection. A nonowner can contribute supported definition-content edits, but manually enabling its replica does not acquire execution ownership. AppDeck records that condition and pauses the replica when it is stopped. An unresolved nonowner ACTIVE definition is unsafe for launching another executor. Globally paused schedules remain paused after synchronization or an ownership change. AppDeck does not control a running Desktop scheduler: manually enabling a replica inside an already-running Codex can execute it until that process is stopped. The launch guard cannot retroactively prevent such an external action.

The executor selection UI changes one definition's owner. All participating Desktop processes must be stopped. The current journal is synchronized and reloaded after the dialog before applying the choice. Old owners and other replicas are paused before any ACTIVE owner projection is written. Immediately before an ACTIVE write, other participating homes must be readable and contain no ACTIVE definition with that ID. A failed pause or unreadable peer blocks activation. The new owner must subsequently run Codex with suitable account access; AppDeck does not impersonate that account or run the schedule itself.

A profile that still owns a live definition cannot be detached from sharing or removed from the manager until another owner is selected. Turning off group sharing while definitions exist requires the Desktop processes to be stopped. Leaving the group does not delete the underlying profile directory.

Automation reconciliation keeps per-profile baselines and deletion tombstones. An ordinary edit is propagated in both directions. A concurrent incompatible edit, or an edit racing a deletion, preserves the conflicting definitions and reports a conflict. Tombstoned IDs are not implicitly reused: an untracked definition appearing under an already deleted ID requires review or a new ID. Owner-only status control is deliberate: the PAUSED replica shown in another profile is not a switch for pausing or resuming the group's executor.

### Heartbeats

Heartbeat definitions retain their existing `target_thread_id`; synchronization never creates a replacement task or rewrites that ID. Shared task history can make the target available to another profile, but execution still depends on Codex's own account permissions, target task state, renderer eligibility, collaboration mode and approval/sandbox state. Copying a heartbeat definition or selecting an owner does not prove that a live heartbeat will execute successfully. It also does not transfer the prior profile's heartbeat cooldowns or runtime history.

## When changes are applied

AppDeck periodically observes the group and records deltas and pending status in its own journal. That timer does not run native project helpers or write profile files. Actual application happens through the explicit synchronization action, explicit project/owner operations, or reconciliation before starting a stopped participating profile. The original Codex is subject to the same stopped-state requirement as copies. Closing only a window is insufficient if its Desktop process is still running. Bringing forward an existing window does not rewrite its live Desktop state. Independent profiles with sharing disabled do not join group launch admission.

Changes destined for a running participant are marked pending. They are applied after the process stops, before AppDeck starts it again. A new participant with no previous state can receive an initial projection. A previously observed Desktop state file or a previously populated automation directory disappearing is unavailable information, not evidence that every contained item was deleted. Unreadable or unrecognized existing state remains unchanged. The supported guarantee is convergence through a full process restart, not instant sidebar refresh in every already-open window.

The manager uses its existing per-data-root singleton lock and an in-process synchronization guard. It rechecks process ownership before writes, and compares the observed file with its current contents before replacing it. A project API request is followed by another process/file check before committing the Desktop cache. These checks do not lock Codex against an independent external launch or an unrelated program editing files; operations outside AppDeck remain a concurrency boundary.

## Persistence and recovery

The sequence is:

1. Read supported participant observations and reconcile their changes.
2. Persist the resulting canonical records, tombstones and observations in AppDeck's journal.
3. Apply allowed projections only to stopped participants, keeping previous files.
4. Acknowledge each successful projection in the journal; leave failed and running participants pending.
5. Read definitions again before determining whether remaining automation state is safe for launch.

A write target is never acknowledged merely because it was planned. A crash after the canonical commit leaves work pending for the next pass. A crash after a profile write but before acknowledgement can be retried: the actual profile contents are observed, imports use stable idempotency keys, and a matching projection does not require another content change. Native project API writes may partially succeed before a later operation or cache write fails; the next pass reconciles that partial progress. No database backup is replayed to force recovery.

The `.appdeck-previous` files are the immediate prior versions, not a versioned backup archive. The journal contains automation prompts and project paths and should be treated as private application data. Diagnostics should report IDs, counts and error categories, not definition prompts, account credentials or arbitrary app-server messages.

## Compatibility and verification boundary

Unknown journal versions, unsupported Desktop/TOML schema, excessive size, unsafe links, unavailable paths, changed files, unsupported app-server methods and incomplete responses are errors or skipped observations. They do not authorize a full home copy, an arbitrary JSON merge or direct database maintenance. Unavailable information is distinct from an explicitly empty supported project registry or an enumerated empty definition directory.

Portable tests exercise the actual in-memory production engines with a Foundation-compatible adapter, including first observation, deletion propagation, stale resurrection, deterministic project conflicts, definition conflicts, PAUSED replicas, owner transfer ordering, multiline TOML and unsupported input. Native helper tests exercise the project API against isolated data. These checks do not establish real multi-account GUI behavior, live heartbeat completion, Intel execution or the minimum macOS version. Those acceptance results are recorded in [TESTING.md](TESTING.md) and must be produced with an isolated `APPDECK_DATA_ROOT`; existing user profiles are not disposable test fixtures.

## Confirmed native deletions

An unchanged Desktop cache can outlive deletion from the native registry. Explicit apply validates the native mappings through one stopped participant even when local revisions match. The adapter reports all mapped-but-absent legacy IDs after a complete successful list and before any mutation. The coordinator turns that verified evidence into durable `codex-server` deletion observations and retries (at most three passes). Unknown mappings and unsuccessful native reads never imply deletion. The 188-assertion native group integration covers this exact stale-cache scenario.

`APPDECK_SYNC_ONCE=1` runs a bounded maintenance apply, uses the normal manager lock and stopped-process checks, emits a small status report, and exits without starting or closing managed windows. It requires the GUI manager to be closed.
