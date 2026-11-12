#!/bin/bash
# PAM helper (pam_exec expose_authtok): skip Howdy when the user typed a password.
# Exit 0 = password non-empty → caller should jump past pam_howdy.
# Exit 1 = empty → allow Howdy to run.
set -u
pass=$(cat 2>/dev/null || true)
[[ -n "${pass}" ]]
exit $?
