using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace Wmv.Wow.Tests
{
    // Optional local corpus only. No client bytes or derived poses are checked in.
    public static class OrcPostureCorpusTests
    {
        static long Id(M2Sequence s) { return ((long)s.AnimId << 32) ^ (uint)s.SubAnimId; }
        static void Require(bool value, string message) { if (!value) throw new Exception(message); }
        static object Track<T>(M2Track<T> t, uint[] globals)
        {
            return new { interpolation = (int)t.Interpolation, period = t.GlobalSequence >= 0 ? (long)globals[t.GlobalSequence] : -1,
                         times = t.Times, values = t.Values };
        }
        static void Same<T>(M2Track<T> a, uint[] ga, M2Track<T> b, uint[] gb)
        {
            Require(a.Interpolation == b.Interpolation, "source interpolation mismatch");
            long pa = a.GlobalSequence >= 0 ? ga[a.GlobalSequence] : -1;
            long pb = b.GlobalSequence >= 0 ? gb[b.GlobalSequence] : -1;
            Require(pa == pb, "source global period mismatch");
            Require((a.Values?.Length ?? 0) == (b.Values?.Length ?? 0), "source key count mismatch");
            for (int k = 0; k < (a.Values?.Length ?? 0); k++)
                Require(a.Times[k] == b.Times[k] && a.Values[k].Equals(b.Values[k]), "source key mismatch");
        }
        public static int Run(string folder, Action<string> log)
        {
            try
            {
                byte[] parent = File.ReadAllBytes(Path.Combine(folder, "1964411.bin"));
                byte[] child = File.ReadAllBytes(Path.Combine(folder, "1969462.bin"));
                var poses = new List<object>(); int checks = 0;
                foreach (int modelId in new[] { 917116, 1968587 })
                {
                    bool upright = modelId == 1968587;
                    byte[] m2 = File.ReadAllBytes(Path.Combine(folder, modelId + ".bin"));
                    var model = M2Parser.Parse(m2);
                    M2Parser.ApplySkeleton(model, upright ? child : parent, upright ? parent : null, 0);
                    var nativeParent = M2Parser.Parse(m2); M2Parser.ApplySkeleton(nativeParent, parent, null, 0);
                    var nativeChild = M2Parser.Parse(m2); M2Parser.ApplySkeleton(nativeChild, child, null, 0);
                    var own = new Dictionary<long, int>();
                    for (int i = 0; i < nativeChild.Sequences.Length; i++) own.Add(Id(nativeChild.Sequences[i]), i);
                    int clips = 0;
                    for (int i = 0; i < model.Sequences.Length; i++)
                    {
                        var sequence = model.Sequences[i]; if (sequence.AnimId < 0 || sequence.AnimId > 225) continue;
                        bool ownClip = upright && own.ContainsKey(Id(sequence));
                        var native = ownClip ? nativeChild : nativeParent; int local = -1;
                        for (int j = 0; j < native.Sequences.Length; j++) if (Id(native.Sequences[j]) == Id(sequence)) local = j;
                        Require(local >= 0, "source sequence missing");
                        int file = M2Parser.ExternalAnimFileId(native, local);
                        Require(M2Parser.ExternalAnimFileId(model, i) == file, "wrong posture AFID");
                        byte[] external = file == 0 ? null : File.ReadAllBytes(Path.Combine(folder, file + ".bin"));
                        M2Parser.ReadAnimationInto(m2, i, model, external); M2Parser.ReadAnimationInto(m2, local, native, external);
                        Require(model.AnimatedSequence == i && model.Sequences[i].Length == native.Sequences[local].Length, "wrong sequence/duration");
                        for (int b = 0; b < model.Bones.Length; b++)
                        {
                            Same(model.Bones[b].Translation, model.GlobalSequences, native.Bones[b].Translation, native.GlobalSequences);
                            Same(model.Bones[b].Rotation, model.GlobalSequences, native.Bones[b].Rotation, native.GlobalSequences);
                            Same(model.Bones[b].Scale, model.GlobalSequences, native.Bones[b].Scale, native.GlobalSequences); checks += 3;
                        }
                        if ((sequence.AnimId == 0 || sequence.AnimId == 4 || sequence.AnimId == 5) && sequence.SubAnimId == 0)
                        {
                            var bones = new List<object>();
                            foreach (var bone in model.Bones) bones.Add(new { parent = bone.Parent, pivot = bone.Pivot,
                                translation = Track(bone.Translation, model.GlobalSequences), rotation = Track(bone.Rotation, model.GlobalSequences), scale = Track(bone.Scale, model.GlobalSequences) });
                            poses.Add(new { modelFileId = modelId, animationId = sequence.AnimId, durationMs = sequence.Length, bones });
                        }
                        clips++;
                    }
                    Require(clips == 151, "151-clip selection changed");
                    log("PASS Orc " + modelId + ": 151 preview sequences match their native skeleton tracks, aliases, globals and external files");
                }
                File.WriteAllText(Path.Combine(folder, "preview-tracks.json"), JsonSerializer.Serialize(poses, new JsonSerializerOptions { IncludeFields = true }));
                log("PASS Orc preview corpus: " + checks + " bone tracks; six Stand/Walk/Run pose sources written locally");
                return 0;
            }
            catch (Exception e) { log("FAIL Orc preview corpus: " + e); return 1; }
        }
    }
}
