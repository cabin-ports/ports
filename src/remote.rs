//! Remote publication through the existing `cabin publish` flow.
//!
//! The preflighted packages upload in publication order, in batches of
//! `BATCH_SIZE`, each one `cabin -Z remote-registry publish` invocation
//! with a `--manifest-path` per package, so every upload runs exactly the
//! code path an ordinary publish runs.  This tool adds only the ordering,
//! the batching, and the receipts log (`crate::batch`).
//!
//! Batching keeps a large set from riding one credential: under GitHub
//! Actions each invocation exchanges the run's OIDC token for a
//! trusted-publishing token that expires 30 minutes later.  A failed
//! batch stops the run before the next one, after logging a `failed`
//! receipt for the package it stopped at.
//!
//! `--publish` never skips a version based on the public index, which
//! hides pending versions: identical bytes answer as a no-op, which is
//! also how a failed run resumes.  Rate-limit pacing lives in the client.

use std::fs::File;
use std::io::{BufRead as _, BufReader, Write as _};
use std::path::Path;
use std::process::{Command, Stdio};

use anyhow::{Context, Result, bail};
use cabin_core::Checksum;
use serde::Deserialize;

use crate::batch::{Outcome, Receipt};

/// Packages per `cabin publish` invocation.  The workflow's token draws
/// on the operator class's publish bucket (burst 100, refill 60 a
/// minute), so even a drained bucket paces a batch for under a minute,
/// leaving the rest of the token's 30 minutes to the uploads; and 200
/// ports still need only four exchanges.
const BATCH_SIZE: usize = 50;

/// One preflighted package to upload.
#[derive(Debug)]
pub struct Upload<'a> {
    /// Its scratch package directory.
    pub package_dir: &'a Path,
    /// Scoped package name.
    pub name: &'a str,
    pub version: String,
    /// The archive checksum the preflight staged from `package_dir`.
    pub checksum: &'a str,
}

/// The fields of the client's `--format json` report this tool reads.
#[derive(Debug, Deserialize)]
struct Report {
    name: String,
    version: String,
    checksum: Checksum,
    no_op: bool,
    revision: Option<String>,
    verification: Option<String>,
    #[serde(default)]
    warnings: Vec<String>,
}

/// Upload every package in the given (publication) order, one `cabin
/// publish` invocation per batch, appending each receipt to
/// `receipts` when given.
///
/// `new_revision` passes `--new-revision`: `--publish` treats the
/// committed ports as the source of truth, so a change that reaches it
/// is the deliberate intent to respin the published version (identical
/// bytes still no-op), while `--publish-new` only ever adds versions.
///
/// # Errors
/// Returns an error naming the first batch whose `cabin publish`
/// invocation cannot start or exits non-zero (later batches never run),
/// an unwritable receipts log, or an upload whose checksum is not the
/// one the preflight staged.
pub fn publish_all(
    uploads: &[Upload<'_>],
    index_url: &str,
    cabin: &Path,
    new_revision: bool,
    receipts: Option<&Path>,
) -> Result<()> {
    let mut log = receipts
        .map(|path| {
            File::options()
                .create(true)
                .append(true)
                .open(path)
                .with_context(|| format!("opening the receipts log {}", path.display()))
        })
        .transpose()?;
    publish_in_batches(uploads, |batch| {
        // Stderr is inherited on purpose: the client's progress,
        // rate-limit pacing warnings, and - under GitHub Actions - its
        // `::add-mask::` workflow commands must reach the real output
        // streams unaltered.  Stdout carries the JSON reports.
        let mut child = publish_command(cabin, batch, index_url, new_revision)
            .stdout(Stdio::piped())
            .spawn()
            .with_context(|| format!("running {} publish", cabin.display()))?;
        let stdout = child.stdout.take().context("capturing cabin's report")?;
        let reports = read_reports(stdout, batch, index_url, log.as_mut());
        if reports.is_err() {
            // Stop the uploads this run could no longer record.
            let _ = child.kill();
        }
        let status = child.wait().context("waiting for cabin publish")?;
        let (received, diverged) = reports?;
        let missing = batch.iter().zip(&received).find(|(_, got)| !**got);
        if !status.success() {
            // The client uploads in order and stops at the first
            // failure, so the first package without a report is where
            // it stopped (or, when staging or the token exchange
            // failed, the batch's first package).
            if let Some((upload, _)) = missing {
                let staged = Checksum::parse(upload.checksum)?;
                record(
                    log.as_mut(),
                    index_url,
                    upload,
                    Outcome::Failed,
                    &staged,
                    serde_json::Map::new(),
                )?;
                bail!(
                    "`cabin publish` failed ({status}) at {} {}",
                    upload.name,
                    upload.version
                );
            }
            bail!("`cabin publish` failed ({status})");
        }
        if let Some((upload, _)) = missing {
            bail!(
                "`cabin publish` reported no receipt for {} {}",
                upload.name,
                upload.version
            );
        }
        if let Some((at, uploaded)) = diverged {
            bail!(
                "{} {} uploaded as {uploaded}, not the {} its preflight staged",
                batch[at].name,
                batch[at].version,
                batch[at].checksum
            );
        }
        Ok(())
    })
}

/// A batch index and the checksum it was uploaded as.
type Mismatch = (usize, Checksum);

/// Which of `batch` the client reported, appending each receipt, and
/// the first upload whose checksum is not the staged one.
fn read_reports(
    stdout: impl std::io::Read,
    batch: &[Upload<'_>],
    index_url: &str,
    mut log: Option<&mut File>,
) -> Result<(Vec<bool>, Option<Mismatch>)> {
    let mut received = vec![false; batch.len()];
    let mut diverged = None;
    for line in BufReader::new(stdout).lines().map_while(Result::ok) {
        let parsed = serde_json::from_str::<Report>(&line)
            .ok()
            .and_then(|report| {
                let at = batch
                    .iter()
                    .position(|u| u.name == report.name && u.version == report.version)?;
                Some((at, report))
            });
        let Some((at, report)) = parsed else {
            println!("{line}");
            continue;
        };
        received[at] = true;
        let mut details = serde_json::Map::new();
        details.insert("revision".into(), report.revision.clone().into());
        details.insert("no_op".into(), report.no_op.into());
        details.insert("verification".into(), report.verification.clone().into());
        record(
            log.as_deref_mut(),
            index_url,
            &batch[at],
            Outcome::Uploaded,
            &report.checksum,
            details,
        )?;
        println!(
            "{} {} {} (revision {}, verification: {})",
            report.name,
            report.version,
            if report.no_op {
                "was already published with identical bytes"
            } else {
                "published"
            },
            report.revision.as_deref().unwrap_or("unreported"),
            report.verification.as_deref().unwrap_or("unreported"),
        );
        for warning in &report.warnings {
            eprintln!("warning: {warning}");
        }
        if report.checksum.as_str() != batch[at].checksum {
            diverged.get_or_insert((at, report.checksum));
        }
    }
    Ok((received, diverged))
}

/// Append one receipt line; the log is flushed line by line, so a run
/// that dies later keeps every receipt before it.
fn record(
    log: Option<&mut File>,
    registry: &str,
    upload: &Upload<'_>,
    outcome: Outcome,
    checksum: &Checksum,
    details: serde_json::Map<String, serde_json::Value>,
) -> Result<()> {
    let Some(log) = log else { return Ok(()) };
    let receipt = Receipt {
        registry: registry.to_owned(),
        name: upload.name.to_owned(),
        version: upload.version.clone(),
        outcome,
        checksum: checksum.clone(),
        details,
    };
    writeln!(log, "{}", serde_json::to_string(&receipt)?)
        .and_then(|()| log.flush())
        .context("appending to the receipts log")
}

fn publish_in_batches<T>(uploads: &[T], mut publish: impl FnMut(&[T]) -> Result<()>) -> Result<()> {
    let batches = uploads.len().div_ceil(BATCH_SIZE);
    for (index, batch) in uploads.chunks(BATCH_SIZE).enumerate() {
        let number = index + 1;
        println!(
            "publishing batch {number}/{batches} ({} packages)",
            batch.len()
        );
        publish(batch).with_context(|| {
            format!(
                "publishing batch {number}/{batches} failed; a rerun resumes it (published \
                 packages answer as byte-identical no-ops)"
            )
        })?;
    }
    Ok(())
}

fn publish_command(
    cabin: &Path,
    batch: &[Upload<'_>],
    index_url: &str,
    new_revision: bool,
) -> Command {
    let mut cmd = Command::new(cabin);
    cmd.arg("-Z").arg("remote-registry").arg("publish");
    if new_revision {
        cmd.arg("--new-revision");
    }
    // Reruns after a partial publish hit the same drained publish
    // bucket, and every attempt charges it - so even a one-port tree
    // waits out 429s instead of failing the run.
    cmd.arg("--retry-rate-limits").arg("--format").arg("json");
    for upload in batch {
        cmd.arg("--manifest-path")
            .arg(upload.package_dir.join("cabin.toml"));
    }
    cmd.arg("--index-url").arg(index_url);
    cmd
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The batches a run makes, as the items each one carried.
    fn batches(count: usize) -> Vec<Vec<usize>> {
        let items: Vec<usize> = (0..count).collect();
        let mut seen = Vec::new();
        publish_in_batches(&items, |batch| {
            seen.push(batch.to_vec());
            Ok(())
        })
        .unwrap();
        seen
    }

    #[test]
    fn batches_cover_every_package_once_in_publication_order() {
        // Zero packages must run nothing: `cabin publish` without a
        // `--manifest-path` would publish the current directory.
        for (count, sizes) in [
            (0, vec![]),
            (1, vec![1]),
            (BATCH_SIZE, vec![BATCH_SIZE]),
            (BATCH_SIZE + 1, vec![BATCH_SIZE, 1]),
            (2 * BATCH_SIZE + 7, vec![BATCH_SIZE, BATCH_SIZE, 7]),
        ] {
            let seen = batches(count);
            assert_eq!(seen.iter().map(Vec::len).collect::<Vec<_>>(), sizes);
            assert_eq!(
                seen.concat(),
                (0..count).collect::<Vec<_>>(),
                "{count} packages"
            );
        }
    }

    #[test]
    fn a_failed_batch_stops_the_run() {
        let items: Vec<usize> = (0..=2 * BATCH_SIZE).collect();
        let mut attempted = Vec::new();
        let err = publish_in_batches(&items, |batch| {
            attempted.push(batch[0]);
            if attempted.len() == 2 {
                bail!("`cabin publish` failed (exit status: 1)");
            }
            Ok(())
        })
        .unwrap_err();
        assert_eq!(attempted, [0, BATCH_SIZE]);
        let message = format!("{err:#}");
        assert!(message.contains("batch 2/3 failed"), "{message}");
        assert!(message.contains("exit status: 1"), "{message}");
    }

    /// Only `--publish` opts into packaging revisions.
    #[test]
    fn a_batch_is_one_publish_invocation_over_exactly_its_packages() {
        let upload = |dir| Upload {
            package_dir: Path::new(dir),
            name: "cabin-ports/a",
            version: "1.0.0".into(),
            checksum: "",
        };
        let batch = [upload("a"), upload("b")];
        let args = |new_revision| {
            let cmd = publish_command(
                Path::new("cabin"),
                &batch,
                "https://registry.example",
                new_revision,
            );
            cmd.get_args()
                .map(|arg| arg.to_str().unwrap().to_owned())
                .collect::<Vec<_>>()
        };
        let a = Path::new("a").join("cabin.toml");
        let b = Path::new("b").join("cabin.toml");
        let tail = [
            "--retry-rate-limits",
            "--format",
            "json",
            "--manifest-path",
            a.to_str().unwrap(),
            "--manifest-path",
            b.to_str().unwrap(),
            "--index-url",
            "https://registry.example",
        ];
        let head = ["-Z", "remote-registry", "publish"];
        assert_eq!(args(true), [&head[..], &["--new-revision"], &tail].concat());
        assert_eq!(args(false), [&head[..], &tail].concat());
    }
}
