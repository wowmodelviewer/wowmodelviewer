/*
 * Listfile.h
 *
 * THE LISTFILE, READ ONCE. listfile.csv names the FileDataIDs of every client (~2.3 million lines, ~150 MB), and it
 * does not depend on the product: one reading serves every client and installation opened in a session. It is read
 * again only when the file changed (the community list was refreshed). Which of its files a client has is decided
 * per client, by that client's storage (WoWFolder::initFromListfile); nothing here is product state.
 *
 * Lookups: the path an id is listed under, the id of a path, and every entry in path order -- the order a folder's
 * name index is built in, so that building it never searches. The path order takes a sort of every path; it is kept
 * beside the listfile (listfile.csv.order) for as long as the listfile does not change, and checked when it is read.
 */

#ifndef _LISTFILE_H_
#define _LISTFILE_H_

#include <functional>
#include <memory>
#include <vector>

#include <QDateTime>
#include <QString>

namespace wow
{
  class Listfile
  {
  public:
    // The listfile at path: the one read before when the file has not changed since (reused is then true), otherwise
    // read now. Null when it cannot be opened. progress, if set, gets 0..1 while the file is read.
    static std::shared_ptr<const Listfile> get(const QString & path, const std::function<void(float)> & progress,
                                               bool & reused);

    size_t size() const { return m_ids.size(); }
    int id(size_t entry) const { return m_ids[entry]; }
    // Lower case, as the viewer has always listed them.
    const QString & path(size_t entry) const { return m_paths[entry]; }

    // The path a FileDataID is listed under ("" if none); the last line naming it, when several do.
    QString pathOf(int id) const;
    // The FileDataID listed for a path (exact, lower case), -1 if none; the last line naming it, when several do.
    int idOf(const QString & path) const;
    // Every entry index, in path order (entries of the same path in line order).
    const std::vector<quint32> & pathOrder() const { return m_byPath; }

  private:
    bool read(const QString & file, const std::function<void(float)> & progress);
    bool readOrder(const QString & orderFile);
    void sortOrder();
    void writeOrder(const QString & orderFile) const;
    bool pathLess(quint32 a, quint32 b) const;

    QString m_file;
    qint64 m_size = -1;
    QDateTime m_modified;
    std::vector<int> m_ids;        // in line order
    std::vector<QString> m_paths;  // in line order
    bool m_idsAscending = true;    // the community listfile is sorted by id; another may not be
    std::vector<quint32> m_byId;   // entries by id (equal ids in line order), only when the lines are not
    std::vector<quint32> m_byPath; // entries by path (equal paths in line order)
  };
}

#endif /* _LISTFILE_H_ */
