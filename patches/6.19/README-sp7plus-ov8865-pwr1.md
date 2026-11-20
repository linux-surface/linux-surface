# Surface Pro 7+ OV8865 `pwr1` test evidence

This file accompanies the RFC patch in `0014-ov8865-pwr1.patch`.

## Tested hardware and software

- Microsoft Surface Pro 7+
- Intel Tiger Lake IPU6 (`8086:9a19`)
- Rear sensor: OmniVision OV8865 (`INT347A:00`)
- Kernel: `6.19.8-surface-3`
- libcamera: `0.7.0`

## Failure before the patch

The INT3472 layer exposed a regulator named `INT3472:01-pwr1`, but it had
zero users and remained disabled. The OV8865 runtime-resume path failed on
the first I2C software-reset write:

```text
ov8865 i2c-INT347A:00: failed to perform sw reset
ov8865 i2c-INT347A:00: Error -121 runtime-resuming sensor, cannot instantiate VCM
```

Increasing the power-up delay and retrying the reset five times still
returned `-121` on every attempt.

## Result after the patch

```text
int3472-discrete INT3472:01:   con_id=pwr1, flags=0x0
ov8865 i2c-INT347A:00: using optional pwr1 regulator
ov8865 i2c-INT347A:00: Instantiated dw9719 VCM
```

Raw capture:

```text
3264x2448-SBGGR10/RAW
15.00 fps
10/10 frames captured
bytesused: 15980544
```

Processed capture:

```text
1280x720-ABGR8888/sRGB
30.0 fps
30/30 frames captured
bytesused: 3686400
```

## Front camera

The OV5693 front camera also works on this device after programming
MIPI register `0x4800` to `0x2d` before stream-on, as proposed in
linux-surface PR #2171. On this Surface Pro 7+ it captures 1296x972
SBGGR10 at approximately 28.7 fps.
