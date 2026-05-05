# Contributing to OpenNasalance

OpenNasalance is a research-grade nasometer being developed in the open. We
welcome bug reports, fixes, hardware-bring-up patches, algorithm improvements, and clinical-protocol contributions from researchers, clinicians, and hobbyists.

This document covers:

1. Ground rules
2. How to file an issue
3. How to submit a pull request
4. The CLA — what it is, why we need it, and how to sign
5. Coding style and review expectations

---

## 1. Ground rules

- Be civil. We work with academic collaborators and clinicians; assume the
  best of other contributors.
- Don't submit code you don't have the right to submit (no copy-pasted code
  from non-permissive sources, no proprietary code from a previous employer
  unless they've cleared it).
- Patient/subject data of any kind must never be checked in, even
  pseudonymised. Recordings used for development should be synthesised or
  collected from informed consenting adults under an appropriate protocol.

## 2. Filing issues

- One issue per bug or feature.
- Include: hardware variant (e.g. `ESP_ES3N28P`), ESP-IDF version, the
  flag combination from `main/include/config.h`, and (where relevant) a
  serial-monitor capture.
- For algorithm questions, cite the paper or protocol section you're
  comparing against.

## 3. Pull requests

1. Fork the repository and create a feature branch.
2. Keep PRs focused — one logical change per PR. If a refactor is needed to
   land a fix, prefer a separate prep PR.
3. Match existing code style (see §5).
4. Update `README.md`, `main/include/config.h` comments, or
   `docs/research/` if your change affects user-visible behaviour or
   methodology.
5. **Sign off and sign the CLA** (see §4). PRs without a signed CLA will be
   held in review until the CLA is in place.

Every commit in a PR must be signed off using the
[Developer Certificate of Origin](https://developercertificate.org/) line:

```
Signed-off-by: Your Name <you@example.com>
```

(`git commit -s` does this automatically.) The DCO sign-off is in addition to
— not a substitute for — the CLA.

## 4. The Contributor License Agreement (CLA)

### Why we ask for a CLA

OpenNasalance is licensed under **GPL-3.0-or-later** (see
[`LICENSE.md`](LICENSE.md)). The CLA is a separate agreement in which You
grant Atomic Style, LLC the right to redistribute Your contribution under
terms that include the right to offer commercial dual-licenses to
organisations whose policies are incompatible with the GPL (typically
proprietary embedded medical-device manufacturers).

In exchange, the CLA binds Atomic Style, LLC to keep the Project available
under GPL-3.0-or-later (or another FSF/OSI-approved license) in perpetuity.
This is the same model used by Qt, MariaDB, and historically MongoDB. You
keep the copyright on your work; you simply grant a broad license back.

### Which CLA applies to you

- **Individuals** (contributing on your own behalf, no employer claim on
  your code): sign [`CLA-INDIVIDUAL.md`](CLA-INDIVIDUAL.md).
- **Companies, universities, research labs** (where your employer has rights
  in code you produce): an authorised signatory should sign
  [`CLA-ENTITY.md`](CLA-ENTITY.md), listing the employees authorised to
  contribute on the entity's behalf in Schedule A. Each named employee may
  then submit PRs without signing an Individual CLA separately.
- **You're not sure**: if your employment contract has an IP-assignment
  clause, you almost certainly need the Entity CLA (or written permission
  from your employer to contribute under the Individual CLA).

### How to sign — recommended mechanism

Atomic Style, LLC plans to use [**CLA Assistant**](https://cla-assistant.io/) (a free GitHub App run by SAP) to collect signatures. Once enabled on the repository:

1. Open a pull request as normal.
2. CLA Assistant comments on the PR with a link.
3. Click the link and sign in with GitHub. The text of the relevant CLA
   (Individual or Entity) is presented; click "I agree".
4. CLA Assistant records the signature against your GitHub identity. You
   only sign once across all PRs to this Project.

If CLA Assistant is not yet enabled when you open your PR, please instead:

1. Read [`CLA-INDIVIDUAL.md`](CLA-INDIVIDUAL.md) or
   [`CLA-ENTITY.md`](CLA-ENTITY.md).
2. Email a signed PDF copy to **scott@atomic.style** with subject
   `OpenNasalance CLA — <your name / entity>`. A digital signature
   (DocuSign, HelloSign, or a typed name accompanied by your verified
   work-email signature block) is acceptable.
3. Reference the CLA acceptance in your PR description (e.g.
   "CLA signed via email on 2026-05-05").

Alternative collection mechanisms we've considered, in case the CLA
Assistant model proves friction-heavy:

- **EasyCLA** (Linux Foundation): heavier-weight, intended for foundations
  with multiple projects.
- **GitHub Action `contributor-assistant/github-action`**: self-hosted
  alternative to CLA Assistant, signatures stored in a repo file.

### What if I won't sign a CLA?

We understand that some contributors — particularly those at institutions
with strict IP policies — cannot sign CLAs. Two paths remain open:

- File an issue describing the change in detail. We may be able to
  reimplement the idea independently.
- Submit a small, clearly-marked patch (e.g. a one-line bug fix or a typo)
  and we will treat it as covered by the inbound=outbound principle of
  GPL-3.0-or-later under section 5(c) of the GPL. Anything beyond a trivial
  patch will be held until a CLA is on file.

## 5. Coding style and review expectations

- C, loose C99 conformity. ESP-IDF 6.0.1 toolchain.
- 2-space indentation, no tabs (see `.clang-format`).
- Component-private symbols prefixed with the component shortname
  (`a_mic_*`, `atomic_lcd_*`, etc.).
- Public headers under `components/<name>/include/`; private helpers under
  `src/`.
- New source files **must** carry the standard copyright header — see any
  existing `.c` for the template (8-line GPL notice citing
  `SPDX-License-Identifier: GPL-3.0-or-later`).
- Reviewers will look for: correct error propagation (`ESP_RETURN_ON_ERROR`
  patterns), no blocking calls inside ISRs, allocation strategy matching
  the rest of the file, and a clear reason for any new build-time flag.

Thank you for contributing.
