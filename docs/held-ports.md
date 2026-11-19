# Held ports

Port versions that the maintainers took out of the catalog, or kept out of it, until a license
question about their code is settled.  A held version has no directory under `ports/`, so no
dry-run, no mode of `port-publish`, and no consumer of the catalog stages or publishes it.
`publish-holds.txt` does not list these versions: it names only versions in the catalog and binds
only the `new` mode ([Eligibility and holds](publishing.md#eligibility-and-holds)).  Removal stops
new uploads only: it retracts nothing a registry already has, which is the operator's separate
decision.

A hold here is precautionary.  It is not a finding of infringement, and the version is not
rejected for good.  It returns when the evidence is sufficient and every licensing obligation
that applies can be met, or when recipe distribution settles it.  Declaring upstream's advertised
license does not settle the question: a license declaration establishes no right for third-party
code.

Under recipe distribution ([Licenses and notices](../CONTRIBUTING.md#licenses-and-notices)),
Cabin does not pass on the code in question.  That settles a hold only for material that the
package's builds on targets Cabin supports do not compile: code they compile reaches everything
their users ship.  Each section says which applies.

## Reinstating a version

1. Settle the question recorded below, or confirm that recipe distribution settles the hold, and
   give the evidence in the pull request.
2. Restore the version's directory from the parent of the commit that removed it
   (`git log --diff-filter=D -- ports/<prefix>/<name>/<version>/cabin.toml` finds that commit;
   `git restore --source=<commit>~1 -- ports/<prefix>/<name>/<version>` restores the directory),
   and each version removed with it once every version it requires is in the catalog.
3. Bring the restored files under today's rules: declare `[package] license`, drop the patches
   and `include` entries [Licenses and notices](../CONTRIBUTING.md#licenses-and-notices) no longer
   allows, and add any notice the result requires.
4. Remove the version's section from this file.  A version that returns because of recipe
   distribution records why in its `cabin.toml` and has a `publish-holds.txt` line until recipes
   publish ([Recipe distribution](publishing.md#recipe-distribution)).

A reinstated version that the receipts log already records as uploaded is not uploaded by the
`new` mode; publishing it is the operator's `all` dispatch
([Eligibility and holds](publishing.md#eligibility-and-holds)).

## Held on 2026-11-12

The versions below were in the catalog without a `license` declaration.  Re-examine them in the
order listed, which is an order of investigation, not of risk.

### qhull 8.0.2

- **Code.**  `src/libqhullcpp/QhullIterator.h`: line 27 says "Derived from
  Qt/core/tools/qiterator.h", and about 120 lines of macros expand Qt 4's Java-style iterators in
  a public header.  Qt 4 is under the GPL and the LGPL.
- **Question.**  Which Qt terms apply to the macros, and whether the package can meet them.
- **Recipe distribution.**  Does not settle it: every consumer of libqhullcpp compiles the
  macros.

### plutovg 1.3.3

- **Code.**  `source/plutovg-blend.c` (1144 lines) appears to reproduce code of Qt's
  `qdrawhelper` and `qcompositionfunctions` (LGPL-3.0 or GPL), which it does not credit.
  `INTERPOLATE_PIXEL_255`, `INTERPOLATE_PIXEL_256`, and `BYTE_MUL` match qtbase v5.15.2
  `qdrawhelper_p.h:609-660`, and the radial gradient uses Qt's variable names.
- **Question.**  Which copyright and license obligations apply to the reproduced code.
- **Recipe distribution.**  Does not settle it: the library compiles the code.
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
- **Recipe distribution.**  Does not settle it: a consumer that calls these `gtx` functions
  compiles both blocks.

## Moved from `publish-holds.txt` on 2026-11-12

The version below declared a `license` and was held by a `publish-holds.txt` line.  A hold binds
only the `new` mode, so an `all` dispatch could still have uploaded it; it left the catalog so that
no mode can.  No version in the catalog required it.  A version that returns records the grant,
the ruling, or why recipe distribution settles the hold in its restored `cabin.toml` and drops
what that file says about the hold.

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
- **Recipe distribution.**  Does not settle it: every consumer of `Lazy` compiles the allocator
  code.

## Held before entering the catalog

The versions below were held before they merged, so no commit removed them: reinstating one
starts from the pull request its section names, or from a new port.

### fcl 0.7.0

Proposed in #309 (head `5f9c47a`) and held awaiting confirmation of license and rights.

- **Code.**  Two blocks of fcl 0.7.0 (the `0.7.0` tag archive, sha256
  `90409e940b24045987506a6b239424a4222e2daf648c86dd146cbcb692ebdcbc`) correspond to PQP 1.3 by
  Eric Larsen and Stefan Gottschalk (UNC Chapel Hill), compared at
  GammaUNC/PQP@713de5b70dd1849b915f6412330078a9814e01ab.
  `include/fcl/math/bv/RSS-inl.h:449-1935` (`clipToRange`, `segCoords`, `inVoronoi`, and two
  `rectDistance` overloads) follows PQP's `src/RectDist.h:48-751`: the three helpers match
  statement for statement, and the first `rectDistance` keeps PQP's local names, makes the same
  32 `InVoronoi` and 16 `SegCoords` calls in the same order, and contains all 18 distinct comment
  lines of PQP's `RectDist`.  `include/fcl/math/geometry-inl.h:475-562` (`eigen_old`) follows
  PQP's `src/MatVec.h:788-871` (`Meigen`, which PQP labels "from Numerical Recipes in C") in
  local names, loop bounds, tests, and the error message; fcl's bounding-volume fitting calls it.
  Both fcl files carry only fcl's BSD-3-Clause header.  PQP's `LICENSE` permits use, copying,
  modification, and distribution "for educational, research, and non-profit purposes" with its
  notice kept.  fcl's sources, `README.md`, `CHANGELOG.md`, and `LICENSE` credit neither PQP nor
  Numerical Recipes for this code and state no permission.  Not compared:
  `triangle_distance-inl.h` with PQP's `src/TriDist.cpp`, `obbDisjoint` with PQP's
  `src/OBB_Disjoint.h`, and `Meigen` with the Numerical Recipes text.
- **Question.**  Under which terms the code of both blocks may be redistributed.  The version
  enters the catalog on concrete evidence that explains those terms: a relicensing grant from the
  right holder, another license that applies, or evidence that the code is not what this
  comparison assumes.  Adding PQP's notice does not lift its purpose restriction, and upstream's
  BSD-3-Clause declaration does not settle the question.
- **Recipe distribution.**  Does not settle it: fcl's distance queries and bounding-volume fitting
  compile both blocks, and PQP's terms restrict use as well as redistribution.

### hlslpp 3.9

Held before a pull request opened.

- **Code.**  `matrix_float.h:175-224` (`_hlslpp_inv_4x4_ps`) of hlslpp 3.9 (tag `3.9`, commit
  `3a5b1cf0d807f945ec861201b316c425e9cf5061`) matches, statement for statement and with two
  comments verbatim, `inverse_stubbe` in Rune Stubbe's unlisted gist `matrix_inverse_test.cpp`
  (one revision, 2018-06-21), which states no license or permission.  hlslpp took it in with
  commit `bce100a` ("Replaced 4x4 matrix inverse with much faster alternative"), citing only the
  gist's URL; `_hlslpp_adjoint_4x4_ps` (`:89-135`) is the same code without the division.
- **Question.**  Under which terms the code may be used and redistributed.  The version returns on
  a license or permission from the gist's author that covers the code, or with an hlslpp release
  whose inverse does not derive from the gist; a port does not patch the code out.
- **Recipe distribution.**  Does not settle it: every 4x4 matrix inverse compiles the code.
