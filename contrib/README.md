# Scripts, Tweaks, and Configuration Bits

This directory contains scripts, tweaks, and configuration examples aiming to improve your Linux experience on Surface devices.
Any config files in these directories should be treated as examples and basis for adapting your own config files.
Feel free to send us a pull-request if you feel that something is missing or can be improved.

## Layout and Structure

Entries should be put into individual directories with a README.md.
This readme should state the purpose of that tweak/script and, if there are any config files, where those files should be placed (or any other installation instructions and hints).
Ideally, it should, for config files, also give an explanation of what is/has to be changed and why.
In general, config files should be kept minimal, if possible contain comments, and users should be encouraged to modify their config file instead of simply copying the one provided.

The overall structure should look something like this:

```
contrib
 │
 ├─ some-script
 │ ├─ the-script.sh
 │ └─ README.md
 │
 └─ some-tweak
   ├─ a.conf
   ├─ b.conf
   └─ README.md
```
