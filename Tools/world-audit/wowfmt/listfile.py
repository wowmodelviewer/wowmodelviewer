"""The community listfile ("<FileDataID>;<path>" lines) as the viewer downloads it. Paths are compared lowercased with '/'.
The listfile is a lookup aid, not proof: a name from it is reported as the listfile's, and FileDataIDs taken from the
game's own files (Map.db2, MAID) always win over names."""


class Listfile:
    def __init__(self, path):
        self.by_id = {}
        self.by_path = {}
        with open(path, encoding='utf-8', errors='replace') as f:
            for line in f:
                sep = line.find(';')
                if sep <= 0:
                    continue
                try:
                    fid = int(line[:sep])
                except ValueError:
                    continue
                p = line[sep + 1:].strip().replace('\\', '/').lower()
                self.by_id[fid] = p
                self.by_path[p] = fid

    def id_of(self, path):
        return self.by_path.get(path.replace('\\', '/').lower())

    def path_of(self, fid):
        return self.by_id.get(fid)
