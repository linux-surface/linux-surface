# S0ix / suspend tweaks for the Surface Pro 7+

Configuration bits that improve `s2idle` (S0ix) behaviour on the **Surface
Pro 7+** (Tiger Lake-UP3). The wakeup fix in particular is the important one:
without it the device wakes itself up repeatedly while suspended.

Everything here was tested on a Surface Pro 7+ running Fedora with kernel
`6.19.8-3.surface`. Most of it is not Pro 7+ specific and should apply to
other Tiger Lake Surface devices, but the device ids differ -- check them
before copying (see the comments in each file).

| File | Goes to |
|---|---|
| `99-no-wakeup-als.rules` | `/etc/udev/rules.d/` |
| `50-nvme-runtime-pm.rules` | `/etc/udev/rules.d/` |
| `disable-wake-sources.service` | `/etc/systemd/system/` |
| `99-log-wakeup` | `/usr/lib/systemd/system-sleep/` (must be executable) |

Treat these as examples: read the comments and adapt them rather than copying
them blindly.

## 1. Spurious wakeups from the ambient light sensor

**Symptom.** The device wakes itself a few seconds after suspending, over and
over, draining the battery in a closed bag. `/sys/power/pm_wakeup_irq` reports
IRQ 14, which on this machine is `INT34C5:00` -- the Tiger Lake pin
controller.

**Cause.** The APDS9960 ambient light / proximity sensor (`i2c-MSHW0184:00`)
has wakeup enabled by default. Its interrupt reaches the system through the
pin controller and is treated as a wake event, so proximity changes wake the
machine. Disabling adaptive brightness in the desktop environment does not
help, because the sensor is still armed at the kernel level.

**Fix.** Install `99-no-wakeup-als.rules`, then reboot or run:

```bash
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=i2c
```

Verify:

```bash
cat /sys/bus/i2c/devices/i2c-MSHW0184:00/power/wakeup   # -> disabled
```

Note that IRQ 14 is shared: the pin controller also carries legitimate wake
sources, so seeing IRQ 14 as the wake reason after this fix is normal and
expected. What goes away is the runaway wake loop.

## 2. NVMe runtime power management

The KIOXIA BG4 in this machine comes up with `power/control=on`, which keeps
the PCIe link from entering its low-power states while idle. Install
`50-nvme-runtime-pm.rules` and check with:

```bash
cat /sys/bus/pci/devices/0000:01:00.0/power/control   # -> auto
```

The vendor/device ids in the rule are specific to that SSD; the Pro 7+ shipped
with more than one model, so confirm yours with `lspci -nn | grep -i nvme`.

## 3. Unnecessary ACPI wake sources

`PEG0` (the PCIe bridge to the NVMe) and `TXHC` (the Thunderbolt 4 controller)
are wakeup-enabled by default. Neither is useful on a tablet with no external
Thunderbolt peripherals:

```bash
sudo cp disable-wake-sources.service /etc/systemd/system/
sudo systemctl enable --now disable-wake-sources.service
grep -E 'PEG0|TXHC' /proc/acpi/wakeup    # -> both *disabled
```

Writing a name to `/proc/acpi/wakeup` toggles it rather than setting it, so
the unit checks the current state first and is safe to run repeatedly. Drop
the `TXHC` line if you do wake the machine from Thunderbolt devices.

## 4. Diagnosing wakeups and S0ix residency

`99-log-wakeup` is a `systemd-sleep` hook that appends one entry per
suspend/resume cycle to `/var/log/wake-diagnostics.log`.

```bash
sudo install -m 755 99-log-wakeup /usr/lib/systemd/system-sleep/
```

A cycle looks like this:

```
=== 2026-09-01 22:20:05 SUSPEND (suspend) ===
  --- LTR: IP blocks advertising a latency requirement ---
    XHCI               non-snoop=0         snoop=499712    (ns)
    AGGREGATED_SYSTEM  non-snoop=0         snoop=1047552   (ns)
=== 2026-09-01 22:20:15 RESUME (suspend) ===
  wake IRQ: 14
      14:  10  ...  IR-IO-APIC  14-fasteoi  INT34C5:00
  suspended for : 34315 s
  in S0ix       : 34293 s
  S0ix residency: 99%
  last_hw_sleep : 4228 s (wrapped, see note in script)
  --- substate residency delta ---
    S0i3.0     34267 s
```

Two different failures show up here, and they are easy to confuse:

**Something is waking you up.** The `wake IRQ` line and the wakeup-source
table name the culprit. Note that `wakeup_count` is cumulative since boot, so
compare it across resumes rather than reading it as a per-cycle value.

**Something is stopping you from staying asleep.** Compare `S0ix residency`
against the wall-clock time. A machine suspended for eight hours that only
spent one in S0ix is not being woken up -- it is failing to stay asleep, and
no amount of chasing wake sources will fix that. The substate delta shows
whether the time went to the deep state (`S0i3.x`) or a shallow one.

Measure this with the PMC `slp_s0_residency_usec` counter, which is what the
residency line above uses. **Do not use
`/sys/power/suspend_stats/last_hw_sleep` for this.** It is derived from a
32-bit microsecond counter that wraps every 2^32 us -- 4295 s, or 71.6
minutes -- so for any longer suspend it reports `actual mod 71.6 min`. On this
machine a 9.5 h suspend at 99.9% residency reads as 4228 s there, which looks
like a catastrophic 12%. Two overnight cycles matched the wrap prediction to
within 0.3 s. The hook logs the value for reference and flags it once the
suspend exceeds the wrap period.

The `LTR` block is the first place to look for the second failure. Each IP
block advertises how long it tolerates being unattended; a block asking for
tens of microseconds keeps the SoC out of substates whose exit latency is
measured in milliseconds. These values are sampled while the system is still
awake, so treat them as a lead rather than proof.

The log is not rotated; add a `/etc/logrotate.d/` snippet if you intend to
leave it installed long-term.

## Results

Measured over 114 suspend/resume cycles across 15 days of daily use, plus two
~10 h cycles instrumented with the PMC counter, all with the tweaks above
applied:

| | Result |
|---|---|
| S0ix residency, suspends of 2-70 min (38 cycles) | 100% median, 98% worst |
| Cycles in that set that failed to reach S0ix | 0 |
| S0ix residency, two consecutive ~10 h suspends | 99.94% |
| Substate the time went to | `S0i3.0` (the deep one) |

Before the ALS fix the device could not stay suspended for more than a few
seconds at a time; that behaviour is gone. The original failure was not
instrumented, so there is no measured "before" column to compare against.

Note the first row stops at 70 minutes. Those cycles were logged with
`last_hw_sleep`, which is only trustworthy below the 71.6 minute wrap
described in section 4; 27 of the 114 cycles ran longer and are not
interpretable from that counter. The two 10 h figures come from
`slp_s0_residency_usec` instead, which does not wrap.

### A note on measuring this

An earlier revision of this file reported poor S0ix residency on long
suspends. That was wrong, and the cause is worth repeating: it was the
`last_hw_sleep` wrap described in section 4, not a hardware problem. Measured
against the PMC counter, two consecutive ~10 h suspends both reached 99.94%
residency.

If you are diagnosing suspend battery drain on one of these machines, check
which counter your numbers come from before concluding anything.
