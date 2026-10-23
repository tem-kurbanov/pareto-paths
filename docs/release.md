# Release notes

The author has selected the **MIT License** and authorized public publication as `tem-kurbanov/pareto-paths`.

The software license is in the root `LICENSE` file. The README and `CITATION.cff` credit the associated paper and all its authors. Publisher PDFs and external datasets are excluded; their separate licensing terms are unaffected.

## Publishing

The intended public repository is [tem-kurbanov/pareto-paths](https://github.com/tem-kurbanov/pareto-paths). Check the repository and its Actions page to confirm live publication and CI status; this document does not itself certify a successful upload or workflow run.

When GitHub CLI access is available, the initial publication command is:

```sh
gh repo create tem-kurbanov/pareto-paths --public --source . --remote origin --push \
  --description 'Multicriteria routing with k-path-cover acceleration. C++20 research library and CLI.'
```

If the repository already exists, verify ownership and its contents before adding it as `origin` and pushing. Do not overwrite unrelated remote work or force-push.

Before a versioned release, run the documented clean build and tests, inspect the committed file list for accidental data, credentials, PDFs or binaries, and confirm GitHub Actions succeeds. Suggested repository topics: `cpp20`, `multiobjective-optimization`, `shortest-path`, `graph-algorithms`, and `research-software`.
