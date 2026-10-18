// WmvTerrainSessionDoodads.cs
//
// DOODADS (protocol 17, docs/world-viewer/07-doodads.md): the half of WmvTerrainSession that places a world load's M2
// doodads. A loadWorld that carries the doodad budgets (maxM2ModelsPerLoad > 0) places them once its world models are
// settled -- every world-model placement placed, failed, unsupported or not requested:
//
//   discover   the tile's obj0 MDDF records (kept from the world plan's obj0 parse), one doodad per uniqueId, in the Tile
//              frame; and every placed world model's active doodad sets -- set 0 plus the set its MODF record names -- each
//              MODD record composed with the placement in double (DoodadScene.cs). The instance budget
//              (maxDoodadInstancesPerLoad) and the models budget (maxM2ModelsPerLoad) apply in that order; a record that
//              names no FileDataID is "unsupported". The camera is placed now (IWmvWorldModelView.WorldModelsSettled): it
//              stands on the world models' floors, never on a doodad, and nothing re-frames as doodads appear.
//   fetch      per unique M2: m2Model (MD21 with an MD20 payload) -> M2Parser.Parse on the worker -> its first skin profile
//              (SFID[0], m2Skin) -> M2SkinParser.Parse on the worker -> the textures its batches bind
//              (WmvModelBuilder.BatchTextureSlots), m2Texture each once per load, decoded on the worker; at most
//              MaxDoodadOutstanding requests out (a parse or decode counts until it is done) and MaxActiveDoodadModels models
//              fetched and not yet built (their parsed data held).
//   build      one static prototype per M2 (IWmvWorldModelView.BuildDoodadPrototype: the Models builder, WmvBuildOptions.Static,
//              the scene's shared textures), on the main thread under a per-frame time budget (at least one a frame); a
//              model past the doodads' GPU budgets is "not requested".
//   place      one instance per doodad under its own per-frame time budget, each on screen the frame it is placed.
//
// Rules (beside the world models', WmvTerrainSessionWorld.cs):
//   - EXACTLY ONE REPORT PER LOAD still: it comes when the doodads have settled too, two frames after the last placement, with
//     their own state ("complete" | "partial" | "none" | "failed" | "ended" | "notOffered") beside the world models' and the
//     whole world's assembly state.
//   - A load without doodad budgets (a protocol-16 host) loads exactly as in Stage 06; its report says "notOffered".
//   - THE DOODADS' TIMEOUT: DoodadTimeoutMs after they began, any model not yet built fails ("timed out"), the requests out
//     are retired, and the load is reported with what was placed.
//   - Shared data: one prototype per M2 per load, every doodad an instance of it; a texture is fetched and decoded once per
//     load, its pixels released once the last model waiting for it is built (its file's bytes kept to decode it again for a
//     later model: the host serves an id once per load), and uploaded once per scene (WmvDoodadTextures).
//   - The scene owns its doodads: disposed before its world models, with its tiles (DisposeWorldScene).

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using Wmv.Wow;
using Wmv.Wow.World;

public sealed partial class WmvTerrainSession
{
    public const string RoleM2Model = "m2Model", RoleM2Skin = "m2Skin", RoleM2Texture = "m2Texture";
    /// <summary>A world load's doodad requests out at once (a parse or decode counts until done), beside the tile's and the
    /// world models' own.</summary>
    public const int MaxDoodadOutstanding = 4;
    /// <summary>Doodad models fetched and not yet built at once: their parsed model, skin and textures are held.</summary>
    public const int MaxActiveDoodadModels = 8;
    /// <summary>The doodads' own timeout, from the moment they began (the world models settled).</summary>
    public const double DoodadTimeoutMs = 180000.0;
    /// <summary>Main-thread time a frame gives prototype builds (at least one is built a frame) and placements (at least one).
    /// Fields, not constants, only so the self-test can set 0 and step them one a frame.</summary>
    public static double DoodadBuildBudgetMs = 6.0, DoodadPlaceBudgetMs = 4.0;
    /// <summary>The doodads' GPU budgets (estimated before a prototype is built; past them a model is not requested).</summary>
    public const long DoodadGpuTextureBudget = 1L << 30, DoodadGpuGeometryBudget = 256L << 20;
    /// <summary>terrainTileLoaded lists at most this many doodad sets and doodad models (the rest are counted).</summary>
    public const int ReportDoodadSetsMax = 64, ReportDoodadModelsMax = 128;
    /// <summary>A change of the doodads' phase alone is sent in worldProgress at most this often (their phase can change every
    /// frame: building, placing, requesting).</summary>
    public const double DoodadPhaseIntervalMs = 100.0;

    // Borrowed (streaming): waiting for the prototype another tile of the session is fetching.
    enum DoodadModelState { Queued, Model, Skin, Textures, Ready, Built, Failed, NotRequested, Unsupported, Borrowed }

    static string DoodadModelStateName(DoodadModelState s)
    {
        switch (s)
        {
            case DoodadModelState.Queued: return "queued";
            case DoodadModelState.Model: return "model";
            case DoodadModelState.Skin: return "skin";
            case DoodadModelState.Textures: return "textures";
            case DoodadModelState.Ready: return "ready";
            case DoodadModelState.Built: return "built";
            case DoodadModelState.Failed: return "failed";
            case DoodadModelState.Unsupported: return "unsupported";
            case DoodadModelState.Borrowed: return "waiting";
            default: return "notRequested";
        }
    }

    /// <summary>One unique M2 of a load's doodads: its doodads, its files and its prototype.</summary>
    sealed class DoodadModel
    {
        public uint FileDataID;
        public DoodadModelState State = DoodadModelState.Queued;
        public string Reason = "";
        public readonly List<DoodadWork> Instances = new List<DoodadWork>();
        public M2ParsedModel Parsed;
        public M2ParsedSkin Skin;
        public int SkinFileDataID;
        public bool SkinAsked;
        public readonly Dictionary<int, uint> SlotFiles = new Dictionary<int, uint>();   // slot -> texture FileDataID
        public readonly List<uint> TextureIds = new List<uint>();
        public readonly HashSet<uint> TexturesWaiting = new HashSet<uint>();
        public bool Active;                    // counted against MaxActiveDoodadModels
        public long Bytes;
        public string Partial;                 // why some of it is drawn without its texture, or null
        public WmvDoodadPrototype Built;
        public int Placed;
        // Streaming (WmvTerrainSessionStream.cs): as a world model's (Model).
        public Job Job;
        public ProtoEntry Shared;
        public bool Claimed, Unpinned;
    }

    /// <summary>A doodad of the plan and what became of it.</summary>
    sealed class DoodadWork
    {
        public WorldDoodad D;
        public DoodadModel Model;
        public string State = "planned";       // "planned" | "placed" | "failed" | "unsupported" | "notRequested" | "ended"
        public string Reason = "";
        public WmvDoodadInstance Instance;
        public bool Shared;                    // streaming: placed for another tile of the session, found here
    }

    /// <summary>A texture of the load's doodad models: fetched and decoded once, shared by every model that binds it.</summary>
    sealed class DoodadTexture
    {
        public uint Id;
        public BlpImage Image;
        public string Error;
        public bool Asked, Done, Decoded, Decoding;
        public int Users;                      // models not yet built that bind it
        public byte[] Raw;                     // the file's bytes, checked: decoded again for a model after the pixels went
        public bool ForTheLoad;                // streaming: missing for this tile's load only (refused, stale, past
                                               // maxM2TexturesPerLoad)
        public readonly List<DoodadModel> Waiting = new List<DoodadModel>();
    }

    sealed class DoodadsWork
    {
        public bool Offered, Started, Done, TimedOut;
        public string State = "", Reason = "";
        public DoodadPlacement[] Mddf = new DoodadPlacement[0];
        public bool HaveMddf;
        public int MddfRecords, MddfPlacements, WmoDoodadRecords, WmoPlacementsWithDoodads;
        public readonly List<WmoDoodadSelection> Sets = new List<WmoDoodadSelection>();
        public readonly List<DoodadWork> Instances = new List<DoodadWork>();
        public readonly List<DoodadModel> Models = new List<DoodadModel>();
        public readonly Dictionary<uint, DoodadTexture> Textures = new Dictionary<uint, DoodadTexture>();
        public readonly Queue<uint> TextureQueue = new Queue<uint>();
        public readonly Queue<DoodadTexture> RedecodeQueue = new Queue<DoodadTexture>();
        public readonly Queue<DoodadModel> BuildQueue = new Queue<DoodadModel>();
        public readonly Queue<DoodadWork> PlaceQueue = new Queue<DoodadWork>();
        public int Pending, Preparing;
        public int FilesAsked, FilesAnswered, ModelsAsked, SkinsAsked, TexturesAsked, TexturesNotRequested, TexturesDecoded,
                   TexturesMissing, TexturesRedecoded;
        public long FileBytes, TextureBytes, PixelBytesHeld, PixelBytesHeldMax;
        public long GpuGeometryPlanned;        // the prototypes built so far (estimated before each build)
        public int Placed;
        public double BeganMs = -1, DiscoverMs = -1, FirstVisibleMs = -1, FirstAskMs = -1, LastAnswerMs = -1, ReadyMs = -1;
        public double ParseCpuMs, DecodeCpuMs, BuildMs, BuildMaxMs, PlaceMs, PlaceMaxMs;
        public double TimeoutStartedAtMs = double.NaN;
        public long LastPlaceFrame;
        public int Out { get { return Pending + Preparing; } }
    }

    // ---------------------------------------------------------------- discovery

    /// <summary>The world models are settled (CheckWorldDone): the camera's moment, then the doodads' plan.</summary>
    void StartDoodads(Job j)
    {
        WorldWork w = j.World;
        DoodadsWork d = w.Doodads;
        if (d == null || d.Started)
            return;
        d.Started = true;
        d.BeganMs = j.Clock.Elapsed.TotalMilliseconds;
        if (OwnsScene(j))
        {
            try { worldView.WorldModelsSettled(world, j.Cmd.Load); }
            catch (Exception e) { Log("load {0}: placing the camera at the world models' settle failed: {1}", j.Cmd.Load, e.Message); }
        }
        if (!d.Offered)
        {
            d.Done = true;
            d.State = "notOffered";
            d.Reason = "the host did not offer doodads (it speaks protocol 16 or less)";
            Log("load {0}: doodads not offered: {1}", j.Cmd.Load, d.Reason);
            return;
        }
        if (!OwnsScene(j))
        {
            d.Done = true;
            d.State = "none";
            d.Reason = "the load has no world scene on screen";
            return;
        }
        WmvIpcClient.TerrainTileCommand cmd = j.Cmd;
        var sw = Stopwatch.StartNew();
        var all = new List<WorldDoodad>();
        if (!w.Global && d.HaveMddf)
        {
            var notes = new List<string>();
            List<WorldDoodad> mddf = DoodadPlan.FromMddf(d.Mddf, PlacementFrame.Tile, notes);
            d.MddfRecords = d.Mddf.Length;
            d.MddfPlacements = mddf.Count;
            all.AddRange(mddf);
            foreach (string n in notes)
                Log("load {0}: doodad plan note: {1}", cmd.Load, n);
        }
        int wmoWithoutRoot = 0, missingGroupDoodads = 0;
        for (int i = 0; i < w.Placements.Count; i++)
        {
            PlacementWork pw = w.Placements[i];
            if (pw.State != "placed" || pw.Instance == null || pw.Instance.Prototype == null)
                continue;
            WmoRoot root = pw.Instance.Prototype.Root;
            if (root == null)
            {
                wmoWithoutRoot++;
                continue;
            }
            WmoDoodadSelection sel = WmvTerrainDebug.AllDoodadSets ? DoodadPlan.AllSets(root, pw.P.UniqueId, pw.P.FileDataID, pw.P.Record.Word58)
                                                                   : DoodadPlan.SelectSets(root, pw.P.UniqueId, pw.P.FileDataID, pw.P.Record.Word58);
            d.Sets.Add(sel);
            List<WorldDoodad> list = DoodadPlan.FromWmo(pw.P, i, root, sel);
            // A world model built without some of its group files: the doodads no group that was read lists are not placed.
            WmoGroup[] groups = pw.Instance.Prototype.Groups;
            if (Array.IndexOf(groups, null) >= 0)
                missingGroupDoodads += DoodadPlan.MarkMissingGroups(list, DoodadPlan.GroupMembership(root, groups), groups);
            if (list.Count > 0)
                d.WmoPlacementsWithDoodads++;
            d.WmoDoodadRecords += list.Count;
            all.AddRange(list);
        }
        d.DiscoverMs = sw.Elapsed.TotalMilliseconds;
        if (WmvTerrainDebug.AllDoodadSets)
            d.Reason = "DIAGNOSTIC: every doodad set of every placed world model drawn (-wmvDoodadSets=all), not set 0 plus the placement's";
        int allowed = cmd.MaxDoodadInstancesPerLoad > 0 ? cmd.MaxDoodadInstancesPerLoad : int.MaxValue;
        int modelsAllowed = cmd.MaxM2ModelsPerLoad;
        var byId = new Dictionary<uint, DoodadModel>();
        int planned = 0, unsupported = 0, notRequested = 0;
        foreach (WorldDoodad dd in all)
        {
            var dw = new DoodadWork { D = dd };
            d.Instances.Add(dw);
            if (dd.Unsupported != null)
            {
                dw.State = "unsupported";
                dw.Reason = dd.Unsupported;
                unsupported++;
                continue;
            }
            if (planned >= allowed)
            {
                dw.State = "notRequested";
                dw.Reason = "this load places at most " + allowed + " doodads (maxDoodadInstancesPerLoad)";
                notRequested++;
                continue;
            }
            DoodadModel m;
            if (!byId.TryGetValue(dd.FileDataID, out m))
            {
                if (byId.Count >= modelsAllowed)
                {
                    dw.State = "notRequested";
                    dw.Reason = "this load asks for at most " + modelsAllowed + " doodad models (maxM2ModelsPerLoad)";
                    notRequested++;
                    continue;
                }
                m = new DoodadModel { FileDataID = dd.FileDataID, Job = j };
                byId[dd.FileDataID] = m;
                d.Models.Add(m);
            }
            m.Instances.Add(dw);
            dw.Model = m;
            planned++;
        }
        Log("load {0}: doodad plan in {1:0.###} ms: {2} MDDF record(s) -> {3} doodad(s) by uniqueId; {4} world-model placement(s) with doodads -> " +
            "{5} MODD record(s) in their active sets{6}{7}; {8} doodad(s) to place over {9} unique M2(s), {10} unsupported, {11} not requested",
            cmd.Load, d.DiscoverMs, d.MddfRecords, d.MddfPlacements, d.WmoPlacementsWithDoodads, d.WmoDoodadRecords,
            wmoWithoutRoot > 0 ? " (" + wmoWithoutRoot + " placed world model(s) without their root data)" : "",
            missingGroupDoodads > 0 ? " (" + missingGroupDoodads + " of them in groups whose files could not be read)" : "", planned, d.Models.Count,
            unsupported, notRequested);
        foreach (WmoDoodadSelection s in d.Sets)
            if (s.SetCount > 1 || s.Reason != null)
                Log("load {0}:   WMO {1} uniqueId {2}: placement set {3} of {4} -> sets [{5}] ({6}), {7} record(s){8}", cmd.Load, s.WmoFileDataID,
                    s.UniqueId, s.Requested, s.SetCount, string.Join(", ", s.Active), string.Join(", ", s.Names), s.Records,
                    s.Reason != null ? " -- " + s.Reason : "");
        AdvanceDoodads(j);
    }

    // ---------------------------------------------------------------- fetching

    /// <summary>The doodads' next requests: textures decoded again and textures first, then skins, then the next models --
    /// within MaxDoodadOutstanding and MaxActiveDoodadModels; then whether the doodads are done.</summary>
    void AdvanceDoodads(Job j)
    {
        WorldWork w = j.World;
        DoodadsWork d = w != null ? w.Doodads : null;
        if (d == null || !d.Started || d.Done || d.TimedOut || !Live(j) || j.Dead)
            return;
        // Streaming: the session's prototypes first (WmvTerrainSessionStream.cs).
        if (j.Stream != null && stream != null)
            ClaimDoodadModels(j);
        while (d.Out < MaxDoodadOutstanding && StreamCredit(j))
        {
            if (d.RedecodeQueue.Count > 0)
            {
                RedecodeDoodadTexture(j, d.RedecodeQueue.Dequeue());
                continue;
            }
            if (d.TextureQueue.Count > 0)
            {
                AskDoodad(j, RoleM2Texture, (int)d.TextureQueue.Dequeue(), null);
                continue;
            }
            DoodadModel next = null;
            foreach (DoodadModel m in d.Models)
                if (m.State == DoodadModelState.Skin && !m.SkinAsked)
                {
                    next = m;
                    break;
                }
            if (next != null)
            {
                next.SkinAsked = true;
                d.SkinsAsked++;
                AskDoodad(j, RoleM2Skin, next.SkinFileDataID, next);
                continue;
            }
            int active = 0;
            foreach (DoodadModel m in d.Models)
                if (m.Active) active++;
            if (active >= MaxActiveDoodadModels)
                break;
            foreach (DoodadModel m in d.Models)
                if (m.State == DoodadModelState.Queued)
                {
                    next = m;
                    break;
                }
            if (next == null)
                break;
            next.Active = true;
            next.State = DoodadModelState.Model;
            d.ModelsAsked++;
            AskDoodad(j, RoleM2Model, (int)next.FileDataID, next);
        }
        CheckDoodadsDone(j);
    }

    void AskDoodad(Job j, string role, int fileDataID, DoodadModel m)
    {
        DoodadsWork d = j.World.Doodads;
        double at = j.Clock.Elapsed.TotalMilliseconds;
        string id = SendMapRequest(j, role, fileDataID);
        j.Pending.Add(new Request { Id = id, Role = role, FileDataID = fileDataID, AskedMs = at, Doodad = true, DModel = m });
        d.Pending++;
        d.FilesAsked++;
        if (d.FirstAskMs < 0)
            d.FirstAskMs = at;
        if (role == RoleM2Texture) d.TexturesAsked++;
        OutstandingPeak = Math.Max(Outstanding, OutstandingPeak);
    }

    void DoodadAnswered(Job j)
    {
        DoodadsWork d = j.World.Doodads;
        d.Pending--;
        d.FilesAnswered++;
        d.LastAnswerMs = j.Clock.Elapsed.TotalMilliseconds;
        // Streaming: the doodads' timeout counts from their last answer.
        if (j.Stream != null)
            d.TimeoutStartedAtMs = double.NaN;
    }

    /// <summary>The kind of a doodad model's bytes: MD21 in reading order, its payload starting MD20 (the host checks the
    /// same before it serves them).</summary>
    static string M2KindProblem(byte[] b)
    {
        if (b == null || b.Length < 12)
            return "not an M2 model: " + (b == null ? 0 : b.Length) + " byte(s)";
        if (b[0] != 'M' || b[1] != 'D' || b[2] != '2' || b[3] != '1')
            return "not an M2 model: it does not start with the MD21 chunk";
        if (b[8] != 'M' || b[9] != 'D' || b[10] != '2' || b[11] != '0')
            return "not an M2 model: its MD21 chunk does not start with MD20";
        return null;
    }

    void OnM2Model(Job j, WmvIpcClient.AssetResponse r, Request q)
    {
        DoodadsWork d = j.World.Doodads;
        DoodadAnswered(j);
        DoodadModel m = q.DModel;
        string why = WorldAnswerProblem(r, "doodad model", q.FileDataID, j.Cmd.MaxM2FileBytes);
        if (why != null)
        {
            if (j.Stream != null && RefusedForTheLoad(r))
                FailForTheLoad(() => FailDoodadModel(j, m, why));
            else
                FailDoodadModel(j, m, why);
            return;
        }
        byte[] bytes = r.data;
        string hostMd5 = r.md5 ?? "";
        d.FileBytes += bytes.Length;
        m.Bytes += bytes.Length;
        d.Preparing++;
        RunWorker(j, () =>
        {
            string md5 = Md5Hex(bytes);
            var sw = Stopwatch.StartNew();
            M2ParsedModel model = null;
            string error = null;
            if (!string.IsNullOrEmpty(hostMd5) && !string.Equals(hostMd5, md5, StringComparison.OrdinalIgnoreCase))
                error = string.Format("the doodad model bytes received hash to {0}, the host served {1}", md5, hostMd5);
            else if ((error = M2KindProblem(bytes)) == null)
            {
                try { model = M2Parser.Parse(bytes); }
                catch (Exception e) { error = "doodad model parse failed: " + e.Message; }
            }
            double ms = sw.Elapsed.TotalMilliseconds;
            return () => OnM2ModelParsed(j, m, model, ms, error);
        });
    }

    void OnM2ModelParsed(Job j, DoodadModel m, M2ParsedModel model, double ms, string error)
    {
        DoodadsWork d = j.World.Doodads;
        if (d.TimedOut)
            return;
        d.Preparing--;
        d.ParseCpuMs += ms;
        if (error != null)
        {
            FailDoodadModel(j, m, error);
            AdvanceDoodads(j);
            return;
        }
        if (model.SkinFileDataIDs.Length == 0 || model.SkinFileDataIDs[0] <= 0)
        {
            SettleDoodadModel(j, m, DoodadModelState.Unsupported, "the M2 names no skin profile (no SFID entry): nothing to draw it with");
            AdvanceDoodads(j);
            return;
        }
        m.Parsed = model;
        m.SkinFileDataID = model.SkinFileDataIDs[0];
        m.State = DoodadModelState.Skin;
        AdvanceDoodads(j);
    }

    void OnM2Skin(Job j, WmvIpcClient.AssetResponse r, Request q)
    {
        DoodadsWork d = j.World.Doodads;
        DoodadAnswered(j);
        DoodadModel m = q.DModel;
        string why = WorldAnswerProblem(r, "doodad skin", q.FileDataID, j.Cmd.MaxM2FileBytes);
        if (why != null)
        {
            if (j.Stream != null && RefusedForTheLoad(r))
                FailForTheLoad(() => FailDoodadModel(j, m, why));
            else
                FailDoodadModel(j, m, why);
            return;
        }
        byte[] bytes = r.data;
        string hostMd5 = r.md5 ?? "";
        d.FileBytes += bytes.Length;
        m.Bytes += bytes.Length;
        d.Preparing++;
        RunWorker(j, () =>
        {
            string md5 = Md5Hex(bytes);
            var sw = Stopwatch.StartNew();
            M2ParsedSkin skin = null;
            string error = null;
            if (!string.IsNullOrEmpty(hostMd5) && !string.Equals(hostMd5, md5, StringComparison.OrdinalIgnoreCase))
                error = string.Format("the doodad skin bytes received hash to {0}, the host served {1}", md5, hostMd5);
            else if (bytes.Length < 4 || bytes[0] != 'S' || bytes[1] != 'K' || bytes[2] != 'I' || bytes[3] != 'N')
                error = "not an M2 skin: it does not start with SKIN";
            else
            {
                try { skin = M2SkinParser.Parse(bytes); }
                catch (Exception e) { error = "doodad skin parse failed: " + e.Message; }
            }
            double ms = sw.Elapsed.TotalMilliseconds;
            return () => OnM2SkinParsed(j, m, skin, ms, error);
        });
    }

    void OnM2SkinParsed(Job j, DoodadModel m, M2ParsedSkin skin, double ms, string error)
    {
        DoodadsWork d = j.World.Doodads;
        if (d.TimedOut)
            return;
        d.Preparing--;
        d.ParseCpuMs += ms;
        if (error != null)
        {
            FailDoodadModel(j, m, error);
            AdvanceDoodads(j);
            return;
        }
        if (skin.Batches.Length == 0)
        {
            // Nothing a static doodad can draw: a model of emitters alone (fire, smoke), which this stage does not draw.
            SettleDoodadModel(j, m, DoodadModelState.Unsupported, m.Parsed.ParticleEmitterCount + m.Parsed.RibbonEmitterCount > 0
                ? string.Format("its skin profile has no draw batches: it draws {0} particle and {1} ribbon emitter(s) only, which doodads do " +
                                "not draw in this stage", m.Parsed.ParticleEmitterCount, m.Parsed.RibbonEmitterCount)
                : "its skin profile has no draw batches and it has no emitter: nothing to draw");
            AdvanceDoodads(j);
            return;
        }
        m.Skin = skin;
        // The textures its batches bind, each fetched once per load; a slot with no FileDataID (a replaceable texture, or one an
        // M2 without TXID names by path) has no source for a doodad: the model is drawn without it, partially supported.
        var replaceable = new List<string>();
        int cap = j.Cmd.MaxM2TexturesPerLoad > 0 ? j.Cmd.MaxM2TexturesPerLoad : int.MaxValue;
        foreach (int slot in WmvModelBuilder.BatchTextureSlots(m.Parsed, skin))
        {
            M2TextureDef def = m.Parsed.Textures[slot];
            if (def.FileDataID <= 0)
            {
                replaceable.Add("slot " + slot + (def.Type != 0 ? " (replaceable, type " + def.Type + ")"
                                                  : !string.IsNullOrEmpty(def.FileName)
                                                      ? " (named by path, " + def.FileName + ": the M2 has no TXID, and map files are asked for by FileDataID)"
                                                      : " (no file)"));
                continue;
            }
            uint id = (uint)def.FileDataID;
            m.SlotFiles[slot] = id;
            if (m.TextureIds.Contains(id))
                continue;
            m.TextureIds.Add(id);
            DoodadTexture t;
            if (!d.Textures.TryGetValue(id, out t))
            {
                t = new DoodadTexture { Id = id };
                d.Textures[id] = t;
                if (d.TexturesAsked + d.TextureQueue.Count >= cap)
                {
                    if (!TakeSessionDoodadImage(j, t))
                    {
                        t.Error = "not requested: this load asks for at most " + cap + " doodad textures (maxM2TexturesPerLoad)";
                        t.Done = true;
                        t.ForTheLoad = j.Stream != null;
                        d.TexturesNotRequested++;
                    }
                }
                else if (ClaimDoodadTexture(j, t))
                {
                    // Streaming: the session's image attached, its failure taken, or another tile's fetch waited for.
                }
                else
                {
                    t.Asked = true;
                    d.TextureQueue.Enqueue(id);
                }
            }
            t.Users++;
            if (!t.Done)
            {
                m.TexturesWaiting.Add(id);
                t.Waiting.Add(m);
            }
            else if (t.Decoded && t.Image == null && t.Raw == null && ReattachDoodadImage(j, t))
            {
                // Streaming: its pixels went with an earlier model of this job; the session keeps its image.
            }
            else if (t.Decoded && t.Image == null && t.Raw != null)
            {
                // Its pixels went with the last model built before this one bound it: decoded again from its bytes, in turn.
                m.TexturesWaiting.Add(id);
                t.Waiting.Add(m);
                if (!t.Decoding)
                {
                    t.Decoding = true;
                    t.Done = false;
                    d.RedecodeQueue.Enqueue(t);
                }
            }
        }
        if (replaceable.Count > 0)
            m.Partial = "drawn without " + string.Join(", ", replaceable.ToArray());
        m.State = DoodadModelState.Textures;
        if (m.TexturesWaiting.Count == 0)
            DoodadModelReady(j, m);
        AdvanceDoodads(j);
    }

    void OnM2Texture(Job j, WmvIpcClient.AssetResponse r, Request q)
    {
        DoodadsWork d = j.World.Doodads;
        DoodadAnswered(j);
        uint id = (uint)q.FileDataID;
        DoodadTexture t;
        if (!d.Textures.TryGetValue(id, out t))
            throw new InvalidOperationException("doodad texture " + id + " was asked for but is not in the load's list");
        string why = WorldAnswerProblem(r, "doodad texture", q.FileDataID, j.Cmd.MaxM2TextureBytes);
        if (why != null)
        {
            t.Error = why;
            if (j.Stream != null && RefusedForTheLoad(r))
                t.ForTheLoad = true;            // this tile's gap, not the file's: a prototype built without it is the tile's own
            DoodadTextureDone(j, t, true);
            // (A refusal for this tile's load, or a stale answer, hands the fetch on; the file's own failure is the session's.)
            PublishDoodadImage(j, t, null, why, !RefusedForTheLoad(r));
            return;
        }
        byte[] bytes = r.data;
        string hostMd5 = r.md5 ?? "";
        d.TextureBytes += bytes.Length;
        d.Preparing++;
        t.Raw = bytes;
        WmvTerrainFormatSupport support = j.Support;
        RunWorker(j, () =>
        {
            string md5 = Md5Hex(bytes);
            var sw = Stopwatch.StartNew();
            BlpImage img = null;
            string error = null;
            if (!string.IsNullOrEmpty(hostMd5) && !string.Equals(hostMd5, md5, StringComparison.OrdinalIgnoreCase))
                error = string.Format("the doodad texture bytes received hash to {0}, the host served {1}", md5, hostMd5);
            else
            {
                // Stage 08: its own DXT blocks when it qualifies, else decoded (BlpBlocks.PrepareForModels).
                try { img = BlpBlocks.PrepareForModels(bytes, support.Supports); }
                catch (Exception e) { error = "decode failed: " + e.Message; }
            }
            double ms = sw.Elapsed.TotalMilliseconds;
            return () =>
            {
                if (d.TimedOut)
                    return;
                d.Preparing--;
                d.DecodeCpuMs += ms;
                t.Image = img;
                t.Error = error;
                t.Decoded = img != null;
                if (img == null)
                    t.Raw = null;
                else
                    HoldPixels(d, img);
                DoodadTextureDone(j, t, true);
                PublishDoodadImage(j, t, img, error, true);
                AdvanceDoodads(j);
            };
        });
    }

    static void HoldPixels(DoodadsWork d, BlpImage img)
    {
        d.PixelBytesHeld += img.HeldBytes;
        d.PixelBytesHeldMax = Math.Max(d.PixelBytesHeldMax, d.PixelBytesHeld);
    }

    void RedecodeDoodadTexture(Job j, DoodadTexture t)
    {
        DoodadsWork d = j.World.Doodads;
        d.Preparing++;
        d.TexturesRedecoded++;
        byte[] bytes = t.Raw;
        WmvTerrainFormatSupport support = j.Support;
        RunWorker(j, () =>
        {
            var sw = Stopwatch.StartNew();
            BlpImage img = null;
            string error = null;
            try { img = BlpBlocks.PrepareForModels(bytes, support.Supports); }
            catch (Exception e) { error = "decode failed: " + e.Message; }
            double ms = sw.Elapsed.TotalMilliseconds;
            return () =>
            {
                if (d.TimedOut)
                    return;
                d.Preparing--;
                d.DecodeCpuMs += ms;
                t.Decoding = false;
                t.Image = img;
                if (img != null)
                    HoldPixels(d, img);
                else
                {
                    t.Error = error;
                    t.Decoded = false;
                }
                DoodadTextureDone(j, t, false);
                AdvanceDoodads(j);
            };
        });
    }

    void DoodadTextureDone(Job j, DoodadTexture t, bool count)
    {
        DoodadsWork d = j.World.Doodads;
        t.Done = true;
        if (count)
        {
            if (t.Decoded) d.TexturesDecoded++;
            else d.TexturesMissing++;
        }
        foreach (DoodadModel m in t.Waiting)
        {
            m.TexturesWaiting.Remove(t.Id);
            if (m.TexturesWaiting.Count == 0 && m.State == DoodadModelState.Textures)
                DoodadModelReady(j, m);
        }
        t.Waiting.Clear();
        if (t.Users <= 0 && t.Image != null)
            ReleaseDoodadPixels(d, t);
    }

    void DoodadModelReady(Job j, DoodadModel m)
    {
        m.State = DoodadModelState.Ready;
        j.World.Doodads.BuildQueue.Enqueue(m);
    }

    void FailDoodadModel(Job j, DoodadModel m, string reason)
    {
        SettleDoodadModel(j, m, DoodadModelState.Failed, reason);
    }

    /// <summary>A model ends without a prototype (failed, unsupported, not requested): its doodads with it, its textures
    /// released.</summary>
    void SettleDoodadModel(Job j, DoodadModel m, DoodadModelState state, string reason)
    {
        if (m.Shared != null)
            DoodadModelSettledShared(j, m, state, reason);
        DoodadsWork d = j.World.Doodads;
        m.State = state;
        m.Reason = reason;
        m.Active = false;
        foreach (DoodadWork dw in m.Instances)
            if (dw.State == "planned")
            {
                dw.State = state == DoodadModelState.NotRequested ? "notRequested" : state == DoodadModelState.Unsupported ? "unsupported" : "failed";
                dw.Reason = reason;
            }
        ReleaseDoodadModelTextures(d, m);
        m.Parsed = null;
        m.Skin = null;
        Log("load {0}: doodad model {1} {2}: {3} ({4} doodad(s))", j.Cmd.Load, m.FileDataID, DoodadModelStateName(state), reason, m.Instances.Count);
    }

    static void ReleaseDoodadModelTextures(DoodadsWork d, DoodadModel m)
    {
        foreach (uint id in m.TextureIds)
        {
            DoodadTexture t;
            if (!d.Textures.TryGetValue(id, out t))
                continue;
            t.Waiting.Remove(m);
            if (--t.Users <= 0 && t.Image != null)
                ReleaseDoodadPixels(d, t);
        }
        m.TextureIds.Clear();
        m.TexturesWaiting.Clear();
    }

    static void ReleaseDoodadPixels(DoodadsWork d, DoodadTexture t)
    {
        if (t.Image != null)
            d.PixelBytesHeld -= t.Image.HeldBytes;     // (what HoldPixels added: an image of DXT blocks too)
        t.Image = null;
    }

    // ---------------------------------------------------------------- frames: building and placing

    /// <summary>One frame of the doodads: their timeout, prototype builds and placements under their time budgets, the next
    /// requests, whether they are done.</summary>
    void PumpDoodads(Job j, double nowMs)
    {
        DoodadsWork d = j.World.Doodads;
        if (double.IsNaN(d.TimeoutStartedAtMs))
            d.TimeoutStartedAtMs = nowMs;
        if (!d.TimedOut && nowMs - d.TimeoutStartedAtMs > DoodadTimeoutMs)
            TimeOutDoodads(j);
        var clock = Stopwatch.StartNew();
        int built = 0;
        bool streaming = j.Stream != null && stream != null;
        // Streaming: the session's one frame budget for every tile (the nearest first), not one per tile.
        while (d.BuildQueue.Count > 0 && (streaming ? StreamMayWork() : (built == 0 || clock.Elapsed.TotalMilliseconds < DoodadBuildBudgetMs)))
        {
            BuildDoodadModel(j, d.BuildQueue.Dequeue());
            built++;
            StreamWorked();
            if (!Live(j))
                return;
        }
        if (d.PlaceQueue.Count > 0 && (!streaming || StreamMayWork()))
        {
            var place = Stopwatch.StartNew();
            int placed = 0;
            while (d.PlaceQueue.Count > 0 && (streaming ? (placed == 0 || StreamMayWork()) : (placed == 0 || place.Elapsed.TotalMilliseconds < DoodadPlaceBudgetMs)))
            {
                PlaceDoodad(j, d.PlaceQueue.Dequeue());
                placed++;
                StreamWorked();
            }
            double ms = place.Elapsed.TotalMilliseconds;
            d.PlaceMs += ms;
            d.PlaceMaxMs = Math.Max(d.PlaceMaxMs, ms);
            d.LastPlaceFrame = frame;
        }
        AdvanceDoodads(j);
        CheckDoodadsDone(j);
    }

    void BuildDoodadModel(Job j, DoodadModel m)
    {
        DoodadsWork d = j.World.Doodads;
        if (m.Built != null && m.Shared != null)
        {
            // Streaming: the session's prototype (built for another tile, or pooled): its doodads placed from it, never built again.
            m.State = DoodadModelState.Built;
            m.Active = false;
            foreach (DoodadWork dw in m.Instances)
                if (dw.State == "planned")
                    d.PlaceQueue.Enqueue(dw);
            return;
        }
        var bySlot = new Dictionary<int, BlpImage>();
        var missing = new List<string>();
        // The texture budget against what the scene's cache has uploaded (every alpha and wrap variant counted), plus one upload
        // of each file this model is the first to bind.
        WmvDoodadTextures cache = world.DoodadTextures;
        long texturesUploaded = cache != null ? cache.GpuBytes : 0;
        long textureEstimate = 0;
        var newTextures = new List<uint>();
        var forTheLoad = new List<string>();     // streaming: what of it this tile's load could not get
        foreach (KeyValuePair<int, uint> kv in m.SlotFiles)
        {
            DoodadTexture t;
            if (d.Textures.TryGetValue(kv.Value, out t) && t.Image != null)
            {
                bySlot[kv.Key] = t.Image;
                if ((cache == null || !cache.HasFile(kv.Value)) && !newTextures.Contains(kv.Value))
                {
                    newTextures.Add(kv.Value);
                    textureEstimate += BlpBlocks.UploadBytes(t.Image);   // exact: the blocks, or RGBA32 with its chain
                }
            }
            else
            {
                missing.Add("texture " + kv.Value + (t != null && t.Error != null ? " (" + t.Error + ")" : ""));
                if (t != null && t.ForTheLoad && !t.Decoded && !forTheLoad.Contains("texture " + kv.Value))
                    forTheLoad.Add("texture " + kv.Value);
            }
        }
        if (missing.Count > 0)
            m.Partial = (m.Partial != null ? m.Partial + "; " : "") + "drawn without " + string.Join(", ", missing.ToArray());
        // The GPU budgets, estimated before anything is uploaded: the vertices (40 bytes each) and indices, and the textures
        // this model is the first to bind.
        long indices = 0;
        foreach (M2Submesh s in m.Skin.Submeshes)
            indices += s.IndexCount;
        long geometryEstimate = (long)m.Parsed.Vertices.Length * 40L + indices * (m.Parsed.Vertices.Length > 65000 ? 4L : 2L);
        long geometryUsed = d.GpuGeometryPlanned, geometryBudget = DoodadGpuGeometryBudget, textureBudget = DoodadGpuTextureBudget;
        if (j.Stream != null && stream != null)
        {
            // Streaming: the scene's budgets, against everything on the GPU now (terrain, world models, doodads).
            SceneGpuBytes(out texturesUploaded, out geometryUsed);
            geometryBudget = StreamGpuGeometryBudget;
            textureBudget = StreamGpuTextureBudget;
        }
        if (geometryUsed + geometryEstimate > geometryBudget || texturesUploaded + textureEstimate > textureBudget)
        {
            bool geometry = geometryUsed + geometryEstimate > geometryBudget;
            SettleDoodadModel(j, m, DoodadModelState.NotRequested, string.Format(
                "fetched, but not built: the doodads' GPU {0} budget ({1} MB; {2} MB used, this model {3} MB)", geometry ? "geometry" : "texture",
                (geometry ? geometryBudget : textureBudget) >> 20, (geometry ? geometryUsed : texturesUploaded) >> 20,
                (geometry ? geometryEstimate : textureEstimate) >> 20));
            return;
        }
        var clock = Stopwatch.StartNew();
        WmvDoodadPrototype proto;
        try { proto = worldView.BuildDoodadPrototype(world, m.FileDataID, m.Parsed, m.Skin, bySlot, m.SlotFiles); }
        catch (Exception e)
        {
            FailDoodadModel(j, m, "building the doodad model failed: " + e.GetType().Name + ": " + e.Message);
            return;
        }
        double buildMs = clock.Elapsed.TotalMilliseconds;
        d.BuildMs += buildMs;
        d.BuildMaxMs = Math.Max(d.BuildMaxMs, buildMs);
        d.GpuGeometryPlanned += geometryEstimate;
        m.Built = proto;
        m.State = DoodadModelState.Built;
        m.Active = false;
        if (forTheLoad.Count > 0 && m.Shared != null && j.Stream != null && stream != null)
        {
            string why = "built without " + string.Join(", ", forTheLoad.ToArray()) + ", which tile " + j.Stream.Name +
                         "'s load could not get";
            m.Partial = (m.Partial != null ? m.Partial + "; " : "") + "kept for this tile: " + why;
            KeepDoodadModelForTheTile(j, m, proto, why);
        }
        else
        {
            world.DoodadPrototypes[m.FileDataID] = proto;
            if (m.Shared != null)
                DoodadPrototypeBuilt(m, proto);
        }
        ReleaseDoodadModelTextures(d, m);
        m.Parsed = null;
        m.Skin = null;
        foreach (DoodadWork dw in m.Instances)
            if (dw.State == "planned")
                d.PlaceQueue.Enqueue(dw);
    }

    void PlaceDoodad(Job j, DoodadWork dw)
    {
        DoodadsWork d = j.World.Doodads;
        if (dw.State != "planned" || dw.Model == null || dw.Model.Built == null || dw.Model.Built.Disposed)
            return;
        // Streaming: a doodad another tile of the session placed is found, never placed twice -- one placed from a tile's private
        // prototype is replaced by this one when this one is complete (WmvTerrainSessionStream.cs).
        DoodadEntry replace = null;
        if (j.Stream != null && stream != null && FindSharedDoodad(j, dw, !IsPrivate(dw.Model.Shared), out replace))
        {
            dw.Model.Placed++;
            d.Placed++;
            return;
        }
        try
        {
            dw.Instance = worldView.PlaceDoodad(dw.Model.Built, dw.D, world.Origin, j.Cmd.Load);
            world.Doodads.Add(dw.Instance);
            dw.State = "placed";
            if (j.Stream != null && stream != null)
                RegisterDoodad(j, dw, replace);
            dw.Model.Placed++;
            d.Placed++;
            if (d.FirstVisibleMs < 0)
                d.FirstVisibleMs = j.Clock.Elapsed.TotalMilliseconds - d.BeganMs;
        }
        catch (Exception e)
        {
            dw.State = "failed";
            dw.Reason = "placing it failed: " + e.GetType().Name + ": " + e.Message;
        }
    }

    void TimeOutDoodads(Job j)
    {
        DoodadsWork d = j.World.Doodads;
        string why = "timed out after " + (DoodadTimeoutMs / 1000.0).ToString("0.###", System.Globalization.CultureInfo.InvariantCulture) +
                     " s of the doodads (" + DoodadsWaitingFor(d) + ")";
        var keep = new List<Request>();
        foreach (Request q in j.Pending)
        {
            if (q.Doodad)
                RetireRequest(j, q, "the doodads timed out");
            else
                keep.Add(q);
        }
        j.Pending.Clear();
        j.Pending.AddRange(keep);
        d.Pending = 0;
        d.TextureQueue.Clear();
        d.RedecodeQueue.Clear();
        d.BuildQueue.Clear();
        bool wasTransient = streamTransientFailure;
        streamTransientFailure = true;          // (streaming: a fetch that timed out is handed on, not failed for every tile)
        try
        {
            foreach (DoodadModel m in d.Models)
                if (m.State != DoodadModelState.Built && m.State != DoodadModelState.Failed && m.State != DoodadModelState.NotRequested &&
                    m.State != DoodadModelState.Unsupported)
                    FailDoodadModel(j, m, why);
        }
        finally { streamTransientFailure = wasTransient; }
        // Worker results still out are dropped when they come (TimedOut); the models are settled. Placements of the models
        // already built go on (PlaceQueue).
        d.TimedOut = true;
        d.Preparing = 0;
        if (j.Stream != null)
            ReleaseJobImages(j);
        Log("load {0}: doodads {1}", j.Cmd.Load, why);
        CheckDoodadsDone(j);
    }

    void CheckDoodadsDone(Job j)
    {
        WorldWork w = j.World;
        DoodadsWork d = w != null ? w.Doodads : null;
        if (d == null || d.Done || !d.Started)
            return;
        if (d.Out > 0 || d.BuildQueue.Count > 0 || d.PlaceQueue.Count > 0 || d.TextureQueue.Count > 0 || d.RedecodeQueue.Count > 0)
            return;
        foreach (DoodadModel m in d.Models)
            if (m.State != DoodadModelState.Built && m.State != DoodadModelState.Failed && m.State != DoodadModelState.NotRequested &&
                m.State != DoodadModelState.Unsupported)
                return;
        d.Done = true;
        d.ReadyMs = j.Clock.Elapsed.TotalMilliseconds - d.BeganMs;
        // Streaming: every doodad of the tile placed: its models let go of their pins (their instances hold the prototypes).
        if (j.Stream != null && stream != null)
            foreach (DoodadModel m in d.Models)
                if (m.Shared != null && !m.Unpinned)
                {
                    m.Unpinned = true;
                    Unpin(m.Shared);
                }
        if (d.LastPlaceFrame == 0)
            d.LastPlaceFrame = frame;
        d.State = DoodadStateName(d);
        // Everything is uploaded: the decoded pixels and the files' bytes go.
        foreach (DoodadTexture t in d.Textures.Values)
        {
            ReleaseDoodadPixels(d, t);
            t.Raw = null;
        }
        SendProgress(j, true);
        Log("load {0}: the doodads are settled ({1}) after {2:0.#} ms: {3} of {4} placed over {5} prototype(s); decoded texture pixels held at most " +
            "{6:0.0} MB; {7} texture(s) decoded, {8} decoded again for a later model; reported after frame {9}", j.Cmd.Load, d.State, d.ReadyMs, d.Placed,
            d.Instances.Count, world != null ? world.DoodadPrototypes.Count + world.PrivateDoodadPrototypes.Count : 0,
            d.PixelBytesHeldMax / 1048576.0, d.TexturesDecoded, d.TexturesRedecoded, Math.Max(w.LastCommitFrame, d.LastPlaceFrame) + 2);
    }

    /// <summary>The load ended (any report, a timeout, Dispose): the doodads' queues go, every texture's pixels and bytes
    /// with them; a doodad not yet placed is "ended".</summary>
    void EndDoodads(Job j, string ended)
    {
        DoodadsWork d = j.World != null ? j.World.Doodads : null;
        if (d == null)
            return;
        d.Pending = 0;
        d.TextureQueue.Clear();
        d.RedecodeQueue.Clear();
        d.BuildQueue.Clear();
        d.PlaceQueue.Clear();
        foreach (DoodadTexture t in d.Textures.Values)
        {
            ReleaseDoodadPixels(d, t);
            t.Raw = null;
        }
        foreach (DoodadWork dw in d.Instances)
            if (dw.State == "planned")
            {
                dw.State = "ended";
                dw.Reason = "the load ended (" + ended + ") before it was placed";
            }
        foreach (DoodadModel m in d.Models)
        {
            m.Parsed = null;
            m.Skin = null;
        }
        if (d.Offered && !d.Started)
        {
            d.State = "ended";
            d.Reason = "the load ended (" + ended + ") before the world models were settled";
        }
    }

    // ---------------------------------------------------------------- progress and the report

    /// <summary>The world is settled: the world models, and the doodads when the load places them.</summary>
    static bool WorldSettled(WorldWork w)
    {
        return w.Done && (w.Doodads == null || w.Doodads.Done);
    }

    /// <summary>The last frame the world's scene changed (a world model or a doodad placed).</summary>
    static long WorldLastFrame(WorldWork w)
    {
        return w.Doodads != null ? Math.Max(w.LastCommitFrame, w.Doodads.LastPlaceFrame) : w.LastCommitFrame;
    }

    static string DoodadStateName(DoodadsWork d)
    {
        if (!d.Offered)
            return "notOffered";
        int placed = 0, ended = 0, partial = 0;
        foreach (DoodadWork dw in d.Instances)
        {
            if (dw.State == "placed") placed++;
            else if (dw.State == "ended" || dw.State == "planned") ended++;
        }
        foreach (DoodadModel m in d.Models)
            if (m.State == DoodadModelState.Built && m.Partial != null) partial++;
        if (ended > 0 || (!d.Started && !d.Done))
            return "ended";
        if (d.Instances.Count == 0)
            return "none";
        if (placed == d.Instances.Count && partial == 0)
            return "complete";
        if (placed == 0)
            return "failed";
        return "partial";
    }

    /// <summary>The doodads' phase as of this frame: building while prototypes wait to be built, placing while doodads wait
    /// to be placed or when this frame placed some, requesting otherwise.</summary>
    string DoodadPhase(DoodadsWork d)
    {
        if (d == null)
            return "none";
        if (!d.Offered)
            return "notOffered";
        if (!d.Started)
            return "waiting";
        if (d.Done)
            return d.State;
        if (d.BuildQueue.Count > 0)
            return "building";
        if (d.PlaceQueue.Count > 0 || d.LastPlaceFrame == frame)
            return "placing";
        return "requesting";
    }

    /// <summary>The whole world: "complete" only when the world models and the doodads are complete (or there are none) and
    /// the doodads were offered; "partial" when anything was placed but something was not; "none" when nothing was placed.</summary>
    static string AssemblyState(string worldState, string doodadState, bool anyPlaced)
    {
        if (worldState == "ended" || doodadState == "ended")
            return "ended";
        bool worldOk = worldState == "complete" || worldState == "none";
        bool doodadsOk = doodadState == "complete" || doodadState == "none";
        if (worldOk && doodadsOk)
            return worldState == "none" && doodadState == "none" ? "none" : "complete";
        if (anyPlaced)
            return "partial";
        if (worldState == "failed" || doodadState == "failed")
            return "failed";
        return "none";
    }

    static string DoodadsWaitingFor(DoodadsWork d)
    {
        var sb = new StringBuilder();
        int waiting = 0;
        foreach (DoodadModel m in d.Models)
            if (m.State != DoodadModelState.Built && m.State != DoodadModelState.Failed && m.State != DoodadModelState.NotRequested &&
                m.State != DoodadModelState.Unsupported)
            {
                if (waiting++ < 4)
                    sb.Append(sb.Length > 0 ? ", " : "").Append("M2 ").Append(m.FileDataID).Append(" (").Append(DoodadModelStateName(m.State))
                      .Append(m.State == DoodadModelState.Textures ? string.Format(", {0} texture(s) to come", m.TexturesWaiting.Count) : "").Append(')');
            }
        return string.Format("{0} doodad model(s) not built{1}; {2} request(s) out, {3} on the worker, {4} to place", waiting,
                             sb.Length > 0 ? ": " + sb + (waiting > 4 ? ", ..." : "") : "", d.Pending, d.Preparing, d.PlaceQueue.Count);
    }

    void FillDoodadProgress(WorldWork w, WmvIpcClient.WorldProgressReport r)
    {
        DoodadsWork d = w.Doodads;
        r.HasDoodads = true;
        r.DoodadPhase = DoodadPhase(d);
        if (d == null || !d.Offered)
        {
            r.DoodadInstances = r.DoodadPlaced = r.DoodadModels = r.DoodadModelsBuilt = r.DoodadModelsFailed = -1;
            r.DoodadFilesAsked = r.DoodadFilesAnswered = -1;
            r.DoodadBytes = -1;
            return;
        }
        r.DoodadInstances = d.Instances.Count;
        r.DoodadPlaced = d.Placed;
        r.DoodadModels = d.Models.Count;
        foreach (DoodadModel m in d.Models)
        {
            if (m.State == DoodadModelState.Built) r.DoodadModelsBuilt++;
            else if (m.State == DoodadModelState.Failed) r.DoodadModelsFailed++;
        }
        r.DoodadFilesAsked = d.FilesAsked;
        r.DoodadFilesAnswered = d.FilesAnswered;
        r.DoodadBytes = d.FileBytes + d.TextureBytes;
    }

    void FillDoodadReport(Job j, WmvIpcClient.TerrainTileReport r)
    {
        WorldWork w = j.World;
        DoodadsWork d = w.Doodads;
        r.HasDoodads = true;
        if (d == null)
            return;
        if (!d.Started && !d.Done && d.Offered)
        {
            d.State = "ended";
            if (string.IsNullOrEmpty(d.Reason))
                d.Reason = "the load ended before the world models were settled";
        }
        r.DoodadState = d.Done || !d.Offered ? (d.State.Length > 0 ? d.State : DoodadStateName(d)) : DoodadStateName(d);
        r.DoodadReason = d.Reason ?? "";
        r.DoodadMddfRecords = d.MddfRecords;
        r.DoodadMddfPlacements = d.MddfPlacements;
        r.DoodadWmoRecords = d.WmoDoodadRecords;
        r.DoodadWmoPlacements = d.WmoPlacementsWithDoodads;
        for (int i = 0; i < d.Sets.Count && i < ReportDoodadSetsMax; i++)
        {
            WmoDoodadSelection s = d.Sets[i];
            r.DoodadSets.Add(new WmvIpcClient.DoodadSetReport
            {
                UniqueId = s.UniqueId, Wmo = s.WmoFileDataID, Set = s.Requested, SetName = string.Join("+", s.Names), SetCount = s.SetCount,
                Active = s.Records, Reason = s.Reason ?? "",
            });
        }
        r.DoodadSetsTotal = d.Sets.Count;
        r.DoodadInstances = d.Instances.Count;
        foreach (DoodadWork dw in d.Instances)
        {
            switch (dw.State)
            {
                case "placed": r.DoodadPlaced++; break;
                case "failed": r.DoodadFailed++; break;
                case "unsupported": r.DoodadUnsupported++; break;
                case "notRequested": r.DoodadNotRequested++; break;
                default: r.DoodadEnded++; break;
            }
        }
        r.DoodadModels = d.Models.Count;
        foreach (DoodadModel m in d.Models)
        {
            switch (m.State)
            {
                case DoodadModelState.Built:
                    r.DoodadModelsBuilt++;
                    if (m.Partial != null) r.DoodadModelsPartial++;
                    if (m.Built != null && !m.Built.Disposed) r.DoodadGpuGeometryBytes += m.Built.GpuGeometryBytes;
                    break;
                case DoodadModelState.Failed: r.DoodadModelsFailed++; break;
                case DoodadModelState.Unsupported: r.DoodadModelsUnsupported++; break;
                case DoodadModelState.NotRequested: r.DoodadModelsNotRequested++; break;
            }
        }
        r.DoodadTextures = d.Textures.Count;
        r.DoodadTexturesDecoded = d.TexturesDecoded;
        r.DoodadTexturesMissing = d.TexturesMissing + d.TexturesNotRequested;
        WmvDoodadTextures cache = OwnsScene(j) ? world.DoodadTextures : null;
        r.DoodadTextureUses = cache != null ? cache.Uses : 0;
        r.DoodadTextureUploads = cache != null ? cache.Uploads : 0;
        r.DoodadGpuTextureBytes = cache != null ? cache.GpuBytes : 0;
        r.DoodadFileBytes = d.FileBytes;
        r.DoodadTextureBytes = d.TextureBytes;
        r.DoodadDiscoverMs = d.DiscoverMs;
        r.DoodadFirstVisibleMs = d.FirstVisibleMs;
        r.DoodadFetchMs = d.FirstAskMs >= 0 && d.LastAnswerMs >= d.FirstAskMs ? d.LastAnswerMs - d.FirstAskMs : -1;
        r.DoodadParseMs = d.Started ? d.ParseCpuMs : -1;
        r.DoodadDecodeMs = d.Started ? d.DecodeCpuMs : -1;
        r.DoodadBuildMs = d.Started ? d.BuildMs : -1;
        r.DoodadBuildMaxMs = d.Started ? d.BuildMaxMs : -1;
        r.DoodadPlaceMs = d.Started ? d.PlaceMs : -1;
        r.DoodadPlaceMaxMs = d.Started ? d.PlaceMaxMs : -1;
        r.DoodadReadyMs = d.ReadyMs;
        // Every model not built first (in plan order), then the most placed.
        var order = new List<DoodadModel>();
        foreach (DoodadModel m in d.Models)
            if (m.State != DoodadModelState.Built || m.Partial != null)
                order.Add(m);
        var builtModels = new List<DoodadModel>();
        foreach (DoodadModel m in d.Models)
            if (m.State == DoodadModelState.Built && m.Partial == null)
                builtModels.Add(m);
        builtModels.Sort((a, b) => b.Placed != a.Placed ? b.Placed.CompareTo(a.Placed) : a.FileDataID.CompareTo(b.FileDataID));
        order.AddRange(builtModels);
        for (int i = 0; i < order.Count && i < ReportDoodadModelsMax; i++)
        {
            DoodadModel m = order[i];
            string state = m.State == DoodadModelState.Built ? (m.Partial != null ? "partial" : "built")
                         : m.State == DoodadModelState.Failed ? "failed"
                         : m.State == DoodadModelState.Unsupported ? "unsupported"
                         : m.State == DoodadModelState.NotRequested ? "notRequested" : "ended";
            r.DoodadList.Add(new WmvIpcClient.DoodadModelReport
            {
                FileDataID = (int)m.FileDataID, State = state, Reason = m.State == DoodadModelState.Built ? (m.Partial ?? "") : m.Reason ?? "",
                Instances = m.Instances.Count, Placed = m.Placed,
            });
        }
        r.DoodadListTotal = order.Count;
    }

    void LogDoodads(int load, WmvIpcClient.TerrainTileReport r)
    {
        Log("load {0}: doodads {1}{2}: {3} MDDF record(s) -> {4} doodad(s); {5} MODD record(s) in the active sets of {6} world-model placement(s); " +
            "{7} doodad(s): {8} placed, {9} failed, {10} unsupported, {11} not requested, {12} ended; {13} M2(s): {14} built ({15} partially), " +
            "{16} failed, {17} unsupported, {18} not requested; {19} texture(s) ({20} decoded, {21} missing), {22} use(s) of {23} upload(s); " +
            "{24} file bytes, {25} texture bytes; GPU {26} KB geometry, {27} KB textures; timings discover {28:0.###}, first visible {29:0.#}, " +
            "fetch {30:0.#}, parse {31:0.#} (worker), decode {32:0.#} (worker), build {33:0.#} (longest {34:0.#}), place {35:0.#} (longest frame " +
            "{36:0.###}), ready {37:0.#} ms; the world's assembly {38}",
            load, r.DoodadState, string.IsNullOrEmpty(r.DoodadReason) ? "" : " (" + r.DoodadReason + ")", r.DoodadMddfRecords, r.DoodadMddfPlacements,
            r.DoodadWmoRecords, r.DoodadWmoPlacements, r.DoodadInstances, r.DoodadPlaced, r.DoodadFailed, r.DoodadUnsupported, r.DoodadNotRequested,
            r.DoodadEnded, r.DoodadModels, r.DoodadModelsBuilt, r.DoodadModelsPartial, r.DoodadModelsFailed, r.DoodadModelsUnsupported,
            r.DoodadModelsNotRequested, r.DoodadTextures, r.DoodadTexturesDecoded, r.DoodadTexturesMissing, r.DoodadTextureUses,
            r.DoodadTextureUploads, r.DoodadFileBytes, r.DoodadTextureBytes, r.DoodadGpuGeometryBytes / 1024, r.DoodadGpuTextureBytes / 1024,
            r.DoodadDiscoverMs, r.DoodadFirstVisibleMs, r.DoodadFetchMs, r.DoodadParseMs, r.DoodadDecodeMs, r.DoodadBuildMs, r.DoodadBuildMaxMs,
            r.DoodadPlaceMs, r.DoodadPlaceMaxMs, r.DoodadReadyMs, r.WorldAssembly);
        int listed = 0;
        foreach (WmvIpcClient.DoodadModelReport m in r.DoodadList)
        {
            if (m.State == "built" || listed++ >= 32)
                continue;
            Log("load {0}:   M2 {1}: {2}{3}; {4} of {5} doodad(s) placed", load, m.FileDataID, m.State, string.IsNullOrEmpty(m.Reason) ? "" : " (" + m.Reason + ")",
                m.Placed, m.Instances);
        }
    }
}
