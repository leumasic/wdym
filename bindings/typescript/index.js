import { createRequire } from "node:module"

const require = createRequire(import.meta.url)

export const { BytePairEncoding } = require(
  `./prebuilds/${process.platform}-${process.arch}/wdym.node`,
)
