// TerrainHeight.cs
//
// The height of the drawn terrain under a point (protocol 15, docs/world-viewer/05-maps-viewer-ui.md section 21): where
// the Maps viewer's free camera stands when the host asks for a position "on the ground". It is read from the tile's
// own mesh -- the triangles TerrainMeshBuilder made and the renderer draws (TerrainMesh.cs: four triangles fanned from
// each cell's inner vertex, hole cells without triangles) -- never from a second model of the ground, so the camera
// stands on what is seen.
//
// The chunk under the point comes from the integer layout (TerrainLayout.cs: chunk (ix, iy) spans y in
// ((32 - tx)T - (ix + 1)C, (32 - tx)T - ix C] and x likewise with ty), and the triangle by a barycentric test on the
// mesh's Unity-space positions (origin-local, double arithmetic on the stored floats). A point on a shared edge belongs to
// either neighbour: both give the same height there, as the seams are exact (00-audit.md 6.2). No triangle under the
// point (a hole, a chunk not built, or a point off the tile) is NaN, never a guess.
//
// Pure C#, no Unity: the self-test checks it against ramps whose height is known everywhere.

using System;

namespace Wmv.Wow.World
{
    public static class TerrainHeight
    {
        /// <summary>The game-frame height (z, yards) of the mesh's triangle under world point (x, y), or NaN when no triangle
        /// of the tile is under it.</summary>
        public static double At(TerrainTileMesh mesh, double x, double y)
        {
            if (mesh == null || mesh.Chunks == null)
                return double.NaN;
            WorldOrigin tile = WorldToUnity.TileOrigin(mesh.TileX, mesh.TileY);
            double c = TerrainCoordinates.ChunkSize;
            // Distances south and east of the tile's north-west corner, in chunks.
            double south = (tile.X - x) / c, east = (tile.Y - y) / c;
            const double edge = 1e-6;
            if (south < -edge || east < -edge || south > TerrainCoordinates.ChunksPerTileSide + edge ||
                east > TerrainCoordinates.ChunksPerTileSide + edge)
                return double.NaN;
            int iy = Clamp((int)Math.Floor(south)), ix = Clamp((int)Math.Floor(east));
            // The point in the mesh's Unity space: (-(y - o.Y), z - o.Z, x - o.X).
            double ux = -(y - mesh.Origin.Y), uz = x - mesh.Origin.X;
            double h = InChunk(mesh, iy * TerrainCoordinates.ChunksPerTileSide + ix, ux, uz);
            if (double.IsNaN(h))
            {
                // On a chunk border, within rounding: the neighbour that shares the edge.
                for (int dy = -1; dy <= 1 && double.IsNaN(h); dy++)
                    for (int dx = -1; dx <= 1 && double.IsNaN(h); dx++)
                    {
                        if (dx == 0 && dy == 0)
                            continue;
                        int nx = ix + dx, ny = iy + dy;
                        if (nx < 0 || ny < 0 || nx >= TerrainCoordinates.ChunksPerTileSide || ny >= TerrainCoordinates.ChunksPerTileSide)
                            continue;
                        const double tol = 1e-4;   // chunks: a point on that neighbour's edge, within rounding
                        if (east < nx - tol || east > nx + 1 + tol || south < ny - tol || south > ny + 1 + tol)
                            continue;
                        h = InChunk(mesh, ny * TerrainCoordinates.ChunksPerTileSide + nx, ux, uz);
                    }
            }
            return double.IsNaN(h) ? double.NaN : h + mesh.Origin.Z;
        }

        static int Clamp(int i)
        {
            return Math.Max(0, Math.Min(TerrainCoordinates.ChunksPerTileSide - 1, i));
        }

        /// <summary>The Unity y of chunk n's triangle under (ux, uz), or NaN.</summary>
        static double InChunk(TerrainTileMesh mesh, int n, double ux, double uz)
        {
            if (n < 0 || n >= mesh.Chunks.Length)
                return double.NaN;
            TerrainChunkMesh ch = mesh.Chunks[n];
            if (ch == null || ch.State != TerrainChunkState.Built || ch.Positions == null || ch.Indices == null)
                return double.NaN;
            float[] p = ch.Positions;
            ushort[] idx = ch.Indices;
            // A little beyond a triangle's edges: the tile's last row and column are stored as floats up to 2e-5 yd short of
            // the true edge, and a point between the two belongs to no other chunk. 1e-5 of a cell triangle is about 4e-5 yd:
            // nowhere near a hole's width.
            const double eps = 1e-5;
            for (int t = 0; t + 2 < idx.Length; t += 3)
            {
                int a = 3 * idx[t], b = 3 * idx[t + 1], d = 3 * idx[t + 2];
                double ax = p[a], az = p[a + 2], bx = p[b], bz = p[b + 2], dx = p[d], dz = p[d + 2];
                double det = (bz - dz) * (ax - dx) + (dx - bx) * (az - dz);
                if (Math.Abs(det) < 1e-12)
                    continue;
                double l1 = ((bz - dz) * (ux - dx) + (dx - bx) * (uz - dz)) / det;
                double l2 = ((dz - az) * (ux - dx) + (ax - dx) * (uz - dz)) / det;
                double l3 = 1.0 - l1 - l2;
                if (l1 < -eps || l2 < -eps || l3 < -eps)
                    continue;
                return l1 * p[a + 1] + l2 * p[b + 1] + l3 * p[d + 1];
            }
            return double.NaN;
        }
    }
}
