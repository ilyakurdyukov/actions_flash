## Payload code source

Tested ONLY on ATJ2157.

### Usage

An example of how to run a payload:
```
sudo ./actions_dump \
	simple_switch 0x118000 adfus.bin \
	simple_exec 0x11e000 hello.bin -1
```

How to dump ROM:
```
sudo ./actions_dump \
	simple_switch 0x118000 adfus.bin \
	read_mem2 0 64K rom.bin
```

### Build

Use `NAME=hello` to build `hello.bin`, defaults to building `adfus.bin`.

You can build:

1. `adfus` - ~~reverse engineered binary to run payloads~~ (__TODO__).
2. `hello` - just an example.
3. `nandhwsc` - reads NAND flash ID.
4. `nandread` - ~~binary needed for NAND reading commands~~ (__TODO__).

#### with GCC from the old NDK

* GCC has been removed since r18, and hasn't updated since r13. But sometimes it makes the smallest code.

```
NDK=$HOME/android-ndk-r15c
SYSROOT=$NDK/platforms/android-21/arch-arm
TOOLCHAIN=$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi

make all TOOLCHAIN="$TOOLCHAIN" SYSROOT="$SYSROOT"
```

#### with Clang from the old NDK

* NDK, SYSROOT, TOOLCHAIN as before.

```
CLANG="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang -target armv7-none-linux-androideabi -gcc-toolchain $NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64"

make all TOOLCHAIN="$TOOLCHAIN" SYSROOT="$SYSROOT" CC="$CLANG"
```

#### with Clang from the newer NDK

```
NDK=$HOME/android-ndk-r25b
TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm
CLANG=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang

make all TOOLCHAIN=$TOOLCHAIN CC=$CLANG
```

