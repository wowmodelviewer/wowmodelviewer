#pragma once
#include <QString>
#include <QRegularExpression>

// User-entered names are validated, never silently rewritten.
inline QString equipmentSetNameError(const QString & name)
{
  if (name.trimmed().isEmpty()) return "Enter a set name.";
  if (name.contains(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]")))
    return "Set name contains invalid characters: < > : \" / \\ | ? *";
  if (name.endsWith('.') || name.endsWith(' ')) return "Set name cannot end with a space or period.";
  if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9\\x{00b9}\\x{00b2}\\x{00b3}]|LPT[1-9\\x{00b9}\\x{00b2}\\x{00b3}])($|\\.)", QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())
    return "This set name is reserved by Windows.";
  if (name.size() > 80) return "Set name is too long (maximum 80 characters).";
  return QString();
}
