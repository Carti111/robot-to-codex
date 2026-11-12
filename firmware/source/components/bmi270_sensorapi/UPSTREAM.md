Bosch Sensortec BMI270 SensorAPI

https://github.com/boschsensortec/BMI270_SensorAPI

Pinned commit 41129fcfe39c583ee5462d79195741945d51c1fe; original BSD-3-Clause notices retained.

BMI260 uses the same Bosch BMI2 base register API for basic accelerometer and gyroscope operation, with its own chip ID and initialization image. No BMI270 feature tables are applied to BMI260.

The unchanged 8192-byte bmi260-init-data.fw and LICENSE.bmi260 come from linux-firmware commit 5e3247fdbb6a8791e75d8d2aa20b30086f8b4de2. Copyright 2020 Bosch Sensortec GmbH, BSD-3-Clause. SHA256 cf0527e2a34aaf3acfdf5ca539cfb47c2381693d064b49d1479ebcfab0cca377.

https://gitlab.com/kernel-firmware/linux-firmware/-/tree/5e3247fdbb6a8791e75d8d2aa20b30086f8b4de2

The current assembled robot reports chip ID 0x27 (BMI260). The factory project's espressif2022/bmi270 1.1.0~2 dependency supports IDs 0x24 and 0x26, so its selection alone does not cover this hardware.
