//! The record a plan leaves of what it staged and built, and the check
//! an upload makes against it (docs/publishing.md, "Execution
//! boundaries").
//!
//! A build can run a port's build script, so the job holding the OIDC
//! token builds nothing.  A job without the token builds first and
//! writes this record; the upload job stages the same commit again and
//! uploads a version only when the record of its own run lists it with
//! the archive and metadata it has just staged.  The record comes from
//! a job that ran port code, so nothing in it is trusted: a wrong one
//! can only stop the upload, never change a byte of it.

use std::collections::BTreeMap;
use std::fs;
use std::path::PathBuf;

use anyhow::{Context, Result, bail};
use cabin_core::Checksum;
use serde::{Deserialize, Serialize};

use crate::remote::Upload;
use crate::{Options, batch};

/// `--prepared` and the workflow run it belongs to.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PreparedFile {
    pub path: PathBuf,
    /// `--run-id`.
    pub run: String,
}

#[derive(Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Prepared {
    /// The ports commit that was staged.
    commit: String,
    run: String,
    /// `<name> <version>` to what was staged of it.
    packages: BTreeMap<String, Staged>,
}

#[derive(Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Staged {
    /// Checksum of the archive (`sha256:<hex>`).
    archive: String,
    /// Checksum of the canonical metadata document.
    metadata: String,
}

/// What this process staged of `uploads`, from the checked-out commit.
fn staged(file: &PreparedFile, options: &Options, uploads: &[Upload<'_>]) -> Result<Prepared> {
    // Inside the directory the preflight clears.
    let scratch = options.work_dir.join("run").join("prepared");
    let mut packages = BTreeMap::new();
    for upload in uploads {
        let label = format!("{} {}", upload.name, upload.version);
        // Staged a second time: the only way the pinned Cabin hands
        // out the metadata document.
        let report = cabin_publish::dry_run(cabin_publish::DryRunRequest {
            manifest_path: &upload.package_dir.join("cabin.toml"),
            output_dir: &scratch.join(packages.len().to_string()),
            resolved_project: None,
            workspace_dep_requirements: cabin_core::WorkspaceDepRequirements::default(),
        })
        .with_context(|| format!("staging {label}"))?;
        let metadata = fs::File::open(&report.metadata_path)
            .and_then(Checksum::of_reader)
            .with_context(|| format!("reading {}", report.metadata_path.display()))?;
        packages.insert(
            label,
            Staged {
                archive: upload.checksum.to_owned(),
                metadata: metadata.to_string(),
            },
        );
    }
    Ok(Prepared {
        commit: batch::head(&options.ports_dir)?,
        run: file.run.clone(),
        packages,
    })
}

/// Record what a plan staged and built of `uploads`, when `--prepared`
/// asks for it.
///
/// # Errors
/// A package that no longer stages, an unresolvable commit, or an
/// unwritable file.
pub fn write(options: &Options, uploads: &[Upload<'_>]) -> Result<()> {
    let Some(file) = &options.prepared else {
        return Ok(());
    };
    let prepared = staged(file, options, uploads)?;
    let mut text = serde_json::to_string_pretty(&prepared)?;
    text.push('\n');
    fs::write(&file.path, text).with_context(|| format!("writing {}", file.path.display()))
}

/// Refuse to upload unless the `--prepared` record is this run's, of
/// the checked-out commit, and lists every one of `uploads` with
/// exactly the archive and metadata this process staged.
///
/// # Errors
/// No record or an unreadable one, another commit's or run's, or a
/// version it lacks or lists with other bytes.
pub fn require(options: &Options, uploads: &[Upload<'_>]) -> Result<()> {
    let file = options
        .prepared
        .as_ref()
        .context("an upload needs --prepared, the record of what a plan built")?;
    let path = file.path.display();
    let text = fs::read_to_string(&file.path).with_context(|| format!("reading {path}"))?;
    let prepared: Prepared =
        serde_json::from_str(&text).with_context(|| format!("reading {path}"))?;
    let staged = staged(file, options, uploads)?;
    if prepared.commit != staged.commit {
        bail!(
            "{path} was built from commit {}, not the checked-out {}; uploaded nothing",
            prepared.commit,
            staged.commit
        );
    }
    if prepared.run != staged.run {
        bail!(
            "{path} was built by run {}, not this run {}; uploaded nothing",
            prepared.run,
            staged.run
        );
    }
    for (label, staged) in &staged.packages {
        match prepared.packages.get(label) {
            None => bail!("{path} does not list {label}, so it was not built; uploaded nothing"),
            Some(built) if built != staged => bail!(
                "{label} stages archive {} and metadata {}, but {path} built archive {} and \
                 metadata {}; uploaded nothing",
                staged.archive,
                staged.metadata,
                built.archive,
                built.metadata
            ),
            Some(_) => {}
        }
    }
    Ok(())
}
