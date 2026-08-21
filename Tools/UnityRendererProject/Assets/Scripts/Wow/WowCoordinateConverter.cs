// WowCoordinateConverter.cs
//
// THE single place where WoW's coordinate convention becomes Unity's. Nothing else in the
// renderer is allowed to flip a sign: if something looks mirrored or inside-out, it is fixed
// here and everything downstream follows.
//
//   WoW model space : right-handed, Z up, +X forward (the direction the model faces),
//                     +Y to the model's left. 1 unit ~= 1 yard.
//   Unity           : left-handed, Y up, +Z forward, +X right.
//
// Mapping (forward -> forward, up -> up, left -> -right):
//
//     unity.x = -wow.y      (right       = -left)
//     unity.y =  wow.z      (up          =  up)
//     unity.z =  wow.x      (forward     =  forward)
//
// That matrix has determinant -1 -- it reverses handedness, which is exactly what converting
// between a right- and a left-handed system must do. A mirroring transform also reverses
// triangle orientation, so triangle winding is flipped alongside it (FlipWinding) to keep
// front faces facing out and Unity's default back-face culling correct.
//
// Scale: left at 1.0. WoW units are yards and Unity units are metres, so a model comes out
// ~9% smaller than life; that is irrelevant for viewing and the camera frames by bounds
// anyway. Introducing a scale factor here (0.9144) would be the one-line change.

namespace Wmv.Wow
{
    public static class WowCoordinateConverter
    {
        /// <summary>Unity units per WoW unit. 1.0 keeps model space as authored.</summary>
        public const float Scale = 1.0f;

        /// <summary>Convert a WoW model-space position to Unity space.</summary>
        public static void ConvertPosition(WowVec3 wow, out float x, out float y, out float z)
        {
            x = -wow.Y * Scale;
            y = wow.Z * Scale;
            z = wow.X * Scale;
        }

        /// <summary>
        /// Convert a WoW normal. Same linear map as positions but without scale, so unit
        /// normals stay unit length.
        /// </summary>
        public static void ConvertNormal(WowVec3 wow, out float x, out float y, out float z)
        {
            x = -wow.Y;
            y = wow.Z;
            z = wow.X;
        }

        /// <summary>
        /// WoW texture coordinates have V running down from the top; Unity's run up from the
        /// bottom, so V is flipped once, here.
        /// </summary>
        public static void ConvertTexCoord(WowVec2 wow, out float u, out float v)
        {
            u = wow.X;
            v = 1.0f - wow.Y;
        }

        /// <summary>
        /// Reverse each triangle's winding, compensating for the handedness flip above.
        /// Operates in place on a triangle list (length must be a multiple of 3).
        /// </summary>
        public static void FlipWinding(int[] indices)
        {
            if (indices == null)
                return;
            for (int i = 0; i + 2 < indices.Length; i += 3)
            {
                int tmp = indices[i + 1];
                indices[i + 1] = indices[i + 2];
                indices[i + 2] = tmp;
            }
        }
    }
}
