# Overrides for the surface patches

Some patches in `patches/<series>` conflict with changes that kernel-ark
carries on top of the stable kernel (usually backports from mainline). A patch
file in this directory replaces the patch file with the same name in
`patches/<series>` for the Fedora package.

The commits of the original patch file are recorded in `<patch>.subjects`.
The build and the autoupdate script fail if they change, since the override
then has to be updated (or removed) manually. Changes within a commit are not
detected, so also check overrides when the original patch file changes.

After updating an override, update the subjects from the original patch:

    awk -f pkg/fedora/kernel-surface/patch-subjects.awk \
        patches/<series>/<patch> > pkg/fedora/kernel-surface/overrides/<patch>.subjects

## 0013-cameras.patch

kernel-ark has the ipu-bridge changes of v7.4 since 7.3.5, including sensor
configs and rotation quirks for the Surface Pro 9.

- *Add camera support for Surface Pro 9*: Without the ipu-bridge changes.
- *media: ipu-bridge: Add front camera rotation quirks for Surface Pro 8 and
  9*: Only the Surface Pro 8 quirk.
- *media: ipu-bridge: fix rear camera rotation on Surface Pro 9*: Dropped.
