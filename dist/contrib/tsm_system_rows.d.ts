import { d as PGliteInterface } from '../pglite-BoI3NpBu.js';

declare const tsm_system_rows: {
    name: string;
    setup: (_pg: PGliteInterface, _emscriptenOpts: any) => Promise<{
        bundlePath: URL;
    }>;
};

export { tsm_system_rows };
