# Extra kernel patches

Put `*.patch` files here. `build.sh` applies them, in name order, after all
the linux-surface patches. Name them `0001-....patch`, `0002-....patch`, …
to set the order.

Each patch must apply with `patch -Np1` from the top of the kernel source
tree, which is what `git format-patch` produces.

To check that a patch applies before a multi-hour build, run
`./build.sh --build-only` and watch for "Applying patch extra-…" in the
first minute. A patch that doesn't apply stops the build right there.
