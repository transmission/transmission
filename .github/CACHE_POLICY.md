# CI cache policy

GitHub Actions gives this repository 10 GiB of cache storage. The goal is
one generation of every cache family, small enough that a generation plus
the replacements briefly coexisting with it stays under that limit.

## Cache classes

### Compiler output (ccache)

`setup-ccache` derives one key per job from the job name, an optional
matrix suffix, and the workflow-level `CACHE_SCHEMA`; `finalize-ccache`
trims, saves, verifies, and deletes the snapshots it supersedes. Pull
requests restore main's newest snapshot and never save. Jobs whose working
set outgrows the trim window pass a shorter `trim-age`; the local size
limit is deliberately generous because trimming, not the limit, governs
persisted size.

Jobs delete their own superseded snapshots rather than leaving them all to
the janitor so that, within a push, the cache never holds much more than
one generation -- the janitor alone would let two full generations coexist
until the last build finished.

### Incremental analysis (ctcache)

clang-tidy results are keyed by toolkit and configuration hash and saved
on every run, pull requests included: PR-side reuse is the point, and fork
PRs' read-only tokens could not supersede anyway, so the janitor collapses
each family to its newest snapshot instead.

### Content-addressed dependencies

Windows dependency prefixes, Android vcpkg binaries, and the BSD VM images
are keyed by their inputs, so identical keys hold identical bytes. Branch
copies are deleted once main holds the same key and version; every branch
can restore main's copy through the base-branch fallback. Gradle's caches
are excluded: gradle/actions manages its own rotation.

Windows dependency keys also carry `CACHE_SCHEMA`, which is what lets the
janitor retire them. Content addressing alone retires an entry only when
its own inputs change, so a change to the key *format* strands the
previous generation: nothing produces the old key again, and no newer
sibling supersedes it.

## Repository maintenance

After all cache producers finish, the janitor (`prune-caches`) removes
superseded snapshots, keys from retired `CACHE_SCHEMA` generations, and
branch copies shadowed by main, then warns when usage crosses 9 GiB.
Families that stop being produced age out through the provider's 7-day
unused-entry eviction.

When adding a cache:

1. Include every compiler, platform, configuration, or dependency input
   that changes whether an entry can be reused.
2. Use a content-addressed key for immutable dependencies.
3. Use `setup-ccache`/`finalize-ccache` for evolving compiler output.
4. Add the producing job to the janitor's `needs` list.
5. Account for its steady-state size within the 9 GiB warning threshold.
