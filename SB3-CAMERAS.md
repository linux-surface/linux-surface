# SB3 IPU4P cameras (contrib)

Branch: `sb3-ipu4-cameras`

linux-surface still lists Book 3 cameras as unsupported. This branch does
**not** dump the full IPU4P driver into `patches/6.19/` (that series would
fail `git am` on vanilla). The working DPHY lock is an overlay on top of
[ruslanbay/ipu4-drivers](https://github.com/ruslanbay/ipu4-drivers).

Self-contained tree: `contrib/sb3-ipu4-cameras/` (kernel overlay + userspace
installer). IR/Howdy STREAMON is present in the plumbing but disabled until
ISYS can survive it — planned for a later commit.
