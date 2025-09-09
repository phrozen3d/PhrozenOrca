#  <p><img align="left" src="https://github.com/phrozen3d/PhrozenOrca/blob/phrozen-custom-dev/resources/images/PhrozenOrca.ico" width="40"> Phrozen Orca</p>

PhrozenOrca is the official release of our customized slicing software, based on OrcaSlicer and optimized for Phrozen FDM 3D printers.  
This version provides an integrated workflow from model preparation to print monitoring, ensuring a smoother and more reliable slicing experience tailored for Phrozen printers.


# Features

#### Printer Settings
- Built-in profiles for Phrozen Arco printers.

#### Filament Settings
- Preset filament profiles optimized for Phrozen materials.  
- User-defined custom filament support.

#### Printing Settings
- Standard/Advanced  slicing parameters.

#### User Interface
- Prepare Page for model editing and slicing.  
- Preview Page with G-code visualization, layer slider, and path inspection.  
- Device Page for printer control, monitor, progress tracking, and print history. 
- Phrozen-themed UI design.

# Wiki

Wiki page is currently under construction. Stay tuned!

# Download

### Stable Release
📥 **[Download the Latest Stable Release](https://github.com/phrozen3d/PhrozenOrca/releases/latest)**  
Visit our GitHub Releases page for the latest stable version of Phrozen Orca, recommended for most users.



# How to install
### Windows
Download the **Windows Installer exe**  for your preferred version from the [releases page](https://github.com/phrozen3d/PhrozenOrca/releases).

 - *For convenience there is also a portable build available.*
    <details>
    <summary>Troubleshooting</summary>

    - *If you have troubles to run the build, you might need to install following runtimes:*
    - [MicrosoftEdgeWebView2RuntimeInstallerX64](https://github.com/SoftFever/OrcaSlicer/releases/download/v1.0.10-sf2/MicrosoftEdgeWebView2RuntimeInstallerX64.exe)
        - [Details of this runtime](https://aka.ms/webview2)
        - [Alternative Download Link Hosted by Microsoft](https://go.microsoft.com/fwlink/p/?LinkId=2124703)
    - [vcredist2019_x64](https://github.com/SoftFever/OrcaSlicer/releases/download/v1.0.10-sf2/vcredist2019_x64.exe)
        -  [Alternative Download Link Hosted by Microsoft](https://aka.ms/vs/17/release/vc_redist.x64.exe)
        -  This file may already be available on your computer if you've installed visual studio.  Check the following location: `%VCINSTALLDIR%Redist\MSVC\v142`
    </details>

### Mac:
1. Download the DMG for your computer: `arm64` version for Apple Silicon and `x86_64` for Intel CPU.
2. Drag PhrozenOrca.app to Application folder.
3. *If you want to run a build from a PR, you also need to follow the instructions below:*
    <details>
    <summary>Quarantine</summary>

    - Option 1 (You only need to do this once. After that the app can be opened normally.):
      - Step 1: Hold _cmd_ and right click the app, from the context menu choose **Open**.
      - Step 2: A warning window will pop up, click _Open_

    - Option 2:
      Execute this command in terminal:
      ```shell
      xattr -dr com.apple.quarantine /Applications/PhrozenOrca.app`
      ```
    - Option 3:
        - Step 1: open the app, a warning window will pop up  
            ![mac_cant_open](./SoftFever_doc/mac_cant_open.png)
        - Step 2: in `System Settings` -> `Privacy & Security`, click `Open Anyway`:
            ![mac_security_setting](./SoftFever_doc/mac_security_setting.png)
    </details>

## Linux (Ubuntu):
Linux support will be added in the future.


# Klipper Note:
If you're running Klipper, it's recommended to add the following configuration to your `printer.cfg` file.
```gcode
# Enable object exclusion
[exclude_object]

# Enable arcs support
[gcode_arcs]
resolution: 0.1
```


## Some background
PhrozenOrca was forked from OrcaSlicer.

[OrcaSlicer](https://github.com/SoftFever/OrcaSlicer) was originally forked from Bambu Studio, it was previously known as BambuStudio-SoftFever.
[Bambu Studio](https://github.com/bambulab/BambuStudio) is forked from [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, which is from [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community.
Orca Slicer incorporates a lot of features from [SuperSlicer](https://github.com/supermerill/SuperSlicer) by @supermerill
Orca Slicer's logo is designed by community member Justin Levine(@freejstnalxndr).

# License
**Phrozen Orca** is licensed under the GNU Affero General Public License, version 3. Phrozen Orca is based on OrcaSlicer by SoftFever.

**Orca Slicer** is licensed under the GNU Affero General Public License, version 3. Orca Slicer is based on Bambu Studio by BambuLab.

**Bambu Studio** is licensed under the GNU Affero General Public License, version 3. Bambu Studio is based on PrusaSlicer by PrusaResearch.

**PrusaSlicer** is licensed under the GNU Affero General Public License, version 3. PrusaSlicer is owned by Prusa Research. PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

**Slic3r** is licensed under the GNU Affero General Public License, version 3. Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The GNU Affero General Public License, version 3 ensures that if you use any part of this software in any way (even behind a web server), your software must be released under the same license.

Orca Slicer includes a pressure advance calibration pattern test adapted from Andrew Ellis' generator, which is licensed under GNU General Public License, version 3. Ellis' generator is itself adapted from a generator developed by Sineos for Marlin, which is licensed under GNU General Public License, version 3.

The Bambu networking plugin is based on non-free libraries from BambuLab. It is optional to the Orca Slicer and provides extended functionalities for Bambulab printer users.
