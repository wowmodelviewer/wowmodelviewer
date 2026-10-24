"""Independent byte-level oracle for exported events (no production C++ reader).

python verify-events.py RAW_FOLDER SIDECAR [SIDECAR ...]
Source raw files are named <FileDataID>.bin. Never put them in Git.
"""
import json
import pathlib
import struct
import sys


class Oracle:
    def __init__(self, root):
        self.root = pathlib.Path(root)
        self.cache = {}

    def chunks(self, file_id):
        if file_id not in self.cache:
            raw = (self.root / f"{file_id}.bin").read_bytes()
            chunks, offset = {}, 0
            while offset < len(raw):
                assert offset + 8 <= len(raw)
                name, size = struct.unpack_from("<4sI", raw, offset)
                assert offset + 8 + size <= len(raw) and name not in chunks
                chunks[name] = raw[offset + 8:offset + 8 + size]
                offset += 8 + size
            self.cache[file_id] = chunks
        return self.cache[file_id]

    @staticmethod
    def array(data, at, stride):
        count, offset = struct.unpack_from("<II", data, at)
        assert count == 0 or offset + count * stride <= len(data)
        return [offset + stride * i for i in range(count)]

    def sources(self, model):
        m = self.chunks(model)
        sid = struct.unpack_from("<I", m[b"SKID"])[0] if b"SKID" in m else model
        sources, visited = [], set()
        while sid:
            assert sid not in visited
            visited.add(sid)
            chunks = self.chunks(sid)
            data = chunks[b"MD21"] if sid == model else chunks[b"SKS1"]
            seqs = []
            for at in self.array(data, 28 if sid == model else 8, 64):
                anim, sub, duration = struct.unpack_from("<HHI", data, at)
                flags = struct.unpack_from("<I", data, at + 12)[0]
                alias = struct.unpack_from("<H", data, at + 62)[0]
                seqs.append((anim, sub, duration, flags, alias))
            globals_ = [struct.unpack_from("<I", data, p)[0]
                        for p in self.array(data, 20 if sid == model else 0, 4)]
            afid = {v[:2]: v[2] for v in struct.iter_unpack("<HHI", chunks.get(b"AFID", b""))}
            sources.append((sid, seqs, globals_, afid))
            sid = struct.unpack_from("<I", chunks[b"SKPD"], 8)[0] if b"SKPD" in chunks else 0
        return sources

    def events(self, model, clip):
        chunks = self.chunks(model)
        m = chunks[b"MD21"]
        sources = self.sources(model)
        identity = clip["animationId"], clip["subAnimationId"]
        black = set(v[0] for v in struct.iter_unpack("<H", chunks.get(b"PABC", b"")))
        for owner, (file_id, seqs, globals_, afid) in enumerate(sources):
            matches = [i for i, q in enumerate(seqs) if q[:2] == identity]
            if matches and (not owner or identity[0] not in black):
                sequence = matches[0]
                break
        else:
            raise AssertionError(f"No native identity for {model}/{identity}")
        assert clip["sequenceOwnerFileId"] == file_id
        assert clip["sourceSequenceIndex"] == sequence
        assert seqs[sequence][2] == clip["durationMs"]
        key, seen = sequence, set()
        while seqs[key][3] & 0x40 and not (seqs[key][3] & 0x20) and not afid.get(seqs[key][:2]):
            assert key not in seen
            seen.add(key)
            key = seqs[key][4]
            assert key < len(seqs)
        assert key == clip["resolvedSequenceIndex"] and seqs[key][2] == clip["durationMs"]
        definitions, result = [], []
        for index, at in enumerate(self.array(m, 256, 36)):
            code, payload, bone, x, y, z = struct.unpack_from("<4sII3f", m, at)
            definition = dict(eventIndex=index, code=code.decode("latin1"), codeHex=code.hex(),
                              payload=payload, bone=-1 if bone == 0xffffffff else bone, position=[x, y, z])
            definitions.append(definition)
            if owner:
                tracks = chunks[b"PEDC"]
                positions = self.array(tracks, 0, 12)
                assert len(positions) == len(self.array(m, 256, 36))
                track_at = positions[index]
            else:
                tracks, track_at = m, at + 24
            interpolation, global_index = struct.unpack_from("<Hh", tracks, track_at)
            outer = self.array(tracks, track_at + 4, 8)
            entry = 0 if global_index >= 0 else key
            if entry >= len(outer):
                continue
            count, offset = struct.unpack_from("<II", tracks, outer[entry])
            if not count:
                continue
            external = 0
            timestamps = tracks
            if owner == 0 and global_index < 0 and not (seqs[key][3] & 0x130):
                model_afid = {v[:2]: v[2] for v in struct.iter_unpack("<HHI", chunks.get(b"AFID", b""))}
                external = model_afid.get(seqs[key][:2]) or afid.get(seqs[key][:2])
                assert external
                timestamps = self.chunks(external)[b"AFM2"]
            assert offset + 4 * count <= len(timestamps)
            times = list(struct.unpack_from(f"<{count}I", timestamps, offset))
            period = globals_[global_index] if global_index >= 0 else None
            assert times == sorted(times) and max(times) <= (period if period is not None else clip["durationMs"])
            event = dict(definition, timestampsMs=times, interpolationType=interpolation,
                         globalSequence=global_index, clock="global" if global_index >= 0 else "clip",
                         trackChunk="PEDC" if owner else "MD21",
                         timestampChunk="AFM2" if external else "PEDC" if owner else "MD21",
                         timestampFileId=external or model)
            if period is not None:
                event["periodMs"] = period
            result.append(event)
        return definitions, result


def verify(raw, paths):
    oracle = Oracle(raw)
    summary = []
    for path in paths:
        path = pathlib.Path(path)
        data = json.loads(path.read_text(encoding="utf-8"))
        assert data["schema"] == "wmv.animation-events" and data["version"] == 1 and data["status"] == "complete"
        assert data["product"] == "wow" and data["build"] == "12.1.5.70391"
        takes, identities, timestamps, empty = set(), set(), 0, 0
        for clip in data["clips"]:
            identity = clip["animationId"], clip["subAnimationId"]
            assert clip["take"] not in takes and identity not in identities
            takes.add(clip["take"])
            identities.add(identity)
            definitions, events = oracle.events(data["modelFileId"], clip)
            # JSON float rendering is shorter than the original float32 bits.
            for original, exported in zip(definitions, data["eventDefinitions"]):
                assert all(abs(a-b) < 1e-6 for a,b in zip(original["position"], exported["position"]))
                original["position"] = exported["position"]
            assert definitions == data["eventDefinitions"]
            assert len(events) == len(clip["events"])
            for original, exported in zip(events, clip["events"]):
                assert all(abs(a-b) < 1e-6 for a,b in zip(original["position"], exported["position"]))
                original["position"] = exported["position"]
            assert events == clip["events"], (path, clip["take"], events, clip["events"])
            timestamps += sum(len(e["timestampsMs"]) for e in events)
            empty += not events
        summary.append(dict(file=str(path), model=data["modelFileId"], clips=len(takes), emptyClips=empty, timestamps=timestamps))
    print(json.dumps(summary, indent=2))
    return summary


if __name__ == "__main__":
    verify(sys.argv[1], sys.argv[2:])
