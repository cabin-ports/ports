//! Repository tool that publishes the curated foundation ports
//! (`ports/`) as registry packages under the `cabin-ports` scope, each
//! published verbatim apart from its consumer `tests/` and materialized
//! into a scratch directory, never in place.
//!
//! Every mode but `--list-new` runs the local [`preflight`]:
//!
//! - `--dry-run` stops there, optionally narrowed by a
//!   [`select::Selection`];
//! - `--publish` uploads every package ([`remote`]), changed bytes as
//!   packaging revisions, and nothing unless the checked-out commit is
//!   `main`'s, when given;
//! - `--publish-new` uploads only the versions the receipts log records
//!   no upload of, never as a packaging revision ([`batch`]).
//!
//! An upload builds nothing.  `--plan` and `--plan-new` build what
//! `--publish` and `--publish-new` would upload, and record it for the
//! upload to check its own staging against ([`prepared`]).

use std::path::PathBuf;

use anyhow::{Context, Result, bail};

pub mod batch;
pub mod plan;
pub mod preflight;
pub mod prepared;
pub mod remote;
pub mod select;

/// What to do after the local preflight.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Mode {
    /// Stop after the local preflight of the selected ports.
    DryRun(select::Selection),
    /// Stage and build every port, as `Publish` would upload them.
    Plan,
    /// Preflight, then upload every package to the registry whose
    /// sparse index lives at this URL.
    Publish {
        /// Registry index URL (the publish API origin comes from its
        /// `config.json`).
        index_url: String,
        /// `main`'s current commit: unless it is the checked-out one,
        /// upload nothing.
        main: Option<String>,
    },
    /// Plan, or upload, the first publication of new versions only.
    New(batch::New),
}

/// Resolved tool invocation.
#[derive(Debug)]
pub struct Options {
    pub mode: Mode,
    /// `ports/` directory to publish.
    pub ports_dir: PathBuf,
    /// Cabin cache root (upstream archives are reused from and
    /// cached into `<cache>/ports`).
    pub cache_dir: PathBuf,
    /// Scratch root for the staged packages, the temporary registry,
    /// and preflight build outputs.
    pub work_dir: PathBuf,
    /// `cabin` binary for the preflight builds and the uploads.
    pub cabin: PathBuf,
    /// Receipts log each upload appends to.
    pub receipts_out: Option<PathBuf>,
    /// What a plan staged and built: a plan writes it, and an upload
    /// requires it.
    pub prepared: Option<prepared::PreparedFile>,
}

/// Run the tool.
///
/// # Errors
/// Returns the first conversion, preflight, or upload failure.
pub fn run(options: &Options) -> Result<()> {
    if !options.ports_dir.is_dir() {
        bail!(
            "ports directory {} does not exist; pass --ports-dir",
            options.ports_dir.display()
        );
    }
    if !options.cabin.is_file() {
        bail!(
            "cabin binary not found at {}; install the pinned Cabin revision (CONTRIBUTING.md) and \
             pass it as --cabin",
            options.cabin.display()
        );
    }
    std::fs::create_dir_all(&options.work_dir)
        .with_context(|| format!("creating {}", options.work_dir.display()))?;

    let conversions = plan::load_conversions(&options.ports_dir)?;
    println!(
        "loaded {} ports; publication order: {}",
        conversions.len(),
        conversions
            .iter()
            .map(|c| format!("{} {}", c.scoped_name.as_str(), c.published_version))
            .collect::<Vec<_>>()
            .join(", ")
    );

    // Cheap and catalog-wide whatever a dry-run selects; every mode
    // validates the hold list, so none merges that --publish-new refuses.
    preflight::ensure_consumer_tests_used(&conversions)?;
    let holds = batch::load_holds(&options.ports_dir, &conversions)?;
    let total = conversions.len();
    let conversions = match &options.mode {
        Mode::DryRun(selection) => select::select(conversions, selection, &options.ports_dir)?,
        Mode::Plan => conversions,
        Mode::Publish { main, .. } => {
            if let Some(main) = main {
                let (head, main) = batch::head_and_main(&options.ports_dir, main)?;
                if head != main {
                    println!(
                        "::warning::superseded by main's {main}: published nothing; dispatch \
                         again on main"
                    );
                    return Ok(());
                }
            }
            conversions
        }
        Mode::New(new) => return batch::run(new, &conversions, &holds, options),
    };

    let report = preflight::preflight(&preflight::PreflightRequest {
        conversions: &conversions,
        cache_dir: &options.cache_dir,
        work_dir: &options.work_dir,
        cabin: &options.cabin,
        // Building a probe can run a port's build script and running
        // one runs upstream code, so neither happens beside the
        // publish job's OIDC token; the PR dry-run runs them.
        probes: match options.mode {
            Mode::DryRun(_) => preflight::Probes::Run,
            Mode::Plan => preflight::Probes::Build,
            Mode::Publish { .. } | Mode::New(_) => preflight::Probes::Skip,
        },
    })?;
    let uploads: Vec<remote::Upload<'_>> = conversions
        .iter()
        .zip(report.package_dirs.iter().zip(&report.checksums))
        .map(|(conversion, (package_dir, checksum))| remote::Upload {
            package_dir,
            name: conversion.scoped_name.as_str(),
            version: conversion.published_version.to_string(),
            checksum,
        })
        .collect();

    match &options.mode {
        Mode::DryRun(_) => {
            println!(
                "dry run complete: {} of {total} packages staged in {} and built against it",
                conversions.len(),
                report.registry_dir.display()
            );
        }
        Mode::Plan => {
            prepared::write(options, &uploads)?;
            println!("plan complete: {} packages to upload", uploads.len());
        }
        Mode::Publish { index_url, .. } => {
            prepared::require(options, &uploads)?;
            remote::publish_all(
                &uploads,
                index_url,
                &options.cabin,
                true,
                options.receipts_out.as_deref(),
            )?;
            println!("published {} packages", conversions.len());
        }
        Mode::New(_) => unreachable!("returned above"),
    }
    Ok(())
}
