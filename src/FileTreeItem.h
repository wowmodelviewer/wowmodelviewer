/*
 * FileTreeItem.h
 *
 *  Created on: 20 déc. 2014
 *      Author: Jerome
 */

#ifndef SRC_FILETREEITEM_H_
#define SRC_FILETREEITEM_H_

#include <wx/string.h>

struct FileTreeItem {
  wxString displayName;
  wxString fileName;

  int color;

  /// Comparison
  bool operator<(const FileTreeItem &i) const {
    return displayName < i.displayName;
  }

  bool operator>(const FileTreeItem &i) const {
    return displayName < i.displayName;
  }
};


#endif /* SRC_FILETREEITEM_H_ */
