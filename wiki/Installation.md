# Installation

Use the [README installer](../README.md#install) after release assets
are published, or install locally prepared artifacts using the
[packaging guide](../docs/packaging.md). The frontend, helper, profiles and schemas
come from the same payload. Direct installations use `/usr/local`; distribution
packages use `/usr`. Mutable service state is stored separately.

Direct releases also provide a standalone executable with built-in profiles and
a bundled helper. The helper is set up with administrator authorization on first
use when a matching installed helper is unavailable. Debian/PPA installations
continue to require the complete package. See
[installing on another server](../docs/packaging.md#installing-on-another-server)
for standalone setup, the installed layout and older-version troubleshooting.

Checksum verification, supported architecture/glibc and trusted helper ancestry
matter. A user-owned custom prefix can support that user's workflows but cannot
serve as a trusted root helper. A standalone direct build uses its administrator-owned
helper cache for system operations through ordinary sudo authorization.

Removing the executable package preserves generated services. Remove an individual
service with `tired remove NAME`, not by deleting its application directory. Keep
private environment revisions and administrative state when uninstalling tired;
standard systemd can continue supervising the service.

GitHub assets, a downloadable `.deb`, an Ubuntu PPA, the Debian archive and the Snap
Store are distinct distribution routes. Local preparation does not mean a hosted
repository or downstream listing already exists.
