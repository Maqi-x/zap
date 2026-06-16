# import_map example

Demonstrates the `--import-map` flag, which lets you define short aliases for
module paths so deep imports stay readable regardless of where the file lives.

## Project layout

```
import_map/
├── main.zp
└── src/
    ├── models/
    │   └── product.zp
    ├── services/
    │   └── cart.zp
    └── utils/
        └── math.zp
```

`cart.zp` lives under `src/services/` but imports from both `models/` and
`utils/`. Without an import map it would need `../models/product` and
`../utils/math`. With aliases those become `@models/product` and `@utils/math`
— the same string works from any file in the project.

## Build

```sh
zapc main.zp \
  --import-map @models=./src/models \
  --import-map @utils=./src/utils \
  --import-map @services=./src/services \
  -o shop
./shop
```

## Thor integration

When using [Thor](https://github.com/thezaplang/thor), declare the mappings
once in `thor.toml`:

```toml
[imports]
"@models"   = "./src/models"
"@utils"    = "./src/utils"
"@services" = "./src/services"
```

Thor translates each entry into an `--import-map` flag automatically.
