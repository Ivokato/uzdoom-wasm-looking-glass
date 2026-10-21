# UZDoom WASM — Looking Glass multi-view fork

Public corresponding source for the **Looking Glass Doom** portfolio project.

- Upstream: [abootnet/uzdoom-wasm](https://github.com/abootnet/uzdoom-wasm) (UZDoom / GPLv3)
- Changes: `vr_mode 15` quilt cameras, sparse quality, GLES quilt pack, flat UI present path, `web/lg-swizzle.js`

## License

This tree remains **GPLv3** (same as UZDoom / upstream). See upstream license files.

## Build

Requires Docker. From this repo:

```bash
docker build -t uzdoom-lg-multiview:local .
```

Extract staged web assets from the image (see the portfolio project’s
`coding-dev/projects/looking-glass-doom/spike/multiview/build-docker.sh`).

Or from the portfolio checkout:

```bash
export ENGINE_SRC=/Users/ivo/tinker/uzdoom-wasm-looking-glass
./spike/multiview/build-docker.sh
```

## IWAD note

Do not commit commercial Doom IWADs. The portfolio project ships Doom **shareware**
(`doom1.wad`) and/or Freedoom only.
