## Smartlink firmware helper

For Actions ATJ2127 firmware dumps.

### Usage

Prints info about the contents of the LFI firmware dump.

`./fphelper dump.bin scan_lfi`  

Use this command to run an LFI dump scan and extract files:

`./fphelper dump.bin unpack_lfi`  

Prints info about a single file from the LFI firmware dump.

`./fphelper KERNEL.DRV scan_file`  

Use this command to repair the LFI dump:

`./fphelper lfi_raw.bin lfi_repair <fw_sectors> <pages_per_block> <page_size> lfi_out.bin`  

