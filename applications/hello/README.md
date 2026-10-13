# Hello console

A small included C program that prints a greeting, waits for Enter and exits.
It is a starting point for a new manifest and uses the repository's BSD license.
Install the [common prerequisites](../README.md), then from the repository root:

```sh
sh applications/hello/build.sh
# Optional alternate output directory:
sh applications/hello/build.sh /absolute/path/to/build/hello
```

Outputs: `build/hello/hello.uf2` and `build/hello/hello-volume.uf2`.
The volume uses the platform's default 512 KB region at flash offset `0x180000`.
Exit uses the default hardware reboot. Image construction is tested in the cloud;
physical USB interaction still needs a Pico.
