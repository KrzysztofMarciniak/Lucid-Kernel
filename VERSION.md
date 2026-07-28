# Versioning Policy

Lucid Kernel follows Semantic Versioning (SemVer) with development stage suffixes.

Version format:

```
MAJOR.MINOR.PATCH[-STAGE]
```

Example:

```
0.0.1-pre
1.2.3
2.0.0-rc1
```

---

## Version Components

### MAJOR

Increment when introducing breaking changes to the kernel ABI, system interfaces, or architecture.

Examples:

- incompatible driver API
- incompatible syscall interface
- major redesign

---

### MINOR

Increment when adding new functionality while maintaining compatibility.

Examples:

- new filesystem
- new scheduler
- networking support
- package manager improvements

---

### PATCH

Increment for backwards-compatible bug fixes.

Examples:

- memory leak fixes
- scheduler bug fixes
- security patches
- compiler warning cleanup

---

## Development Stages

### pre

Very early development.

Characteristics:

- unstable
- incomplete
- internal use only
- interfaces may change without notice

Example:

```
0.0.1-pre
```

---

### alpha

Major functionality is under active development.

Characteristics:

- many missing features
- unstable APIs
- intended primarily for developers

Examples:

```
0.2.0-alpha1
0.2.0-alpha2
```

---

### beta

Feature complete for the targeted release.

Characteristics:

- public testing
- bug fixing
- performance improvements
- no major architectural changes

Examples:

```
0.9.0-beta1
0.9.0-beta2
```

---

### rc (Release Candidate)

Potential final release.

Characteristics:

- only critical bugs should remain
- no new features
- release-ready if no blockers are found

Examples:

```
1.0.0-rc1
1.0.0-rc2
```

---

### Stable

No suffix.

Examples:

```
1.0.0
1.0.1
1.1.0
2.0.0
```

Stable releases are intended for general use.

