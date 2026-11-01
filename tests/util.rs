use fingolfin::util::{
    artifact::write_atomic,
    budget::Budget,
    text::{LineIndex, Position},
};
use std::{
    fs,
    sync::atomic::{AtomicU64, Ordering},
};
static NEXT: AtomicU64 = AtomicU64::new(0);
fn directory() -> std::path::PathBuf {
    let p = std::env::temp_dir().join(format!(
        "fg-util-{}-{}",
        std::process::id(),
        NEXT.fetch_add(1, Ordering::Relaxed)
    ));
    fs::create_dir(&p).unwrap();
    p
}
#[test]
fn budget_handles_exact_limits_and_overflow_without_replenishing() {
    let mut budget = Budget::new(u64::MAX);
    budget.charge(u64::MAX - 1).unwrap();
    assert_eq!(budget.charge(2).unwrap_err().remaining, 1);
    budget.charge(1).unwrap();
    assert_eq!(budget.consumed(), u64::MAX);
    assert!(budget.charge(1).is_err());
    budget.charge(0).unwrap();
    assert_eq!(budget.remaining(), 0);
}
#[test]
fn unicode_positions_crlf_eof_and_invalid_offsets() {
    let index = LineIndex::new("中a\r\nβ\n");
    assert_eq!(index.position(3), Some(Position { line: 1, column: 2 }));
    assert_eq!(index.position(6), Some(Position { line: 2, column: 1 }));
    assert_eq!(index.position(9), Some(Position { line: 3, column: 1 }));
    for offset in [1, 2, 7, 10, usize::MAX] {
        assert_eq!(index.position(offset), None);
    }
    assert_eq!(index.line(1), Some("中a"));
    assert_eq!(index.line(2), Some("β"));
    assert_eq!(index.line(3), Some(""));
    assert_eq!(index.line(0), None);
    assert_eq!(index.line(4), None);
    assert_eq!(
        LineIndex::new("").position(0),
        Some(Position { line: 1, column: 1 })
    );
}
#[test]
fn artifact_failure_preserves_destination_and_cleans_temporary_files() {
    let root = directory();
    let destination = root.join("existing-directory");
    fs::create_dir(&destination).unwrap();
    fs::write(destination.join("keep"), b"original").unwrap();
    assert!(write_atomic(&destination, b"replacement").is_err());
    assert_eq!(fs::read(destination.join("keep")).unwrap(), b"original");
    assert_eq!(fs::read_dir(&root).unwrap().count(), 1);
    fs::remove_dir_all(root).unwrap();
}
#[test]
fn concurrent_artifact_writers_publish_whole_files() {
    let root = directory();
    let destination = root.join("result");
    write_atomic(&destination, &[0; 32768]).unwrap();
    std::thread::scope(|scope| {
        for byte in 1..=8u8 {
            let destination = &destination;
            scope.spawn(move || {
                for _ in 0..4 {
                    write_atomic(destination, &vec![byte; 32768]).unwrap();
                }
            });
        }
        for _ in 0..100 {
            let bytes = fs::read(&destination).unwrap();
            assert_eq!(bytes.len(), 32768);
            assert!(bytes.iter().all(|b| *b == bytes[0]));
        }
    });
    assert_eq!(fs::read_dir(&root).unwrap().count(), 1);
    fs::remove_dir_all(root).unwrap();
}
#[test]
fn artifact_replaces_symlink_without_modifying_target() {
    let root = directory();
    let target = root.join("target");
    let link = root.join("link");
    fs::write(&target, b"keep").unwrap();
    std::os::unix::fs::symlink(&target, &link).unwrap();
    write_atomic(&link, b"new").unwrap();
    assert_eq!(fs::read(&target).unwrap(), b"keep");
    assert_eq!(fs::read(&link).unwrap(), b"new");
    assert!(
        !fs::symlink_metadata(&link)
            .unwrap()
            .file_type()
            .is_symlink()
    );
    fs::remove_dir_all(root).unwrap();
}

#[test]
fn content_hashes_have_stable_framing_and_known_digest() {
    use fingolfin::util::hash::{ContentHash, sha256};
    assert_eq!(
        sha256(b"abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
    let mut left = ContentHash::new();
    left.field(b"a");
    left.field(b"bc");
    let mut right = ContentHash::new();
    right.field(b"ab");
    right.field(b"c");
    assert_ne!(left.finish(), right.finish());
    let mut hash = ContentHash::new();
    hash.field(b"abc");
    let mut encoded = 3u64.to_le_bytes().to_vec();
    encoded.extend_from_slice(b"abc");
    assert_eq!(hash.finish(), sha256(&encoded));
}
