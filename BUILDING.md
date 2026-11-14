# Building FastVideoDSPlayer 2

The player uses the classic libnds FIFO API. The CI image is pinned to
**devkitARM r65-1, libnds 1.8.3-1 and ndstool 2.3.1-1**. From the repository root:

```sh
docker run --rm -v "$PWD:/work" -w /work \
  devkitpro/devkitarm@sha256:a998edf6b06416b5c053edbcd879abfa22b1b88e9cd3f267f5c4ee9fec71a93a \
  make -j2
```

The result is `FastVideoDS.nds`. A native installation of the same SDK can use
`make -j2` with the usual devkitPro environment variables. Build output is ignored
by Git. Clean before switching SDK versions (`make clean`).

## Legacy interrupt-wait fix

`arm9/source/IntrWait.s` provides the libnds 1.8 ARM9 wait routines with the
missing MMIO-base initialization on their immediate-return path. The normal
Makefile includes it automatically; the installed SDK does not need modifying.
The original source notice and revision are retained in the file.

The override is enabled only for libnds 1.8. libnds 1.8.3's generated version
header still reports 1.8.0, so the guard checks major/minor. With a newer SDK the
file supplies no replacement symbols. This guard alone does not port the rest
of the player to libnds 2 or establish compatibility with that SDK.

Keep Gericom's original-player credit, Mathos42's fork credit and their license
explanation in `readme.md`. The root zlib license covers Mathos42's additions;
it does not relicense the original player. The wait routine retains its own
notice, and FatFs retains the notice in `arm7/source/fat/ff.c`.
