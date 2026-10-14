# App Store catalog

The App Store reads its list of programs from a JSON file,
`userland/store/catalog.json`.  The list can change without a new NovaOS
build: an installed NovaOS downloads the current file from GitHub when the
user refreshes the App Store.

## Changing the catalog

1. Edit `userland/store/catalog.json` in a pull request.
2. Run the host tests (below) before you push.
3. Once the change is on `main`, users get it with **Refresh** in the
   App Store, F5, or `store refresh` in the Terminal.

The ISO also carries the catalog as it was at build time, as a fallback.

## How NovaOS loads it

| File on drive C: | What it is |
|---|---|
| `C:\Windows\AppStore\catalog.json` | The last catalog downloaded by a refresh.  Read first. |
| `C:\Windows\AppStore\default-catalog.json` | The catalog built into the image.  Used when `catalog.json` is missing or invalid. |
| `C:\Windows\AppStore\catalog-url.txt` | Optional.  One HTTPS URL to refresh from instead of the default. |

The default refresh URL is
`https://raw.githubusercontent.com/dean-plude/os/main/userland/store/catalog.json`.

A refresh downloads the catalog over HTTPS (following up to eight
redirects), parses and validates the whole document, saves it to
`catalog.json` through a temporary file and a rename, and only then
replaces the list on screen and clears each row's status message.  If
anything fails (no network, a non-HTTPS URL, a download error, invalid
JSON, an unsupported version, an unsafe path, a write error) the App
Store keeps the list it has and says why.

- Downloaded and installed programs are found from their files, as
  before.  A program removed from the catalog disappears from the App
  Store, but its downloaded and installed files stay on disk.
- A refresh waits for a download or unpack in progress to finish, and new
  actions wait for the refresh.
- To update a machine without a network, copy a valid catalog to
  `C:\Windows\AppStore\catalog.json` and reopen the App Store.
- Delete `catalog-url.txt` to go back to the default URL.

Only use a catalog source you trust: its URLs and install instructions
decide what the App Store downloads and runs.  The download is checked by
NovaOS's normal HTTPS certificate validation; the catalog itself is not
signed.

The App Store's **Updates** page updates NovaOS itself
([updates.md](updates.md)); that has nothing to do with refreshing the
catalog.

## Format

```json
{
  "schema_version": 1,
  "apps": [
    {
      "name": "Example Editor",
      "publisher": "Example Project",
      "summary": "A portable text editor",
      "category": "developer",
      "url": "https://example.org/releases/1.0/editor.zip",
      "file": "editor-1.0.zip",
      "dest": "Example Editor",
      "exe": "Example Editor\\editor.exe",
      "kind": "archive",
      "size_mb": 12,
      "note": "Compatibility testing pending",
      "label": "Ed",
      "color": "#3671A6"
    }
  ]
}
```

| Field | Required | Meaning |
|---|---|---|
| `schema_version` | yes | The integer `1`. |
| `apps` | yes | Array of at most 256 programs.  An empty array is valid. |
| `name` | yes | Display name, at most 80 bytes of UTF-8.  Unique (case-insensitive). |
| `publisher` | yes | At most 100 bytes. |
| `summary` | yes | One line, at most 320 bytes. |
| `category` | yes | `utilities`, `internet`, `media`, `graphics`, `office`, `developer` or `runtimes`. |
| `url` | yes | `https://` download URL, at most 500 bytes, no spaces. |
| `file` | yes | File name the download is saved as in `C:\Downloads`, at most 180 bytes.  One path component, unique (case-insensitive). |
| `kind` | yes | `setup`: the download is an `.exe` installer or `.msi` package that **Install** runs.  `portable`: the download is the program itself.  `archive`: a `.zip`, `.7z`, `.tar.gz`, `.tar.zst` or 7-Zip self-extractor that 7-Zip (installed from the App Store) unpacks. |
| `dest` | for `archive` | The folder under `C:\Programs` to unpack into: one path component.  Optional or `null` otherwise. |
| `exe` | no | The installed program, relative to `C:\Programs`, or starting with `\` for a path from the root of C:.  A `**` component matches any folders (for archives with a versioned top folder).  **Open** appears when this file exists. |
| `size_mb` | yes | Approximate download size, an integer from 0 to 4096.  Downloads are still limited by what the network stack allows. |
| `note` | yes | Compatibility note shown with the program, at most 512 bytes. |
| `label` | yes | Tile label, at most 4 bytes. |
| `color` | yes | Tile colour, `#RRGGBB`. |
| `system` | no | `archive` only: a space-separated list of files in the archive to install into the system folders instead of `dest` (files from an `x86` or `x32` folder go to `SysWOW64`, others to `System32`).  `path>name` renames a file, as in `x64\file.dll>renamed.dll`.  Vulkan driver manifests among them are registered. |

Other rules:

- The whole document is at most 512 KiB.
- Display strings must not be empty or contain control characters.
- Paths may not contain `..` or characters that could break a quoted
  command line.
- Unknown fields are ignored.  A change in what a required field means
  needs a new `schema_version` and matching NovaOS support.

## Tests

Run the host tests before publishing a change:

```bash
python3 -m unittest discover -s tests/tools -p test_store_catalog.py -v
python3 -m unittest discover -s tests/tools -p test_corpus_store_catalog.py -v
```

`test_store_catalog.py` compiles NovaOS's own catalog parser
(`kernel/apps/store_catalog.h`) with the host C compiler and runs it on
the repository's catalog, so a catalog NovaOS would refuse fails the
test.  It also tests refresh and validation edge cases.  Real downloads
and installs are tested by the boot-test suites and the nightly app
corpus.

## Other users of the catalog

The catalog is also the source of download URLs for the test tools:

- `tools/appcorpus.py` (the nightly app corpus) looks up each App Store
  program it tests by name.  The entry must be unique and its `url` must
  match the test's download exactly.
- `tools/ci/stage-graphics.sh` takes the Mesa 3D and DXVK downloads for
  the CI graphics tests from the catalog.  Venus and VKD3D-Proton, which
  NovaOS builds itself and publishes on the `latest` release, are built by
  `tools/build_venus.py` and `tools/build_vkd3d_proton.py` instead (or
  taken from `VENUS_7Z` and `VKD3D_7Z`).  The nightly corpus takes
  VKD3D-Proton from `CORPUS_LOCAL_DOWNLOADS` when the nightly job has
  built it.
- CI's cache key for the graphics downloads includes the catalog's hash,
  so changing the catalog refreshes that cache.
