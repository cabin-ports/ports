//! Repository tool that publishes the curated foundation ports
//! (`ports/`) as ordinary registry packages under
//! the `cabin-ports` scope.  Every committed port is a package
//! directory, published verbatim apart from its consumer `tests/`.
//!
//! Every mode runs the local preflight (materialize ports and publish
//! them into a temporary file registry in publication order, building
//! every port against the generated packages as soon as it is staged):
//!
//! - `--dry-run` stops after the preflight, which a [`select::Selection`]
//!   may narrow to the ports a change needs;
//! - `--publish` additionally uploads every package through the
//!   existing remote registry client, in consecutive batches of one
//!   `cabin publish` invocation each, relying on the registry's
//!   byte-identical idempotency instead of skipping versions (the
//!   public index hides pending versions, so it cannot be used to
//!   decide what is already published), and respinning changed bytes
//!   as packaging revisions;
//! - `--publish-new` uploads only the versions a receipts log records
//!   no upload of, never as a packaging revision, and `--plan-new`
//!   previews it ([`batch`]).
//!
//! The committed tree is an input only: every port materializes into
//! a scratch directory, never in place.

use std::path::PathBuf;

use anyhow::{Context, Result, bail};

pub mod batch;
pub mod plan;
pub mod preflight;
pub mod remote;
pub mod select;

/// What to do after the local preflight.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Mode {
    /// Stop after the local preflight of the selected ports.
    DryRun(select::Selection),
    /// Preflight, then upload every package to the registry whose
    /// sparse index lives at this URL.
    Publish {
        /// Registry index URL (the publish API origin comes from its
        /// `config.json`).
        index_url: String,
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
        Mode::Publish { .. } => conversions,
        Mode::New(new) => return batch::run(new, &conversions, &holds, options),
    };

    let report = preflight::preflight(&preflight::PreflightRequest {
        conversions: &conversions,
        cache_dir: &options.cache_dir,
        work_dir: &options.work_dir,
        cabin: &options.cabin,
        // Probes run upstream code, so they never run beside the
        // publish job's OIDC token; the PR dry-run runs them.
        run_probes: matches!(options.mode, Mode::DryRun(_)),
    })?;

    match &options.mode {
        Mode::DryRun(_) => {
            println!(
                "dry run complete: {} of {total} packages staged in {} and built against it",
                conversions.len(),
                report.registry_dir.display()
            );
        }
        Mode::Publish { index_url } => {
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
