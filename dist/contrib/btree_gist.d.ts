import { d as PGliteInterface } from '../pglite-BoI3NpBu.js';

declare const btree_gist: {
    name: string;
    setup: (_pg: PGliteInterface, _emscriptenOpts: any) => Promise<{
        bundlePath: URL;
    }>;
};

export { btree_gist };
