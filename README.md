# M5Stack Core2 for AWS IoT Kit Project Template

Project template for the M5Stack Core2 for AWS IoT Kit reference hardware. Use this template as a baseline for creating your applications on your hardware. This includes all the drivers and configurations for using the hardware features on the devices. View the API reference for using the included board support package.

## Cloning
This repo uses [Git Submodules](https://git-scm.com/book/en/v2/Git-Tools-Submodules) to bring in dependent components.

Note: If you download the ZIP file provided by GitHub UI, you will not get the contents of the submodules. Since the downloaded zip will also not be a git repository, you will not be able to compile the code since that is a toolchain requirement. You must clone the repository using the instructions below.

To clone using HTTPS:
```
git clone https://github.com/m5stack/Project_Template-Core2_for_AWS.git --recurse-submodules
```
Using SSH:
```
git clone git@github.com:m5stack/Project_Template-Core2_for_AWS.git --recurse-submodules
```

If you have downloaded the repo without using the `--recurse-submodules` argument, you need to run:
```
git submodule update --init --recursive
```

## Important files/folders

### main/main.c

This is the entry point for your application. Modify this file to create your own custom application.

### components/Core2-for-AWS-IoT-Kit

This is the location of the [board support package](https://github.com/m5stack/Core2-for-AWS-IoT-Kit). These include drivers and helper libraries for controlling the on-board peripherals on the device.

### managed_components/

Third-party dependencies of the board support package (LVGL, esp-cryptoauthlib, ESP LCD/touch drivers, network provisioning, etc.) are declared in the BSP's `idf_component.yml` and downloaded here automatically by the ESP-IDF Component Manager at build time. This folder is not tracked by git; do not edit its contents.

### sdkconfig.defaults

Project configuration defaults (flash, PSRAM, Bluetooth, mbedTLS, LVGL, and the BSP's hardware feature switches). The generated `sdkconfig.core2foraws` only picks up a default when that option is not already present, so delete it after changing this file to regenerate it.

The serial console runs at 921600 baud, matching `monitor_speed` in `platformio.ini`.

### partitions_16MB.csv

This is the partition table recommended for most applications. It provides sufficient file system sizes for storing Wi-Fi credentials, the user application, OTA updates, core dumps, additional file storage, and storage for SPIFFS in the on-board flash.

### test/test_hw_stability

On-device Unity test suite for the board support package and the template configuration. With the device connected, run:
```
pio test -e core2foraws
```

## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more information.

## License

This library is licensed under the MIT License. See the LICENSE file.

