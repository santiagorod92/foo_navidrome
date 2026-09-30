# WTL (Windows Template Library) — vendored

Header-only library required by the foobar2000 SDK helper/shared projects
(`<atlapp.h>`, `<atlctrls.h>`, …). It ships in no Visual Studio image, so it is
vendored here rather than downloaded on every CI run.

- **Version:** WTL 10.01 Release (2026-03-01)
- **Source:** https://sourceforge.net/projects/wtl/files/WTL%2010/WTL%2010.01%20Release/WTL10_01_Release.zip/download
- **Contents:** only `Include/` (the headers) plus `MS-PL.txt` (license) and
  `ReadMe.html`. The AppWizard/ and Samples/ trees from the upstream zip are
  omitted.
- **License:** Microsoft Public License (MS-PL), see `MS-PL.txt`.

`Include/` is added to the build's include path by the `Directory.Build.targets`
that `.github/workflows/build-windows.yml` writes at the build root. To upgrade,
download a newer release, replace `Include/`, and update the version above.
