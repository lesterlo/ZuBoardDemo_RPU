# ZuBoardDemo_RPU

FreeRTOS firmware for both Cortex-R5 cores in the ZUBoard demo.

## Build order

The RPU channel configuration must be generated from the same Yocto machine
configuration as the Linux device tree. From the `zudemo` workspace root:

1. Generate/install the `zudemo` machine configuration so these files exist:
   `yocto-build/build/conf/dts/zudemo/zudemo-cortexr5-{0,1}-freertos.dts`.
2. Source the Yocto SDK environment and build the native lopper tools:

   ```bash
   cd yocto-build
   source ./setupSDK build
   bitbake esw-conf-native
   cd ..
   ```

3. Generate one OpenAMP channel header for each R5 core:

   ```bash
   ZuBoardDemo_RPU/scripts/gen_openamp_channel_headers.sh
   ```

   The headers are written to
   `runtime-generated/openamp_gen/psu_cortexr5_{0,1}/amd_platform_info.h`.
   `R5c0` and `R5c1` each compile against only their corresponding header.

4. Create the Vitis platform and build both applications from the PL XSA:

   ```bash
   mkdir -p /tmp/xilinx-vitis-data
   export XILINX_VITIS_DATA_DIR=/tmp/xilinx-vitis-data
   vitis -s ZuBoardDemo_RPU/scripts/create_platform_from_xsa.py -- \
     --workspace ZuBoardDemo_RPU \
     --xsa runtime-generated/bin_file/ZuBoardDemo_PL.xsa \
     --force
   ```

The resulting firmware files are `ZuBoardDemo_RPU/R5c0/build/R5c0.elf` and
`ZuBoardDemo_RPU/R5c1/build/R5c1.elf`. The automated workspace `make_RPU.sh`
stage performs the same sequence and packages only those two ELF files.

The OpenAMP shared-memory addresses, sizes, buffer offsets, and IPI channel
mask intentionally have no hard-coded fallback. A missing generated header
therefore fails at compile time instead of silently producing firmware that
does not match the Linux remoteproc device tree.
