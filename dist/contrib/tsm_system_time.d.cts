import { d as PGliteInterface } from '../pglite-BoI3NpBu.cjs';

declare const tsm_system_time: {
    name: string;
    setup: (_pg: PGliteInterface, _emscriptenOpts: any) => Promise<{
        bundlePath: URL;
    }>;
};

export { tsm_system_time };
