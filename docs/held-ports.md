# Held ports

Port versions that the maintainers took out of the catalog until a redistribution question is
settled.  A held version has no directory under `ports/`, so no dry-run, no mode of
`port-publish`, and no consumer of the catalog stages or publishes it.  `publish-holds.txt` does
not list these versions: it names only versions in the catalog and binds only the `new` mode
([Eligibility and holds](publishing.md#eligibility-and-holds)).  Removal stops new uploads only:
it retracts nothing a registry already has, which is the operator's separate decision.

A hold here is precautionary.  It is not a finding of infringement, and the version is not
rejected for good.  It returns when the evidence is sufficient to justify redistribution and
every licensing obligation that applies can be met.  Declaring upstream's advertised license does
not settle the question: a license declaration establishes no redistribution right for
third-party code.

## Reinstating a version

1. Settle the question recorded below, and give the evidence in the pull request.
2. Restore the version's directory from the parent of the commit that removed it
   (`git log --diff-filter=D -- ports/<prefix>/<name>/<version>/cabin.toml` finds that commit;
   `git restore --source=<commit>~1 -- ports/<prefix>/<name>/<version>` restores the directory),
   and each version removed with it once every version it requires is in the catalog.
3. Declare `[package] license` and add any notice the result requires
   ([Licenses and notices](../CONTRIBUTING.md#licenses-and-notices)).
4. Remove the version's section from this file.

A reinstated version that the receipts log already records as uploaded is not uploaded by the
`new` mode; publishing it is the operator's `all` dispatch
([Eligibility and holds](publishing.md#eligibility-and-holds)).

## Held on 2026-11-12

The versions below were in the catalog without a `license` declaration.  Re-examine them in the
order listed, which is an order of investigation, not of risk.

### boost-interprocess 1.92.0

- **Code.**  `boost/interprocess/sync/detail/condition_algorithm_8a.hpp`, from line 36: about
  120 comment lines of pseudo-code for Alexander Terekhov's condition-variable "Algorithm 8a".
  The file says it comes from the pthreads-win32 discussion, and the same text documents
  pthreads-win32's `pthread_cond_wait.c` (LGPL).  The C++ implementation is Ion Gaztañaga's.
- **Question.**  Whether the pseudo-code is protected expression that needs further licensing
  compliance, or only describes an algorithm.
- **Removed with it.**  boost-flyweight 1.92.0 and boost-log 1.92.0, which require it.  boost-log
  also requires boost-asio and returns only when both do.

### asio 1.38.2 and boost-asio 1.92.0

- **Code.**  `asio/detail/impl/socket_ops.ipp`, about 650 lines from line 3052, and the same code
  in `boost/asio/detail/impl/socket_ops.ipp`: the `getaddrinfo` emulation, which appears to be a
  C++ port of `libgai` from W. Richard Stevens's "UNIX Network Programming", volume 1, second
  edition.  Functions, branch structure, and the order of literals correspond.  The book's source
  distribution carries a warranty disclaimer and no grant to redistribute.  The code is compiled
  only where the platform lacks `getaddrinfo`, but it is part of the published sources.
- **Question.**  Whether protected implementation code was copied, or only unprotected algorithms
  and ideas were used.
- **Removed with boost-asio.**  boost-beast, boost-cobalt, boost-log, boost-mqtt5, and
  boost-process 1.92.0, which require it.  boost-log also requires boost-interprocess, and
  boost-mqtt5 requires boost-beast.

### qhull 8.0.2

- **Code.**  `src/libqhullcpp/QhullIterator.h`: line 27 says "Derived from
  Qt/core/tools/qiterator.h", and about 120 lines of macros expand Qt 4's Java-style iterators in
  a public header.  Qt 4 is under the GPL and the LGPL.
- **Question.**  Which Qt terms apply to the macros, and whether the package can meet them.

### plutovg 1.3.3

- **Code.**  `source/plutovg-blend.c` (1144 lines) appears to reproduce code of Qt's
  `qdrawhelper` and `qcompositionfunctions` (LGPL-3.0 or GPL), which it does not credit.
  `INTERPOLATE_PIXEL_255`, `INTERPOLATE_PIXEL_256`, and `BYTE_MUL` match qtbase v5.15.2
  `qdrawhelper_p.h:609-660`, and the radial gradient uses Qt's variable names.
- **Question.**  Which copyright and license obligations apply to the reproduced code.
- **Removed with it.**  lunasvg 3.5.0 and plutosvg 0.0.8, which require it.

### glm 1.0.3

- **Code.**  `glm/gtx/pca.inl:94-280` (`findEigenvaluesSymReal`) appears to be a close rewrite
  of `tred2` and `tqli` from "Numerical Recipes in C": the variable names, the 1-based indices,
  and the "Too many iterations" message match.  The Numerical Recipes terms do not allow
  redistributing its source.  `glm/gtx/intersect.inl:28-94` appears to copy Möller and
  Trumbore's `raytri.c` with its comments, and no grant to redistribute that file was found.
  Both are public API (`gtx`), so narrowing `[package] include` would change upstream behavior.
- **Question.**  The provenance of both blocks and the permission to redistribute them.  The
  version returns on sufficient evidence of lawful redistribution or of independent
  implementation, or on another legitimate resolution.

## Moved from `publish-holds.txt` on 2026-11-12

The versions below declared a `license` and were held by a `publish-holds.txt` line.  A hold binds
only the `new` mode, so an `all` dispatch could still have uploaded them; they left the catalog so
that no mode can.  No version in the catalog required either.  A version that returns records the
grant or the ruling in its restored `cabin.toml` and drops what that file says about the hold.

### async-simple 1.4.0

- **Code.**  `async_simple/coro/PromiseAllocator.h:40-51` and `77-359` copy the allocator support
  of the reference implementation of P2502R2 (Casey Carter, Lewis Baker, and Corentin Jabot,
  <https://godbolt.org/z/5hcaPcfvP>): with comments dropped and the case and underscores of
  identifiers ignored, 64% of the file's 10-token sequences occur in it, and two of its
  identifiers and eight of its nine comments survive verbatim.  `coro/Generator.h:93-112` and
  `327-619` follow its generator, promise base, nesting state, awaiters, and iterator.
  `Generator.h:131-323` and `PromiseAllocator.h:60-65` reproduce P2502R2's prose and examples,
  and `PromiseAllocator.h:67-73` the reference's allocator example.  Neither the reference
  implementation nor the paper grants any rights.  Microsoft's later version under Apache-2.0
  WITH LLVM-exception (`stl/inc/generator` at CaseyCarter/STL@ad0d033) covers only part of the
  code (52% by the same measure) and none of the prose.  The material cannot be left out: every
  `Lazy` promise derives from `PromiseAllocator<void, true>` (`coro/Lazy.h:93`).
- **Question.**  Whether the authors of the reference implementation grant the right to
  redistribute the code and the prose.  The version returns when such a grant is found or the
  maintainers rule on it.

### json-schema-validator 2.4.1

- **Code.**  `src/string-format-check.cpp:209-288` quotes all of RFC 3986 Appendix A's ABNF (60
  non-blank lines) in comments.  RFC 3986 (2005) is subject to BCP 78 (RFC 3667, then RFC 3978),
  whose section 3.3 grants rights only to the ISOC and the IETF.  The IETF Trust Legal Provisions
  license only documents published after they took effect (section 2.b-c); other rights come, in
  most cases, only from the authors (section 2.d).  The removed port shipped RFC 3986's Full
  Copyright Statement in `NOTICE.RFC`.
- **Question.**  Whether the quoted grammar may be redistributed, or is not protected expression.
  The version returns when a grant is found or the maintainers rule the grammar unprotected.
