## Payload code source

Tested ONLY on ATJ2127.

### Usage

An example of how to run a payload:
```
sudo ./atj2127_dump \
	simple_switch 0xbfc18000 adfus.bin \
	simple_exec 0xbfc1e000 hello.bin -1
```

How to dump ROM and RAM:
```
sudo ./atj2127_dump \
	simple_switch 0xbfc18000 adfus.bin \
	read_mem2 0x9fc00000 256K dump.bin
```

### Build

Use `NAME=hello` to build `hello.bin`, defaults to building `adfus.bin`.

You can build:

1. `adfus` - reverse engineered binary to run payloads.
2. `hello` - just an example.
3. `nandhwsc` - reads NAND flash ID.
4. `nandread` - binary needed for NAND reading commands.

#### with GCC from the old NDK

* MIPS support has been removed since r17.

* GCC hasn't updated since r13. But sometimes it makes the smallest code.

```
NDK=$HOME/android-ndk-r15c
SYSROOT=$NDK/platforms/android-21/arch-mips
TOOLCHAIN=$NDK/toolchains/mipsel-linux-android-4.9/prebuilt/linux-x86_64/bin/mipsel-linux-android

make all TOOLCHAIN="$TOOLCHAIN" SYSROOT="$SYSROOT"
```

#### with Clang from the old NDK

* NDK, SYSROOT, TOOLCHAIN as before.

* Clang from NDK is too buggy to compile for MIPS16.

```
CLANG="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang -target mipsel-none-linux-androideabi -gcc-toolchain $NDK/toolchains/mipsel-linux-android-4.9/prebuilt/linux-x86_64"

make all TOOLCHAIN="$TOOLCHAIN" SYSROOT="$SYSROOT" CC="$CLANG MIPS=32"
```

