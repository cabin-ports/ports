//! Port discovery and publication planning: scans the committed
//! `ports/<prefix>/<name>/<version>/` directories ([`port_prefix`]) and
//! orders them so every port publishes after the ports it depends on.

use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, anyhow, bail};
use cabin_core::{DependencySource, PackageName, UpstreamProvenance};
use semver::Version;

/// Registry scope every port publishes under.
const REGISTRY_SCOPE: &str = "cabin-ports";

/// One committed port, ready to materialize and publish.
#[derive(Debug, Clone)]
pub struct PortConversion {
    /// `ports/<prefix>/<name>/<version>/` source directory.
    pub committed_dir: PathBuf,
    /// The declared provenance the shared materializer runs.
    pub upstream: Box<UpstreamProvenance>,
    /// Scoped registry name (`cabin-ports/<lowercase>`).
    pub scoped_name: PackageName,
    /// The upstream version, verbatim: a packaging correction
    /// republishes it as a new registry revision.
    pub published_version: Version,
    /// The committed manifest text, published verbatim.
    pub manifest: String,
    /// The publication-order edges, with their requirements so ordering
    /// waits for a *satisfying* version: the normal dependencies a
    /// default build resolves (non-optional, or optional and activated by
    /// the default features), a subset of
    /// [`Self::declared_requirements`].
    pub dependencies: Vec<PortDependencyEdge>,
    /// Every registry requirement the published manifest declares,
    /// ordering edge or not.
    pub declared_requirements: Vec<DeclaredRequirement>,
    /// Language standards a probe consumer must use to satisfy every
    /// probed target's declared interface requirement.  `None` keeps
    /// the probe's defaults.
    pub probe_standards: ProbeStandards,
    /// Published keys of the library-like targets the preflight probe
    /// links, referenced through explicit `package:target` selectors
    /// unless [`Self::sole_library_target`] allows the bare-package
    /// shorthand.
    pub library_like_target_keys: Vec<String>,
    /// Whether the published package declares exactly one
    /// library-like target *in total*, gated ones included.  Only then
    /// may the probe use the bare-package shorthand, which Cabin
    /// refuses as ambiguous against several targets.
    pub sole_library_target: bool,
}

/// The `c-standard` / `cxx-standard` a generated probe consumer must
/// declare, and which probed targets each consumer links.  `None`
/// keeps that probe's default.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ProbeStandards {
    /// Lowest C standard satisfying the declared `interface-c-standard`
    /// of `c_targets` and `dual_targets`.
    pub c: Option<cabin_core::CStandard>,
    /// Lowest C++ standard satisfying the remaining probed targets'
    /// declared `interface-cxx-standard`.
    pub cxx: Option<cabin_core::CxxStandard>,
    /// Probed targets with `interface-cxx-standard = "none"`, linked by
    /// the C consumer instead of the C++ one.
    pub c_targets: Vec<String>,
    /// C++-consumable probed targets that also declare a C interface:
    /// linked by both consumers.
    pub dual_targets: Vec<String>,
}

/// One inter-port dependency edge.
#[derive(Debug, Clone)]
pub struct PortDependencyEdge {
    /// Scoped registry name of the dependency.
    pub scoped: PackageName,
    /// Version requirement the conversion carries for it.
    pub req: semver::VersionReq,
}

/// One registry requirement a published manifest declares, with
/// whether a default invocation actually resolves it.
#[derive(Debug, Clone)]
pub struct DeclaredRequirement {
    /// Scoped registry name of the dependency.
    pub scoped: PackageName,
    /// Version requirement declared for it.
    pub req: semver::VersionReq,
    /// Whether some default invocation resolves this edge: a
    /// non-optional normal dependency, an optional one the default
    /// features activate, or a dev dependency (`cabin test` activates
    /// those for the selected package).
    pub default_active: bool,
}

/// The scoped registry name for a port: `cabin-ports/<name>`, with
/// the directory spelled exactly as the package name.
///
/// # Errors
/// Returns an error when the directory name is not a valid package
/// name (`[a-z0-9][a-z0-9_-]*`).
pub(crate) fn scoped_package_name(port_name: &str) -> Result<PackageName> {
    Ok(PackageName::new(format!("{REGISTRY_SCOPE}/{port_name}"))?)
}

/// The directory a port named `name` lives under in `ports/`: the
/// name's first two characters, or the whole name when it has one.
/// Only the `<name>` directory beneath it is the port's identity.
#[must_use]
pub fn port_prefix(name: &str) -> &str {
    name.char_indices()
        .nth(2)
        .map_or(name, |(end, _)| &name[..end])
}

/// Scan `ports_dir` and return every committed port in publication
/// order (dependencies first; name-sorted within a rank, versions
/// ascending within a name).
///
/// # Errors
/// Returns an error when the directory cannot be read, any port fails
/// to load, or the inter-port dependency graph has a cycle.
pub fn load_conversions(ports_dir: &Path) -> Result<Vec<PortConversion>> {
    let packages = discover_ports(ports_dir)?;
    if packages.is_empty() {
        bail!("no ports found under {}", ports_dir.display());
    }

    let conversions = packages
        .iter()
        .map(|package_dir| load_package(package_dir))
        .collect::<Result<Vec<_>>>()?;

    ensure_requirements_satisfiable(&conversions)?;
    order_by_dependencies(conversions)
}

/// Load one committed port directory.  The directory layout is the
/// identity, and a manifest that disagrees with it fails rather than
/// publishing under a surprising name.
fn load_package(package_dir: &Path) -> Result<PortConversion> {
    let manifest_path = package_dir.join("cabin.toml");
    // Following a symlink would let published metadata depend on
    // checkout-local state.
    let marker = fs::symlink_metadata(&manifest_path)
        .with_context(|| format!("inspecting {}", manifest_path.display()))?;
    if !marker.is_file() {
        bail!(
            "{} is not a regular file; a migrated port's manifest must be committed \
             directly, not through a symlink",
            manifest_path.display()
        );
    }
    let manifest = fs::read_to_string(&manifest_path)
        .with_context(|| format!("reading {}", manifest_path.display()))?;
    let parsed = cabin_manifest::parse_manifest_str(&manifest)
        .with_context(|| format!("parsing {}", manifest_path.display()))?;
    let package = parsed
        .package
        .ok_or_else(|| anyhow!("{} has no [package] table", manifest_path.display()))?;

    let version_dir = dir_name(package_dir)?;
    let name_dir = match package_dir.parent() {
        Some(parent) => dir_name(parent)?,
        None => String::new(),
    };
    if package.name.scope() != Some(REGISTRY_SCOPE) {
        bail!(
            "{} declares `{}`; port packages must publish under the `{}` scope",
            manifest_path.display(),
            package.name.as_str(),
            REGISTRY_SCOPE
        );
    }
    let expected = scoped_package_name(&name_dir)
        .with_context(|| format!("package directory `{name_dir}/`"))?;
    if package.name != expected {
        bail!(
            "{} declares `{}`, which disagrees with its `{name_dir}/` directory",
            manifest_path.display(),
            package.name.as_str()
        );
    }
    if package.version.to_string() != version_dir {
        bail!(
            "{} declares version {}, which disagrees with its `{version_dir}/` directory",
            manifest_path.display(),
            package.version
        );
    }
    let Some(upstream) = package.upstream.clone() else {
        bail!(
            "{} declares no [package.upstream] provenance; port packages are materialized \
             from their pinned upstream archives",
            manifest_path.display()
        );
    };

    let (dependencies, declared_requirements) = registry_edges(&package, &manifest_path)?;

    // The probe enables the default features and no others, so it
    // links every target they satisfy - dropping one would let a target
    // that does not compile pass preflight - and none gated behind a
    // non-default feature, which would fail the probe's own build.
    let default_features = package
        .features
        .expand(&package.features.default.iter().cloned().collect());
    let library_like: Vec<&cabin_core::Target> = package
        .targets
        .iter()
        .filter(|target| target.kind.is_library_like())
        .collect();
    let library_like_target_keys: Vec<String> = library_like
        .iter()
        .filter(|target| {
            target
                .missing_required_features(&default_features)
                .is_empty()
        })
        .map(|target| target.name.as_str().to_owned())
        .collect();

    let probe_standards = probe_standards(&package, &library_like_target_keys).map_err(|why| {
        anyhow!(
            "{}: the probed targets' declared interface standards cannot be satisfied by one \
             consumer ({why}); the preflight probe builds one consumer per language for all of \
             them",
            manifest_path.display()
        )
    })?;

    Ok(PortConversion {
        committed_dir: package_dir.to_path_buf(),
        upstream: Box::new(upstream),
        probe_standards,
        scoped_name: package.name.clone(),
        published_version: package.version.clone(),
        manifest,
        dependencies,
        declared_requirements,
        sole_library_target: library_like.len() == 1,
        library_like_target_keys,
    })
}

/// The publication-order edges a published manifest declares, the
/// full set of registry requirements it carries, and the validation
/// every port package's dependency table must pass.
fn registry_edges(
    package: &cabin_core::Package,
    manifest_path: &Path,
) -> Result<(Vec<PortDependencyEdge>, Vec<DeclaredRequirement>)> {
    let activated_by_default = default_activated_optional_deps(package);

    let mut dependencies = Vec::new();
    let mut declared = Vec::new();
    for dep in &package.dependencies {
        if dep.condition.is_some() {
            bail!(
                "{} declares `{}` under a cfg-conditional table; publication ordering reads \
                 the declared edges without evaluating conditions, so port packages carry \
                 unconditional dependencies only",
                manifest_path.display(),
                dep.name.as_str()
            );
        }
        match &dep.source {
            DependencySource::Version(req) => {
                if dep.name.scope() != Some(REGISTRY_SCOPE) {
                    bail!(
                        "{} depends on `{}`; port packages may only depend on other `{}/*` \
                         packages",
                        manifest_path.display(),
                        dep.name.as_str(),
                        REGISTRY_SCOPE
                    );
                }
                // Only a resolved dependency orders publication: a
                // consumer's resolution of a registry package drops its
                // dev dependencies and the optional ones its default
                // features leave off, and so does the probe, so two
                // packages referencing each other only that way form no
                // cycle.
                //
                // Limitation: activation is read from this package's own
                // default closure, not from a `features = [..]` request
                // on an edge, which needs `cabin-feature`'s cross-package
                // activation.  No committed port makes one; if one does,
                // preflight fails on the missing package before anything
                // publishes.
                let resolved_by_default =
                    !dep.optional || activated_by_default.contains(dep.name.as_str());
                if dep.kind == cabin_core::DependencyKind::Normal && resolved_by_default {
                    dependencies.push(PortDependencyEdge {
                        scoped: dep.name.clone(),
                        req: req.clone(),
                    });
                }
                declared.push(DeclaredRequirement {
                    scoped: dep.name.clone(),
                    req: req.clone(),
                    // `cabin test` activates dev dependencies, which are
                    // never optional.
                    default_active: resolved_by_default,
                });
            }
            _ => bail!(
                "{} depends on `{}` through a non-registry source; port packages carry \
                 registry dependencies only",
                manifest_path.display(),
                dep.name.as_str()
            ),
        }
    }
    Ok((dependencies, declared))
}

/// Names of the optional dependencies a package's default feature
/// closure activates, through `dep:<name>` or `<dep>/<feature>` as
/// `cabin-feature`'s resolver does.  Manifest validation owns the
/// diagnosis of unparsable entries.
fn default_activated_optional_deps(package: &cabin_core::Package) -> BTreeSet<String> {
    let roots: BTreeSet<String> = package.features.default.iter().cloned().collect();
    let mut entries: Vec<&String> = package.features.default.iter().collect();
    for feature in package.features.expand(&roots) {
        if let Some(values) = package.features.features.get(&feature) {
            entries.extend(values.iter());
        }
    }

    let optional: BTreeSet<&str> = package
        .dependencies
        .iter()
        .filter(|dep| dep.optional)
        .map(|dep| dep.name.as_str())
        .collect();
    let mut activated = BTreeSet::new();
    for entry in entries {
        match cabin_core::FeatureEntry::parse(entry) {
            Ok(cabin_core::FeatureEntry::OptionalDep(name)) => {
                activated.insert(name);
            }
            Ok(cabin_core::FeatureEntry::DepFeature { dep, .. })
                if optional.contains(dep.as_str()) =>
            {
                activated.insert(dep);
            }
            _ => {}
        }
    }
    activated
}

/// The lowest standard a probe consumer can declare and still
/// satisfy every target it links, per language.  The requirements
/// are joined through `cabin_core::Requirement`, the build's own
/// intersection, so the publisher cannot drift from the rule the
/// build enforces.
fn probe_standards(
    package: &cabin_core::Package,
    probed: &[String],
) -> Result<ProbeStandards, &'static str> {
    let targets: Vec<&cabin_core::Target> = package
        .targets
        .iter()
        .filter(|t| probed.iter().any(|key| key == t.name.as_str()))
        .collect();

    // Effective-standard precedence: a target-level declaration wins,
    // and the package-level one applies where the target is silent.
    let declared = |target: &cabin_core::Target| {
        (
            target
                .language
                .interface_c_standard
                .or(package.language.interface_c_standard)
                .and_then(cabin_core::StandardDeclaration::value),
            target
                .language
                .interface_cxx_standard
                .or(package.language.interface_cxx_standard)
                .and_then(cabin_core::StandardDeclaration::value),
        )
    };

    // Splitting C-only targets from the rest keeps a package that mixes
    // both shapes probeable: one consumer of the whole set could
    // satisfy neither side.
    let (c_targets, cxx_targets): (Vec<_>, Vec<_>) = targets
        .into_iter()
        .partition(|t| matches!(declared(t).1, Some(cabin_core::InterfaceRequirement::None)));
    // A C++-consumable target that declares a C interface is linked by
    // the C consumer too, so its C claim is probed, not only stated.
    let dual_targets: Vec<_> = cxx_targets
        .iter()
        .copied()
        .filter(|t| {
            matches!(
                declared(t).0,
                Some(cabin_core::InterfaceRequirement::Requirement(_))
            )
        })
        .collect();

    // Each probe joins only the language it compiles, so a C++ library
    // declaring `interface-c-standard = "none"` is not refused for a C
    // requirement no probe exercises.
    //
    // Limitation: declared interfaces only.  A higher floor Cabin
    // composes across public target edges fails preflight with Cabin's
    // own compatibility error.
    Ok(ProbeStandards {
        c: join_interface(c_targets.iter().chain(&dual_targets).map(|t| declared(t).0))?,
        cxx: join_interface(cxx_targets.iter().map(|t| declared(t).1))?,
        c_targets: c_targets.iter().map(|t| t.name.to_string()).collect(),
        dual_targets: dual_targets.iter().map(|t| t.name.to_string()).collect(),
    })
}

/// Join declared interface requirements and return the lowest
/// satisfying consumer level, or `Err` when no level satisfies them
/// together.
fn join_interface<S: Copy + Ord + cabin_core::StandardLevel>(
    declared: impl Iterator<Item = Option<cabin_core::InterfaceRequirement<S>>>,
) -> Result<Option<S>, &'static str> {
    let mut any = false;
    let joined = cabin_core::Requirement::join_all(declared.flatten().map(|req| {
        any = true;
        match req {
            cabin_core::InterfaceRequirement::None => cabin_core::Requirement::Forbidden,
            cabin_core::InterfaceRequirement::Requirement(r) => {
                cabin_core::Requirement::from_declared(r)
            }
        }
    }));
    if !any {
        return Ok(None);
    }
    match joined {
        cabin_core::Requirement::Forbidden => Err("no consumer standard satisfies them together"),
        cabin_core::Requirement::Unconstrained => Ok(None),
        other => Ok(other.lower_bound()),
    }
}

fn dir_name(dir: &Path) -> Result<String> {
    dir.file_name()
        .and_then(|name| name.to_str())
        .map(str::to_owned)
        .ok_or_else(|| anyhow!("{} has no UTF-8 directory name", dir.display()))
}

/// Every declared requirement must be satisfiable by a committed
/// version, so an unsatisfiable edge fails with its real diagnosis
/// rather than as a phantom "dependency cycle".  This reads
/// `declared_requirements`, not the ordering edges: an inert
/// requirement still publishes, and an unsatisfiable one is broken
/// metadata.
fn ensure_requirements_satisfiable(conversions: &[PortConversion]) -> Result<()> {
    let mut published: BTreeMap<&str, Vec<&Version>> = BTreeMap::new();
    for conversion in conversions {
        published
            .entry(conversion.scoped_name.base_name())
            .or_default()
            .push(&conversion.published_version);
    }
    for conversion in conversions {
        // Cabin joins same-name requirements across `[dependencies]` and
        // `[dev-dependencies]` into one `VersionReq`, so those a default
        // invocation resolves together must be satisfiable by one
        // version: `^1` normal beside `^2` dev fails under `cabin test`.
        // An optional dependency the default features leave off is never
        // resolved beside them, so it is checked alone.
        let mut by_name: BTreeMap<&str, Vec<&DeclaredRequirement>> = BTreeMap::new();
        for dep in &conversion.declared_requirements {
            by_name.entry(dep.scoped.as_str()).or_default().push(dep);
        }
        for (name, deps) in by_name {
            let base = deps[0].scoped.base_name();
            let versions = published.get(base);
            let (active, inactive): (Vec<_>, Vec<_>) =
                deps.iter().partition(|dep| dep.default_active);
            let mut groups: Vec<Vec<&DeclaredRequirement>> = Vec::new();
            if !active.is_empty() {
                groups.push(active.into_iter().copied().collect());
            }
            groups.extend(inactive.into_iter().map(|dep| vec![*dep]));

            for group in groups {
                let satisfied = versions.is_some_and(|versions| {
                    versions
                        .iter()
                        .any(|v| group.iter().all(|dep| dep.req.matches(v)))
                });
                if !satisfied {
                    let reqs: Vec<String> = group.iter().map(|dep| dep.req.to_string()).collect();
                    bail!(
                        "{} depends on `{}` with requirement `{}`, which no committed port \
                         version satisfies",
                        conversion.committed_dir.display(),
                        name,
                        reqs.join(", ")
                    );
                }
            }
        }
    }
    Ok(())
}

/// Every `ports/<prefix>/<name>/<version>/` directory, `<prefix>` being
/// [`port_prefix`] of `<name>`.  Anything else at those depths fails, a
/// file included, so a misplaced port can never silently drop out of
/// publication, nor a copy under another prefix silently replace it.
fn discover_ports(ports_dir: &Path) -> Result<Vec<PathBuf>> {
    let mut packages = Vec::new();
    for prefix_dir in read_sorted_dirs(ports_dir)? {
        let prefix = dir_name(&prefix_dir)?;
        if port_prefix(&prefix) != prefix {
            bail!(
                "{} is not a prefix directory: a port lives in \
                 ports/<prefix>/<name>/<version>/, <prefix> being the first two characters of \
                 <name>",
                prefix_dir.display()
            );
        }
        for name_dir in read_sorted_dirs(&prefix_dir)? {
            let name = dir_name(&name_dir)?;
            if port_prefix(&name) != prefix {
                bail!(
                    "{} is misplaced: the port `{name}` belongs in the `{}/` prefix directory",
                    name_dir.display(),
                    port_prefix(&name)
                );
            }
            for version_dir in read_sorted_dirs(&name_dir)? {
                if !version_dir.join("cabin.toml").is_file() {
                    bail!("{} has no cabin.toml", version_dir.display());
                }
                packages.push(version_dir);
            }
        }
    }
    Ok(packages)
}

fn read_sorted_dirs(dir: &Path) -> Result<Vec<PathBuf>> {
    let mut dirs = Vec::new();
    for entry in fs::read_dir(dir).with_context(|| format!("reading {}", dir.display()))? {
        let path = entry
            .with_context(|| format!("reading {}", dir.display()))?
            .path();
        // A symlinked directory would source manifests and patch bytes
        // from outside the committed tree.
        let metadata = fs::symlink_metadata(&path)
            .with_context(|| format!("inspecting {}", path.display()))?;
        if metadata.file_type().is_symlink() && fs::metadata(&path).is_ok_and(|m| m.is_dir()) {
            bail!(
                "{} is a symlinked directory; ports must be committed as regular directories",
                path.display()
            );
        }
        if !metadata.is_dir() {
            bail!(
                "{} is not a directory; ports/ holds only <prefix>/<name>/<version>/ directories",
                path.display()
            );
        }
        dirs.push(path);
    }
    dirs.sort();
    Ok(dirs)
}

/// Kahn's algorithm over the scoped-name dependency edges.  Ready
/// nodes are drained in name order (versions ascending within a
/// name), so the publication order is deterministic.
fn order_by_dependencies(conversions: Vec<PortConversion>) -> Result<Vec<PortConversion>> {
    let mut remaining: BTreeMap<(String, Version), PortConversion> = conversions
        .into_iter()
        .map(|c| {
            (
                (
                    c.scoped_name.as_str().to_owned(),
                    c.published_version.clone(),
                ),
                c,
            )
        })
        .collect();
    let mut ordered = Vec::with_capacity(remaining.len());
    let mut published: BTreeMap<String, Vec<Version>> = BTreeMap::new();
    while !remaining.is_empty() {
        // Readiness is per requirement, not per name: a dependent may
        // require a version that publishes in a later rank.
        let ready: Vec<(String, Version)> = remaining
            .iter()
            .filter(|(_, c)| {
                c.dependencies.iter().all(|dep| {
                    published
                        .get(dep.scoped.as_str())
                        .is_some_and(|versions| versions.iter().any(|v| dep.req.matches(v)))
                })
            })
            .map(|(key, _)| key.clone())
            .collect();
        if ready.is_empty() {
            let stuck: Vec<&str> = remaining.keys().map(|(name, _)| name.as_str()).collect();
            bail!("inter-port dependency cycle among: {}", stuck.join(", "));
        }
        for key in ready {
            let conversion = remaining.remove(&key).expect("key came from the map");
            published
                .entry(key.0.clone())
                .or_default()
                .push(conversion.published_version.clone());
            ordered.push(conversion);
        }
    }
    Ok(ordered)
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    #[test]
    fn scoped_names_spell_the_directory_exactly() {
        assert_eq!(
            scoped_package_name("nlohmann_json").unwrap().as_str(),
            "cabin-ports/nlohmann_json"
        );
        assert!(scoped_package_name("cJSON").is_err());
        assert!(scoped_package_name("foo.bar").is_err());
    }

    /// One committed package directory under a temp ports tree, loaded
    /// through `load_conversions`.
    pub(crate) fn write_package(dir: &std::path::Path, name: &str, version: &str, manifest: &str) {
        let package_dir = dir.join(port_prefix(name)).join(name).join(version);
        std::fs::create_dir_all(&package_dir).unwrap();
        std::fs::write(package_dir.join("cabin.toml"), manifest).unwrap();
    }

    pub(crate) fn package_manifest(scoped: &str, version: &str, extra: &str) -> String {
        format!(
            "[package]\nname = \"{scoped}\"\nversion = \"{version}\"\n\n\
             [package.upstream]\nurl = \"https://ports.invalid/a.tar.gz\"\nchecksum = \"sha256:{}\"\n\
             format = \"tar.gz\"\n\n{extra}\
             [target.t]\ntype = \"library\"\nsources = [\"a.c\"]\ninclude-dirs = [\".\"]\n\
             c-standard = \"c11\"\n",
            "a".repeat(64)
        )
    }

    #[test]
    fn prefixes_are_the_first_two_characters() {
        assert_eq!(port_prefix("zlib"), "zl");
        assert_eq!(port_prefix("xz"), "xz");
        assert_eq!(port_prefix("z"), "z");
        assert_eq!(port_prefix("c-ares"), "c-");
        assert_eq!(port_prefix("7zip"), "7z");
    }

    /// Every port loads from its prefix directory, one-character names
    /// included, in name and version order.
    #[test]
    fn ports_load_from_prefix_name_version_directories() {
        let dir = assert_fs::TempDir::new().unwrap();
        for (name, version) in [
            ("zlib", "1.3.1"),
            ("zlib", "1.3.2"),
            ("zlib-ng", "2.2.4"),
            ("z", "1.0.0"),
            ("xz", "5.8.1"),
        ] {
            let manifest = package_manifest(&format!("cabin-ports/{name}"), version, "");
            write_package(dir.path(), name, version, &manifest);
        }
        assert!(dir.path().join("zl/zlib/1.3.1/cabin.toml").is_file());
        assert!(dir.path().join("z/z/1.0.0/cabin.toml").is_file());
        let loaded: Vec<String> = load_conversions(dir.path())
            .unwrap()
            .iter()
            .map(|c| format!("{} {}", c.scoped_name.as_str(), c.published_version))
            .collect();
        assert_eq!(
            loaded,
            [
                "cabin-ports/xz 5.8.1",
                "cabin-ports/z 1.0.0",
                "cabin-ports/zlib 1.3.1",
                "cabin-ports/zlib 1.3.2",
                "cabin-ports/zlib-ng 2.2.4"
            ]
        );
    }

    /// Covers the flat layout, a wrong prefix (which could otherwise hold
    /// a second copy of a name), a manifest above its version directory,
    /// and a version directory without a manifest.
    #[test]
    fn a_port_outside_its_prefix_directory_is_refused() {
        let manifest = package_manifest("cabin-ports/zlib", "1.3.1", "");
        for (place, error) in [
            ("zlib/1.3.1", "zlib is not a prefix directory"),
            ("zz/zlib/1.3.1", "belongs in the `zl/` prefix directory"),
            ("z/zlib/1.3.1", "belongs in the `zl/` prefix directory"),
            ("zl/1.3.1", "belongs in the `1./` prefix directory"),
            ("ne/newname", "cabin.toml is not a directory"),
            ("zl", "cabin.toml is not a directory"),
            ("", "ports/ holds only"),
        ] {
            let dir = assert_fs::TempDir::new().unwrap();
            write_package(dir.path(), "zlib", "1.3.1", &manifest);
            let package_dir = dir.path().join(place);
            std::fs::create_dir_all(&package_dir).unwrap();
            std::fs::write(package_dir.join("cabin.toml"), &manifest).unwrap();
            let err = format!("{:#}", load_conversions(dir.path()).unwrap_err());
            assert!(err.contains(error), "{place}: {err}");
        }

        let dir = assert_fs::TempDir::new().unwrap();
        write_package(dir.path(), "zlib", "1.3.1", &manifest);
        let patches = dir.path().join("zl/zlib/1.3.2/patches");
        std::fs::create_dir_all(&patches).unwrap();
        std::fs::write(patches.join("0001-a.patch"), "").unwrap();
        let err = format!("{:#}", load_conversions(dir.path()).unwrap_err());
        assert!(err.contains("1.3.2 has no cabin.toml"), "{err}");
    }

    #[test]
    fn a_package_whose_name_disagrees_with_its_directory_is_refused() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "fmt",
            "12.2.0",
            &package_manifest("cabin-ports/notfmt", "12.2.0", ""),
        );
        let err = load_conversions(dir.path()).unwrap_err();
        assert!(
            format!("{err:#}").contains("disagrees with its `fmt/` directory"),
            "{err:#}"
        );
    }

    #[test]
    fn a_package_whose_version_disagrees_with_its_directory_is_refused() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "fmt",
            "12.2.0",
            &package_manifest("cabin-ports/fmt", "12.2.1", ""),
        );
        let err = load_conversions(dir.path()).unwrap_err();
        assert!(
            format!("{err:#}").contains("disagrees with its `12.2.0/` directory"),
            "{err:#}"
        );
    }

    #[test]
    fn a_package_outside_the_ports_scope_is_refused() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "fmt",
            "12.2.0",
            &package_manifest("otherscope/fmt", "12.2.0", ""),
        );
        let err = load_conversions(dir.path()).unwrap_err();
        assert!(
            format!("{err:#}").contains("must publish under the `cabin-ports` scope"),
            "{err:#}"
        );
    }

    #[test]
    fn a_package_without_upstream_provenance_is_refused() {
        let dir = assert_fs::TempDir::new().unwrap();
        let manifest = "[package]\nname = \"cabin-ports/fmt\"\nversion = \"12.2.0\"\n\n\
                        [target.t]\ntype = \"library\"\nsources = [\"a.c\"]\ninclude-dirs = \
                        [\".\"]\nc-standard = \"c11\"\n"
            .to_owned();
        write_package(dir.path(), "fmt", "12.2.0", &manifest);
        let err = load_conversions(dir.path()).unwrap_err();
        assert!(
            format!("{err:#}").contains("declares no [package.upstream]"),
            "{err:#}"
        );
    }

    /// Each version publishes verbatim and is probed on its own targets.
    #[test]
    fn migrated_versions_may_expose_different_targets() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "fmt",
            "12.2.0",
            &package_manifest("cabin-ports/fmt", "12.2.0", ""),
        );
        let manifest =
            package_manifest("cabin-ports/fmt", "12.3.0", "").replace("[target.t]", "[target.u]");
        write_package(dir.path(), "fmt", "12.3.0", &manifest);
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(conversions.len(), 2);
        let mut keys: Vec<&str> = conversions
            .iter()
            .flat_map(|c| c.library_like_target_keys.iter().map(String::as_str))
            .collect();
        keys.sort_unstable();
        assert_eq!(keys, ["t", "u"]);
    }

    #[test]
    fn orders_dependencies_before_dependents() {
        let dir = assert_fs::TempDir::new().unwrap();
        // `apng` sorts before `zlib` but depends on it, so the order
        // must invert the name sort.
        write_package(
            dir.path(),
            "apng",
            "1.0.0",
            &package_manifest(
                "cabin-ports/apng",
                "1.0.0",
                "[dependencies]\n\"cabin-ports/zlib\" = \"^1.3\"\n\n",
            ),
        );
        write_package(
            dir.path(),
            "zlib",
            "1.3.1",
            &package_manifest("cabin-ports/zlib", "1.3.1", ""),
        );
        let conversions = load_conversions(dir.path()).unwrap();
        let names: Vec<&str> = conversions.iter().map(|c| c.scoped_name.as_str()).collect();
        assert_eq!(names, ["cabin-ports/zlib", "cabin-ports/apng"]);
        assert_eq!(
            conversions[1]
                .dependencies
                .iter()
                .map(|dep| (dep.scoped.as_str(), dep.req.to_string()))
                .collect::<Vec<_>>(),
            [("cabin-ports/zlib", "^1.3".to_owned())]
        );
    }

    #[test]
    fn detects_dependency_cycles() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "aport",
            "1.0.0",
            &package_manifest(
                "cabin-ports/aport",
                "1.0.0",
                "[dependencies]\n\"cabin-ports/bport\" = \"^1\"\n\n",
            ),
        );
        write_package(
            dir.path(),
            "bport",
            "1.0.0",
            &package_manifest(
                "cabin-ports/bport",
                "1.0.0",
                "[dependencies]\n\"cabin-ports/aport\" = \"^1\"\n\n",
            ),
        );
        let err = load_conversions(dir.path()).unwrap_err();
        assert!(format!("{err:#}").contains("cycle"), "{err:#}");
    }

    /// `apng` needs zlib `^2`, so publishing zlib 1.3.1 first does not
    /// unblock it.
    #[test]
    fn dependents_wait_for_a_satisfying_version_not_just_the_name() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "apng",
            "1.0.0",
            &package_manifest(
                "cabin-ports/apng",
                "1.0.0",
                "[dependencies]\n\"cabin-ports/zlib\" = \"^2\"\n\n",
            ),
        );
        write_package(
            dir.path(),
            "zlib",
            "1.3.1",
            &package_manifest("cabin-ports/zlib", "1.3.1", ""),
        );
        write_package(
            dir.path(),
            "zlib",
            "2.0.0",
            &package_manifest(
                "cabin-ports/zlib",
                "2.0.0",
                "[dependencies]\n\"cabin-ports/xxhash\" = \"^0.8\"\n\n",
            ),
        );
        write_package(
            dir.path(),
            "xxhash",
            "0.8.3",
            &package_manifest("cabin-ports/xxhash", "0.8.3", ""),
        );
        let conversions = load_conversions(dir.path()).unwrap();
        let order: Vec<String> = conversions
            .iter()
            .map(|c| format!("{} {}", c.scoped_name.as_str(), c.published_version))
            .collect();
        let position = |needle: &str| {
            order
                .iter()
                .position(|entry| entry == needle)
                .unwrap_or_else(|| panic!("{needle} missing from {order:?}"))
        };
        assert!(position("cabin-ports/zlib 2.0.0") < position("cabin-ports/apng 1.0.0"));
        assert!(position("cabin-ports/xxhash 0.8.3") < position("cabin-ports/zlib 2.0.0"));
        assert!(position("cabin-ports/zlib 1.3.1") < position("cabin-ports/zlib 2.0.0"));
    }

    /// Two packages optionally depending on each other, with neither
    /// dependency activated by default, are valid, not a cycle.
    #[test]
    fn optional_dependencies_do_not_order_publication() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "alpha",
            "1.0.0",
            &package_manifest(
                "cabin-ports/alpha",
                "1.0.0",
                "[dependencies]\n\"cabin-ports/beta\" = { version = \"1.0.0\", optional = \
                 true }\n\n",
            ),
        );
        write_package(
            dir.path(),
            "beta",
            "1.0.0",
            &package_manifest(
                "cabin-ports/beta",
                "1.0.0",
                "[dependencies]\n\"cabin-ports/alpha\" = { version = \"1.0.0\", optional = \
                 true }\n\n",
            ),
        );
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(conversions.len(), 2);
        assert!(
            conversions.iter().all(|c| c.dependencies.is_empty()),
            "optional edges must not order publication"
        );
    }

    #[test]
    fn default_activated_optional_dependencies_order_publication() {
        for extra in [
            "[features]\ndefault = [\"ssl\"]\nssl = [\"dep:cabin-ports/beta\"]\n\n\
             [dependencies]\n\"cabin-ports/beta\" = { version = \"1.0.0\", optional = true }\n\n",
            // Transitive: default -> outer -> ssl -> dep:beta.
            "[features]\ndefault = [\"outer\"]\nouter = [\"ssl\"]\nssl = \
             [\"dep:cabin-ports/beta\"]\n\n[dependencies]\n\"cabin-ports/beta\" = \
             { version = \"1.0.0\", optional = true }\n\n",
            // The `<dep>/<feature>` form activates an optional dep too.
            "[features]\ndefault = [\"ssl\"]\nssl = [\"cabin-ports/beta/fast\"]\n\n\
             [dependencies]\n\"cabin-ports/beta\" = { version = \"1.0.0\", optional = true }\n\n",
        ] {
            let dir = assert_fs::TempDir::new().unwrap();
            write_package(
                dir.path(),
                "alpha",
                "1.0.0",
                &package_manifest("cabin-ports/alpha", "1.0.0", extra),
            );
            write_package(
                dir.path(),
                "beta",
                "1.0.0",
                &package_manifest("cabin-ports/beta", "1.0.0", ""),
            );
            let conversions = load_conversions(dir.path()).unwrap();
            let names: Vec<&str> = conversions.iter().map(|c| c.scoped_name.as_str()).collect();
            assert_eq!(names, ["cabin-ports/beta", "cabin-ports/alpha"], "{extra}");
            let alpha = conversions.last().unwrap();
            assert_eq!(alpha.dependencies.len(), 1, "{extra}");
        }
    }

    #[test]
    fn dev_dependencies_do_not_order_publication() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "alpha",
            "1.0.0",
            &package_manifest(
                "cabin-ports/alpha",
                "1.0.0",
                "[dev-dependencies]\n\"cabin-ports/beta\" = \"1.0.0\"\n\n",
            ),
        );
        write_package(
            dir.path(),
            "beta",
            "1.0.0",
            &package_manifest(
                "cabin-ports/beta",
                "1.0.0",
                "[dev-dependencies]\n\"cabin-ports/alpha\" = \"1.0.0\"\n\n",
            ),
        );
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(conversions.len(), 2);
        assert!(
            conversions.iter().all(|c| c.dependencies.is_empty()),
            "dev edges must not order publication"
        );
    }

    #[test]
    fn a_package_with_a_conditional_dependency_is_refused() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "alpha",
            "1.0.0",
            &package_manifest(
                "cabin-ports/alpha",
                "1.0.0",
                "[target.'cfg(os = \"linux\")'.dependencies]\n\
                 \"cabin-ports/beta\" = \"1.0.0\"\n\n",
            ),
        );
        let err = load_conversions(dir.path()).unwrap_err();
        assert!(
            format!("{err:#}").contains("cfg-conditional table"),
            "{err:#}"
        );
    }

    #[test]
    fn default_enabled_feature_gated_targets_stay_in_the_probe_link_set() {
        let dir = assert_fs::TempDir::new().unwrap();
        let manifest = package_manifest(
            "cabin-ports/fmt",
            "12.2.0",
            "[features]\ndefault = [\"ssl\"]\nssl = []\n\n",
        )
        .replace(
            "c-standard = \"c11\"\n",
            "c-standard = \"c11\"\nrequired-features = [\"ssl\"]\n",
        );
        write_package(dir.path(), "fmt", "12.2.0", &manifest);
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(conversions[0].library_like_target_keys, ["t"]);
    }

    #[test]
    fn feature_gated_targets_stay_out_of_the_probe_link_set() {
        let dir = assert_fs::TempDir::new().unwrap();
        let manifest = package_manifest("cabin-ports/fmt", "12.2.0", "[features]\nextra = []\n\n")
            .replace(
                "c-standard = \"c11\"\n",
                "c-standard = \"c11\"\nrequired-features = [\"extra\"]\n",
            );
        write_package(dir.path(), "fmt", "12.2.0", &manifest);
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(conversions.len(), 1);
        assert!(
            conversions[0].library_like_target_keys.is_empty(),
            "{:?}",
            conversions[0].library_like_target_keys
        );
    }

    #[test]
    fn a_gated_sibling_target_forbids_the_bare_shorthand() {
        let dir = assert_fs::TempDir::new().unwrap();
        let manifest = format!(
            "{}\n[target.gated]\ntype = \"library\"\nsources = [\"b.c\"]\n\
             include-dirs = [\".\"]\nc-standard = \"c11\"\nrequired-features = [\"extra\"]\n",
            package_manifest("cabin-ports/fmt", "12.2.0", "[features]\nextra = []\n\n")
        );
        write_package(dir.path(), "fmt", "12.2.0", &manifest);
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(conversions[0].library_like_target_keys, ["t"]);
        assert!(!conversions[0].sole_library_target);
    }

    #[test]
    fn probe_standards_rise_to_the_declared_interface_floor() {
        let dir = assert_fs::TempDir::new().unwrap();
        let manifest = package_manifest("cabin-ports/fmt", "12.2.0", "").replace(
            "c-standard = \"c11\"\n",
            "c-standard = \"c11\"\ninterface-cxx-standard = \"c++20\"\n",
        );
        write_package(dir.path(), "fmt", "12.2.0", &manifest);
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(
            conversions[0].probe_standards.cxx,
            Some(cabin_core::CxxStandard::Cxx20)
        );
    }

    #[test]
    fn package_level_interface_standards_reach_the_probe() {
        let dir = assert_fs::TempDir::new().unwrap();
        let manifest = package_manifest("cabin-ports/fmt", "12.2.0", "").replace(
            "version = \"12.2.0\"\n",
            "version = \"12.2.0\"\ninterface-cxx-standard = \"c++20\"\n",
        );
        write_package(dir.path(), "fmt", "12.2.0", &manifest);
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(
            conversions[0].probe_standards.cxx,
            Some(cabin_core::CxxStandard::Cxx20)
        );
    }

    #[test]
    fn c_only_interfaces_are_probed_from_c() {
        let dir = assert_fs::TempDir::new().unwrap();
        let manifest = package_manifest("cabin-ports/zlib", "1.3.1", "").replace(
            "c-standard = \"c11\"\n",
            "c-standard = \"c11\"\ninterface-cxx-standard = \"none\"\n\
             interface-c-standard = \"c99\"\n",
        );
        write_package(dir.path(), "zlib", "1.3.1", &manifest);
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(conversions[0].probe_standards.c_targets, ["t"]);
        assert_eq!(
            conversions[0].probe_standards.c,
            Some(cabin_core::CStandard::C99)
        );
        assert_eq!(conversions[0].probe_standards.cxx, None);
    }

    #[test]
    fn mixed_language_targets_are_split_across_probes() {
        let dir = assert_fs::TempDir::new().unwrap();
        let manifest = format!(
            "{}\n[target.u]\ntype = \"library\"\nsources = [\"b.cc\"]\n\
             include-dirs = [\".\"]\ncxx-standard = \"c++17\"\n\
             interface-cxx-standard = \"c++20\"\n",
            package_manifest("cabin-ports/mixed", "1.0.0", "").replace(
                "c-standard = \"c11\"\n",
                "c-standard = \"c11\"\ninterface-cxx-standard = \"none\"\n\
                 interface-c-standard = \"c99\"\n",
            )
        );
        write_package(dir.path(), "mixed", "1.0.0", &manifest);
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(conversions[0].probe_standards.c_targets, ["t"]);
        assert_eq!(
            conversions[0].probe_standards.c,
            Some(cabin_core::CStandard::C99)
        );
        assert_eq!(
            conversions[0].probe_standards.cxx,
            Some(cabin_core::CxxStandard::Cxx20)
        );
    }

    #[test]
    fn a_cxx_library_forbidding_c_consumption_is_accepted() {
        let dir = assert_fs::TempDir::new().unwrap();
        let manifest = package_manifest("cabin-ports/fmt", "12.2.0", "").replace(
            "c-standard = \"c11\"\n",
            "c-standard = \"c11\"\ninterface-c-standard = \"none\"\n\
             interface-cxx-standard = \"c++14\"\n",
        );
        write_package(dir.path(), "fmt", "12.2.0", &manifest);
        let conversions = load_conversions(dir.path()).unwrap();
        assert!(conversions[0].probe_standards.c_targets.is_empty());
        assert_eq!(conversions[0].probe_standards.c, None);
        assert_eq!(
            conversions[0].probe_standards.cxx,
            Some(cabin_core::CxxStandard::Cxx14)
        );
    }

    /// A C library consumable from C++ that declares its C interface is
    /// probed from both languages at the declared floors, whether or not
    /// it also declares its C++ interface.
    #[test]
    fn a_declared_c_interface_is_probed_from_c_as_well() {
        for (cxx_interface, cxx) in [
            (
                "interface-cxx-standard = \"c++11\"\n",
                Some(cabin_core::CxxStandard::Cxx11),
            ),
            ("", None),
        ] {
            let dir = assert_fs::TempDir::new().unwrap();
            let manifest = package_manifest("cabin-ports/lz4", "1.11.0", "").replace(
                "c-standard = \"c11\"\n",
                &format!("c-standard = \"c11\"\ninterface-c-standard = \"c99\"\n{cxx_interface}"),
            );
            write_package(dir.path(), "lz4", "1.11.0", &manifest);
            let conversions = load_conversions(dir.path()).unwrap();
            let standards = &conversions[0].probe_standards;
            assert!(standards.c_targets.is_empty());
            assert_eq!(standards.dual_targets, ["t"]);
            assert_eq!(standards.c, Some(cabin_core::CStandard::C99));
            assert_eq!(standards.cxx, cxx);
        }
    }

    #[test]
    fn probe_standards_default_when_no_interface_is_declared() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "fmt",
            "12.2.0",
            &package_manifest("cabin-ports/fmt", "12.2.0", ""),
        );
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(conversions[0].probe_standards, ProbeStandards::default());
    }

    #[test]
    fn a_package_with_a_non_registry_dependency_is_refused() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "fmt",
            "12.2.0",
            &package_manifest(
                "cabin-ports/fmt",
                "12.2.0",
                "[dependencies]\nzlib = { path = \"../zlib\" }\n\n",
            ),
        );
        let err = load_conversions(dir.path()).unwrap_err();
        assert!(
            format!("{err:#}").contains("non-registry source"),
            "{err:#}"
        );
    }

    /// A mixed-case directory is refused even when its manifest declares
    /// the lowercase name, so no two directories can differ only in case
    /// and publish as one package.
    #[test]
    fn a_mixed_case_package_directory_is_refused() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "CJSON",
            "1.0.0",
            &package_manifest("cabin-ports/cjson", "1.0.0", ""),
        );
        let err = format!("{:#}", load_conversions(dir.path()).unwrap_err());
        assert!(err.contains("package directory `CJSON/`"), "{err}");
        assert!(err.contains("did you mean \"cabin-ports/cjson\"?"), "{err}");
    }

    #[test]
    fn a_package_with_a_registry_invalid_name_is_refused() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "foo.bar",
            "1.0.0",
            &package_manifest("cabin-ports/foo.bar", "1.0.0", ""),
        );
        let err = load_conversions(dir.path()).unwrap_err();
        assert!(
            format!("{err:#}").contains("\"cabin-ports/foo.bar\" is not valid"),
            "{err:#}"
        );
    }

    #[cfg(unix)]
    #[test]
    fn a_symlinked_migrated_manifest_is_refused() {
        let dir = assert_fs::TempDir::new().unwrap();
        std::fs::write(
            dir.path().join("outside.toml"),
            package_manifest("cabin-ports/fmt", "12.2.0", ""),
        )
        .unwrap();
        let package_dir = dir.path().join("ports/fm/fmt/12.2.0");
        std::fs::create_dir_all(&package_dir).unwrap();
        std::os::unix::fs::symlink(
            dir.path().join("outside.toml"),
            package_dir.join("cabin.toml"),
        )
        .unwrap();
        let err = load_conversions(&dir.path().join("ports")).unwrap_err();
        assert!(format!("{err:#}").contains("not a regular file"), "{err:#}");
    }

    #[cfg(unix)]
    #[test]
    fn a_symlinked_port_directory_is_refused() {
        let dir = assert_fs::TempDir::new().unwrap();
        let outside = dir.path().join("outside/fmt");
        std::fs::create_dir_all(outside.join("12.2.0")).unwrap();
        std::fs::write(
            outside.join("12.2.0/cabin.toml"),
            package_manifest("cabin-ports/fmt", "12.2.0", ""),
        )
        .unwrap();
        let ports = dir.path().join("ports");
        std::fs::create_dir_all(ports.join("fm")).unwrap();
        std::os::unix::fs::symlink(&outside, ports.join("fm/fmt")).unwrap();
        let err = load_conversions(&ports).unwrap_err();
        assert!(
            format!("{err:#}").contains("symlinked directory"),
            "{err:#}"
        );

        // A symlinked prefix directory too.
        std::fs::remove_dir_all(ports.join("fm")).unwrap();
        std::fs::create_dir_all(dir.path().join("prefix")).unwrap();
        std::fs::rename(&outside, dir.path().join("prefix/fmt")).unwrap();
        std::os::unix::fs::symlink(dir.path().join("prefix"), ports.join("fm")).unwrap();
        let err = load_conversions(&ports).unwrap_err();
        assert!(
            format!("{err:#}").contains("symlinked directory"),
            "{err:#}"
        );
    }

    #[test]
    fn an_unsatisfiable_dev_dependency_is_diagnosed() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "fmt",
            "12.2.0",
            &package_manifest(
                "cabin-ports/fmt",
                "12.2.0",
                "[dev-dependencies]\n\"cabin-ports/zlib\" = \"^2\"\n\n",
            ),
        );
        write_package(
            dir.path(),
            "zlib",
            "1.3.1",
            &package_manifest("cabin-ports/zlib", "1.3.1", ""),
        );
        let err = load_conversions(dir.path()).unwrap_err();
        let message = format!("{err:#}");
        assert!(
            message.contains("which no committed port version satisfies"),
            "{message}"
        );
    }

    #[test]
    fn an_unsatisfiable_optional_dependency_is_diagnosed() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "fmt",
            "12.2.0",
            &package_manifest(
                "cabin-ports/fmt",
                "12.2.0",
                "[dependencies]\n\"cabin-ports/zlib\" = { version = \"^2\", optional = true }\n\n",
            ),
        );
        write_package(
            dir.path(),
            "zlib",
            "1.3.1",
            &package_manifest("cabin-ports/zlib", "1.3.1", ""),
        );
        let err = load_conversions(dir.path()).unwrap_err();
        let message = format!("{err:#}");
        assert!(
            message.contains("which no committed port version satisfies"),
            "{message}"
        );
    }

    #[test]
    fn disjoint_requirements_across_kinds_are_diagnosed() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "fmt",
            "12.2.0",
            &package_manifest(
                "cabin-ports/fmt",
                "12.2.0",
                "[dependencies]\n\"cabin-ports/zlib\" = \"^1\"\n\n\
                 [dev-dependencies]\n\"cabin-ports/zlib\" = \"^2\"\n\n",
            ),
        );
        // BOTH majors are committed, so each requirement is
        // independently satisfiable - only the conjunction is not.
        write_package(
            dir.path(),
            "zlib",
            "1.3.1",
            &package_manifest("cabin-ports/zlib", "1.3.1", ""),
        );
        write_package(
            dir.path(),
            "zlib",
            "2.0.0",
            &package_manifest("cabin-ports/zlib", "2.0.0", ""),
        );
        let err = load_conversions(dir.path()).unwrap_err();
        let message = format!("{err:#}");
        assert!(
            message.contains("which no committed port version satisfies"),
            "{message}"
        );
    }

    #[test]
    fn an_inactive_optional_is_not_conjoined_with_the_dev_requirement() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "fmt",
            "12.2.0",
            &package_manifest(
                "cabin-ports/fmt",
                "12.2.0",
                "[dependencies]\n\"cabin-ports/zlib\" = { version = \"^1\", optional = true }\n\n\
                 [dev-dependencies]\n\"cabin-ports/zlib\" = \"^2\"\n\n",
            ),
        );
        write_package(
            dir.path(),
            "zlib",
            "1.3.1",
            &package_manifest("cabin-ports/zlib", "1.3.1", ""),
        );
        write_package(
            dir.path(),
            "zlib",
            "2.0.0",
            &package_manifest("cabin-ports/zlib", "2.0.0", ""),
        );
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(conversions.len(), 3);
    }

    #[test]
    fn compatible_requirements_across_kinds_are_accepted() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "fmt",
            "12.2.0",
            &package_manifest(
                "cabin-ports/fmt",
                "12.2.0",
                "[dependencies]\n\"cabin-ports/zlib\" = \"^1\"\n\n\
                 [dev-dependencies]\n\"cabin-ports/zlib\" = \"^1.3\"\n\n",
            ),
        );
        write_package(
            dir.path(),
            "zlib",
            "1.3.1",
            &package_manifest("cabin-ports/zlib", "1.3.1", ""),
        );
        let conversions = load_conversions(dir.path()).unwrap();
        assert_eq!(conversions.len(), 2);
    }

    #[test]
    fn an_unsatisfiable_package_dependency_is_diagnosed_as_such() {
        let dir = assert_fs::TempDir::new().unwrap();
        write_package(
            dir.path(),
            "fmt",
            "12.2.0",
            &package_manifest(
                "cabin-ports/fmt",
                "12.2.0",
                "[dependencies]\n\"cabin-ports/zlib\" = \"^2\"\n\n",
            ),
        );
        write_package(
            dir.path(),
            "zlib",
            "1.3.1",
            &package_manifest("cabin-ports/zlib", "1.3.1", ""),
        );
        let err = load_conversions(dir.path()).unwrap_err();
        // The real diagnosis, never Kahn's phantom "cycle".
        let message = format!("{err:#}");
        assert!(
            message.contains("which no committed port version satisfies"),
            "{message}"
        );
        assert!(!message.contains("cycle"), "{message}");
    }
}
