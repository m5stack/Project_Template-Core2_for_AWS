# M5Stack Core2 for AWS IoT EduKit Project Template

Project template for the M5Stack Core2 for AWS IoT EduKit reference hardware. Use this template as a baseline for creating your applications on your hardware. This includes all the drivers and configurations for using the hardware features on the devices. View the [API reference](https://edukit.workshop.aws/en/api-reference/v2/index.html) for using the included board support package.

## Cloning
This repo uses [Git Submodules](https://git-scm.com/book/en/v2/Git-Tools-Submodules) to bring in dependent components.

Note: If you download the ZIP file provided by GitHub UI, you will not get the contents of the submodules. Since the downloaded zip will also not be a git repository, you will not be able to compile the code since that is a toolchain requirement. You must clone the repository using the instructions below.

If using Windows, because this repository and its submodules contain symbolic links, set `core.symlinks` to true with the following command:
```
git config --global core.symlinks true
```
In addition to this, either enable [Developer Mode](https://docs.microsoft.com/en-us/windows/apps/get-started/enable-your-device-for-development) or, whenever using a git command that writes to the system (e.g. `git pull`, `git clone`, and `git submodule update --init --recursive`), use a console elevated as administrator so that git can properly create symbolic links for this repository. Otherwise, symbolic links will be written as normal files with the symbolic links' paths in them as text. [This](https://blogs.windows.com/windowsdeveloper/2016/12/02/symlinks-windows-10/) gives more explanation.

To clone using HTTPS:
```
git clone https://github.com/aws-iot-edukit/Project_Template-Core2_for_AWS.git --recurse-submodules
```
Using SSH:
```
git clone git@github.com:aws-iot-edukit/Project_Template-Core2_for_AWS.git --recurse-submodules
```

If you have downloaded the repo without using the `--recurse-submodules` argument, you need to run:
```
git submodule update --init --recursive
```

## Important files/folders

### main/main.c

This is the entry point for your application. Modify this file to create your own custom application.

### components/Core2-for-AWS-IoT-EduKit

This is the location of the [board support package](https://github.com/m5stack/Core2-for-AWS-IoT-EduKit). These include drivers and helper libraries for controlling the on-board peripherals on the device.

### components/esp-cryptoauthlib

This is a [ported cryptoauthlib](https://github.com/espressif/esp-cryptoauthlib) from Espressif. This fork is a specific port for the Core2 for AWS reference hardware. This library enables use of the on-board secure element and is tightly integrated with the ESP-IDF. The name of the folder should not be modified.

### partitions_16mb.csv

This is the partition table recommended for most applications. It provides sufficient file system sizes for storing Wi-Fi credentials, the user application, OTA updates, additional file storage, and storage for SPIFFS in the on-board flash. This utilizes the internal + external flash memory.

## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more information.

## License

This library is licensed under the MIT-0 License. See the LICENSE file.

