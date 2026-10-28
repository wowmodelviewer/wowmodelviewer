// LightGeometry.cs
//
// THE ZONE AND POINT GEOMETRY of the client's light tables, shared by the liquids' zone light (Stage 09, LiquidLights.cs) and
// the outdoor light (Stage 10, WorldLightSet in WorldLight.cs). Extracted from LiquidLights.cs without a change in behaviour:
// the same arithmetic in the same order (Tests/LiquidLightTests.cs is unchanged and green). Pure C#, no UnityEngine.
//   polygons      a ZoneLight's corners (game X north, game Y west, yards; PROVEN_BYTES), 3 points or more, every coordinate
//                 finite; a point is inside when it lies in the polygon's bounding box and the even-odd rule says so (the
//                 data mixes clockwise and counter-clockwise polygons; a point exactly on an edge may fall either way)
//   point lights  a weight from the distance d: 0 at or beyond the falloff end, 1 within the start (and wherever it applies
//                 when end <= start), else linear, (end - d) / (end - start) (INFERRED: the client's code is not read)
//   the order     point lights are applied in ascending order of a key, ties by light id: Stage 09 keys them by their
//                 weight, so the weakest goes first and the strongest has the last word (INFERRED); the outdoor light's debug
//                 arms key them otherwise (WorldLightRules)

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    public static class LightGeometry
    {
        public static bool Finite(double d) { return !double.IsNaN(d) && !double.IsInfinity(d); }

        /// <summary>A polygon a zone can use: 3 points or more (x0, y0, x1, y1, ...: an even coordinate count), every
        /// coordinate finite.</summary>
        public static bool ValidPolygon(double[] poly)
        {
            bool ok = poly != null && poly.Length >= 6 && poly.Length % 2 == 0;
            if (ok)
                foreach (double c in poly)
                    ok &= Finite(c);
            return ok;
        }

        /// <summary>The polygon's bounding box (x0, y0, x1, y1, ...).</summary>
        public static void Bounds(double[] poly, out double minX, out double maxX, out double minY, out double maxY)
        {
            minX = double.PositiveInfinity;
            maxX = double.NegativeInfinity;
            minY = double.PositiveInfinity;
            maxY = double.NegativeInfinity;
            for (int k = 0; k < poly.Length; k += 2)
            {
                minX = Math.Min(minX, poly[k]);
                maxX = Math.Max(maxX, poly[k]);
                minY = Math.Min(minY, poly[k + 1]);
                maxY = Math.Max(maxY, poly[k + 1]);
            }
        }

        /// <summary>Whether (x, y) lies inside the polygon: its bounding box, then the even-odd rule (either winding; a point
        /// exactly on an edge may fall either way).</summary>
        public static bool Contains(double[] poly, double minX, double maxX, double minY, double maxY, double x, double y)
        {
            if (x < minX || x > maxX || y < minY || y > maxY)
                return false;
            bool inside = false;
            int n = poly.Length / 2;
            for (int i = 0, j = n - 1; i < n; j = i++)
            {
                double xi = poly[2 * i], yi = poly[2 * i + 1], xj = poly[2 * j], yj = poly[2 * j + 1];
                // (An edge with yi == yj is never crossed: no division by zero, repeated points are harmless.)
                if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
                    inside = !inside;
            }
            return inside;
        }

        /// <summary>A point light's weight at distance `d`: 0 at or beyond `end`; 1 within `start` (and wherever it applies
        /// when end &lt;= start); else linear, (end - d) / (end - start).</summary>
        public static double PointWeight(double start, double end, double d)
        {
            if (!(d < end))
                return 0.0;
            if (end <= start || d <= start)
                return 1.0;
            return (end - d) / (end - start);
        }

        /// <summary>One applied point light: its order key, its weight, its light id and what it carries.</summary>
        public struct Weighted<T>
        {
            public double Key;
            public double W;
            public int Light;
            public T Item;
        }

        /// <summary>Inserts into `list`, kept in ascending order of the key, ties by light id (a later equal entry goes after
        /// an earlier one only when its light id is not lower). In place, so a reused list allocates nothing.</summary>
        public static void InsertInOrder<T>(List<Weighted<T>> list, double key, double w, int light, T item)
        {
            int at = list.Count;
            while (at > 0 && (key < list[at - 1].Key || (key == list[at - 1].Key && light < list[at - 1].Light)))
                at--;
            list.Insert(at, new Weighted<T> { Key = key, W = w, Light = light, Item = item });
        }
    }
}
